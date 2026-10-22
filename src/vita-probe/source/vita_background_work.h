#ifndef DELTARUNE_VITA_BACKGROUND_WORK_H
#define DELTARUNE_VITA_BACKGROUND_WORK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VITA_BACKGROUND_DEFAULT_BUDGET (8U * 1024U * 1024U)
#define VITA_BACKGROUND_QUEUE_CAPACITY 16U

typedef struct VitaBackgroundWork VitaBackgroundWork;

typedef enum VitaBackgroundJobState {
    VITA_BACKGROUND_JOB_QUEUED = 0,
    VITA_BACKGROUND_JOB_RUNNING,
    VITA_BACKGROUND_JOB_READY,
    VITA_BACKGROUND_JOB_FAILED,
    VITA_BACKGROUND_JOB_CANCELLED
} VitaBackgroundJobState;

typedef bool (*VitaBackgroundExecute)(void* userData,
                                      void** result,
                                      size_t* resultBytes);
typedef void (*VitaBackgroundDestroy)(void* value);

typedef struct VitaBackgroundJob {
    uint32_t ticket;
    uint32_t generation;
    size_t reservedBytes;
    VitaBackgroundJobState state;
    VitaBackgroundExecute execute;
    VitaBackgroundDestroy destroyUserData;
    VitaBackgroundDestroy destroyResult;
    void* userData;
    void* result;
    size_t resultBytes;
} VitaBackgroundJob;

VitaBackgroundWork* VitaBackgroundWork_create(size_t budgetBytes);
bool VitaBackgroundWork_submit(VitaBackgroundWork* work,
                               const VitaBackgroundJob* job,
                               uint32_t* ticketOut);
bool VitaBackgroundWork_poll(VitaBackgroundWork* work,
                             VitaBackgroundJob* completedOut);
void VitaBackgroundWork_cancelGeneration(VitaBackgroundWork* work,
                                         uint32_t generation);
size_t VitaBackgroundWork_reservedBytes(const VitaBackgroundWork* work);
bool VitaBackgroundWork_usedUnlockedCore(const VitaBackgroundWork* work);
void VitaBackgroundJob_release(VitaBackgroundJob* job);
void VitaBackgroundWork_destroy(VitaBackgroundWork* work);

#endif
