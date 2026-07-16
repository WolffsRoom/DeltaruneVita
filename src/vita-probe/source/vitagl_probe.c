#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>
#include "data_win.h"
#include "image_decoder.h"

#define DATA_DIR "ux0:data/deltarune/butterscotch"
#define LOG_PATH DATA_DIR "/butterscotch-probe.log"

static void log_line(const char *text) {
    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static int file_exists(const char *path) {
    SceIoStat stat;
    return sceIoGetstat(path, &stat) >= 0;
}

static uint32_t free_user_memory(void) {
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) < 0) return 0;
    return (uint32_t)info.size_user;
}

static void log_memory(const char *phase) {
    char line[320];
    snprintf(line, sizeof(line), "MEM phase=%s free_user=%u", phase, free_user_memory());
    log_line(line);
}

static void parser_progress(const char *chunk, int index, int total,
                            DataWin *dw, void *user) {
    (void)dw;
    (void)user;
    char line[128];
    snprintf(line, sizeof(line), "PARSER chunk=%.4s index=%d/%d", chunk, index + 1, total);
    log_line(line);
    sceKernelDelayThread(2000);
}

int main(void) {
    sceIoMkdir("ux0:data/deltarune", 0777);
    sceIoMkdir(DATA_DIR, 0777);
    sceIoRemove(LOG_PATH);
    log_line("Deltarune Butterscotch SPRT probe 00.11");
    log_line("Scope=vitaGL+chapter0 GEN8 STRG SPRT TPAG selected TXTR");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    log_line("CTRL=initialized");
    log_memory("before_vitagl");

    int shacc_primary = file_exists("ur0:data/libshacccg.suprx");
    int shacc_external = file_exists("ur0:data/external/libshacccg.suprx");
    char line[320];
    snprintf(line, sizeof(line), "SHACC primary=%d external=%d", shacc_primary, shacc_external);
    log_line(line);

    log_line("VITAGL=init_begin reserve=33554432 msaa=none");
    vglInitExtended(0, 960, 544, 32 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    log_line("VITAGL=init_complete");
    log_memory("after_vitagl");

    log_line("VITAGL=viewport_begin");
    glViewport(0, 0, 960, 544);
    log_line("VITAGL=viewport_complete");

    log_line("PARSER=chapter0_begin");
    DataWinParserOptions options;
    memset(&options, 0, sizeof(options));
    options.parseGen8 = true;
    options.parseStrg = true;
    options.parseSprt = true;
    options.parseTpag = true;
    options.parseTxtr = true;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadTextures = true;
    options.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    options.progressCallback = parser_progress;
    DataWin *dw = DataWin_parse(DATA_DIR "/chapter0/game.droid", options);
    if (!dw || dw->txtr.count == 0) {
        log_line("ERROR=no_txtr_pages");
        sceKernelExitProcess(1);
    }
    snprintf(line, sizeof(line), "PARSER=chapter0_complete name=%s sprt_count=%u tpag_count=%u txtr_count=%u",
             dw->gen8.displayName ? dw->gen8.displayName : "(null)",
             dw->sprt.count, dw->tpag.count, dw->txtr.count);
    log_line(line);
    log_memory("after_parser");

    int sprite_id = -1;
    int fallback_sprite_id = -1;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        Sprite *s = &dw->sprt.sprites[i];
        if (!s->present || s->textureCount == 0 || !s->tpagIndices) continue;
        int tpag_id = s->tpagIndices[0];
        if (tpag_id < 0 || (uint32_t)tpag_id >= dw->tpag.count) continue;
        TexturePageItem *candidate = &dw->tpag.items[tpag_id];
        if (!candidate->present || candidate->texturePageId < 0 ||
            (uint32_t)candidate->texturePageId >= dw->txtr.count) continue;
        if (fallback_sprite_id < 0) fallback_sprite_id = (int)i;
        if (s->name && (strstr(s->name, "logo") || strstr(s->name, "title"))) {
            sprite_id = (int)i;
            break;
        }
    }
    if (sprite_id < 0) sprite_id = fallback_sprite_id;
    if (sprite_id < 0) {
        log_line("ERROR=no_renderable_sprite");
        sceKernelExitProcess(2);
    }

    Sprite *sprite = &dw->sprt.sprites[sprite_id];
    int tpag_id = sprite->tpagIndices[0];
    TexturePageItem selected_tpag = dw->tpag.items[tpag_id];
    uint32_t page_id = (uint32_t)selected_tpag.texturePageId;
    char sprite_name[96];
    snprintf(sprite_name, sizeof(sprite_name), "%s", sprite->name ? sprite->name : "(null)");
    snprintf(line, sizeof(line),
             "SPRT selected id=%d name=%s frames=%u tpag=%d page=%u src=%u,%u,%ux%u target=%ux%u",
             sprite_id, sprite_name, sprite->textureCount, tpag_id, page_id,
             selected_tpag.sourceX, selected_tpag.sourceY,
             selected_tpag.sourceWidth, selected_tpag.sourceHeight,
             selected_tpag.targetWidth, selected_tpag.targetHeight);
    log_line(line);

    Texture *page = &dw->txtr.textures[page_id];
    snprintf(line, sizeof(line), "TXTR selected page=%u offset=%u blob_size=%u",
             page_id, page->blobOffset, page->blobSize);
    log_line(line);
    log_line("TXTR=lazy_load_begin");
    DataWin_loadTxtrIfNeeded(dw, page_id);
    log_line(page->blobData ? "TXTR=lazy_load_complete" : "ERROR=txtr_lazy_load_failed");
    if (!page->blobData) sceKernelExitProcess(2);
    log_memory("after_blob_load");

    int tex_w = 0, tex_h = 0;
    log_line("TXTR=decode_begin");
    uint8_t *pixels = ImageDecoder_decodeToRgba(page->blobData, page->blobSize,
                                                DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0),
                                                &tex_w, &tex_h);
    if (!pixels) {
        log_line("ERROR=txtr_decode_failed");
        sceKernelExitProcess(3);
    }
    snprintf(line, sizeof(line), "TXTR=decode_complete width=%d height=%d", tex_w, tex_h);
    log_line(line);
    log_memory("after_decode");

    GLuint texture = 0;
    log_line("TXTR=upload_begin");
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w, tex_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    DataWin_free(dw);
    log_line("TXTR=upload_complete_data_freed");
    log_memory("after_upload_and_free");

    float draw_w = selected_tpag.targetWidth ? selected_tpag.targetWidth : selected_tpag.sourceWidth;
    float draw_h = selected_tpag.targetHeight ? selected_tpag.targetHeight : selected_tpag.sourceHeight;
    float scale_x = 640.0f / draw_w;
    float scale_y = 416.0f / draw_h;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > 8.0f) scale = 8.0f;
    draw_w *= scale;
    draw_h *= scale;
    float left = (960.0f - draw_w) * 0.5f;
    float top = (544.0f - draw_h) * 0.5f;
    const GLfloat vertices[] = {left, top, left + draw_w, top,
                                left + draw_w, top + draw_h, left, top + draw_h};
    float u0 = (float)selected_tpag.sourceX / (float)tex_w;
    float v0 = (float)selected_tpag.sourceY / (float)tex_h;
    float u1 = (float)(selected_tpag.sourceX + selected_tpag.sourceWidth) / (float)tex_w;
    float v1 = (float)(selected_tpag.sourceY + selected_tpag.sourceHeight) / (float)tex_h;
    const GLfloat uvs[] = {u0, v0, u1, v0, u1, v1, u0, v1};
    snprintf(line, sizeof(line), "SPRT draw name=%s size=%.0fx%.0f uv=%.4f,%.4f,%.4f,%.4f",
             sprite_name, draw_w, draw_h, u0, v0, u1, v1);
    log_line(line);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    log_line("SPRT=draw_pipeline_ready");

    unsigned int frame = 0;
    for (;;) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) {
            log_line("EXIT=start_pressed");
            break;
        }

        if (frame == 0) log_line("VITAGL=background_clear_begin");
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.03f, 0.05f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (frame == 0) log_line("VITAGL=background_clear_complete");
        glBindTexture(GL_TEXTURE_2D, texture);
        if (frame == 0) log_line("SPRT=first_draw_begin");
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        if (frame == 0) log_line("SPRT=first_draw_complete");
        if (frame == 0) log_line("VITAGL=first_swap_begin");
        vglSwapBuffers(GL_FALSE);
        if (frame == 0) log_line("VITAGL=first_swap_complete");
        frame++;
        if (frame == 60) {
            log_line("VITAGL=60_frames_complete");
            log_memory("frame_60");
        }
        sceKernelDelayThread(1000);
    }

    snprintf(line, sizeof(line), "FRAMES=%u", frame);
    log_line(line);
    glDeleteTextures(1, &texture);
    log_line("PROCESS=exit_clean");
    sceKernelExitProcess(0);
    return 0;
}
