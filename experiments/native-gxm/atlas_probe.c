/*
 * Native GXM texture/atlas bring-up, deliberately self-contained.
 * The 4x2 synthetic RGBA atlas verifies texture upload, UV sub-rectangles,
 * nearest sampling, transparency, animated sprites, rotations and scissor.
 * No game texture files are bundled with the diagnostic VPK.
 */
#include "atlas_probe.h"
#include "tpag_sprite.h"
#include "nanovg.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define ATLAS_CELL 64
#define ATLAS_COLS 4
#define ATLAS_ROWS 2
#define ATLAS_W (ATLAS_CELL * ATLAS_COLS)
#define ATLAS_H (ATLAS_CELL * ATLAS_ROWS)

static const uint8_t kColors[8][3] = {
    {255,  70, 102}, { 90, 235, 130}, { 72, 175, 255}, {255, 215,  70},
    {212,  99, 255}, { 80, 235, 220}, {255, 140,  55}, {238, 238, 238}
};

int atlas_probe_create(NVGcontext *vg) {
    uint8_t *rgba = (uint8_t *)calloc((size_t)ATLAS_W * ATLAS_H, 4u);
    if (!rgba) return -1;
    for (int y = 0; y < ATLAS_H; ++y) {
        for (int x = 0; x < ATLAS_W; ++x) {
            int col = x / ATLAS_CELL, row = y / ATLAS_CELL;
            int cell = row * ATLAS_COLS + col;
            int u = x % ATLAS_CELL, v = y % ATLAS_CELL;
            uint8_t *pixel = rgba + ((size_t)y * ATLAS_W + x) * 4u;
            /* Deliberately asymmetric art to reveal flipped and offset UVs. */
            int inside = (u >= 6 && u < 58 && v >= 6 && v < 58 &&
                          (u + v >= 24) && (u + v < 104));
            if (!inside) continue;
            int marker = (u >= 10 && u < 25 && v >= 10 && v < 16);
            int grid = ((u - 6) % 12 == 0) || ((v - 6) % 12 == 0);
            pixel[0] = marker ? 255 : grid ? 26 : kColors[cell][0];
            pixel[1] = marker ? 255 : grid ? 26 : kColors[cell][1];
            pixel[2] = marker ? 255 : grid ? 26 : kColors[cell][2];
            pixel[3] = (uint8_t)((cell == 3 || cell == 6) ? 170 : 255);
        }
    }
    int image = nvgCreateImageRGBA(vg, ATLAS_W, ATLAS_H,
                                   NVG_IMAGE_NEAREST, rgba);
    free(rgba);
    return image > 0 ? image : -1;
}

/* Equivalent of the common source-page (TPAG) sub-rectangle transform.
 * Keep offsets in image pixels rather than normalized atlas UVs so that
 * GameMaker atlas entries can eventually feed this function unchanged.
 */
static void draw_subimage(NVGcontext *vg, int image,
                          int srcX, int srcY, int srcW, int srcH,
                          float x, float y, float width, float height,
                          float radians, float alpha) {
    if (srcW <= 0 || srcH <= 0 || width <= 0 || height <= 0) return;
    NativeGXMTpag tpag = {
        srcX, srcY, srcW, srcH,
        0, 0, srcW, srcH, srcW, srcH
    };
    native_gxm_draw_tpag(vg, image, ATLAS_W, ATLAS_H, &tpag,
                         x, y, 0, 0, width / srcW, height / srcH,
                         -radians * (180.0f / 3.14159265358979323846f), alpha);
}

void atlas_probe_draw(NVGcontext *vg, int image, float t) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 34, 28, 892, 488, 8);
    nvgFillColor(vg, nvgRGBA(22, 24, 43, 255));
    nvgFill(vg);

    /* Eight cells displayed by cropping one resident GXM texture. */
    for (int i = 0; i < 8; ++i) {
        float x = 98.0f + (i % 4) * 194.0f;
        float y = 78.0f + (i / 4) * 179.0f;
        nvgBeginPath(vg);
        nvgRect(vg, x - 12, y - 12, 136, 136);
        nvgFillColor(vg, nvgRGBA(7, 8, 20, 255));
        nvgFill(vg);
        draw_subimage(vg, image,
            (i % ATLAS_COLS) * ATLAS_CELL,
            (i / ATLAS_COLS) * ATLAS_CELL,
            ATLAS_CELL, ATLAS_CELL, x, y, 112, 112, 0.0f, 1.0f);
    }

    /* Narrow scissor window: enlarged, moving, semitransparent sprite. */
    nvgSave(vg);
    nvgScissor(vg, 310, 426, 345, 60);
    int frame = ((int)(t * 3.0f)) & 7;
    float spriteX = 448.0f + sinf(t * 1.4f) * 122.0f;
    draw_subimage(vg, image,
        (frame % ATLAS_COLS) * ATLAS_CELL,
        (frame / ATLAS_COLS) * ATLAS_CELL,
        ATLAS_CELL, ATLAS_CELL,
        spriteX, 446.0f, 98, 98, sinf(t) * 0.25f, 0.72f);
    nvgRestore(vg);

    /* Cropped 32x32 segment, scaled with fractional coordinates. */
    draw_subimage(vg, image, 16, 16, 32, 32,
                  45, 430, 72, 72, 0.0f, 0.9f);
    /* Exercise source dimensions != trimmed logical target dimensions. */
    NativeGXMTpag trimmed = {128, 0, 32, 32, 8, 12, 64, 48, 80, 72};
    native_gxm_draw_tpag(vg, image, ATLAS_W, ATLAS_H, &trimmed,
                         817, 441, 20, 17, 0.94f, 0.94f, 0.0f, 0.95f);
}

void atlas_probe_destroy(NVGcontext *vg, int image) {
    if (image > 0) nvgDeleteImage(vg, image);
}
