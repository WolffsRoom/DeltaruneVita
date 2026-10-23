#ifndef DELTARUNE_VITA_FRAME_PACER_H
#define DELTARUNE_VITA_FRAME_PACER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct VitaFramePacer {
    uint32_t fps;
    uint32_t periodUs;
    uint32_t periodRemainder;
    uint32_t remainderCarry;
    uint32_t sleepGuardUs;
    uint64_t deadlineUs;
    uint64_t lateFrames;
    uint64_t oversleepEvents;
    uint64_t resetEvents;
} VitaFramePacer;

typedef struct VitaFramePacerPlan {
    bool enabled;
    uint64_t deadlineUs;
    uint32_t coarseSleepUs;
} VitaFramePacerPlan;

void VitaFramePacer_init(VitaFramePacer* pacer, uint32_t fps, uint64_t nowUs);
void VitaFramePacer_setRate(VitaFramePacer* pacer, uint32_t fps,
                            uint64_t nowUs);
VitaFramePacerPlan VitaFramePacer_plan(VitaFramePacer* pacer,
                                       uint64_t nowUs);
void VitaFramePacer_noteSleep(VitaFramePacer* pacer, uint32_t requestedUs,
                              uint64_t elapsedUs);
void VitaFramePacer_finish(VitaFramePacer* pacer, uint64_t nowUs);
void VitaFramePacer_reset(VitaFramePacer* pacer, uint64_t nowUs);

#endif
