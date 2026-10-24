#ifndef BS_AUDIO_STREAM_CLOCK_H
#define BS_AUDIO_STREAM_CLOCK_H
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

static inline uint64_t AudioStream_positionSamples(uint64_t played, int offset,
                                                  uint32_t length, bool loop) {
    uint64_t position = played + (uint64_t)(offset > 0 ? offset : 0);
    return loop && length ? position % length : position;
}

static inline uint32_t AudioStream_seekSample(double seconds, int rate,
                                             uint32_t length, bool loop) {
    double sample = seconds * rate;
    if (!isfinite(sample) || sample < 0) sample = 0;
    if (length) {
        if (loop) sample = fmod(sample, length);
        else if (sample >= length) sample = length - 1;
    }
    if (sample > UINT32_MAX) sample = UINT32_MAX;
    return (uint32_t)sample;
}
#endif
