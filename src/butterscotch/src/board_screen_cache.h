#ifndef BS_BOARD_SCREEN_CACHE_H
#define BS_BOARD_SCREEN_CACHE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct BoardScreenCache {
    bool ready;
    uint32_t lastFrame;
    int32_t source, target;
    float x, y, width, height;
} BoardScreenCache;

// At most one frame of latency. A changed crop, lost surface, or a monitor
// returning after being hidden must refresh immediately, regardless of phase.
static inline bool BoardScreenCache_reuse(BoardScreenCache* c, uint32_t frame,
    uint32_t phase, bool surfacesValid, int32_t source, int32_t target,
    float x, float y, float width, float height) {
    bool reuse = c->ready && surfacesValid && frame - c->lastFrame == 1u &&
        ((frame + phase) & 1u) != 0u && c->source == source &&
        c->target == target && c->x == x && c->y == y &&
        c->width == width && c->height == height;
    c->ready = surfacesValid;
    c->lastFrame = frame;
    c->source = source; c->target = target;
    c->x = x; c->y = y; c->width = width; c->height = height;
    return reuse;
}
#endif
