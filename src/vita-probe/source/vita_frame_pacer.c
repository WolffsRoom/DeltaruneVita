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
    uint64_t period = pacerPeriodCeil(pacer);
    pacer->deadlineUs = nowUs + period;
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

    plan.enabled = true;
    plan.deadlineUs = pacer->deadlineUs;
    if (nowUs >= pacer->deadlineUs) {
        /* A late frame must start the next one immediately. Until v0.81-13
         * this branch returned the NEXT grid deadline and the caller spun on
         * it, so a 34 ms frame at 30 FPS was held to 66.7 ms (15 FPS).
         * Slightly late (under a quarter period): keep the grid phase so the
         * cadence stays at 33.3 ms on average. Later than that: re-anchor the
         * grid to now instead of making the next frame pay for this one. */
        ++pacer->lateFrames;
        uint64_t lateBy = nowUs - pacer->deadlineUs;
        if (lateBy < period / 4ULL) {
            pacerAdvanceDeadline(pacer);
        } else {
            pacer->deadlineUs = nowUs + period;
            pacer->remainderCarry = 0;
        }
        plan.deadlineUs = nowUs;
        plan.coarseSleepUs = 0;
        return plan;
    }

    uint64_t remaining = pacer->deadlineUs - nowUs;
    if (remaining > pacer->sleepGuardUs)
        plan.coarseSleepUs = (uint32_t)(remaining - pacer->sleepGuardUs);

    pacerAdvanceDeadline(pacer);
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
    uint64_t period = pacerPeriodCeil(pacer);
    pacer->deadlineUs = nowUs + period;
    pacer->remainderCarry = 0;
    ++pacer->resetEvents;
}
