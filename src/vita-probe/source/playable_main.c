#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "data_win.h"
#include "gl_legacy_renderer.h"
#include "audio/openal/al_audio_system.h"
#include "overlay_file_system.h"
#include "runner.h"
#include "runner_gamepad.h"
#include "runner_keyboard.h"
#include "runner_mouse.h"
#include "spatial_grid.h"
#include "vita_settings.h"
#include "vita_borders.h"
#include "vm.h"
#include "profiler.h"
#include "stb_image.h"

#define DATA_ROOT "ux0:data/deltarune/deltarunevita/"
#define SAVE_PATH "ux0:data/deltarune_saves/"
#define LOG_PATH DATA_ROOT "butterscotch-probe.log"
#define NEXT_CHAPTER_PATH DATA_ROOT "next-chapter.txt"
#define DEV_LOG_ROOT DATA_ROOT "devlogs"
#define PORT_BUILD_VERSION "v0.57-r3"

// Read by the Vita renderer to apply chapter-specific memory safety policies.
int g_vitaActiveChapter = 0;

static SceUID dev_log_fd = -1;
static GLuint loading_overlay_texture = 0;
static GLuint loading_textures_tex = 0;
static GLuint loading_frame_textures[4] = {0};
static void log_line(const char *text);

typedef struct { GLfloat u, v, x, y; } LoadingVertex;

static GLuint load_loading_texture(const char* path) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0) { sceIoClose(fd); return 0; }
    unsigned char* data = (unsigned char*)malloc((size_t)size);
    if (data == NULL || sceIoRead(fd, data, (unsigned int)size) != size) {
        free(data); sceIoClose(fd); return 0;
    }
    sceIoClose(fd);
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    free(data);
    if (pixels == NULL) return 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return texture;
}

static void loading_screen_init(void) {
    loading_overlay_texture = load_loading_texture("app0:assets/loading/Loading.png");
    loading_textures_tex = load_loading_texture("app0:assets/loading/textures.png");
    char path[96];
    for (int i = 0; i < 4; ++i) {
        snprintf(path, sizeof(path), "app0:assets/loading/frame_%d.png", i);
        loading_frame_textures[i] = load_loading_texture(path);
    }
}

static void loading_screen_shutdown(void) {
    if (loading_overlay_texture != 0) glDeleteTextures(1, &loading_overlay_texture);
    if (loading_textures_tex != 0) glDeleteTextures(1, &loading_textures_tex);
    for (int i = 0; i < 4; i++) {
        if (loading_frame_textures[i] != 0) glDeleteTextures(1, &loading_frame_textures[i]);
    }
    loading_overlay_texture = 0;
    loading_textures_tex = 0;
    memset(loading_frame_textures, 0, sizeof(loading_frame_textures));
}

static void draw_loading_texture(GLuint texture, float left, float top, float right, float bottom) {
    if (texture == 0) return;
    float x0 = left / 480.0f - 1.0f, x1 = right / 480.0f - 1.0f;
    float y0 = 1.0f - top / 272.0f, y1 = 1.0f - bottom / 272.0f;
    const LoadingVertex vertices[4] = {
        {0, 0, x0, y0}, {1, 0, x1, y0}, {1, 1, x1, y1}, {0, 1, x0, y1}
    };
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(LoadingVertex), &vertices[0].u);
    glVertexPointer(2, GL_FLOAT, sizeof(LoadingVertex), &vertices[0].x);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void dev_log_write(const char* text) {
    if (dev_log_fd < 0 || text == NULL) return;
    sceIoWrite(dev_log_fd, text, strlen(text));
    sceIoWrite(dev_log_fd, "\n", 1);
}

static void dev_log_stop(void) {
    if (dev_log_fd < 0) return;
    dev_log_write("DEV_CAPTURE=end");
    sceIoClose(dev_log_fd);
    dev_log_fd = -1;
}

static void dev_log_start(const char* room) {
    dev_log_stop();
    sceIoMkdir(DEV_LOG_ROOT, 0777);
    char safeRoom[64];
    size_t j = 0;
    const char* source = room != NULL ? room : "unknown";
    for (size_t i = 0; source[i] != '\0' && j + 1 < sizeof(safeRoom); ++i) {
        char c = source[i];
        safeRoom[j++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_';
    }
    safeRoom[j] = '\0';
    SceDateTime now;
    memset(&now, 0, sizeof(now));
    sceRtcGetCurrentClockLocalTime(&now);
    char path[256];
    snprintf(path, sizeof(path), DEV_LOG_ROOT "/%s_%04d%02d%02d-%02d%02d%02d.log",
             safeRoom, now.year, now.month, now.day, now.hour, now.minute, now.second);
    dev_log_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (dev_log_fd >= 0) {
        dev_log_write("DELTARUNE_VITA_DEV_CAPTURE=1");
        dev_log_write("BUILD_VERSION=" PORT_BUILD_VERSION);
        char line[320];
        snprintf(line, sizeof(line), "START_ROOM=%s FILE=%s", source, path);
        dev_log_write(line);
    }
}

static bool place_contact_sprite(const char* name) {
    if (name == NULL) return false;
    // Vessel creator assets. Keep this deliberately narrow: preloading every
    // dialogue "face" or every battle "body" would fill CDRAM before gameplay.
    return strncmp(name, "spr_face_b", 10) == 0 ||
           strcmp(name, "spr_face_tbody") == 0 ||
           strstr(name, "contact") != NULL || strstr(name, "vessel") != NULL;
}

static void preload_place_contact_atlases(DataWin* dw, GLLegacyRenderer* gl) {
    if (dw == NULL || gl == NULL) return;
    bool* seen = (bool*)calloc(dw->txtr.count, sizeof(bool));
    if (seen == NULL) return;
    uint32_t pages = 0;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        Sprite* sprite = &dw->sprt.sprites[i];
        if (!sprite->present || !place_contact_sprite(sprite->name)) continue;
        for (uint32_t frame = 0; frame < sprite->textureCount; ++frame) {
            int32_t tpagIndex = sprite->tpagIndices[frame];
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            int32_t page = dw->tpag.items[tpagIndex].texturePageId;
            if (page < 0 || (uint32_t)page >= dw->txtr.count || seen[page]) continue;
            seen[page] = true;
            if (GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t)page)) pages++;
        }
    }
    char line[96];
    snprintf(line, sizeof(line), "PLACE_CONTACT_PRELOAD=complete pages=%u", pages);
    log_line(line);
    free(seen);
}

// The Vita defaults the main thread to a 256 KiB stack. The first fixed-pipeline
// shader generated by vitaGL/ShaccCg needs substantially more and corrupted the
// return stack before the first draw. Keep this symbol global: the Vita runtime
// reads it while creating the main thread.
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;

typedef struct { uint32_t mask; int key; } KeyMap;
static const KeyMap KEY_MAP[] = {
    {SCE_CTRL_UP, VK_UP}, {SCE_CTRL_DOWN, VK_DOWN},
    {SCE_CTRL_LEFT, VK_LEFT}, {SCE_CTRL_RIGHT, VK_RIGHT},
    {SCE_CTRL_CROSS, 'Z'}, {SCE_CTRL_CIRCLE, 'X'},
    {SCE_CTRL_SQUARE, 'X'}, {SCE_CTRL_TRIANGLE, 'C'},
    {SCE_CTRL_LTRIGGER, VK_PAGEDOWN},
    {SCE_CTRL_RTRIGGER, VK_PAGEUP}
};

static void log_line(const char *text) {
    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static void restore_original_cyber_music_room(DataWin* dw, int chapter) {
    if (dw == NULL || chapter != 2) return;
    int current = -1, original = -1;
    for (uint32_t i = 0; i < dw->room.count; ++i) {
        const char* name = dw->room.rooms[i].name;
        if (name == NULL) continue;
        if (strcmp(name, "room_dw_cyber_music_bullet") == 0) current = (int)i;
        else if (strcmp(name, "room_dw_cyber_music_bullet_original") == 0) original = (int)i;
    }
    if (current < 0 || original < 0) return;
    const char* currentName = dw->room.rooms[current].name;
    const char* originalName = dw->room.rooms[original].name;
    Room temp = dw->room.rooms[current];
    dw->room.rooms[current] = dw->room.rooms[original];
    dw->room.rooms[original] = temp;
    // Preserve resource indices/names so compiled GML comparisons against the
    // normal room keep working while its payload comes from the original room.
    dw->room.rooms[current].name = currentName;
    dw->room.rooms[original].name = originalName;
    log_line("ROOM_PATCH=cyber_music_bullet_original_draw_path");
}

static void progress(const char *chunk, int index, int total, DataWin *dw, void *user) {
    (void)dw; (void)user;
    char line[96];
    snprintf(line, sizeof(line), "LOAD chunk=%.4s %d/%d", chunk, index + 1, total);
    log_line(line);
    sceKernelDelayThread(1000);
}

static void draw_loading_screen(bool showProgress, float ratio, int labelType) {
    glViewport(0, 0, 960, 544);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    draw_loading_texture(loading_overlay_texture, 0, 0, 960, 544);
    int animationFrame = (int)((sceKernelGetProcessTimeWide() / 750000ULL) % 4ULL);
    draw_loading_texture(loading_frame_textures[animationFrame], 367, 128, 623, 272);
    
    if (labelType == 1 && loading_textures_tex != 0) {
        draw_loading_texture(loading_textures_tex, 0, 0, 960, 544);
    }
    if (showProgress) {
        // A loading bar made with glBegin triggers VitaGL's fixed-pipeline
        // shader compiler before the first normal frame. Scissored clears draw
        // the same bar without creating another shader.
        glEnable(GL_SCISSOR_TEST);
        glScissor(298, 113, 364, 14);
        glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        int fill = (int)(356.0f * ratio);
        if (fill < 1) fill = 1;
        glScissor(302, 117, fill, 6);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
    vglSwapBuffers(GL_FALSE);
}

static void texture_loading_progress(uint32_t current, uint32_t total, void *user) {
    (void)user;
    float ratio = total > 0 ? (float)current / (float)total : 1.0f;
    draw_loading_screen(true, ratio, 1);
}

static void set_key(RunnerKeyboardState *kb, int key, bool down, bool *previous) {
    if (down && !*previous) RunnerKeyboard_onKeyDown(kb, key);
    else if (!down && *previous) RunnerKeyboard_onKeyUp(kb, key);
    *previous = down;
}

static void set_key_pulse(RunnerKeyboardState *kb, int key, bool down, bool *physicalPrevious) {
    if (down && !*physicalPrevious) RunnerKeyboard_onKeyDown(kb, key);
    else if (*physicalPrevious && kb->keyDown[key]) RunnerKeyboard_onKeyUp(kb, key);
    *physicalPrevious = down;
}

static int consume_next_chapter(void) {
    char value = '0';
    SceUID fd = sceIoOpen(NEXT_CHAPTER_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoRead(fd, &value, 1);
        sceIoClose(fd);
        sceIoRemove(NEXT_CHAPTER_PATH);
    }
    return value >= '0' && value <= '5' ? value - '0' : 0;
}

static bool restart_into_chapter(int chapter) {
    if (chapter < 0 || chapter > 5) return false;
    char value = (char)('0' + chapter);
    SceUID fd = sceIoOpen(NEXT_CHAPTER_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    bool written = sceIoWrite(fd, &value, 1) == 1;
    sceIoClose(fd);
    if (!written) return false;
    char line[80];
    snprintf(line, sizeof(line), "GAME_CHANGE=loadexec chapter=%d", chapter);
    log_line(line);
    int result = sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
    snprintf(line, sizeof(line), "GAME_CHANGE=loadexec_returned result=0x%08X", result);
    log_line(line);
    return result >= 0;
}

static int chapter_from_request(const char *working_directory) {
    if (working_directory == NULL) return -1;
    if (strstr(working_directory, "launcher") != NULL) return 0;
    const char *chapter = strstr(working_directory, "chapter");
    if (chapter == NULL) return -1;
    chapter += strlen("chapter");
    return chapter[0] >= '0' && chapter[0] <= '5' ? chapter[0] - '0' : -1;
}

static void migrate_save_directory(const char* old_save_dir) {
    SceUID dfd = sceIoDopen(old_save_dir);
    if (dfd >= 0) {
        SceIoDirent dir;
        while (sceIoDread(dfd, &dir) > 0) {
            if (strstr(dir.d_name, "filech") == dir.d_name ||
                strcmp(dir.d_name, "dr.ini") == 0 ||
                strcmp(dir.d_name, "true_config.ini") == 0) {
                char old_path[256];
                char new_path[256];
                snprintf(old_path, sizeof(old_path), "%s%s", old_save_dir, dir.d_name);
                snprintf(new_path, sizeof(new_path), "%s%s", SAVE_PATH, dir.d_name);
                SceIoStat stat;
                if (sceIoGetstat(new_path, &stat) < 0) {
                    sceIoRename(old_path, new_path);
                }
            }
        }
        sceIoDclose(dfd);
    }
}

static void migrate_old_saves(void) {
    // v0.53-v0.56 used this mixed-case directory. Copy its individual save
    // files into the new stable path without overwriting newer saves.
    migrate_save_directory("ux0:data/DeltaruneSaves/");
    migrate_save_directory("ux0:data/deltarune/");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    sceIoMkdir("ux0:data/deltarune", 0777);
    sceIoMkdir(DATA_ROOT, 0777);
    sceIoMkdir(SAVE_PATH, 0777);
    migrate_old_saves();
    sceIoRemove(LOG_PATH);
    int active_chapter = consume_next_chapter();
    g_vitaActiveChapter = active_chapter;

    log_line("Deltarune Windows data + Butterscotch VitaRenderer " PORT_BUILD_VERSION);
    log_line("MAIN_STACK=4194304");
    char startup_line[96];
    snprintf(startup_line, sizeof(startup_line), "AUDIO=openal ENTRY=chapter%d CONTROLS=vita+touch", active_chapter);
    log_line(startup_line);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    // The generated fixed-pipeline shader used by the legacy renderer trips the
    // aggressive runtime optimizer on Vita. O0 is slower only during the first
    // compilation and avoids that optimizer path entirely.
    vglSetupRuntimeShaderCompiler(SHARK_OPT_SLOW, 0, 0, 0);
    log_line("SHARK=opt_slow fastmath=0 fastprecision=0 fastint=0");
    log_line("RENDERER=vita_arrays_v2 immediate_bridge=all STDIO=unbuffered");
    log_line("VITAGL=init_begin reserve=67108864");
    vglInitExtended(0, 960, 544, 64 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    log_line("VITAGL=init_complete");

    VitaSettings settings;
    VitaSettings_load(&settings);
    VitaSettings_setLauncherMode(active_chapter == 0);
    VitaBorders_init(active_chapter);

    int next_chapter = -1;
    char game_path[192];
    char bundle_path[128];
    snprintf(game_path, sizeof(game_path), DATA_ROOT "chapter%d/game.droid", active_chapter);
    snprintf(bundle_path, sizeof(bundle_path), DATA_ROOT "chapter%d/", active_chapter);
    if (settings.modIndex > 0 && settings.modIndex < settings.modCount) {
        SceIoStat mod_stat;
        char mod_game[160];
        snprintf(mod_game, sizeof(mod_game), DATA_ROOT "mods/%s/chapter%d/game.droid", settings.modNames[settings.modIndex], active_chapter);
        if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
            snprintf(game_path, sizeof(game_path), "%s", mod_game);
            log_line("MOD=alternate_game=enabled");
        }
    }

    DataWinParserOptions options = {0};
    options.parseGen8 = true; options.parseOptn = true; options.parseLang = true;
    options.parseExtn = true; options.parseSond = true; options.parseAgrp = true;
    options.parseSprt = true; options.parseBgnd = true; options.parsePath = true;
    options.parseScpt = true; options.parseGlob = true; options.parseShdr = true;
    options.parseFont = true; options.parseTmln = true; options.parseObjt = true;
    options.parseRoom = true; options.parseTpag = true; options.parseCode = true;
    options.parseVari = true; options.parseFunc = true; options.parseStrg = true;
    options.parseTxtr = true; options.parseAudo = true;
    options.skipLoadingPreciseMasksForNonPreciseSprites = true;
    options.lazyLoadRooms = true;
    options.lazyLoadTextures = true;
    options.lazyLoadAudio = true;
    options.loadType = DATAWINLOADTYPE_LOAD_PER_CHUNK;
    options.progressCallback = progress;

    log_line("DATAWIN=parse_begin");
    DataWin *dw = DataWin_parse(game_path, options);
    log_line("DATAWIN=parse_complete");
    restore_original_cyber_music_room(dw, active_chapter);
    VMContext *vm = VM_create(dw);
    log_line("VM=create_complete");
    OverlayFileSystem *fs = OverlayFileSystem_create(bundle_path, SAVE_PATH);
    char mod_path[160];
    if (settings.modIndex > 0 && settings.modIndex < settings.modCount && active_chapter > 0) {
        snprintf(mod_path, sizeof(mod_path), DATA_ROOT "mods/%s/chapter%d/", settings.modNames[settings.modIndex], active_chapter);
        OverlayFileSystem_setModPath(fs, mod_path);
        log_line("MOD=overlay=enabled");
    } else {
        log_line("MOD=Original overlay=disabled");
    }
    Renderer *renderer = GLLegacyRenderer_create();
    AudioSystem *audio = (AudioSystem *)AlAudioSystem_create();
    log_line("SUBSYSTEMS=create_complete");

    Runner *runner = Runner_create(dw, vm, renderer, (FileSystem *)fs, audio);
    // The Steam launcher requires the Windows branch to emit game_change for
    // chapter selection. Reporting os_psvita leaves it in PLACE_CHAPTER_SELECT.
    runner->osType = OS_WINDOWS;
    log_line("PLATFORM=os_windows steam_launcher_compatible");
    VitaSettings_applyAudio(&settings, audio);
    char *launcher_args[] = {"eboot.bin", "-game", "data.win"};
    char *chapter_args[] = {"eboot.bin", "-game", "data.win", "launcher", "switch_-1", "returning_0"};
    if (active_chapter == 0) {
        Runner_setGameArgs(runner, launcher_args, 3);
        log_line("ARGS=eboot.bin|-game|data.win");
    } else {
        Runner_setGameArgs(runner, chapter_args, 6);
        log_line("ARGS=eboot.bin|-game|data.win|launcher|switch_-1|returning_0");
    }
    runner->debugMode = false;
    log_line("RUNNER=create_complete");
    if (active_chapter > 0) {
        // Chapter bootstrap can create and replace a music stream before the
        // first playable/menu room is presented. Keep that work inaudible;
        // otherwise the loading screen exposes a short start/stop/start pop.
        AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, 0.0f, 0.0f);
        log_line("STARTUP_AUDIO=muted_until_first_presented_frame");
        // Always replace the black chapter-start gap with the loading artwork.
        // The progress bar remains exclusive to actual cache preparation.
        loading_screen_init();
        draw_loading_screen(false, 0.0f, 0);
        log_line("CHAPTER_LOADING=visible bar=hidden");
    }
    Runner_initFirstRoom(runner);
    log_line("RUNNER=first_room_complete");
    if (active_chapter == 1 && runner->currentRoom != NULL && runner->currentRoom->name != NULL &&
        strstr(runner->currentRoom->name, "PLACE_CONTACT") != NULL) {
        preload_place_contact_atlases(dw, (GLLegacyRenderer*)renderer);
    }
    if (settings.devMode && settings.debugDevEnabled) {
        dev_log_start(runner->currentRoom != NULL ? runner->currentRoom->name : NULL);
        Profiler_setEnabled(&runner->vmContext->profiler, true);
    }
    if (active_chapter > 0) {
        log_line("AUDIO_PRELOAD=begin scope=active_chapter");
        // Forced SFX decoding during bootstrap is not safe across DELTARUNE's
        // chapter/mod SOND variants (Chapter 2 can fail here as well). Keep the
        // source-pool allocation, but leave every buffer on the proven
        // on-demand path for all chapters.
        bool preloadSfxBuffers = false;
        uint32_t audioCached = AlAudioSystem_preloadChapterSfx((AlAudioSystem*)audio,
                                                               preloadSfxBuffers);
        char audioCacheLine[96];
        snprintf(audioCacheLine, sizeof(audioCacheLine),
                 "AUDIO_PRELOAD=complete cached=%u capacity=%u decode=%s", audioCached,
                 (unsigned)MAX_SFX_BUFFER_CACHE, preloadSfxBuffers ? "enabled" : "deferred");
        log_line(audioCacheLine);
        // The VitaGL fixed-pipeline bridge and the runner must exist before the
        // loading screen draws. Running this directly after DataWin_parse made
        // the first progress frame touch an uninitialized GL pipeline.
        log_line("TEXTURE_PRELOAD=begin stage=runner_ready");
        
        draw_loading_screen(false, 0.0f, 1);
        
        uint32_t cached = GLLegacyRenderer_prepareTextureCache(dw, texture_loading_progress, NULL);
        char cache_line[96];
        snprintf(cache_line, sizeof(cache_line), "TEXTURE_PRELOAD=complete prepared=%u total=%u",
                 cached, dw->txtr.count);
        log_line(cache_line);
        // Keep a final bar-less frame visible until the runner presents its
        // first scene, including the fast path where all cache files exist.
        draw_loading_screen(false, 1.0f, 1);
        loading_screen_shutdown();
    }
    char display_line[160];
    snprintf(display_line, sizeof(display_line), "DISPLAY=gen8_%dx%d applied_%dx%d host_960x544 mode=native_centered",
             (int)dw->gen8.defaultWindowWidth, (int)dw->gen8.defaultWindowHeight,
             (int)dw->gen8.defaultWindowWidth, (int)dw->gen8.defaultWindowHeight);
    log_line(display_line);

    bool previous[sizeof(KEY_MAP) / sizeof(KEY_MAP[0])] = {0};
    uint32_t frame = 0;
    uint64_t last_time = sceKernelGetProcessTimeWide();
    uint64_t next_frame_deadline = last_time;
    bool exit_requested = false;
    int logged_game_w = -1;
    int logged_game_h = -1;
    int logged_room_index = -999;
    uint64_t perf_window_start = last_time;
    uint64_t perf_total_us = 0, perf_max_us = 0;
    uint32_t perf_frames = 0, perf_drops = 0, perf_severe = 0;
    uint64_t last_slow_log = 0;
    bool save_load_fade = false;
    uint64_t save_load_fade_start = last_time;
    const uint64_t save_load_fade_duration = 1000000ULL;
    // The initial chapter audio was muted above. Do not start its fade on a
    // wall-clock timer until the first game frame has actually reached the
    // display; texture upload time must not make music audible over black.
    int audio_present_wait_frames = active_chapter > 0 ? 1 : 0;
    if (audio_present_wait_frames > 0) log_line("STARTUP_AUDIO_FADE=waiting_for_presented_frame");
    bool border_cycle_dpad_held = false;
    bool dev_room_nav_held = false;
    bool settings_touch_held = false;
    int32_t dev_room_target = runner->currentRoomIndex;
    bool dev_force_move = false;

    while (!exit_requested && !runner->shouldExit) {
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
        RunnerMouse_beginFrame(runner->mouse);
        SceCtrlData pad = {0};
        sceCtrlPeekBufferPositive(0, &pad, 1);

        // The settings UI remains touch-capable even when gameplay touch is
        // disabled. Vita touch coordinates are twice the 960x544 UI space.
        if (settings.open) {
            SceTouchData menu_touch = {0};
            bool touching = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &menu_touch, 1) > 0 &&
                            menu_touch.reportNum > 0;
            if (touching && settings.controlEditMode) {
                float tx = (float)menu_touch.report[0].x * 0.5f;
                float ty = (float)menu_touch.report[0].y * 0.5f;
                if (!settings_touch_held) {
                    int best = 0;
                    int bestDistance = 0x7fffffff;
                    for (int i = 0; i < 4; ++i) {
                        int dx = (int)tx - settings.touchControlX[i];
                        int dy = (int)ty - settings.touchControlY[i];
                        int distance = dx * dx + dy * dy;
                        if (distance < bestDistance) { best = i; bestDistance = distance; }
                    }
                    settings.selectedTouchControl = best;
                }
                settings.touchControlX[settings.selectedTouchControl] = (int)tx;
                settings.touchControlY[settings.selectedTouchControl] = (int)ty;
            } else if (touching && !settings_touch_held) {
                float tx = (float)menu_touch.report[0].x * 0.5f;
                float ty = (float)menu_touch.report[0].y * 0.5f;
                if (ty >= 118.0f && ty <= 166.0f && tx >= 120.0f && tx <= 840.0f) {
                    int category = (int)((tx - 120.0f) / 180.0f);
                    if (category < 0) category = 0;
                    if (category > 3) category = 3;
                    settings.category = category;
                    settings.selected = 0;
                } else if (tx >= 180.0f && tx <= 780.0f && ty >= 168.0f && ty <= 420.0f) {
                    int count = VitaSettings_itemCount(settings.category);
                    float firstY = count == 4 ? 184.0f : (count == 3 ? 196.0f : 214.0f);
                    float spacing = count == 4 ? 58.0f : (count == 3 ? 72.0f : 88.0f);
                    int item = (int)((ty - firstY + spacing * 0.5f) / spacing);
                    if (item >= 0 && item < count) {
                        settings.selected = item;
                        // Route activation through the normal input path so
                        // persistence and menu SFX behave exactly like Cross.
                        pad.buttons |= SCE_CTRL_CROSS;
                    }
                }
            }
            settings_touch_held = touching;
        } else {
            settings_touch_held = false;
        }

        if (!settings.open && settings.devMode && settings.devRoomNavEnabled && (pad.buttons & SCE_CTRL_LTRIGGER)) {
            bool navInput = (pad.buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_CROSS)) != 0;
            if (navInput && !dev_room_nav_held) {
                if (pad.buttons & SCE_CTRL_UP) dev_room_target--;
                else if (pad.buttons & SCE_CTRL_DOWN) dev_room_target++;
                if (dev_room_target < 0) dev_room_target = (int32_t)dw->room.count - 1;
                if ((uint32_t)dev_room_target >= dw->room.count) dev_room_target = 0;
                if (pad.buttons & SCE_CTRL_CROSS) {
                    const char* targetName = dw->room.rooms[dev_room_target].name;
                    char jump[256];
                    snprintf(jump, sizeof(jump), "DEV_ROOM_JUMP from=%d to=%d name=%s",
                             runner->currentRoomIndex, dev_room_target,
                             targetName != NULL ? targetName : "<null>");
                    log_line(jump);
                    dev_log_write(jump);
                    runner->pendingRoom = dev_room_target;
                    dev_force_move = true;
                }
            }
            dev_room_nav_held = navInput;
            // Debug navigation must not leak into the game controls.
            pad.buttons &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_CROSS);
        } else {
            dev_room_nav_held = false;
            if (!settings.devMode || !settings.devRoomNavEnabled) {
                dev_room_target = runner->currentRoomIndex;
                dev_force_move = false;
            }
        }
        
        bool r_trigger = (pad.buttons & SCE_CTRL_RTRIGGER);
        bool border_cycled = false;
        if (!settings.open && r_trigger) {
            if ((pad.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN))) {
                if (!border_cycle_dpad_held) {
                    VitaBorders_cycleCurrent(1);
                    border_cycle_dpad_held = true;
                }
                border_cycled = true;
            } else if ((pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_UP))) {
                if (!border_cycle_dpad_held) {
                    VitaBorders_cycleCurrent(-1);
                    border_cycle_dpad_held = true;
                }
                border_cycled = true;
            } else {
                border_cycle_dpad_held = false;
            }
        } else {
            border_cycle_dpad_held = false;
        }

        if (border_cycled) {
            // Mask the complete shortcut so neither movement nor PageUp leaks
            // into DELTARUNE while a border is being selected.
            pad.buttons &= ~(SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP |
                             SCE_CTRL_DOWN | SCE_CTRL_RTRIGGER);
        }
        
        bool restart_for_settings = VitaSettings_handleInput(&settings, &pad, audio);
        if (settings.debugDevChanged) {
            const char* room = runner->currentRoom != NULL ? runner->currentRoom->name : NULL;
            if (settings.debugDevEnabled) {
                dev_log_start(room);
                Profiler_setEnabled(&runner->vmContext->profiler, true);
            } else {
                dev_log_stop();
                Profiler_setEnabled(&runner->vmContext->profiler, false);
            }
            settings.debugDevChanged = false;
        }
        if (restart_for_settings) {
            int settings_target = settings.returnToChapterSelect ? 0 : active_chapter;
            log_line(settings.returnToChapterSelect ? "SETTINGS=return_to_chapter_select" : "SETTINGS=restart_for_mod");
            if (restart_into_chapter(settings_target)) {
                sceKernelDelayThread(500000);
                sceKernelExitProcess(0);
            }
        }
        int dx = (int)pad.lx - 128, dy = (int)pad.ly - 128;
        SceTouchData touch = {0};
        bool touch_up = false, touch_down = false, touch_left = false, touch_right = false;
        float touch_axis_x = 0.0f, touch_axis_y = 0.0f;
        bool touch_confirm = false, touch_cancel = false, touch_menu = false;
        if (settings.touchEnabled && !settings.open && sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) {
            for (unsigned i = 0; i < touch.reportNum; ++i) {
                int tx = touch.report[i].x;
                int ty = touch.report[i].y;
                int stickCenterX = settings.touchControlX[0] * 2;
                int stickCenterY = settings.touchControlY[0] * 2;
                int stickRadius = 210 * settings.touchControlScale[0] / 100;
                int stickDx = tx - stickCenterX;
                int stickDy = ty - stickCenterY;
                if (stickDx * stickDx + stickDy * stickDy <= stickRadius * stickRadius) {
                    int tdx = stickDx;
                    int tdy = stickDy;
                    touch_axis_x = (float)tdx / (float)stickRadius;
                    touch_axis_y = (float)tdy / (float)stickRadius;
                    if (abs(tdx) > abs(tdy)) {
                        touch_left |= tdx < -(stickRadius * 3 / 7);
                        touch_right |= tdx > (stickRadius * 3 / 7);
                    } else {
                        touch_up |= tdy < -(stickRadius * 3 / 7);
                        touch_down |= tdy > (stickRadius * 3 / 7);
                    }
                } else {
                    // Front-touch coordinates are 1920x1088 while the overlay
                    // is drawn at 960x544. Match the exact on-screen centers of
                    // Z (850,385), X (755,455) and C (755,340). The former
                    // horizontal bands made C and X share the same hit region.
                    const int centersX[3] = {settings.touchControlX[1] * 2, settings.touchControlX[2] * 2, settings.touchControlX[3] * 2};
                    const int centersY[3] = {settings.touchControlY[1] * 2, settings.touchControlY[2] * 2, settings.touchControlY[3] * 2};
                    int best = -1;
                    int bestDistance = 0x7fffffff;
                    for (int button = 0; button < 3; ++button) {
                        int hitRadius = 180 * settings.touchControlScale[button + 1] / 100;
                        int hitRadiusSquared = hitRadius * hitRadius;
                        int bdx = tx - centersX[button];
                        int bdy = ty - centersY[button];
                        int distance = bdx * bdx + bdy * bdy;
                        if (distance <= hitRadiusSquared && distance < bestDistance) {
                            best = button;
                            bestDistance = distance;
                        }
                    }
                    if (best == 0) touch_confirm = true;
                    else if (best == 1) touch_cancel = true;
                    else if (best == 2) touch_menu = true;
                }
            }
        }
        float visual_x = dx < -24 || dx > 24 ? (float)dx / 127.0f : touch_axis_x;
        float visual_y = dy < -24 || dy > 24 ? (float)dy / 127.0f : touch_axis_y;
        VitaSettings_setTouchVisuals(&settings, visual_x, visual_y,
                                     (pad.buttons & SCE_CTRL_CROSS) || touch_confirm,
                                     (pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE)) || touch_cancel,
                                     (pad.buttons & SCE_CTRL_TRIANGLE) || touch_menu);
        for (ptrdiff_t i = 0; i < arrlen(runner->instances); ++i) {
            Instance* inst = runner->instances[i];
            if (inst == NULL || inst->objectIndex < 0 || (uint32_t)inst->objectIndex >= dw->objt.count) continue;
            const char* object_name = dw->objt.objects[inst->objectIndex].name;
            if (object_name != NULL && strcmp(object_name, "obj_mobilecontroller") == 0) {
                // The Android controller uses its own fixed coordinates and
                // fights the Vita editor by snapping a second joystick back
                // to the original layout.  The Vita overlay already draws the
                // same game sprites at the edited coordinates.
                inst->visible = false;
            }
        }

        bool controls_enabled = !settings.open && !settings.adjustMode && settings.inputCooldown == 0;
        set_key(runner->keyboard, VK_UP, controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_UP) || dy < -48 || touch_up), &previous[0]);
        set_key(runner->keyboard, VK_DOWN, controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_DOWN) || dy > 48 || touch_down), &previous[1]);
        set_key(runner->keyboard, VK_LEFT, controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_LEFT) || dx < -48 || touch_left), &previous[2]);
        set_key(runner->keyboard, VK_RIGHT, controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_RIGHT) || dx > 48 || touch_right), &previous[3]);
        set_key(runner->keyboard, 'Z', controls_enabled && ((pad.buttons & SCE_CTRL_CROSS) || touch_confirm), &previous[4]);
        set_key(runner->keyboard, 'X', controls_enabled && ((pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE)) || touch_cancel), &previous[5]);
        set_key_pulse(runner->keyboard, 'C', controls_enabled && ((pad.buttons & SCE_CTRL_TRIANGLE) || touch_menu), &previous[7]);
        set_key(runner->keyboard, VK_PAGEDOWN, controls_enabled && (pad.buttons & SCE_CTRL_LTRIGGER), &previous[8]);
        set_key(runner->keyboard, VK_PAGEUP, controls_enabled && (pad.buttons & SCE_CTRL_RTRIGGER), &previous[9]);

        uint64_t frame_begin = sceKernelGetProcessTimeWide();
        uint64_t now = frame_begin;
        runner->deltaTime = (double)(now - last_time);
        last_time = now;
        uint64_t step_begin = sceKernelGetProcessTimeWide();
        if (dev_log_fd >= 0 && runner->vmContext->profiler != NULL)
            Profiler_reset(runner->vmContext->profiler);
        if (!settings.open && !settings.adjustMode) {
            if (frame == 0) log_line("FRAME0=step_begin");
            Runner_step(runner);
            if (dev_force_move && settings.devMode && settings.devRoomNavEnabled && !(pad.buttons & SCE_CTRL_LTRIGGER)) {
                float moveX = dx < -32 || dx > 32 ? (float)dx / 32.0f :
                              ((pad.buttons & SCE_CTRL_LEFT) ? -4.0f : ((pad.buttons & SCE_CTRL_RIGHT) ? 4.0f : 0.0f));
                float moveY = dy < -32 || dy > 32 ? (float)dy / 32.0f :
                              ((pad.buttons & SCE_CTRL_UP) ? -4.0f : ((pad.buttons & SCE_CTRL_DOWN) ? 4.0f : 0.0f));
                if (moveX != 0.0f || moveY != 0.0f) {
                    for (ptrdiff_t i = 0; i < arrlen(runner->instances); ++i) {
                        Instance* inst = runner->instances[i];
                        if (inst == NULL || inst->objectIndex < 0 || (uint32_t)inst->objectIndex >= dw->objt.count) continue;
                        const char* objectName = dw->objt.objects[inst->objectIndex].name;
                        if (objectName != NULL && strcmp(objectName, "obj_mainchara") == 0) {
                            inst->active = true;
                            inst->visible = true;
                            inst->x += moveX;
                            inst->y += moveY;
                            SpatialGrid_markInstanceAsDirty(runner->spatialGrid, inst);
                            break;
                        }
                    }
                }
            }
            if (frame == 0) log_line("FRAME0=step_complete");
        }
        uint64_t step_us = sceKernelGetProcessTimeWide() - step_begin;
        // Profiling still observes every GML frame, but serializing a multiline
        // report every frame added 30-40 ms of Vita-card I/O. Keep a heartbeat
        // every 30 frames and always retain slow frames where the ranking matters.
        if (dev_log_fd >= 0 && runner->vmContext->profiler != NULL &&
            ((frame % 30U) == 0U || step_us >= 8000ULL)) {
            char* gmlReport = Profiler_createReport(runner->vmContext->profiler, 5, 1);
            if (gmlReport != NULL) {
                char header[64];
                snprintf(header, sizeof(header), "GML_TOP frame=%u", frame);
                dev_log_write(header);
                dev_log_write(gmlReport);
                free(gmlReport);
            }
        }
        if (runner->pendingWorkingDirectory != NULL) {
            int requested_chapter = chapter_from_request(runner->pendingWorkingDirectory);
            char line[160];
            snprintf(line, sizeof(line), "GAME_CHANGE=request cwd=%s params=%s parsed=%d",
                     runner->pendingWorkingDirectory,
                     runner->pendingLaunchParameters ? runner->pendingLaunchParameters : "<null>",
                     requested_chapter);
            log_line(line);
            if (requested_chapter >= 0 && requested_chapter <= 5) {
                next_chapter = requested_chapter;
                if (restart_into_chapter(next_chapter)) {
                    sceKernelDelayThread(500000);
                    sceKernelExitProcess(0);
                }
                log_line("GAME_CHANGE=loadexec_failed");
            } else if (active_chapter > 0) {
                next_chapter = 0;
                log_line("GAME_CHANGE=chapter_select_fallback_launcher");
                if (restart_into_chapter(0)) {
                    sceKernelDelayThread(500000);
                    sceKernelExitProcess(0);
                }
                log_line("GAME_CHANGE=launcher_loadexec_failed");
            } else log_line("GAME_CHANGE=invalid_chapter");
            exit_requested = true;
            continue;
        }
        uint64_t audio_begin = sceKernelGetProcessTimeWide();
        runner->audioSystem->vtable->update(runner->audioSystem, (float)(runner->deltaTime / 1000000.0));
        uint64_t audio_us = sceKernelGetProcessTimeWide() - audio_begin;

        uint64_t render_begin = sceKernelGetProcessTimeWide();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        int game_w = runner->applicationWidth > 0 ? runner->applicationWidth : (int)dw->gen8.defaultWindowWidth;
        int game_h = runner->applicationHeight > 0 ? runner->applicationHeight : (int)dw->gen8.defaultWindowHeight;
        runner->widescreenExtraWidth = 0;
        runner->widescreenExtraHeight = 0;
        if (game_w != logged_game_w || game_h != logged_game_h) {
            char runtime_display[128];
            snprintf(runtime_display, sizeof(runtime_display),
                     "DISPLAY=runtime_%dx%d host_960x544 centered", game_w, game_h);
            log_line(runtime_display);
            logged_game_w = game_w;
            logged_game_h = game_h;
        }
        if (frame == 0) log_line("FRAME0=draw_pre_begin");
        Runner_drawPre(runner, 960, 544);
        if (frame == 0) log_line("FRAME0=draw_pre_complete");
        Runner_beginFrame(runner, game_w, game_h, 960, 544, 960, 544);
        if (frame == 0) log_line("FRAME0=begin_frame_complete");
        Runner_drawViews(runner, game_w, game_h, false);
        if (frame == 0) log_line("FRAME0=draw_views_complete");
        VitaBorders_updateRoom(runner->currentRoom != NULL ? runner->currentRoom->name : NULL);
        // The port settings/calibration panels are menus too; leave their side
        // areas black instead of retaining the current gameplay border.
        if (!settings.open && !settings.adjustMode) VitaBorders_draw(960, 544);
        renderer->vtable->endFrameInit(renderer);
        if (frame == 0) log_line("FRAME0=end_frame_init_complete");
        if (frame == 0) log_line("FRAME0=draw_post_begin");
        Runner_drawPost(runner, 960, 544);
        if (frame == 0) log_line("FRAME0=draw_post_complete");
        renderer->vtable->endFrameEnd(renderer);
        if (frame == 0) log_line("FRAME0=end_frame_end_complete");
        Runner_drawGUI(runner, 960, 544, game_w, game_h);
        if (frame == 0) log_line("FRAME0=draw_gui_complete");
        VitaSettings_drawTouchControls(&settings, renderer);
        if (frame == 0) log_line("FRAME0=touch_overlay_complete");
        VitaSettings_draw(&settings, renderer);
        if (frame == 0) log_line("FRAME0=settings_overlay_complete");
        VitaSettings_drawCalibration(&settings, renderer);
        GLLegacyRenderer* legacy = (GLLegacyRenderer*)renderer;
        uint64_t dev_total_us = sceKernelGetProcessTimeWide() - frame_begin;
        uint64_t dev_render_us = sceKernelGetProcessTimeWide() - render_begin;
        float dev_fps = dev_total_us > 0 ? 1000000.0f / (float)dev_total_us : 0.0f;
        VitaSettings_drawDevOverlay(&settings, renderer,
            runner->currentRoom != NULL ? runner->currentRoom->name : NULL,
            dev_fps, step_us, audio_us, dev_render_us, legacy->residentTextureBytes,
            legacy->vitaTextureEvictions, legacy->vitaTextureDeferred, legacy->vitaTextureRamHits,
            dev_room_target >= 0 && (uint32_t)dev_room_target < dw->room.count ? dw->room.rooms[dev_room_target].name : NULL,
            dev_room_target);
        if (frame == 0) log_line("FRAME0=calibration_overlay_complete");
        if (save_load_fade) {
            uint64_t fade_now = sceKernelGetProcessTimeWide();
            uint64_t elapsed = fade_now - save_load_fade_start;
            float progress = (float)elapsed / (float)save_load_fade_duration;
            if (progress >= 1.0f) {
                progress = 1.0f;
                save_load_fade = false;
                log_line("SAVE_LOAD_FADE=complete");
            }
            AlAudioSystem_setCategoryGains((AlAudioSystem*)audio,
                ((float)settings.musicVolume / 10.0f) * progress,
                (float)settings.sfxVolume / 10.0f);
            if (progress < 1.0f) {
                renderer->vtable->drawRectangle(renderer, 0.0f, 0.0f, 960.0f, 544.0f,
                                                 0x000000, 1.0f - progress, false);
            }
        }
        if (runner->pendingRoom == -1) {
            if (frame == 0) log_line("FRAME0=swap_begin");
            // Frame pacing below already enforces the game's 30 FPS cadence.
            // Waiting for VitaGL's 60 Hz swap as well quantized heavier Chapter
            // 5 scenes to ~50 ms (20 FPS). Submit immediately and keep a single
            // pacing mechanism instead of stacking two waits.
            vglSwapBuffers(GL_FALSE);
            if (frame == 0) log_line("FRAME0=swap_complete");
            if (audio_present_wait_frames > 0) {
                audio_present_wait_frames--;
                if (audio_present_wait_frames == 0) {
                    save_load_fade = true;
                    save_load_fade_start = sceKernelGetProcessTimeWide();
                    log_line("AUDIO_FADE=begin_after_present duration_ms=1000");
                }
            }
        }
        const char* previous_room_name = runner->currentRoom != NULL ? runner->currentRoom->name : NULL;
        bool leaving_save_menu = previous_room_name != NULL &&
            (strcmp(previous_room_name, "PLACE_MENU") == 0 ||
             strstr(previous_room_name, "PLACE_MENU") != NULL);
        int room_before_pending = runner->currentRoomIndex;
        bool starting_save_load = active_chapter > 0 && leaving_save_menu && runner->pendingRoom >= 0;
        const char* pending_room_name = runner->pendingRoom >= 0 &&
            (uint32_t)runner->pendingRoom < dw->room.count ? dw->room.rooms[runner->pendingRoom].name : NULL;
        bool entering_place_menu = active_chapter > 0 && pending_room_name != NULL &&
            strstr(pending_room_name, "PLACE_MENU") != NULL;
        if (starting_save_load || entering_place_menu) {
            // Mute before Room End/room construction/Room Start. Music created by
            // the destination room therefore starts silent instead of playing a
            // short burst and being muted one frame later.
            AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, 0.0f,
                                           (float)settings.sfxVolume / 10.0f);
            log_line("SAVE_LOAD_FADE=muted_before_room_change");
        }
        Runner_handlePendingRoomChange(runner);
        if ((starting_save_load || entering_place_menu) && runner->currentRoomIndex != room_before_pending) {
            // Wait for the destination to render and swap once. Starting the
            // fade here lets room construction/first-use texture uploads consume
            // most of it, causing a short audible burst before PLACE_MENU.
            save_load_fade = false;
            audio_present_wait_frames = 1;
            log_line(entering_place_menu ?
                     "PLACE_MENU_AUDIO=waiting_for_presented_frame" :
                     "SAVE_LOAD_FADE=waiting_for_presented_frame");
        }
        if (runner->profRoomTotalUs > 0) {
            char roomPerf[320];
            snprintf(roomPerf, sizeof(roomPerf),
                     "ROOM_LOAD_TIMING from=%d to=%d parse_us=%llu create_us=%llu end_events_us=%llu start_events_us=%llu cleanup_us=%llu total_us=%llu",
                     room_before_pending, runner->currentRoomIndex,
                     (unsigned long long)runner->profRoomParseUs,
                     (unsigned long long)runner->profRoomCreateUs,
                     (unsigned long long)runner->profRoomEndEventsUs,
                     (unsigned long long)runner->profRoomStartEventsUs,
                     (unsigned long long)runner->profRoomCleanupUs,
                     (unsigned long long)runner->profRoomTotalUs);
            log_line(roomPerf);
            dev_log_write(roomPerf);
        }

        uint64_t work_us = sceKernelGetProcessTimeWide() - frame_begin;
        uint64_t render_us = sceKernelGetProcessTimeWide() - render_begin;
        perf_total_us += work_us;
        if (work_us > perf_max_us) perf_max_us = work_us;
        perf_frames++;
        if (work_us > 40000) perf_drops++;
        if (work_us > 80000) perf_severe++;
        if (runner->currentRoomIndex != logged_room_index) {
            char room_change[192];
            snprintf(room_change, sizeof(room_change), "ROOM_CHANGE frame=%u from=%d to=%d name=%s work_us=%llu",
                     frame, logged_room_index, runner->currentRoomIndex,
                     runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>",
                     (unsigned long long)work_us);
            log_line(room_change);
            dev_log_write(room_change);
            logged_room_index = runner->currentRoomIndex;
        }
        if (dev_log_fd >= 0 && (frame % 30U) == 0U) {
            char dev_sample[512];
            snprintf(dev_sample, sizeof(dev_sample),
                     "DEV_SAMPLE frame=%u room=%s fps=%.2f total_us=%llu step_us=%llu audio_us=%llu render_us=%llu gpu_bytes=%llu evictions=%u deferred=%u ram_hits=%u events_us=%llu alarms_us=%llu spatial_us=%llu collision_us=%llu other_us=%llu",
                     frame, runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>",
                     work_us > 0 ? 1000000.0 / (double)work_us : 0.0,
                     (unsigned long long)work_us, (unsigned long long)step_us,
                     (unsigned long long)audio_us, (unsigned long long)render_us,
                     (unsigned long long)legacy->residentTextureBytes,
                     legacy->vitaTextureEvictions,
                     legacy->vitaTextureDeferred,
                     legacy->vitaTextureRamHits,
                     (unsigned long long)runner->profStepEventsUs,
                     (unsigned long long)runner->profStepAlarmsUs,
                     (unsigned long long)runner->profStepSpatialUs,
                     (unsigned long long)runner->profStepCollisionUs,
                     (unsigned long long)runner->profStepOtherUs);
            dev_log_write(dev_sample);
        }
        if (work_us > 50000 && frame_begin - last_slow_log > 1000000ULL) {
            char slow[384];
            snprintf(slow, sizeof(slow), "PERF_SLOW frame=%u total_us=%llu step_us=%llu audio_us=%llu render_us=%llu gpu_bytes=%llu events_us=%llu alarms_us=%llu collision_us=%llu other_us=%llu room=%s index=%d",
                     frame, (unsigned long long)work_us, (unsigned long long)step_us,
                     (unsigned long long)audio_us, (unsigned long long)render_us,
                     (unsigned long long)legacy->residentTextureBytes,
                     (unsigned long long)runner->profStepEventsUs,
                     (unsigned long long)runner->profStepAlarmsUs,
                     (unsigned long long)runner->profStepCollisionUs,
                     (unsigned long long)runner->profStepOtherUs,
                     runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>",
                     runner->currentRoomIndex);
            log_line(slow);
            last_slow_log = frame_begin;
        }
        if (frame_begin - perf_window_start >= 5000000ULL && perf_frames > 0) {
            char summary[192];
            snprintf(summary, sizeof(summary), "PERF_SUMMARY frames=%u avg_us=%llu max_us=%llu drops40=%u severe80=%u room=%s",
                     perf_frames, (unsigned long long)(perf_total_us / perf_frames),
                     (unsigned long long)perf_max_us, perf_drops, perf_severe,
                     runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>");
            log_line(summary);
            perf_window_start = frame_begin;
            perf_total_us = perf_max_us = 0;
            perf_frames = perf_drops = perf_severe = 0;
        }

        frame++;
        if (frame == 1) log_line("FRAME=first_complete");
        if (frame == 60) {
            char room_line[128];
            snprintf(room_line, sizeof(room_line), "FRAME=60_complete chapter=%d room=%s index=%d",
                     active_chapter,
                     runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>",
                     runner->currentRoomIndex);
            log_line(room_line);
        }
        if (settings.vsyncEnabled) {
            const uint64_t target = 1000000ULL / 30ULL;
            next_frame_deadline += target;
            uint64_t current_time = sceKernelGetProcessTimeWide();
            if (next_frame_deadline > current_time) {
                sceKernelDelayThread((unsigned int)(next_frame_deadline - current_time));
            } else if (current_time - next_frame_deadline > target) {
                next_frame_deadline = current_time;
            }
        } else {
            next_frame_deadline = sceKernelGetProcessTimeWide();
        }
    }

    if (runner->shouldExit && active_chapter > 0) {
        log_line("GAME_CHANGE=game_end_return_to_launcher");
        if (restart_into_chapter(0)) {
            sceKernelDelayThread(500000);
            sceKernelExitProcess(0);
        }
        log_line("GAME_CHANGE=launcher_loadexec_failed");
    }
    log_line(next_chapter >= 0 ? "EXIT=chapter_switch" : "EXIT=runner_requested");
    Runner_free(runner);
    runner = NULL;
    dev_log_stop();
    audio->vtable->destroy(audio);
    VitaBorders_shutdown();
    renderer->vtable->destroy(renderer);
    OverlayFileSystem_destroy(fs);
    VM_free(vm);
    DataWin_free(dw);
    log_line("PROCESS=exit_clean");
    sceKernelExitProcess(0);
    return 0;
}
