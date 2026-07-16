#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "debugScreen.h"
#include "data_win.h"

#define DATA_DIR "ux0:data/deltarune/butterscotch"
#define LOG_PATH DATA_DIR "/butterscotch-probe.log"

typedef struct {
    int present;
    int valid_form;
    int has_gen8;
    uint64_t size;
    uint32_t wad_version;
    uint32_t width;
    uint32_t height;
    uint32_t version[4];
    uint32_t free_before;
    uint32_t free_parsed;
    uint32_t free_after;
    char display_name[64];
    char error[48];
} ProbeResult;

static volatile int exit_requested = 0;

static void log_line(const char *text) {
    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t free_user_memory(void) {
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) < 0) return 0;
    return (uint32_t)info.size_user;
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (!src) src = "(sem nome)";
    snprintf(dst, dst_size, "%s", src);
}

static void parser_progress(const char *chunk, int index, int total,
                            DataWin *data_win, void *user_data) {
    (void)data_win;
    int chapter = *(int *)user_data;
    char line[128];
    snprintf(line, sizeof(line), "CH%d PARSER chunk=%.4s index=%d/%d",
             chapter, chunk, index + 1, total);
    log_line(line);
    psvDebugScreenPrintf("\e[5;1H\e[2KCapitulo %d: chunk %.4s (%d/%d)",
                         chapter, chunk, index + 1, total);
    psvDebugScreenPrintf("\e[7;1H\e[2KSTART: parar apos o capitulo atual");
    SceCtrlData pad;
    memset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    if (pad.buttons & SCE_CTRL_START) exit_requested = 1;
    sceKernelDelayThread(2000);
}

static void parse_metadata(int chapter, const char *path, ProbeResult *r) {
    char line[256];
    r->free_before = free_user_memory();
    snprintf(line, sizeof(line), "CH%d METADATA begin free_user=%u", chapter, r->free_before);
    log_line(line);

    DataWinParserOptions options;
    memset(&options, 0, sizeof(options));
    options.parseGen8 = true;
    options.parseStrg = true;
    options.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    options.progressCallback = parser_progress;
    options.progressCallbackUserData = &chapter;

    DataWin *dw = DataWin_parse(path, options);
    if (!dw) {
        strcpy(r->error, "parser retornou nulo");
        return;
    }

    r->free_parsed = free_user_memory();
    r->wad_version = dw->gen8.wadVersion;
    r->width = dw->gen8.defaultWindowWidth;
    r->height = dw->gen8.defaultWindowHeight;
    r->version[0] = dw->gen8.major;
    r->version[1] = dw->gen8.minor;
    r->version[2] = dw->gen8.release;
    r->version[3] = dw->gen8.build;
    copy_string(r->display_name, sizeof(r->display_name), dw->gen8.displayName);

    snprintf(line, sizeof(line),
             "CH%d METADATA ok name=%s wad=%u version=%u.%u.%u.%u resolution=%ux%u free_parsed=%u",
             chapter, r->display_name, r->wad_version,
             r->version[0], r->version[1], r->version[2], r->version[3],
             r->width, r->height, r->free_parsed);
    log_line(line);

    DataWin_free(dw);
    r->free_after = free_user_memory();
    snprintf(line, sizeof(line), "CH%d METADATA freed free_user=%u", chapter, r->free_after);
    log_line(line);
}

static ProbeResult probe_file(const char *path) {
    ProbeResult r = {0};
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        strcpy(r.error, "ausente");
        return r;
    }
    r.present = 1;
    int64_t end = sceIoLseek(fd, 0, SCE_SEEK_END);
    if (end <= 0) {
        strcpy(r.error, "tamanho invalido");
        sceIoClose(fd);
        return r;
    }
    r.size = (uint64_t)end;
    sceIoLseek(fd, 0, SCE_SEEK_SET);

    uint8_t header[8];
    if (sceIoRead(fd, header, sizeof(header)) != sizeof(header) || memcmp(header, "FORM", 4)) {
        strcpy(r.error, "nao e FORM/GameMaker");
        sceIoClose(fd);
        return r;
    }
    r.valid_form = 1;

    uint64_t pos = 8;
    while (pos + 8 <= r.size) {
        uint8_t chunk[8];
        if (sceIoRead(fd, chunk, sizeof(chunk)) != sizeof(chunk)) break;
        uint32_t chunk_size = le32(chunk + 4);
        if (!memcmp(chunk, "GEN8", 4)) {
            r.has_gen8 = 1;
            break;
        }
        uint64_t next = pos + 8ULL + chunk_size;
        if (next <= pos || next > r.size) break;
        if (sceIoLseek(fd, (int64_t)next, SCE_SEEK_SET) < 0) break;
        pos = next;
    }
    if (!r.has_gen8) strcpy(r.error, "GEN8 nao encontrado");
    sceIoClose(fd);
    return r;
}

int main(void) {
    sceIoMkdir("ux0:data/deltarune", 0777);
    sceIoMkdir(DATA_DIR, 0777);
    sceIoRemove(LOG_PATH);
    log_line("Deltarune Butterscotch Vita metadata 00.07");
    log_line("Framebuffer seguro: sem Vita2D/GXM; parser GEN8+STRG");
    log_line("UI=framebuffer_init_begin");
    psvDebugScreenInit();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    psvDebugScreenPrintf("\e[2J\e[1;1HDeltarune - Butterscotch Metadata 00.07\n");
    psvDebugScreenPrintf("Framebuffer seguro; GPU, VM e audio desativados\n");
    psvDebugScreenPrintf("Leitura por chunks com pausa para o sistema.\n");
    log_line("UI=framebuffer_init_complete");

    ProbeResult results[6];
    char path[128], line[192];
    for (int i = 0; i < 6; ++i) {
        snprintf(path, sizeof(path), DATA_DIR "/chapter%d/game.droid", i);
        results[i] = probe_file(path);
        snprintf(line, sizeof(line), "CH%d present=%d FORM=%d GEN8=%d size=%llu error=%s",
                 i, results[i].present, results[i].valid_form, results[i].has_gen8,
                 (unsigned long long)results[i].size,
                 results[i].error[0] ? results[i].error : "nenhum");
        log_line(line);
        if (results[i].has_gen8) parse_metadata(i, path, &results[i]);
        if (exit_requested) {
            log_line("USER=exit_requested");
            break;
        }
    }

    psvDebugScreenPrintf("\e[2J\e[1;1HDeltarune - Resultado 00.07\n\n");
    for (int i = 0; i < 6; ++i) {
        if (results[i].display_name[0]) {
            psvDebugScreenPrintf("CH%d: %s WAD%u %ux%u RAM %u>%u MB\n", i,
                                 results[i].display_name, results[i].wad_version,
                                 results[i].width, results[i].height,
                                 results[i].free_before / (1024 * 1024),
                                 results[i].free_after / (1024 * 1024));
        }
    }
    psvDebugScreenPrintf("\nLog salvo em ux0:data/deltarune/butterscotch/\n");
    psvDebugScreenPrintf("Pressione START para sair.\n");

    for (;;) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;
        sceKernelDelayThread(16000);
    }

    log_line("EXIT=start_pressed");
    psvDebugScreenFinish();
    sceKernelExitProcess(0);
    return 0;
}
