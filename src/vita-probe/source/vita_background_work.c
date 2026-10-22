#include "vita_background_work.h"

#include "vita_thread_policy.h"

#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
#include <string.h>

struct VitaBackgroundWork {
    SceUID thread;
    SceUID queuedSema;
    volatile int lock;
    volatile bool stopping;
    bool usedUnlockedCore;
    uint32_t nextTicket;
    size_t budgetBytes;
    size_t reservedBytes;
    VitaBackgroundJob queued[VITA_BACKGROUND_QUEUE_CAPACITY];
    unsigned int queueRead;
    unsigned int queueWrite;
    VitaBackgroundJob completed[VITA_BACKGROUND_QUEUE_CAPACITY];
    unsigned int completedRead;
    unsigned int completedWrite;
    uint32_t cancelledGenerations[VITA_BACKGROUND_QUEUE_CAPACITY];
    unsigned int cancelledCount;
};

static void backgroundLock(VitaBackgroundWork* work) {
    while (__sync_lock_test_and_set(&work->lock, 1) != 0) {
        sceKernelDelayThread(50);
    }
}

static void backgroundUnlock(VitaBackgroundWork* work) {
    __sync_lock_release(&work->lock);
}

static bool generationCancelled(const VitaBackgroundWork* work,
                                uint32_t generation) {
    for (unsigned int i = 0; i < work->cancelledCount; ++i) {
        if (work->cancelledGenerations[i] == generation) return true;
    }
    return false;
}

void VitaBackgroundJob_release(VitaBackgroundJob* job) {
    if (job == NULL) return;
    if (job->destroyUserData != NULL && job->userData != NULL) {
        job->destroyUserData(job->userData);
    }
    if (job->destroyResult != NULL && job->result != NULL) {
        job->destroyResult(job->result);
    }
    memset(job, 0, sizeof(*job));
}

static int backgroundWorker(SceSize args, void* argp) {
    if (args != sizeof(VitaBackgroundWork*) || argp == NULL) return -1;
    VitaBackgroundWork* work = *(VitaBackgroundWork**)argp;

    for (;;) {
        if (sceKernelWaitSema(work->queuedSema, 1, NULL) < 0) break;

        backgroundLock(work);
        if (work->queueRead == work->queueWrite) {
            bool stop = work->stopping;
            backgroundUnlock(work);
            if (stop) break;
            continue;
        }
        VitaBackgroundJob job =
            work->queued[work->queueRead % VITA_BACKGROUND_QUEUE_CAPACITY];
        ++work->queueRead;
        bool cancelled = generationCancelled(work, job.generation);
        backgroundUnlock(work);

        if (cancelled || work->stopping) {
            job.state = VITA_BACKGROUND_JOB_CANCELLED;
        } else {
            job.state = VITA_BACKGROUND_JOB_RUNNING;
            bool ok = job.execute != NULL &&
                      job.execute(job.userData, &job.result, &job.resultBytes);
            backgroundLock(work);
            cancelled = generationCancelled(work, job.generation);
            backgroundUnlock(work);
            job.state = cancelled ? VITA_BACKGROUND_JOB_CANCELLED :
                        (ok ? VITA_BACKGROUND_JOB_READY :
                              VITA_BACKGROUND_JOB_FAILED);
        }

        backgroundLock(work);
        if (work->completedWrite - work->completedRead <
            VITA_BACKGROUND_QUEUE_CAPACITY) {
            work->completed[work->completedWrite %
                            VITA_BACKGROUND_QUEUE_CAPACITY] = job;
            ++work->completedWrite;
        } else {
            if (work->reservedBytes >= job.reservedBytes)
                work->reservedBytes -= job.reservedBytes;
            VitaBackgroundJob_release(&job);
        }
        backgroundUnlock(work);
    }
    return 0;
}

VitaBackgroundWork* VitaBackgroundWork_create(size_t budgetBytes) {
    VitaBackgroundWork* work = calloc(1, sizeof(*work));
    if (work == NULL) return NULL;
    work->thread = -1;
    work->queuedSema = -1;
    work->budgetBytes = budgetBytes != 0 ? budgetBytes :
                                               VITA_BACKGROUND_DEFAULT_BUDGET;
    work->nextTicket = 1;

    work->queuedSema = sceKernelCreateSema("Deltarune bg work", 0, 0,
                                           VITA_BACKGROUND_QUEUE_CAPACITY,
                                           NULL);
    if (work->queuedSema < 0) {
        free(work);
        return NULL;
    }
    work->thread = VitaThread_createBackground("Deltarune bg worker",
                                                backgroundWorker,
                                                0x10000100, 0x20000, 0,
                                                &work->usedUnlockedCore);
    if (work->thread < 0 ||
        sceKernelStartThread(work->thread, sizeof(work), &work) < 0) {
        if (work->thread >= 0) sceKernelDeleteThread(work->thread);
        sceKernelDeleteSema(work->queuedSema);
        free(work);
        return NULL;
    }
    return work;
}

bool VitaBackgroundWork_submit(VitaBackgroundWork* work,
                               const VitaBackgroundJob* job,
                               uint32_t* ticketOut) {
    if (work == NULL || job == NULL || job->execute == NULL || work->stopping)
        return false;

    backgroundLock(work);
    bool full = work->queueWrite - work->queueRead >=
                VITA_BACKGROUND_QUEUE_CAPACITY;
    bool overBudget = job->reservedBytes > work->budgetBytes ||
                      work->reservedBytes >
                          work->budgetBytes - job->reservedBytes;
    if (full || overBudget || generationCancelled(work, job->generation)) {
        backgroundUnlock(work);
        return false;
    }

    VitaBackgroundJob queued = *job;
    queued.ticket = work->nextTicket++;
    if (work->nextTicket == 0) work->nextTicket = 1;
    queued.state = VITA_BACKGROUND_JOB_QUEUED;
    queued.result = NULL;
    queued.resultBytes = 0;
    work->queued[work->queueWrite % VITA_BACKGROUND_QUEUE_CAPACITY] = queued;
    ++work->queueWrite;
    work->reservedBytes += queued.reservedBytes;
    if (ticketOut != NULL) *ticketOut = queued.ticket;
    backgroundUnlock(work);
    sceKernelSignalSema(work->queuedSema, 1);
    return true;
}

bool VitaBackgroundWork_poll(VitaBackgroundWork* work,
                             VitaBackgroundJob* completedOut) {
    if (work == NULL || completedOut == NULL) return false;
    backgroundLock(work);
    if (work->completedRead == work->completedWrite) {
        backgroundUnlock(work);
        return false;
    }
    *completedOut = work->completed[work->completedRead %
                                    VITA_BACKGROUND_QUEUE_CAPACITY];
    ++work->completedRead;
    if (work->reservedBytes >= completedOut->reservedBytes)
        work->reservedBytes -= completedOut->reservedBytes;
    backgroundUnlock(work);
    return true;
}

void VitaBackgroundWork_cancelGeneration(VitaBackgroundWork* work,
                                         uint32_t generation) {
    if (work == NULL) return;
    backgroundLock(work);
    if (!generationCancelled(work, generation)) {
        if (work->cancelledCount < VITA_BACKGROUND_QUEUE_CAPACITY) {
            work->cancelledGenerations[work->cancelledCount++] = generation;
        } else {
            memmove(&work->cancelledGenerations[0],
                    &work->cancelledGenerations[1],
                    (VITA_BACKGROUND_QUEUE_CAPACITY - 1U) * sizeof(uint32_t));
            work->cancelledGenerations[VITA_BACKGROUND_QUEUE_CAPACITY - 1U] =
                generation;
        }
    }
    backgroundUnlock(work);
}

size_t VitaBackgroundWork_reservedBytes(const VitaBackgroundWork* work) {
    return work != NULL ? work->reservedBytes : 0;
}

bool VitaBackgroundWork_usedUnlockedCore(const VitaBackgroundWork* work) {
    return work != NULL && work->usedUnlockedCore;
}

void VitaBackgroundWork_destroy(VitaBackgroundWork* work) {
    if (work == NULL) return;
    work->stopping = true;
    sceKernelSignalSema(work->queuedSema, 1);
    if (work->thread >= 0) {
        sceKernelWaitThreadEnd(work->thread, NULL, NULL);
        sceKernelDeleteThread(work->thread);
    }

    for (unsigned int i = work->queueRead; i < work->queueWrite; ++i)
        VitaBackgroundJob_release(&work->queued[i %
                                               VITA_BACKGROUND_QUEUE_CAPACITY]);
    for (unsigned int i = work->completedRead; i < work->completedWrite; ++i)
        VitaBackgroundJob_release(&work->completed[i %
                                                  VITA_BACKGROUND_QUEUE_CAPACITY]);
    if (work->queuedSema >= 0) sceKernelDeleteSema(work->queuedSema);
    free(work);
}
