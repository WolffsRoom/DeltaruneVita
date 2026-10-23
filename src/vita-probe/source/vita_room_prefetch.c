#include "vita_room_prefetch.h"
#include "vita_io_profile.h"

#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VITA_ROOM_PREFETCH_RESERVATION (2U * 1024U * 1024U)

typedef struct RoomPayloadJob {
    DataWin* dataWin;
    int32_t roomIndex;
} RoomPayloadJob;

typedef struct RoomPrefetchState {
    VitaBackgroundWork* work;
    DataWin* dataWin;
    uint32_t generation;
    uint32_t ticket;
    int32_t roomIndex;
    bool active;
} RoomPrefetchState;

static RoomPrefetchState g_roomPrefetch;

static bool roomPayloadJob(void* userData, void** result,
                           size_t* resultBytes) {
    RoomPayloadJob* job = userData;
    DataWinRoomPayload* payload = NULL;
    if (job == NULL || result == NULL || resultBytes == NULL) return false;
    VitaIoProfileSample io = VitaIoProfile_begin(
        "room_payload", "data_win", 0, 0, true);
    bool ok = DataWin_stageRoomPayload(job->dataWin, job->roomIndex, &payload);
    VitaIoProfile_end(&io, ok, 0, 0);
    char ioLine[256];
    VitaIoProfile_format(&io, ioLine, sizeof(ioLine));
    fprintf(stderr, "%s room=%d\n", ioLine, job->roomIndex);
    *result = payload;
    *resultBytes = ok ? sizeof(*payload) : 0;
    return ok;
}

void VitaRoomPrefetch_init(VitaBackgroundWork* work, DataWin* dataWin) {
    memset(&g_roomPrefetch, 0, sizeof(g_roomPrefetch));
    g_roomPrefetch.work = work;
    g_roomPrefetch.dataWin = dataWin;
    g_roomPrefetch.generation = 2U;
    g_roomPrefetch.roomIndex = -1;
}

static bool submitRoom(int32_t roomIndex) {
    RoomPayloadJob* request = calloc(1, sizeof(*request));
    if (request == NULL) return false;
    request->dataWin = g_roomPrefetch.dataWin;
    request->roomIndex = roomIndex;

    VitaBackgroundJob job;
    memset(&job, 0, sizeof(job));
    job.generation = g_roomPrefetch.generation;
    job.reservedBytes = VITA_ROOM_PREFETCH_RESERVATION;
    job.execute = roomPayloadJob;
    job.destroyUserData = free;
    job.destroyResult = (VitaBackgroundDestroy)DataWin_freeStagedRoomPayload;
    job.userData = request;
    if (!VitaBackgroundWork_submit(g_roomPrefetch.work, &job,
                                   &g_roomPrefetch.ticket)) {
        free(request);
        return false;
    }
    g_roomPrefetch.roomIndex = roomIndex;
    g_roomPrefetch.active = true;
    fprintf(stderr, "VITA_ROOM_PREFETCH=queued room=%d generation=%u\n",
            roomIndex, g_roomPrefetch.generation);
    return true;
}

bool VitaRoomPrefetch_prepare(int32_t roomIndex) {
    if (g_roomPrefetch.dataWin == NULL || roomIndex < 0 ||
        (uint32_t)roomIndex >= g_roomPrefetch.dataWin->room.count)
        return true;
    if (g_roomPrefetch.dataWin->room.rooms[roomIndex].payloadLoaded)
        return true;
    if (g_roomPrefetch.work == NULL) return true;

    if (!g_roomPrefetch.active) {
        if (!submitRoom(roomIndex)) return true;
        return false;
    }
    if (g_roomPrefetch.roomIndex != roomIndex) {
        VitaRoomPrefetch_cancel();
        return false;
    }

    VitaBackgroundJob completed;
    bool found = false;
    while (VitaBackgroundWork_poll(g_roomPrefetch.work, &completed)) {
        if (completed.ticket == g_roomPrefetch.ticket) {
            found = true;
            break;
        }
        VitaBackgroundJob_release(&completed);
    }
    if (!found) return false;
    g_roomPrefetch.active = false;
    bool adopted = false;
    if (completed.ticket == g_roomPrefetch.ticket &&
        completed.state == VITA_BACKGROUND_JOB_READY) {
        DataWinRoomPayload* payload = completed.result;
        adopted = DataWin_adoptRoomPayload(g_roomPrefetch.dataWin, roomIndex,
                                           payload);
        DataWin_freeStagedRoomPayload(payload);
        completed.result = NULL;
    }
    VitaBackgroundJob_release(&completed);
    fprintf(stderr, "VITA_ROOM_PREFETCH=%s room=%d generation=%u\n",
            adopted ? "adopted" : "fallback", roomIndex,
            g_roomPrefetch.generation);
    return true;
}

void VitaRoomPrefetch_cancel(void) {
    if (!g_roomPrefetch.active) return;
    VitaBackgroundWork_cancelGeneration(g_roomPrefetch.work,
                                        g_roomPrefetch.generation);
    ++g_roomPrefetch.generation;
    g_roomPrefetch.active = false;
    g_roomPrefetch.roomIndex = -1;
}

void VitaRoomPrefetch_shutdown(void) {
    if (g_roomPrefetch.active) {
        uint32_t oldTicket = g_roomPrefetch.ticket;
        VitaRoomPrefetch_cancel();
        VitaBackgroundJob completed;
        bool finished = false;
        while (!finished) {
            if (VitaBackgroundWork_poll(g_roomPrefetch.work, &completed)) {
                finished = completed.ticket == oldTicket;
                VitaBackgroundJob_release(&completed);
            } else {
                sceKernelDelayThread(1000);
            }
        }
    }
    memset(&g_roomPrefetch, 0, sizeof(g_roomPrefetch));
    g_roomPrefetch.roomIndex = -1;
}
