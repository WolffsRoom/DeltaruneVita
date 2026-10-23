#include "vita_frame_pacer.h"

#include <string.h>

#define VITA_FRAME_PACER_MIN_GUARD_US 100U
#define VITA_FRAME_PACER_MAX_GUARD_US 500U

static uint64_t pacerPeriodCeil(const VitaFramePacer* pacer) {
    return (uint64_t)pacer->periodUs +
           (pacer->periodRemainder != 0 ? 1ULL : 0ULL);
}

static void pacerAdvanceDeadline(VitaFramePacer* pacer) {
    pacer->deadlineUs += pacer->periodUs;
    pacer->remainderCarry += pacer->periodRemainder;
    if (pacer->fps != 0 && pacer->remainderCarry >= pacer->fps) {
        pacer->deadlineUs += pacer->remainderCarry / pacer->fps;
        pacer->remainderCarry %= pacer->fps;
    }
}

void VitaFramePacer_init(VitaFramePacer* pacer, uint32_t fps,
                         uint64_t nowUs) {
    if (pacer == NULL) return;
    memset(pacer, 0, sizeof(*pacer));
    pacer->sleepGuardUs = 250U;
    VitaFramePacer_setRate(pacer, fps, nowUs);
}

void VitaFramePacer_setRate(VitaFramePacer* pacer, uint32_t fps,
                            uint64_t nowUs) {
    if (pacer == NULL || pacer->fps == fps) return;
    pacer->fps = fps;
    pacer->periodUs = fps != 0 ? 1000000U / fps : 0;
    pacer->periodRemainder = fps != 0 ? 1000000U % fps : 0;
    pacer->remainderCarry = 0;
    pacer->deadlineUs = nowUs;
    ++pacer->resetEvents;
}

VitaFramePacerPlan VitaFramePacer_plan(VitaFramePacer* pacer,
                                       uint64_t nowUs) {
    VitaFramePacerPlan plan = { false, nowUs, 0 };
    if (pacer == NULL || pacer->fps == 0) return plan;

    uint64_t period = pacerPeriodCeil(pacer);
    if (nowUs > pacer->deadlineUs + period * 2ULL) {
        VitaFramePacer_reset(pacer, nowUs);
    }
    pacerAdvanceDeadline(pacer);
    plan.enabled = true;
    plan.deadlineUs = pacer->deadlineUs;
    if (nowUs >= pacer->deadlineUs) {
        ++pacer->lateFrames;
        pacer->deadlineUs = nowUs;
        plan.deadlineUs = nowUs;
        return plan;
    }

    uint64_t remaining = pacer->deadlineUs - nowUs;
    if (remaining > pacer->sleepGuardUs)
        plan.coarseSleepUs = (uint32_t)(remaining - pacer->sleepGuardUs);
    return plan;
}

void VitaFramePacer_noteSleep(VitaFramePacer* pacer, uint32_t requestedUs,
                              uint64_t elapsedUs) {
    if (pacer == NULL || requestedUs == 0) return;
    uint32_t overshoot = elapsedUs > requestedUs ?
                         (uint32_t)(elapsedUs - requestedUs) : 0U;
    if (overshoot != 0) ++pacer->oversleepEvents;
    uint32_t sample = overshoot + VITA_FRAME_PACER_MIN_GUARD_US;
    if (sample < VITA_FRAME_PACER_MIN_GUARD_US)
        sample = VITA_FRAME_PACER_MIN_GUARD_US;
    if (sample > VITA_FRAME_PACER_MAX_GUARD_US)
        sample = VITA_FRAME_PACER_MAX_GUARD_US;
    pacer->sleepGuardUs = (pacer->sleepGuardUs * 3U + sample) / 4U;
}

void VitaFramePacer_finish(VitaFramePacer* pacer, uint64_t nowUs) {
    if (pacer == NULL || pacer->fps == 0) return;
    uint64_t period = pacerPeriodCeil(pacer);
    if (nowUs > pacer->deadlineUs + period * 2ULL)
        VitaFramePacer_reset(pacer, nowUs);
}

void VitaFramePacer_reset(VitaFramePacer* pacer, uint64_t nowUs) {
    if (pacer == NULL) return;
    pacer->deadlineUs = nowUs;
    pacer->remainderCarry = 0;
    ++pacer->resetEvents;
}
