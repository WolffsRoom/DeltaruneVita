/*
 * Real GameMaker TPAG smoke test. Loads 3 sprite regions from the user's
 * installed chapter0/texture-cache/page_001.r444, using the corresponding
 * TPAG records in chapter0/data.win. Avoids uploading the entire 2048^2
 * atlas (8 MiB R444, 16 MiB RGBA), limiting peak staging to tiny crops.
 * No original game data is embedded in or redistributed with the VPK.
 */
#include "real_tpag_probe.h"
#include "nanovg.h"
#include <psp2/io/fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CH0_ROOT "ux0:data/deltarune/deltarunevita/chapter0"
#define CH0_WIN CH0_ROOT "/data.win"
#define CH0_CACHE CH0_ROOT "/texture-cache/page_001.r444"
#define R444_MAGIC 0x31435456u /* VTC1 */
#define TPAG_MAX_PAGE 2048u
#define MAX_SPRITE_EDGE 512

typedef struct {
    uint32_t magic, sourceSize, sourceOffset, width, height;
} CacheHeader;

static int read_exact(SceUID fd, void *dst, unsigned bytes) {
    return sceIoRead(fd, dst, bytes) == (int)bytes;
}

static int read_tpag(SceUID fd, int32_t fileSize, unsigned index, NativeGXMTpag *out) {
    uint8_t chunk[8];
    int32_t pos = 8;
    if (sceIoLseek32(fd, 0, SCE_SEEK_SET) < 0 ||
        !read_exact(fd, chunk, 8) || memcmp(chunk, "FORM", 4)) return 0;
    while (pos + 8 <= fileSize) {
        if (sceIoLseek32(fd, pos, SCE_SEEK_SET) < 0 ||
            !read_exact(fd, chunk, 8)) return 0;
        uint32_t size = (uint32_t)chunk[4] | (uint32_t)chunk[5] << 8 |
                        (uint32_t)chunk[6] << 16 | (uint32_t)chunk[7] << 24;
        if (size > (uint32_t)(fileSize - pos - 8)) return 0;
        if (!memcmp(chunk, "TPAG", 4)) {
            uint32_t count = 0, address = 0;
            if (!read_exact(fd, &count, 4) || count > 65535 || index >= count ||
                (uint64_t)(index + 1) * 4u + 4u > size) return 0;
            if (sceIoLseek32(fd, pos + 12 + (int32_t)index * 4, SCE_SEEK_SET) < 0 ||
                !read_exact(fd, &address, 4) || address > (uint32_t)fileSize - 22u) return 0;
            uint16_t field[11] = {0};
            if (sceIoLseek32(fd, (int32_t)address, SCE_SEEK_SET) < 0 ||
                !read_exact(fd, field, sizeof(field)) || field[10] != 1) return 0;
            *out = (NativeGXMTpag){
                field[0], field[1], field[2], field[3],
                field[4], field[5], field[6], field[7], field[8], field[9]
            };
            return out->sourceWidth > 0 && out->sourceHeight > 0 &&
                   out->sourceWidth <= MAX_SPRITE_EDGE &&
                   out->sourceHeight <= MAX_SPRITE_EDGE;
        }
        pos += 8 + (int32_t)size;
    }
    return 0;
}

static int next_power_of_two(int value) {
    int out = 1;
    while (out < value) out *= 2;
    return out;
}

static int load_r444_crop(NVGcontext *vg, SceUID fd, CacheHeader hdr,
                          NativeGXMTpag *item, int *outWidth, int *outHeight) {
    if (hdr.magic != R444_MAGIC || hdr.width != 2048 ||
        hdr.height != 2048 || hdr.width > TPAG_MAX_PAGE ||
        item->sourceX < 0 || item->sourceY < 0 ||
        item->sourceX >= (int)hdr.width || item->sourceY >= (int)hdr.height ||
        item->sourceWidth > (int)hdr.width - item->sourceX ||
        item->sourceHeight > (int)hdr.height - item->sourceY) return -1;
    const int w = next_power_of_two(item->sourceWidth);
    const int h = next_power_of_two(item->sourceHeight);
    uint8_t *rgba = (uint8_t*)calloc((size_t)w * (size_t)h, 4);
    uint16_t *row = (uint16_t*)malloc((size_t)item->sourceWidth * 2);
    if (!rgba || !row) {
        free(rgba);
        free(row);
        return -2;
    }
    int image = -1;
    int ok = 1;
    for (int y = 0; y < item->sourceHeight; ++y) {
        uint32_t pixelIndex = (uint32_t)(item->sourceY + y) * hdr.width +
                              (uint32_t)item->sourceX;
        int32_t byteOffset = 20 + (int32_t)(pixelIndex * 2u);
        if (sceIoLseek32(fd, byteOffset, SCE_SEEK_SET) < 0 ||
            !read_exact(fd, row, (unsigned)item->sourceWidth * 2u)) {
            ok = 0;
            break;
        }
        for (int x = 0; x < item->sourceWidth; ++x) {
            uint16_t p = row[x];
            uint8_t *dst = rgba + ((size_t)y * w + x) * 4u;
            dst[0] = (uint8_t)(((p >> 12) & 15u) * 17u);
            dst[1] = (uint8_t)(((p >> 8) & 15u) * 17u);
            dst[2] = (uint8_t)(((p >> 4) & 15u) * 17u);
            dst[3] = (uint8_t)((p & 15u) * 17u);
        }
    }
    if (ok) image = nvgCreateImageRGBA(vg, w, h, NVG_IMAGE_NEAREST, rgba);
    free(row);
    free(rgba);
    if (image <= 0) return -3;
    /* Subrect is now at 0,0 in the cropped page; logical bounds unchanged. */
    item->sourceX = item->sourceY = 0;
    *outWidth = w;
    *outHeight = h;
    return image;
}

int real_tpag_probe_create(NVGcontext *vg, RealTpagProbe *probe) {
    static const unsigned indices[3] = {6, 11, 20};
    if (!vg || !probe) return -1;
    memset(probe, 0, sizeof(*probe));
    SceUID win = sceIoOpen(CH0_WIN, SCE_O_RDONLY, 0);
    if (win < 0) return -2;
    int32_t fileSize = sceIoLseek32(win, 0, SCE_SEEK_END);
    if (fileSize < 32) {
        sceIoClose(win);
        return -3;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!read_tpag(win, fileSize, indices[i], &probe->entries[i])) {
            sceIoClose(win);
            return -4;
        }
    }
    sceIoClose(win);
    SceUID cache = sceIoOpen(CH0_CACHE, SCE_O_RDONLY, 0);
    if (cache < 0) return -5;
    CacheHeader hdr;
    int good = read_exact(cache, &hdr, sizeof(hdr));
    if (!good || hdr.magic != R444_MAGIC || hdr.width != 2048 ||
        hdr.height != 2048) {
        sceIoClose(cache);
        return -6;
    }
    int err = 0;
    for (unsigned i = 0; i < 3; ++i) {
        int img = load_r444_crop(vg, cache, hdr, &probe->entries[i],
                                 &probe->pageW[i], &probe->pageH[i]);
        if (img < 0) {
            err = -7;
            break;
        }
        probe->images[i] = img;
        probe->loaded++;
    }
    sceIoClose(cache);
    if (err) real_tpag_probe_destroy(vg, probe);
    return err ? err : probe->loaded;
}

void real_tpag_probe_draw(NVGcontext *vg, const RealTpagProbe *p, float t) {
    if (!p || p->loaded != 3) return;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 35, 28, 890, 486, 9);
    nvgFillColor(vg, nvgRGBA(16, 19, 33, 255));
    nvgFill(vg);

    /* TPAG 6, trimmed part of a real game texture page. */
    native_gxm_draw_tpag(vg, p->images[0], p->pageW[0], p->pageH[0],
                         &p->entries[0], 215, 148, 128, 128,
                         1.10f, 1.10f, 0.0f, 1.0f);
    native_gxm_draw_tpag(vg, p->images[0], p->pageW[0], p->pageH[0],
                         &p->entries[0], 655, 164, 128, 128,
                         0.85f, 0.85f, sinf(t) * 18.0f, 0.65f);
    /* TPAG 11: cropped 26x23, scaled for legibility. */
    native_gxm_draw_tpag(vg, p->images[1], p->pageW[1], p->pageH[1],
                         &p->entries[1], 160, 424, 13, 11,
                         5, 5, 0.0f, 1.0f);
    /* TPAG 20: trimmed 10x9 sprite; target origin is 1,1. */
    native_gxm_draw_tpag(vg, p->images[2], p->pageW[2], p->pageH[2],
                         &p->entries[2], 630, 424, 6, 5,
                         8, 8, -sinf(t) * 12.0f, 1.0f);
}

void real_tpag_probe_destroy(NVGcontext *vg, RealTpagProbe *probe) {
    if (!probe) return;
    for (int i = 0; i < 3; ++i) {
        if (probe->images[i] > 0 && vg) nvgDeleteImage(vg, probe->images[i]);
        probe->images[i] = 0;
    }
    probe->loaded = 0;
}
