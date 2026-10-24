#include <psp2/ctrl.h>
int _newlib_heap_size_user = 256 * 1024 * 1024;
#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/audioout.h>
#include <psp2/common_dialog.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/gxm.h>
#include <psp2/message_dialog.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/touch.h>
#include <psp2/sysmodule.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "data_win.h"
#include "collision.h"
#include "gl_legacy_renderer.h"
#include "gl_renderer.h"
#include "audio/openal/al_audio_system.h"
#include "wave.h"
#include "overlay_file_system.h"
#include "debug_overlay.h"
#include "runner.h"
#include "runner_gamepad.h"
#include "runner_keyboard.h"
#include "runner_mouse.h"
#include "spatial_grid.h"
#include "vita_settings.h"
#include "vita_borders.h"
#include "vita_video.h"
#include "vita_media_rules.h"
#include "vita_thread_policy.h"
#include "vita_frame_pacer.h"
#include "vita_background_work.h"
#include "vita_startup_preload.h"
#include "vita_room_prefetch.h"
#include "vita_trophies.h"
#include "vita_network.h"
#include "vm.h"
#include "gml_array.h"
#include "vm_builtins.h"
#include "profiler.h"
#include "stb_image.h"

// Exported by the VitaGL fork used by the Vita build, but not declared by the
// public vitaGL.h shipped with the SDK.
extern void vglForceGarbageCollection(void);

#define DATA_ROOT "ux0:data/deltarune/deltarunevita/"
#define SAVE_PATH "ux0:data/deltarune_saves/"
#define LOG_PATH DATA_ROOT "butterscotch-probe.log"
#define NEXT_CHAPTER_PATH DATA_ROOT "next-chapter.txt"
#define DEV_LOG_ROOT DATA_ROOT "devlogs"
#ifndef PORT_BUILD_VERSION
#define PORT_BUILD_VERSION "v0.00 (Unknown Build)"
#endif
#define VITA_CDIALOG_MEMORY_SIZE 0x8C6000

// Read by the Vita renderer to apply chapter-specific memory safety policies.
int g_vitaActiveChapter = 0;
int g_vitaSpanishModActive = 0;
// Shared by every Vita backend that can append to butterscotch-probe.log.
// Keep retail play free from repeated memory-card writes unless Dev Mode was
// explicitly enabled before startup.
int g_vitaProbeLoggingEnabled = 0;
// Read by room-transition code before accessing backend-specific structures.
bool g_vitaModernGlActive = false;
// Experimental-GL deliberately owns no Modern renderer resources. It selects
// the audited programmable-shader policy inside the Legacy backend.
bool g_vitaExperimentalGlActive = false;
// Distinguish a cold LiveArea launch from an internal loadexec back to the
// Chapter Selector. The first-load animation should only run once at startup.
static bool g_vitaHadExplicitChapterRequest = false;
// Room snapshot used only to name archived probe sessions.
static char g_vitaProbeCurrentRoom[96] = "unknown_room";
static int vitaBorderReadGlobalInt(Runner* runner, const char* name, int fallback) {
    if (runner == NULL || runner->vmContext == NULL ||
        runner->vmContext->globalScopeInstance == NULL || name == NULL) return fallback;
    int32_t varId = shget(runner->vmContext->varNameMap, name);
    if (varId < 0) return fallback;
    RValue value = Instance_getSelfVar(runner->vmContext->globalScopeInstance, varId);
    if (value.type == RVALUE_UNDEFINED) return fallback;
    return RValue_toInt32(value);
}

static int vitaBorderReadGlobalArrayInt(Runner* runner, const char* name,
                                        int index, int fallback) {
    if (runner == NULL || runner->vmContext == NULL ||
        runner->vmContext->globalScopeInstance == NULL || name == NULL) return fallback;
    int32_t varId = shget(runner->vmContext->varNameMap, name);
    if (varId < 0) return fallback;
    RValue arrayValue = Instance_getSelfVar(runner->vmContext->globalScopeInstance, varId);
    if (arrayValue.type != RVALUE_ARRAY || arrayValue.array == NULL) return fallback;
    RValue* slot = GMLArray_slot(arrayValue.array, index);
    if (slot == NULL || slot->type == RVALUE_UNDEFINED) return fallback;
    return RValue_toInt32(*slot);
}

static Instance* vitaBorderFirstActiveObject(Runner* runner, const char* objectName) {
    if (runner == NULL || runner->dataWin == NULL || runner->instancesByObject == NULL ||
        objectName == NULL) return NULL;
    int32_t objectIndex = shget(runner->assetsByName, objectName);
    if (objectIndex < 0 || objectIndex >= (int32_t)runner->dataWin->objt.count) return NULL;
    Instance** instances = runner->instancesByObject[objectIndex];
    for (ptrdiff_t i = 0; i < arrlen(instances); ++i) {
        Instance* inst = instances[i];
        if (inst != NULL && inst->active && !inst->destroyed) return inst;
    }
    return NULL;
}

static int vitaBorderInstanceInt(Runner* runner, Instance* inst,
                                 const char* varName, int fallback) {
    if (runner == NULL || runner->vmContext == NULL || inst == NULL || varName == NULL)
        return fallback;
    int32_t varId = shget(runner->vmContext->varNameMap, varName);
    if (varId < 0) return fallback;
    RValue value = Instance_getSelfVar(inst, varId);
    if (value.type == RVALUE_UNDEFINED) return fallback;
    return RValue_toInt32(value);
}


static float vitaBorderInstanceReal(Runner* runner, Instance* inst,
                                    const char* varName, float fallback) {
    if (runner == NULL || runner->vmContext == NULL || inst == NULL || varName == NULL)
        return fallback;
    int32_t varId = shget(runner->vmContext->varNameMap, varName);
    if (varId < 0) return fallback;
    RValue value = Instance_getSelfVar(inst, varId);
    if (value.type == RVALUE_UNDEFINED) return fallback;
    return (float)RValue_toReal(value);
}

static const char* vitaBorderSpriteName(Runner* runner, int32_t spriteIndex) {
    if (runner == NULL || runner->dataWin == NULL || spriteIndex < 0 ||
        (uint32_t)spriteIndex >= runner->dataWin->sprt.count) return NULL;
    Sprite* sprite = &runner->dataWin->sprt.sprites[spriteIndex];
    return sprite->name;
}

static void vitaBorderSampleController(Runner* runner) {
    Instance* controller = vitaBorderFirstActiveObject(runner, "obj_border_controller");
    if (controller == NULL) {
        VitaBorders_updateControllerState(0, NULL, NULL, 0, 0.0f, 0.0f, 0, NULL, 0.0f, 0.0f, 0.0f, 0);
        return;
    }

    int32_t currentIndex = vitaBorderInstanceInt(runner, controller, "_border_image", -1);
    const char* currentName = vitaBorderSpriteName(runner, currentIndex);
    if (currentName == NULL || strncmp(currentName, "border_", 7) != 0) {
        VitaBorders_updateControllerState(0, NULL, NULL, 0, 0.0f, 0.0f, 0, NULL, 0.0f, 0.0f, 0.0f, 0);
        return;
    }

    // Chapter 1 deliberately stays on the established Vita/Better Borders path.
    // Chapter 2 uses the older controller: one selected image plus border_alpha.
    if (g_vitaActiveChapter == 1) {
        VitaBorders_updateControllerState(0, NULL, NULL, 0, 0.0f, 0.0f, 0, NULL, 0.0f, 0.0f, 0.0f, 0);
        return;
    }
    if (g_vitaActiveChapter == 2) {
        float borderAlpha = vitaBorderInstanceReal(runner, controller, "border_alpha", 1.0f);
        if (borderAlpha < 0.0f) borderAlpha = 0.0f;
        if (borderAlpha > 1.0f) borderAlpha = 1.0f;
        VitaBorders_updateControllerState(1, currentName, currentName, 0, 0.0f,
                                          1.0f - borderAlpha, 0x000000U,
                                          NULL, 0.0f, 0.0f, 0.0f, 0);
        return;
    }

    // Chapters 3/4 crossfade when _border_image differs from
    // _border_image_temp. Chapter 5 additionally exposes _border_crossfade.
    int32_t pendingIndex = vitaBorderInstanceInt(runner, controller, "_border_image_temp", currentIndex);
    const char* pendingName = vitaBorderSpriteName(runner, pendingIndex);
    if (pendingName == NULL || strncmp(pendingName, "border_", 7) != 0)
        pendingName = currentName;
    int explicitCrossfade = g_vitaActiveChapter == 5 ?
        (vitaBorderInstanceInt(runner, controller, "_border_crossfade", 0) != 0) : 0;
    int crossfade = VitaBorderRules_controllerCrossfade(g_vitaActiveChapter, currentIndex, pendingIndex, explicitCrossfade);
    float crossfadeAlpha = vitaBorderInstanceReal(runner, controller, "_border_image_temp_alpha", 0.0f);
    float overlayAlpha = vitaBorderInstanceReal(runner, controller, "overlay_alpha", 0.0f);
    unsigned int overlayColor = (unsigned int)vitaBorderInstanceInt(runner, controller, "overlay_color", 0);
    int32_t layerIndex = vitaBorderInstanceInt(runner, controller, "_border_image_layer", -1);
    const char* layerName = vitaBorderSpriteName(runner, layerIndex);
    if (layerName != NULL && strncmp(layerName, "border_", 7) != 0) layerName = NULL;
    float layerAlpha = vitaBorderInstanceReal(runner, controller, "_border_image_alpha", 1.0f);
    float redLayerAlpha = vitaBorderInstanceReal(runner, controller, "_border_red_alpha", 0.0f);
    float customEffectAlpha = vitaBorderInstanceReal(runner, controller, "custom_effect_alpha", 0.0f);
    unsigned int customEffectColor = (unsigned int)vitaBorderInstanceInt(runner, controller, "custom_effect_color", 0);
    VitaBorders_updateControllerState(1, currentName, pendingName, crossfade,
                                      crossfadeAlpha, overlayAlpha, overlayColor,
                                      layerName, layerAlpha, redLayerAlpha,
                                      customEffectAlpha, customEffectColor);
}

static void vitaBorderSampleGameState(Runner* runner) {
    VitaBorderRuleState state;
    memset(&state, 0, sizeof(state));
    state.chapter = g_vitaActiveChapter;
    state.roomIndex = runner != NULL ? runner->currentRoomIndex : -1;
    state.plot = vitaBorderReadGlobalInt(runner, "plot", 0);
    state.darkzone = vitaBorderReadGlobalInt(runner, "darkzone", 0);
    state.flag1324 = vitaBorderReadGlobalArrayInt(runner, "flag", 1324, -1);
    state.gameWon = vitaBorderReadGlobalInt(runner, "gamewon", 0);

    if (state.chapter == 5 && runner != NULL && runner->currentRoom != NULL &&
        runner->currentRoom->name != NULL) {
        const char* room = runner->currentRoom->name;
        if (strcmp(room, "room_dw_pink_encounter") == 0) {
            Instance* date = vitaBorderFirstActiveObject(runner, "obj_date_controller");
            if (date != NULL && vitaBorderInstanceInt(runner, date, "draw_box_timer", 0) >= 500)
                state.sceneFlags |= VITA_BORDER_SCENE_CH5_PINK_ALT;
        }
        if (strcmp(room, "room_dw_garden_floradinnencounter") == 0) {
            Instance* petals = vitaBorderFirstActiveObject(runner, "obj_sakurafubuki_new");
            if (petals != NULL && vitaBorderInstanceInt(runner, petals, "transition_stage", 0) >= 1)
                state.sceneFlags |= VITA_BORDER_SCENE_CH5_GARDEN_REVEAL;
        }

        Instance* pillars = vitaBorderFirstActiveObject(runner, "obj_flowery_towery_pillars");
        if (pillars != NULL) {
            int bgChange = vitaBorderInstanceInt(runner, pillars, "bg_change", -1);
            if (bgChange >= 30 && bgChange <= 90) {
                state.sceneFlags |= VITA_BORDER_SCENE_CH5_CASTLE_TOP;
            } else if (bgChange > 90 && bgChange <= 120) {
                state.sceneFlags |= VITA_BORDER_SCENE_CH5_CASTLE_TOP |
                                    VITA_BORDER_SCENE_HIDE_WHITE;
            } else if (bgChange > 120) {
                state.sceneFlags |= VITA_BORDER_SCENE_CH5_CASTLE_GOLD;
            }
        } else if (strcmp(room, "room_dw_fcastle_top_entrance") == 0) {
            int entrance = vitaBorderReadGlobalInt(runner, "entrance", 0);
            if (entrance == 2) state.sceneFlags |= VITA_BORDER_SCENE_CH5_CASTLE_GOLD;
            else if (entrance == 1) state.sceneFlags |= VITA_BORDER_SCENE_CH5_CASTLE_TOP;
        }
    }

    VitaBorders_updateGameState(&state);
    vitaBorderSampleController(runner);
}

static SceUID dev_log_fd = -1;
static char* dev_log_buffer = NULL;
static size_t dev_log_buffer_size = 0;
static size_t dev_log_buffer_capacity = 0;
static GLuint loading_labels_texture = 0;
#define MAX_LOADING_ANIMATION_FRAMES 32
static GLuint loading_frame_textures[MAX_LOADING_ANIMATION_FRAMES] = {0};
#define LOADING_CAR_FRAME_COUNT 2
static GLuint loading_car_frame_textures[LOADING_CAR_FRAME_COUNT] = {0};
static GLuint loading_minigame_heart_texture = 0;
static GLuint loading_minigame_graze_texture = 0;
static GLuint loading_minigame_labels_texture = 0;
static GLuint loading_minigame_cross_texture = 0;
static GLuint loading_minigame_circle_texture = 0;
static GLuint loading_minigame_diamond_texture = 0;
static GLuint loading_minigame_ball_texture = 0;
static ALuint loading_touch_buffer = 0;
static ALuint loading_touch_source = 0;
static ALuint loading_minigame_hurt_buffer = 0;
static ALuint loading_minigame_hurt_source = 0;
static ALuint loading_minigame_graze_buffer = 0;
static ALuint loading_minigame_graze_source = 0;
static ALuint loading_minigame_play_buffer = 0;
static ALuint loading_minigame_play_source = 0;
static ALuint loading_minigame_stop_buffer = 0;
static ALuint loading_minigame_stop_source = 0;
static ALuint loading_minigame_cardrive_buffer = 0;
static ALuint loading_minigame_cardrive_source = 0;
static ALuint loading_minigame_won_buffer = 0;
static ALuint loading_minigame_won_source = 0;
static bool loading_minigame_cardrive_started = false;
static bool loading_minigame_won_started = false;
static AudioSystem* loading_minigame_audio = NULL;
static float loading_minigame_sfx_gain = 1.0f;
static bool loading_car_touch_held = false;
#define LOADING_MINIGAME_MAX_BULLETS 16
typedef struct LoadingMinigameBullet {
    float x, y, vx, vy;
    uint64_t activate_us;
    uint8_t variant;
    uint8_t kind; // 0 = diamond, 1 = pixel ball
    bool active;
    bool armed;
    bool grazing;
} LoadingMinigameBullet;
static LoadingMinigameBullet loading_minigame_bullets[LOADING_MINIGAME_MAX_BULLETS];
static bool loading_minigame_active = false;
static bool loading_minigame_generation_seen = false;
static float loading_minigame_heart_x = 480.0f;
static float loading_minigame_heart_y = 500.0f;
static uint64_t loading_minigame_last_us = 0;
static uint64_t loading_minigame_next_shot_us = 0;
static uint64_t loading_minigame_invincible_until_us = 0;
static uint32_t loading_minigame_rng = 0xD317A5E5u;
static uint32_t loading_minigame_prev_buttons = 0;
static uint32_t loading_minigame_hits = 0;
static bool loading_minigame_ever_played = false;
static bool loading_minigame_heart_over_ui = false;
static bool loading_minigame_graze_visible = false;
static uint32_t loading_minigame_graze_count = 0;
static uint32_t loading_minigame_graze_combo = 0;
static uint32_t loading_minigame_best_combo = 0;
static uint64_t loading_minigame_last_graze_us = 0;
static uint64_t loading_minigame_damage_shake_until_us = 0;
static uint32_t loading_minigame_pattern_index = 0;
// 0 = normal, 1 = drain remaining bullets, 2 = show final result.
static int loading_minigame_finish_state = 0;
static uint64_t loading_texture_progress_last_draw_us = 0;
static bool loading_car_exit_active = false;
static uint64_t loading_car_exit_started_us = 0;
#define LOADING_CAR_EXIT_DURATION_US 450000ULL
// Set only when the player explicitly enables the loading minigame. The
// synchronous texture-preparation path checks this at page boundaries so the
// remaining work can be handed to a worker without changing normal boot.
volatile int g_vitaTexturePrepareAbortForMinigame = 0;

typedef struct LoadingTextureWorkerJob {
    DataWin* dw;
    volatile uint32_t current;
    volatile uint32_t total;
    volatile uint32_t result;
    volatile int done;
} LoadingTextureWorkerJob;
static LoadingTextureWorkerJob loading_texture_worker_job = {0};
static SceUID loading_texture_worker_thread_id = -1;

static int loading_animation_frame_count = 0;
static uint64_t loading_animation_frame_duration_us = 125000ULL;
static const char* loading_animation_name = "Toby Dance";
static char loading_language_name[32] = "English";
enum {
    LOADING_LABEL_PLAIN = 0,
    LOADING_LABEL_GENERATING_TEXTURES = 1,
    LOADING_LABEL_LOADING_TEXTURES = 2,
    LOADING_LABEL_UPDATING_GAME_FILES = 3,
    LOADING_LABEL_LOADING_SHADERS = 4
};
static uint64_t loading_animation_started_us = 0;
static uint64_t loading_animation_last_present_us = 0;
static bool loading_overlay_active = false;
static int loading_overlay_label_type = LOADING_LABEL_PLAIN;
static bool loading_overlay_show_progress = false;
static float loading_overlay_ratio = 0.0f;
static uint32_t loading_overlay_current = 0;
static uint32_t loading_overlay_total = 0;
#define FIRST_LOAD_FRAME_COUNT 133
static bool first_load_animation_active = false;
static int first_load_animation_next_frame = 0;
static uint64_t first_load_animation_next_at_us = 0;
static uint64_t first_load_animation_start_us = 0;
static GLuint first_load_animation_prefetched_texture = 0;
static int first_load_animation_prefetched_frame = -1;
static GLuint first_load_animation_final_texture = 0;
#define FIRST_LOAD_TOTAL_DURATION_US 4500000ULL
#define FIRST_LOAD_FRAMES_DURATION_US FIRST_LOAD_TOTAL_DURATION_US
#define FIRST_LOAD_DECODE_SLOTS 6
typedef struct FirstLoadDecodedFrame {
    volatile int state; /* 0=empty, 1=ready */
    int frame;
    unsigned char* pixels;
    int width;
    int height;
} FirstLoadDecodedFrame;
static FirstLoadDecodedFrame first_load_decode_slots[FIRST_LOAD_DECODE_SLOTS];
static volatile int first_load_decoder_stop = 0;
static volatile int first_load_decoder_done = 0;
static SceUID first_load_decoder_thread = -1;
static GLuint first_load_stream_texture = 0;
static unsigned char* first_load_frame1_pixels = NULL;
static int first_load_frame1_width = 0;
static int first_load_frame1_height = 0;
static bool first_load_audio_ready = false;
static bool first_load_audio_started = false;
static int first_load_audio_volume = SCE_AUDIO_VOLUME_0DB;
static void log_line(const char *text);
static void dev_log_write(const char* text);
static void loading_screen_init(void);
static void loading_screen_shutdown(void);
static void draw_loading_texture(GLuint texture, float left, float top, float right, float bottom);
static void draw_loading_texture_rotated(GLuint texture, float cx, float cy,
                                         float halfW, float halfH, float angleRadians);
static void draw_loading_texture_region(GLuint texture,
                                        float u0, float v0, float u1, float v1,
                                        float left, float top, float right, float bottom);
static void draw_loading_screen(bool showProgress, float ratio, int labelType);
static void first_load_animation_pump(bool waitForDeadline);
static void first_load_animation_play_to_end(void);
static void first_load_animation_finish(void);
static bool first_load_video_play(float volume);
extern int stb_vorbis_decode_filename(const char *filename, int *channels,
                                      int *sample_rate, short **output);

static bool settings_language_uses_external_mod(const VitaSettings* settings) {
    if (settings == NULL || settings->modIndex < 0 || settings->modIndex >= settings->modCount)
        return false;
    const char* name = settings->modNames[settings->modIndex];
    return strcmp(name, "English") != 0 && strcmp(name, "Japanese") != 0;
}

static unsigned int probe_archive_next_sequence(const char* chapterDir) {
    unsigned int highest = 0;
    SceUID dir = sceIoDopen(chapterDir);
    if (dir < 0) return 1;
    SceIoDirent entry;
    memset(&entry, 0, sizeof(entry));
    while (sceIoDread(dir, &entry) > 0) {
        unsigned int number = 0;
        if (sscanf(entry.d_name, "butterlog%u-", &number) == 1 && number > highest)
            highest = number;
        memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(dir);
    return highest + 1U;
}

static void probe_archive_safe_room(char* output, size_t outputSize, const char* room) {
    if (output == NULL || outputSize == 0) return;
    const char* source = room != NULL && room[0] != '\0' ? room : "unknown_room";
    size_t j = 0;
    for (size_t i = 0; source[i] != '\0' && j + 1 < outputSize; ++i) {
        char c = source[i];
        output[j++] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_';
    }
    output[j] = '\0';
    if (j == 0) snprintf(output, outputSize, "unknown_room");
}

void VitaProbe_rotateLog(void) {
    // Every subsystem writes to one fixed active file. Archive that file into
    // the active chapter and let the next write recreate butterscotch-probe.log.
    SceIoStat sourceStat;
    memset(&sourceStat, 0, sizeof(sourceStat));
    if (sceIoGetstat(LOG_PATH, &sourceStat) >= 0) {
        sceIoMkdir(DEV_LOG_ROOT, 0777);
        char chapterDir[192];
        snprintf(chapterDir, sizeof(chapterDir), DEV_LOG_ROOT "/chapter %d", g_vitaActiveChapter);
        sceIoMkdir(chapterDir, 0777);

        unsigned int sequence = probe_archive_next_sequence(chapterDir);
        char safeRoom[96];
        probe_archive_safe_room(safeRoom, sizeof(safeRoom), g_vitaProbeCurrentRoom);
        SceDateTime now;
        memset(&now, 0, sizeof(now));
        sceRtcGetCurrentClockLocalTime(&now);

        char archivePath[384];
        snprintf(archivePath, sizeof(archivePath),
                 "%s/butterlog%u-%s_%04d%02d%02d-%02d%02d%02d.log",
                 chapterDir, sequence, safeRoom,
                 now.year, now.month, now.day, now.hour, now.minute, now.second);
        sceIoRename(LOG_PATH, archivePath);
    }
    g_vitaProbeLoggingEnabled = 1;
    log_line("PROBE_LOG=new_session active=butterscotch-probe.log");
}

int VitaProbe_sendCurrentLog(char* status, size_t statusSize) {
    return VitaNetwork_sendLogFile(LOG_PATH, PORT_BUILD_VERSION, g_vitaActiveChapter,
                                   g_vitaProbeCurrentRoom, status, statusSize);
}

// Chapter 5's Windows data.win has no console YUV path on Vita, so present the
// decoded Flowey intro directly on the host framebuffer. This guarantees the
// movie is 960x544 and above the console border, while the port UI remains on
// top. The separate OGG determines when the final fade starts on Vita.
static float ch5_flowey_audio_remaining(AudioSystem* audio) {
    if (audio == NULL || audio->vtable == NULL ||
        audio->vtable->getTrackPosition == NULL)
        return -1.0f;
    AlAudioSystem* al = (AlAudioSystem*)audio;
    for (int i = 0; i < MAX_SOUND_INSTANCES; ++i) {
        SoundInstance* inst = &al->instances[i];
        if (!inst->active || !inst->streaming || inst->streamLengthSeconds <= 0.0f)
            continue;
        int slot = inst->soundIndex - AUDIO_STREAM_INDEX_BASE;
        if (slot < 0 || slot >= MAX_AUDIO_STREAMS ||
            al->streams[slot].filePath == NULL ||
            strstr(al->streams[slot].filePath, "ch5_intro_audio.ogg") == NULL)
            continue;
        float remaining = inst->streamLengthSeconds -
            audio->vtable->getTrackPosition(audio, inst->instanceId);
        return remaining > 0.0f ? remaining : 0.0f;
    }
    return -1.0f;
}

static void draw_ch5_flowey_video_host(Runner* runner, Renderer* renderer) {
    static float fadeAlpha = 0.0f;
    static int logged = 0;
    static int audioReleased = 0;
    static int audioObserved = 0;
    static int fadeLogged = 0;
    if (runner == NULL || renderer == NULL || g_vitaActiveChapter != 5 ||
        runner->currentRoom == NULL || runner->currentRoom->name == NULL ||
        strcmp(runner->currentRoom->name, "room_dw_garden_video") != 0 ||
        !VitaVideo_isChapter5Intro() || !VitaVideo_hasDecodedFrame()) {
        fadeAlpha = 0.0f;
        logged = 0;
        audioReleased = 0;
        audioObserved = 0;
        fadeLogged = 0;
        return;
    }

    // The direct video renderer binds the already-active host framebuffer and
    // temporarily owns viewport/program state, restoring them afterwards.
    VitaVideo_drawHost(0.0f, 0.0f, 960.0f, 544.0f);

    // The Chapter 5 intro OGG is created during Room Start while the generic
    // room-transition hold is active. Tie release to the first movie frame
    // that is actually visible so soundtrack start cannot be lost behind
    // texture uploads or a stale room-transition countdown.
    if (!audioReleased && runner->audioSystem != NULL) {
        AlAudioSystem_setTransitionHold((AlAudioSystem*)runner->audioSystem, false);
        runner->vitaAudioTransitionWaitFrames = 0;
        if (runner->audioSystem->vtable->resumeAll != NULL)
            runner->audioSystem->vtable->resumeAll(runner->audioSystem);
        log_line("CH5_VIDEO_AUDIO=released_at_first_presented_frame");
        audioReleased = 1;
    }

    if (!logged) {
        log_line("CH5_VIDEO_HOST=fullscreen_960x544 layer=above_border");
        logged = 1;
    }

    float remaining = ch5_flowey_audio_remaining(runner->audioSystem);
    if (remaining >= 0.0f)
        audioObserved = 1;
    else if (audioObserved)
        remaining = 0.0f;
    else if (VitaVideo_getDuration() > 0.0f)
        remaining = (VitaVideo_getDuration() - VitaVideo_getPosition()) / 1000.0f;
    if (remaining <= 1.0f) {
        // Reach full black as the soundtrack ends, including while GML has
        // paused the movie for its final 90-frame beat.
        float nextAlpha = 1.0f - remaining;
        if (nextAlpha > fadeAlpha) fadeAlpha = nextAlpha;
        if (fadeAlpha > 1.0f) fadeAlpha = 1.0f;
        if (!fadeLogged) {
            log_line("CH5_VIDEO_FADE=audio_final_second");
            fadeLogged = 1;
        }
    }
    if (fadeAlpha <= 0.0f) return;

    int previousOverlayMode = g_vitaPortOverlayFullScreen;
    g_vitaPortOverlayFullScreen = 1;
    renderer->vtable->beginGUI(renderer, 960, 544, 0, 0, 960, 544,
                               RENDER_TARGET_HOST_FRAMEBUFFER);
    renderer->vtable->drawRectangle(renderer, 0.0f, 0.0f, 960.0f, 544.0f,
                                     0x000000, fadeAlpha, false);
    renderer->vtable->endGUI(renderer);
    g_vitaPortOverlayFullScreen = previousOverlayMode;
}

// Butterscotch does not implement GameMaker's legacy part_* particle API.
// Recreate the Garden sakura shower with a small batched set of the game's own
// petal sprite. This is intentionally host-side so it does not add dozens of
// interpreted GML instances or extra surfaces to an already expensive room.
static void draw_ch5_sakura_host(Runner* runner, Renderer* renderer) {
    static unsigned int frameCounter = 0;
    static int logged = 0;
    static int loggedStage = -1;
    static DataWin* resolvedDataWin = NULL;
    static int resolvedSpriteIndex = -1;
    if (runner == NULL || renderer == NULL || g_vitaActiveChapter != 5 ||
        runner->currentRoom == NULL || runner->currentRoom->name == NULL ||
        strcmp(runner->currentRoom->name, "room_dw_garden_floradinnencounter") != 0) {
        frameCounter = 0;
        logged = 0;
        loggedStage = -1;
        return;
    }

    Instance* sakura = vitaBorderFirstActiveObject(runner, "obj_sakurafubuki_new");
    if (sakura == NULL) return;
    int stage = vitaBorderInstanceInt(runner, sakura, "transition_stage", 0);
    float rate = vitaBorderInstanceReal(runner, sakura, "rate", -5.0f);

    // The original obj_sakurafubuki_new particle types use
    // spr_spin_petal_particlefps. Resolve by resource name because translated
    // or repacked data.win files can move numeric asset indices.
    if (resolvedDataWin != runner->dataWin) {
        const char* wantedName = "spr_spin_petal_particlefps";
        const int fallbackIndex = 6127;
        resolvedDataWin = runner->dataWin;
        resolvedSpriteIndex = -1;
        if (resolvedDataWin != NULL && fallbackIndex >= 0 &&
            (uint32_t)fallbackIndex < resolvedDataWin->sprt.count) {
            Sprite* fallback = &resolvedDataWin->sprt.sprites[fallbackIndex];
            if (fallback->present && fallback->name != NULL &&
                strcmp(fallback->name, wantedName) == 0)
                resolvedSpriteIndex = fallbackIndex;
        }
        if (resolvedDataWin != NULL && resolvedSpriteIndex < 0) {
            for (uint32_t i = 0; i < resolvedDataWin->sprt.count; ++i) {
                Sprite* candidate = &resolvedDataWin->sprt.sprites[i];
                if (candidate->present && candidate->name != NULL &&
                    strcmp(candidate->name, wantedName) == 0) {
                    resolvedSpriteIndex = (int)i;
                    break;
                }
            }
        }
    }
    int spriteIndex = resolvedSpriteIndex;
    if (spriteIndex < 0 || runner->dataWin == NULL ||
        (uint32_t)spriteIndex >= runner->dataWin->sprt.count ||
        !runner->dataWin->sprt.sprites[spriteIndex].present)
        return;

    Sprite* sprite = &runner->dataWin->sprt.sprites[spriteIndex];
    int frames = sprite->textureCount > 0 ? (int)sprite->textureCount : 1;

    // Negative GameMaker stream rates mean sparse/probabilistic emission.
    // Stage 1 is the actual pink-petal burst; stage 2 keeps a light ambience.
    int count = stage == 1 ? 52 : (stage >= 2 ? 10 : (rate > 0.0f ? 24 : 12));
    float vx = runner->viewportW > 0 ? (float)runner->viewportX : 117.0f;
    float vy = runner->viewportH > 0 ? (float)runner->viewportY : 0.0f;
    float vw = runner->viewportW > 0 ? (float)runner->viewportW : 726.0f;
    float vh = runner->viewportH > 0 ? (float)runner->viewportH : 544.0f;
    float unit = vh / 480.0f;
    frameCounter++;

    int savedShader = renderer->currentShader;
    if (g_vitaModernGlActive) renderer->vtable->gpuResetShader(renderer);
    int previousOverlayMode = g_vitaPortOverlayFullScreen;
    g_vitaPortOverlayFullScreen = 1;
    renderer->vtable->beginGUI(renderer, 960, 544, 0, 0, 960, 544,
                               RENDER_TARGET_HOST_FRAMEBUFFER);

    for (int i = 0; i < count; ++i) {
        unsigned int seed = (unsigned int)(i * 2654435761u);
        float speedX = stage == 1 ? 5.5f + (float)(i % 4) : 2.0f + (float)(i % 3);
        float speedY = stage == 1 ? 4.0f + (float)(i % 5) * 0.45f : 2.4f + (float)(i % 4) * 0.35f;
        float px = fmodf((float)((seed >> 7) % 1200u) + (float)frameCounter * speedX, vw + 90.0f) - 45.0f;
        float py = fmodf((float)((seed >> 17) % 800u) + (float)frameCounter * speedY, vh + 70.0f) - 35.0f;
        // Original particles travel down-left. Mirror every second petal and
        // add a small deterministic horizontal wobble during the whirlwind.
        float x = vx + vw - px;
        if (stage == 1) x += sinf(((float)frameCounter + (float)i * 11.0f) * 0.11f) * 22.0f;
        float y = vy + py;
        float scale = 2.0f * unit * (0.88f + 0.06f * (float)(i % 5));
        float xscale = (i & 1) ? -scale : scale;
        float angle = fmodf((float)frameCounter * (3.0f + (float)(i % 3)) + (float)i * 29.0f, 360.0f);
        int subimg = ((int)(frameCounter / 5u) + i) % frames;
        Renderer_drawSpriteExt(renderer, spriteIndex, subimg, x, y, xscale, scale,
                               angle, 0xFFFFFF, stage == 1 ? 0.92f : 0.72f);
    }

    renderer->vtable->endGUI(renderer);
    g_vitaPortOverlayFullScreen = previousOverlayMode;
    if (g_vitaModernGlActive && savedShader >= 0)
        renderer->vtable->gpuSetShader(renderer, savedShader);

    if (!logged) {
        char line[128];
        snprintf(line, sizeof(line),
                 "CH5_SAKURA_HOST=enabled source=spr_spin_petal_particlefps index=%d frames=%d",
                 spriteIndex, frames);
        log_line(line);
        logged = 1;
    }
    if (loggedStage != stage) {
        char line[96];
        snprintf(line, sizeof(line), "CH5_SAKURA_HOST=stage value=%d count=%d", stage, count);
        log_line(line);
        loggedStage = stage;
    }
}

// Lightweight final-frame presentation filters. Scanlines draw sparse one-pixel dark lines
// into the host framebuffer after the game and Vita overlays are composed.
// No additional surface, shader program, texture or framebuffer is allocated.
// Sharp Bilinear is handled by the texture upload filtering policy instead and
// therefore also remains a single-pass option.
static void draw_screen_filter(const VitaSettings* settings, Renderer* renderer) {
    if (settings == NULL || renderer == NULL || settings->screenFilterMode == 0 ||
        settings->screenFilterMode == 2) return;
    int previousOverlayMode = g_vitaPortOverlayFullScreen;
    g_vitaPortOverlayFullScreen = 1;
    renderer->vtable->beginGUI(renderer, 960, 544, 0, 0, 960, 544,
                               RENDER_TARGET_HOST_FRAMEBUFFER);
    if (settings->screenFilterMode == 1) {
        // A six-line pitch costs only 91 batched translucent quads per frame.
        for (int y = 3; y < 544; y += 6)
            renderer->vtable->drawRectangle(renderer, 0.0f, (float)y, 960.0f,
                                             (float)(y + 1), 0x000000, 0.18f, false);
    } else if (settings->screenFilterMode == 3) {
        // Sparse ordered-dither blend; no texture, shader or surface needed.
        for (int y = 2; y < 544; y += 16)
            for (int x = ((y / 16) & 1) ? 8 : 0; x < 960; x += 16)
                renderer->vtable->drawRectangle(renderer, (float)x, (float)y,
                                                 (float)(x + 2), (float)(y + 2),
                                                 0xFFFFFF, 0.055f, false);
    } else if (settings->screenFilterMode == 4) {
        // Vita-native adaptation of Vortex Game Studios' OLD TV Filter idea.
        // Keep it to the existing final GUI composition: no application-surface
        // copy, intermediate framebuffer, shader program or auxiliary texture.
        // This preserves the effect's scanlines, analog noise and VHS tracking
        // character without the original package's multi-pass memory/fill cost.
        static unsigned int oldTvFrame = 0;
        oldTvFrame++;

        // Slightly irregular scanline pitch avoids a perfectly digital grid.
        for (int y = 2; y < 544; y += 8) {
            float alpha = ((y + (int)(oldTvFrame & 7u)) & 8) ? 0.105f : 0.075f;
            renderer->vtable->drawRectangle(renderer, 0, (float)y, 960,
                                             (float)(y + 1), 0x000000, alpha, false);
        }

        // Deterministic sparse noise. An LCG is cheaper and more stable than
        // allocating/updating a full-screen static texture every frame.
        unsigned int noise = oldTvFrame * 1664525u + 1013904223u;
        for (int i = 0; i < 42; ++i) {
            noise = noise * 1664525u + 1013904223u;
            float x = (float)((noise >> 8) % 960u);
            noise = noise * 1664525u + 1013904223u;
            float y = (float)((noise >> 8) % 544u);
            float width = (float)(1u + ((noise >> 24) & 3u));
            uint32_t color = (noise & 1u) ? 0xFFFFFF : 0x5D86A8;
            renderer->vtable->drawRectangle(renderer, x, y, x + width, y + 1.0f,
                                             color, 0.075f, false);
        }

        // One moving horizontal tracking band, plus subtle RGB edge fringing.
        float trackingY = (float)((oldTvFrame * 3u) % 608u) - 32.0f;
        renderer->vtable->drawRectangle(renderer, 0, trackingY, 960, trackingY + 2.0f,
                                         0xFFFFFF, 0.055f, false);
        renderer->vtable->drawRectangle(renderer, 0, trackingY + 3.0f, 960, trackingY + 7.0f,
                                         0x000000, 0.045f, false);
        renderer->vtable->drawRectangle(renderer, 0, 0, 2, 544, 0xFF2038, 0.10f, false);
        renderer->vtable->drawRectangle(renderer, 958, 0, 960, 544, 0x20A8FF, 0.10f, false);
    }
    renderer->vtable->endGUI(renderer);
    g_vitaPortOverlayFullScreen = previousOverlayMode;
}

// Display startup failures with the native Vita dialog instead of leaving the
// user at LiveArea with no explanation. VitaGL handles sceCommonDialogUpdate
// when vglSwapBuffers receives GL_TRUE, so this remains available even before
// the GameMaker renderer and its fonts have been created.
static void show_startup_error_and_exit(const char* reason, const char* path) {
    // Initialize loading screen first to draw behind the dialog
    loading_screen_init();

    // Draw loading screen immediately so the screen is not black/blank
    draw_loading_screen(false, 0.0f, 0);
    vglSwapBuffers(GL_TRUE);

    char message[512];
    snprintf(message, sizeof(message),
             "DELTARUNE Vita nao pode iniciar.\n\n%s\n\nCaminho esperado:\n%s\n\nCertifique-se de que os dados do jogo foram copiados para o caminho correto e que os plugins obrigatorios (como o NoTrpDrm para suporte a trofeus nativos) estao devidamente instalados e ativos no seu PS Vita.",
             reason != NULL ? reason : "Os dados do jogo estao incompletos.",
             path != NULL ? path : DATA_ROOT);

    SceAppUtilInitParam appUtilParam;
    SceAppUtilBootParam appUtilBootParam;
    memset(&appUtilParam, 0, sizeof(appUtilParam));
    memset(&appUtilBootParam, 0, sizeof(appUtilBootParam));
    sceAppUtilInit(&appUtilParam, &appUtilBootParam);

    SceCommonDialogConfigParam dialogConfig;
    sceCommonDialogConfigParamInit(&dialogConfig);
    int sysLang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    int enterBtn = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &sysLang);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enterBtn);
    dialogConfig.language = (SceSystemParamLang)sysLang;
    dialogConfig.enterButtonAssign = (SceSystemParamEnterButtonAssign)enterBtn;
    sceCommonDialogSetConfigParam(&dialogConfig);

    SceMsgDialogUserMessageParam userParam;
    memset(&userParam, 0, sizeof(userParam));
    userParam.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    userParam.msg = (const SceChar8*)message;

    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &userParam;

    int dialogResult = sceMsgDialogInit(&param);
    char dialogLog[160];
    snprintf(dialogLog, sizeof(dialogLog),
             "STARTUP_DIALOG init=0x%08X", (unsigned)dialogResult);
    log_line(dialogLog);

    if (dialogResult >= 0) {
        int loopCount = 0;
        while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            // Draw loading screen and spinner behind the native dialog
            draw_loading_screen(false, 0.0f, 0);
            vglSwapBuffers(GL_TRUE);
            loopCount++;
            if (loopCount > 36000) break; // Safeguard so it doesn't hang forever
        }
        SceMsgDialogResult result;
        memset(&result, 0, sizeof(result));
        sceMsgDialogGetResult(&result);
        sceMsgDialogTerm();
    } else {
        // Draw a RED screen for 5 seconds to visual-signal failure
        uint64_t start = sceKernelGetProcessTimeWide();
        while (sceKernelGetProcessTimeWide() - start < 5000000ULL) {
            glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_FALSE);
        }
    }
    sceAppUtilShutdown();
    loading_screen_shutdown();
    sceKernelExitProcess(0);
}

static void show_startup_warning(const char* message) {
    loading_screen_init();
    draw_loading_screen(false, 0.0f, 0);
    vglSwapBuffers(GL_TRUE);

    SceAppUtilInitParam appUtilParam;
    SceAppUtilBootParam appUtilBootParam;
    memset(&appUtilParam, 0, sizeof(appUtilParam));
    memset(&appUtilBootParam, 0, sizeof(appUtilBootParam));
    sceAppUtilInit(&appUtilParam, &appUtilBootParam);

    SceCommonDialogConfigParam dialogConfig;
    sceCommonDialogConfigParamInit(&dialogConfig);
    int sysLang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    int enterBtn = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &sysLang);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enterBtn);
    dialogConfig.language = (SceSystemParamLang)sysLang;
    dialogConfig.enterButtonAssign = (SceSystemParamEnterButtonAssign)enterBtn;
    sceCommonDialogSetConfigParam(&dialogConfig);

    SceMsgDialogUserMessageParam userParam;
    memset(&userParam, 0, sizeof(userParam));
    userParam.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    userParam.msg = (const SceChar8*)message;

    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &userParam;
    int dialogResult = sceMsgDialogInit(&param);
    if (dialogResult >= 0) {
        int loopCount = 0;
        while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            draw_loading_screen(false, 0.0f, 0);
            vglSwapBuffers(GL_TRUE);
            if (++loopCount > 36000) break;
        }
        SceMsgDialogResult result;
        memset(&result, 0, sizeof(result));
        sceMsgDialogGetResult(&result);
        sceMsgDialogTerm();
    }
    sceAppUtilShutdown();
    loading_screen_shutdown();
}

// Missing libshacccg must not enter loading_screen_init: uploading/drawing its
// textures asks vitaGL for a fixed-pipeline shader, which is precisely the
// operation that cannot succeed without the runtime compiler module. A clear
// framebuffer plus the system-owned common dialog needs no game shader.
static void show_shader_dependency_error_and_exit(void) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_TRUE);

    SceAppUtilInitParam appUtilParam;
    SceAppUtilBootParam appUtilBootParam;
    memset(&appUtilParam, 0, sizeof(appUtilParam));
    memset(&appUtilBootParam, 0, sizeof(appUtilBootParam));
    sceAppUtilInit(&appUtilParam, &appUtilBootParam);

    SceCommonDialogConfigParam dialogConfig;
    sceCommonDialogConfigParamInit(&dialogConfig);
    int sysLang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    int enterBtn = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &sysLang);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enterBtn);
    dialogConfig.language = (SceSystemParamLang)sysLang;
    dialogConfig.enterButtonAssign = (SceSystemParamEnterButtonAssign)enterBtn;
    sceCommonDialogSetConfigParam(&dialogConfig);

    static const char message[] =
        "DELTARUNE Vita nao pode iniciar.\n\n"
        "libshacccg.suprx nao foi encontrado.\n\n"
        "Instale ou atualize pelo ShaRKF00D e reinicie o console.\n\n"
        "Caminho esperado:\nur0:data/libshacccg.suprx";
    SceMsgDialogUserMessageParam userParam;
    memset(&userParam, 0, sizeof(userParam));
    userParam.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    userParam.msg = (const SceChar8*)message;
    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &userParam;
    int dialogResult = sceMsgDialogInit(&param);
    if (dialogResult >= 0) {
        int loopCount = 0;
        while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_TRUE);
            if (++loopCount > 36000) break;
        }
        SceMsgDialogResult result;
        memset(&result, 0, sizeof(result));
        sceMsgDialogGetResult(&result);
        sceMsgDialogTerm();
    }
    sceAppUtilShutdown();
    sceKernelExitProcess(0);
}

// Feature 2: verify the essential game data exists BEFORE booting a chapter.
// main() creates the deltarunevita/ directory for save handling, so an empty
// directory alone is not proof the game data was installed. The real signal is
// the presence of chapter0/data.win (the base game) and the active chapter's
// data.win. When anything essential is missing we list every missing item in
// the native dialog and exit cleanly instead of proceeding into a crash (the
// garbage room-name dereference seen in the boot crash dump).
static bool g_initialRootExists = false;



static bool is_directory(const char* path) {
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(path, &st) < 0) return false;
    return SCE_S_ISDIR(st.st_mode);
}

static bool is_file_valid(const char* path) {
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(path, &st) < 0) return false;
    return !SCE_S_ISDIR(st.st_mode) && st.st_size >= 1024;
}

static bool ascii_contains_case_insensitive(const char* text, const char* needle) {
    if (text == NULL || needle == NULL || needle[0] == '\0') return false;
    for (const char* start = text; *start != '\0'; ++start) {
        const char* a = start;
        const char* b = needle;
        while (*a != '\0' && *b != '\0') {
            char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + ('a' - 'A')) : *a;
            char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + ('a' - 'A')) : *b;
            if (ca != cb) break;
            ++a;
            ++b;
        }
        if (*b == '\0') return true;
    }
    return false;
}

static bool tai_config_has_risky_usb_plugin(const char* path, char* found,
                                             size_t foundSize) {
    FILE* config = fopen(path, "rb");
    if (config == NULL) return false;
    // These USB capture plugins compete directly with AvPlayer's hardware
    // audio/video path. VitaGrafix/VGi is deliberately allowed because it is
    // system-wide and does not target Deltarune Vita.
    const char* patterns[] = {"uac_uvc", "uvc_uac", "udcd_uvc"};
    char line[384];
    bool detected = false;
    while (fgets(line, sizeof(line), config) != NULL) {
        char* entry = line;
        while (*entry == ' ' || *entry == '\t') ++entry;
        // taiHEN comments are inactive; section headers do not load plugins.
        if (*entry == '\0' || *entry == '\r' || *entry == '\n' ||
            *entry == '#' || *entry == ';' || *entry == '*') continue;
        for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
            if (!ascii_contains_case_insensitive(entry, patterns[i])) continue;
            if (found != NULL && foundSize > 0)
                snprintf(found, foundSize, "%s in %s", patterns[i], path);
            detected = true;
            break;
        }
        if (detected) break;
    }
    fclose(config);
    return detected;
}

static bool detect_risky_usb_plugin(char* found, size_t foundSize) {
    return tai_config_has_risky_usb_plugin("ur0:tai/config.txt", found, foundSize) ||
           tai_config_has_risky_usb_plugin("ux0:tai/config.txt", found, foundSize);
}

static void verify_essential_data_or_report(int active_chapter) {
    char missing[512] = {0};
    size_t used = 0;
    bool anyMissing = false;

    #define REPORT_APPEND(line) do { \
        int _w = snprintf(missing + used, sizeof(missing) - used, "%s", (line)); \
        if (_w > 0 && (size_t) _w < sizeof(missing) - used) used += (size_t) _w; \
    } while (0)

    REPORT_APPEND("Estrutura de dados incompleta:\n");

    // 1. Check ux0:data/deltarune/
    if (!is_directory("ux0:data/deltarune")) {
        REPORT_APPEND("- pasta ux0:data/deltarune/ ausente\n");
        anyMissing = true;
    }
    // 2. Check ux0:data/deltarune/deltarunevita/
    else if (!is_directory("ux0:data/deltarune/deltarunevita")) {
        REPORT_APPEND("- pasta deltarunevita/ ausente\n");
        anyMissing = true;
    }
    else {
        // Check if there is an old flat data.win (v0.64 indicator)
        SceIoStat oldStat;
        if (sceIoGetstat("ux0:data/deltarune/deltarunevita/data.win", &oldStat) >= 0) {
            log_line("STARTUP_FATAL=old_data_win_detected");
            show_startup_error_and_exit(
                "Detectamos arquivos de dados de uma versao anterior do DELTARUNE (como v0.64).\n\n"
                "Esta versao do VPK exige dados atualizados organizados em pastas por capitulo.",
                "ux0:data/deltarune/deltarunevita/data.win"
            );
        }

        // 3. Check chapter0 directory and data.win (launcher)
        bool has_chapter0 = is_file_valid("ux0:data/deltarune/deltarunevita/chapter0/data.win") ||
                            is_file_valid("app0:assets/chapter0/data.win");
        if (!has_chapter0) {
            REPORT_APPEND("- arquivo chapter0/data.win (launcher) ausente no console e no VPK\n");
            anyMissing = true;
        }

        // 4. Check music directory
        if (!is_directory("ux0:data/deltarune/deltarunevita/music")) {
            REPORT_APPEND("- pasta music/ (musicas) ausente\n");
            anyMissing = true;
        }

        // 5. Check sounds directory
        if (!is_directory("ux0:data/deltarune/deltarunevita/sounds")) {
            REPORT_APPEND("- pasta sounds/ (efeitos) ausente\n");
            anyMissing = true;
        }

        // 6. Check ui directory
        if (!is_directory("ux0:data/deltarune/deltarunevita/ui")) {
            REPORT_APPEND("- pasta ui/ (interface) ausente\n");
            anyMissing = true;
        }

        // 7. Check active chapter's files (if not chapter 0)
        if (active_chapter > 0) {
            char chDir[192];
            char chPath[192];
            snprintf(chDir, sizeof(chDir), "ux0:data/deltarune/deltarunevita/chapter%d", active_chapter);
            snprintf(chPath, sizeof(chPath), "ux0:data/deltarune/deltarunevita/chapter%d/data.win", active_chapter);

            if (!is_directory(chDir)) {
                char chLine[64];
                snprintf(chLine, sizeof(chLine), "- pasta chapter%d/ ausente\n", active_chapter);
                REPORT_APPEND(chLine);
                anyMissing = true;
            } else if (!is_file_valid(chPath)) {
                char chLine[64];
                snprintf(chLine, sizeof(chLine), "- arquivo chapter%d/data.win ausente ou corrompido\n", active_chapter);
                REPORT_APPEND(chLine);
                anyMissing = true;
            }
        }
    }

    #undef REPORT_APPEND

    if (anyMissing) {
        log_line("STARTUP_FATAL=data_structure_incomplete");
        show_startup_error_and_exit(missing, "ux0:data/deltarune/deltarunevita/");
    }
}

static void log_puzzle_diagnostics(Runner* runner) {
    if (dev_log_fd < 0 || runner == NULL || runner->currentRoom == NULL) return;
    const char* room = runner->currentRoom->name;
    if (room == NULL || strstr(room, "darkbulb") == NULL) return;
    char line[320];
    snprintf(line, sizeof(line), "PUZZLE_STATE room=%s frame=%u instances=%d",
             room, runner->frameCount, (int)arrlen(runner->instances));
    dev_log_write(line);
    for (ptrdiff_t i = 0; i < arrlen(runner->instances); ++i) {
        Instance* inst = runner->instances[i];
        if (inst == NULL || !inst->active || inst->objectIndex < 0 ||
            (uint32_t)inst->objectIndex >= runner->dataWin->objt.count) continue;
        const char* objectName = runner->dataWin->objt.objects[inst->objectIndex].name;
        if (objectName == NULL || (strcmp(objectName, "obj_shapepuzzle") != 0 &&
                                  strcmp(objectName, "obj_shapepuzzlepiece") != 0)) continue;
        const char* spriteName = "<none>";
        if (inst->spriteIndex >= 0 && (uint32_t)inst->spriteIndex < runner->dataWin->sprt.count &&
            runner->dataWin->sprt.sprites[inst->spriteIndex].name != NULL)
            spriteName = runner->dataWin->sprt.sprites[inst->spriteIndex].name;
        InstanceBBox bbox = Collision_computeBBox(runner, inst);
        snprintf(line, sizeof(line),
                 "PUZZLE_INSTANCE id=%u obj=%s sprite=%d:%s sub=%.3f pos=%.3f,%.3f scale=%.3f,%.3f rot=%.3f mask=%d bbox=%d:%.3f,%.3f,%.3f,%.3f",
                 inst->instanceId, objectName, inst->spriteIndex, spriteName,
                 (double)inst->imageIndex, (double)inst->x, (double)inst->y,
                 (double)inst->imageXscale, (double)inst->imageYscale,
                 (double)inst->imageAngle, inst->maskIndex, bbox.valid ? 1 : 0,
                 (double)bbox.left, (double)bbox.top, (double)bbox.right, (double)bbox.bottom);
        dev_log_write(line);
    }
    const char* surfaceDiagnostic = VMBuiltins_getShapePuzzleDiagnostic();
    if (surfaceDiagnostic != NULL) dev_log_write(surfaceDiagnostic);
}

typedef struct { GLfloat u, v, x, y; } LoadingVertex;

static unsigned char* load_loading_pixels_rgba(const char* path, int* outW, int* outH) {
    if (outW != NULL) *outW = 0;
    if (outH != NULL) *outH = 0;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return NULL;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0) { sceIoClose(fd); return NULL; }
    unsigned char* data = (unsigned char*)malloc((size_t)size);
    if (data == NULL || sceIoRead(fd, data, (unsigned int)size) != size) {
        free(data); sceIoClose(fd); return NULL;
    }
    sceIoClose(fd);
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    free(data);
    if (pixels == NULL) return NULL;
    if (outW != NULL) *outW = w;
    if (outH != NULL) *outH = h;
    return pixels;
}

static GLuint create_loading_texture_rgba(const unsigned char* pixels, int w, int h) {
    if (pixels == NULL || w <= 0 || h <= 0) return 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return texture;
}

static GLuint load_loading_texture(const char* path) {
    int w = 0, h = 0;
    unsigned char* pixels = load_loading_pixels_rgba(path, &w, &h);
    if (pixels == NULL) return 0;
    GLuint texture = create_loading_texture_rgba(pixels, w, h);
    stbi_image_free(pixels);
    return texture;
}


static uint32_t loading_minigame_rand(void) {
    loading_minigame_rng = loading_minigame_rng * 1664525u + 1013904223u;
    return loading_minigame_rng;
}

static bool loading_minigame_overlap(float ax0, float ay0, float ax1, float ay1,
                                     float bx0, float by0, float bx1, float by1) {
    return ax0 < bx1 && ax1 > bx0 && ay0 < by1 && ay1 > by0;
}

static void loading_direct_sound_shutdown(ALuint* buffer, ALuint* source) {
    if (source != NULL && *source != 0) {
        alSourceStop(*source);
        alDeleteSources(1, source);
        *source = 0;
    }
    if (buffer != NULL && *buffer != 0) {
        alDeleteBuffers(1, buffer);
        *buffer = 0;
    }
}

static bool loading_direct_wav_init(const char* path, ALuint* buffer, ALuint* source,
                                    float gain) {
    if (path == NULL || buffer == NULL || source == NULL || gain <= 0.0f) return false;
    loading_direct_sound_shutdown(buffer, source);

    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    long fileSizeLong = ftell(file);
    if (fileSizeLong <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    size_t fileSize = (size_t)fileSizeLong;
    uint8_t* fileData = (uint8_t*)malloc(fileSize);
    if (fileData == NULL) { fclose(file); return false; }
    size_t bytesRead = fread(fileData, 1, fileSize, file);
    fclose(file);
    if (bytesRead != fileSize) { free(fileData); return false; }

    WAVFile wav = WAV_ParseFileData(fileData, fileSize);
    free(fileData);
    if (wav.data == NULL || wav.data_length == 0 || wav.header.sample_rate == 0 ||
        (wav.header.number_of_channels != 1 && wav.header.number_of_channels != 2) ||
        (wav.header.bits_per_sample != 8 && wav.header.bits_per_sample != 16)) {
        if (wav.data != NULL) free(wav.data);
        return false;
    }

    ALenum format;
    if (wav.header.number_of_channels == 1)
        format = wav.header.bits_per_sample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    else
        format = wav.header.bits_per_sample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;

    alGenBuffers(1, buffer);
    alBufferData(*buffer, format, wav.data, wav.data_length, wav.header.sample_rate);
    free(wav.data);
    alGenSources(1, source);
    alSourcei(*source, AL_BUFFER, (ALint)*buffer);
    alSourcef(*source, AL_GAIN, gain);
    return *buffer != 0 && *source != 0;
}

static bool loading_direct_ogg_init(const char* path, ALuint* buffer, ALuint* source,
                                    float gain) {
    if (path == NULL || buffer == NULL || source == NULL || gain <= 0.0f) return false;
    loading_direct_sound_shutdown(buffer, source);
    int channels = 0, sampleRate = 0;
    short* pcm = NULL;
    int samples = stb_vorbis_decode_filename(path, &channels, &sampleRate, &pcm);
    if (samples <= 0 || pcm == NULL || sampleRate <= 0 ||
        (channels != 1 && channels != 2)) {
        free(pcm);
        return false;
    }
    ALenum format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alGenBuffers(1, buffer);
    alBufferData(*buffer, format, pcm,
                 samples * channels * (int)sizeof(short), sampleRate);
    free(pcm);
    alGenSources(1, source);
    alSourcei(*source, AL_BUFFER, (ALint)*buffer);
    alSourcef(*source, AL_GAIN, gain);
    return *buffer != 0 && *source != 0;
}

static void loading_direct_sound_play(ALuint source, float gain) {
    if (source == 0 || gain <= 0.0f) return;
    alSourceStop(source);
    alSourceRewind(source);
    alSourcef(source, AL_GAIN, gain);
    alSourcePlay(source);
}


static void loading_direct_sound_collect_if_stopped(ALuint* buffer, ALuint* source) {
    if (source == NULL || *source == 0) return;
    ALint state = AL_STOPPED;
    alGetSourcei(*source, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING)
        loading_direct_sound_shutdown(buffer, source);
}

static void loading_minigame_collect_tail_sfx(void) {
    // Tail sources are created up-front but must not be collected while they are
    // still in their initial AL_STOPPED state. Only collect after each cue has
    // actually been started and OpenAL reports that playback has finished.
    if (loading_minigame_cardrive_started && loading_minigame_cardrive_source != 0) {
        ALint state = AL_STOPPED;
        alGetSourcei(loading_minigame_cardrive_source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            loading_direct_sound_shutdown(&loading_minigame_cardrive_buffer,
                                          &loading_minigame_cardrive_source);
            loading_minigame_cardrive_started = false;
        }
    }
    if (loading_minigame_won_started && loading_minigame_won_source != 0) {
        ALint state = AL_STOPPED;
        alGetSourcei(loading_minigame_won_source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            loading_direct_sound_shutdown(&loading_minigame_won_buffer,
                                          &loading_minigame_won_source);
            loading_minigame_won_started = false;
        }
    }
}

static void loading_minigame_prepare_game_sfx(void) {
    // Startup intentionally pauses/mutes AudioSystem-managed sources. These
    // minigame cues use dedicated OpenAL sources decoded directly from OGG.
    bool hurtReady = loading_direct_ogg_init(
        "app0:assets/loading/minigame_sounds/snd_hurt1.ogg",
        &loading_minigame_hurt_buffer, &loading_minigame_hurt_source,
        loading_minigame_sfx_gain * 0.95f);
    bool grazeReady = loading_direct_ogg_init(
        "app0:assets/loading/minigame_sounds/snd_graze.ogg",
        &loading_minigame_graze_buffer, &loading_minigame_graze_source,
        loading_minigame_sfx_gain * 0.85f);
    bool playReady = loading_direct_ogg_init(
        "app0:assets/loading/minigame_sounds/play.ogg",
        &loading_minigame_play_buffer, &loading_minigame_play_source,
        loading_minigame_sfx_gain * 0.85f);
    bool stopReady = loading_direct_ogg_init(
        "app0:assets/loading/minigame_sounds/stop.ogg",
        &loading_minigame_stop_buffer, &loading_minigame_stop_source,
        loading_minigame_sfx_gain * 0.85f);
    bool carReady = loading_direct_ogg_init(
        "app0:assets/loading/minigame_sounds/snd_cardrive.ogg",
        &loading_minigame_cardrive_buffer, &loading_minigame_cardrive_source,
        loading_minigame_sfx_gain * 0.80f);
    bool wonReady = loading_direct_ogg_init(
        "app0:assets/loading/minigame_sounds/snd_won.ogg",
        &loading_minigame_won_buffer, &loading_minigame_won_source,
        loading_minigame_sfx_gain * 0.90f);
    loading_minigame_cardrive_started = false;
    loading_minigame_won_started = false;
    char line[256];
    snprintf(line, sizeof(line),
             "LOADING_MINIGAME_AUDIO=direct_ogg hurt=%d graze=%d play=%d stop=%d cardrive=%d won=%d gain=%.2f",
             hurtReady ? 1 : 0, grazeReady ? 1 : 0,
             playReady ? 1 : 0, stopReady ? 1 : 0, carReady ? 1 : 0,
             wonReady ? 1 : 0, loading_minigame_sfx_gain);
    log_line(line);
}

static void loading_touch_sound_shutdown(void) {
    if (loading_touch_source != 0) {
        alSourceStop(loading_touch_source);
        alDeleteSources(1, &loading_touch_source);
        loading_touch_source = 0;
    }
    if (loading_touch_buffer != 0) {
        alDeleteBuffers(1, &loading_touch_buffer);
        loading_touch_buffer = 0;
    }
}

static void loading_touch_sound_init(void) {
    loading_touch_sound_shutdown();
    if (loading_minigame_audio == NULL || loading_minigame_sfx_gain <= 0.0f) return;
    int channels = 0, sampleRate = 0;
    short* pcm = NULL;
    int samples = stb_vorbis_decode_filename(
        "app0:assets/loading/touch.ogg", &channels, &sampleRate, &pcm);
    if (samples <= 0 || pcm == NULL || (channels != 1 && channels != 2)) {
        free(pcm);
        return;
    }
    ALenum format = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alGenBuffers(1, &loading_touch_buffer);
    alBufferData(loading_touch_buffer, format, pcm,
                 samples * channels * (int)sizeof(short), sampleRate);
    free(pcm);
    alGenSources(1, &loading_touch_source);
    alSourcei(loading_touch_source, AL_BUFFER, (ALint)loading_touch_buffer);
    alSourcef(loading_touch_source, AL_GAIN, loading_minigame_sfx_gain);
}

static void loading_touch_sound_play(void) {
    if (loading_touch_source == 0 || loading_minigame_sfx_gain <= 0.0f) return;
    alSourceStop(loading_touch_source);
    alSourceRewind(loading_touch_source);
    alSourcef(loading_touch_source, AL_GAIN, loading_minigame_sfx_gain);
    alSourcePlay(loading_touch_source);
}

static void loading_minigame_reset(void) {
    memset(loading_minigame_bullets, 0, sizeof(loading_minigame_bullets));
    loading_minigame_active = false;
    loading_minigame_generation_seen = false;
    loading_minigame_heart_x = 480.0f;
    loading_minigame_heart_y = 500.0f;
    loading_minigame_last_us = sceKernelGetProcessTimeWide();
    loading_minigame_next_shot_us = loading_minigame_last_us + 500000ULL;
    loading_minigame_invincible_until_us = 0;
    loading_minigame_prev_buttons = 0;
    loading_minigame_hits = 0;
    loading_minigame_ever_played = false;
    loading_minigame_heart_over_ui = false;
    loading_minigame_graze_visible = false;
    loading_minigame_graze_count = 0;
    loading_minigame_graze_combo = 0;
    loading_minigame_best_combo = 0;
    loading_minigame_last_graze_us = 0;
    loading_minigame_damage_shake_until_us = 0;
    loading_minigame_pattern_index = 0;
    loading_minigame_finish_state = 0;
    loading_car_touch_held = false;
    loading_car_exit_active = false;
    loading_car_exit_started_us = 0;
    loading_texture_progress_last_draw_us = 0;
    g_vitaTexturePrepareAbortForMinigame = 0;
    loading_minigame_rng ^= (uint32_t)loading_minigame_last_us;
}

static float loading_minigame_clampf(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static bool loading_minigame_add_bullet(uint64_t now, int edge, float along,
                                        float targetX, float targetY,
                                        uint8_t kind, uint8_t variant,
                                        float speed, uint64_t delayUs) {
    int slot = -1;
    for (int i = 0; i < LOADING_MINIGAME_MAX_BULLETS; ++i) {
        if (!loading_minigame_bullets[i].active) { slot = i; break; }
    }
    if (slot < 0) return false;

    LoadingMinigameBullet* b = &loading_minigame_bullets[slot];
    memset(b, 0, sizeof(*b));
    edge &= 3;
    if (edge == 0) {
        b->x = loading_minigame_clampf(along, 24.0f, 936.0f);
        b->y = -18.0f;
    } else if (edge == 1) {
        b->x = 978.0f;
        b->y = loading_minigame_clampf(along, 24.0f, 520.0f);
    } else if (edge == 2) {
        b->x = loading_minigame_clampf(along, 24.0f, 936.0f);
        b->y = 562.0f;
    } else {
        b->x = -18.0f;
        b->y = loading_minigame_clampf(along, 24.0f, 520.0f);
    }

    float dx = targetX - b->x;
    float dy = targetY - b->y;
    float norm = (dx < 0.0f ? -dx : dx) + (dy < 0.0f ? -dy : dy);
    if (norm < 1.0f) norm = 1.0f;
    b->vx = dx / norm * speed;
    b->vy = dy / norm * speed;
    b->variant = (uint8_t)(variant & 3u);
    b->kind = kind;
    b->activate_us = now + 130000ULL + delayUs;
    b->active = true;
    b->armed = false;
    b->grazing = false;
    return true;
}

static void loading_minigame_spawn_pattern(uint64_t now) {
    uint32_t r = loading_minigame_rand();
    float ratio = loading_overlay_ratio;
    int phase = ratio < 0.35f ? 0 :
                (ratio < 0.60f ? 1 :
                 (ratio < 0.80f ? 2 :
                  (ratio < 0.95f ? 3 : 4)));
    uint8_t variant = (uint8_t)((r >> 14) & 3u);
    float diamondSpeed = 185.0f + (float)((r >> 20) & 55u);
    float ballSpeed = 270.0f + (float)((r >> 18) & 47u);
    int edge = (int)(r & 3u);
    float along = (edge == 0 || edge == 2) ?
                  36.0f + (float)((r >> 8) % 888u) :
                  36.0f + (float)((r >> 8) % 472u);

    if (phase == 0) {
        // Intro: one readable aimed diamond at a time, with four visibly
        // different approach angles instead of pure radial RNG.
        float tx = loading_minigame_heart_x;
        float ty = loading_minigame_heart_y;
        const float angleOffset[4] = {-120.0f, -42.0f, 46.0f, 124.0f};
        if (edge == 0 || edge == 2) tx += angleOffset[variant];
        else ty += angleOffset[variant];
        loading_minigame_add_bullet(now, edge, along, tx, ty, 0, variant,
                                    diamondSpeed, 0);
        loading_minigame_next_shot_us = now + 520000ULL;
    } else if (phase == 1) {
        // Opposite-edge pair. The second warning arrives shortly after the
        // first, making the pattern recognizable instead of feeling random.
        int opposite = (edge + 2) & 3;
        float oppositeAlong = (opposite == 0 || opposite == 2) ?
                              960.0f - along : 544.0f - along;
        loading_minigame_add_bullet(now, edge, along,
                                    loading_minigame_heart_x,
                                    loading_minigame_heart_y,
                                    0, variant, diamondSpeed, 0);
        loading_minigame_add_bullet(now, opposite, oppositeAlong,
                                    loading_minigame_heart_x,
                                    loading_minigame_heart_y,
                                    0, (uint8_t)(variant + 2),
                                    diamondSpeed + 12.0f, 90000ULL);
        loading_minigame_next_shot_us = now + 620000ULL;
    } else if (phase == 2) {
        // Diagonal crossing pair. Targets are deliberately offset around the
        // Heart so the player reads a lane rather than two identical homing shots.
        int secondEdge = (edge + 1) & 3;
        float secondAlong = (secondEdge == 0 || secondEdge == 2) ?
                            36.0f + (float)((r >> 4) % 888u) :
                            36.0f + (float)((r >> 4) % 472u);
        float offset = (loading_minigame_pattern_index & 1u) ? 100.0f : -100.0f;
        loading_minigame_add_bullet(now, edge, along,
                                    loading_minigame_heart_x + offset,
                                    loading_minigame_heart_y - offset * 0.45f,
                                    0, variant, diamondSpeed + 10.0f, 0);
        loading_minigame_add_bullet(now, secondEdge, secondAlong,
                                    loading_minigame_heart_x - offset,
                                    loading_minigame_heart_y + offset * 0.45f,
                                    0, (uint8_t)(variant + 1),
                                    diamondSpeed + 18.0f, 70000ULL);
        loading_minigame_next_shot_us = now + 580000ULL;
    } else if (phase == 3) {
        // Three-shot fan. Pixel balls join here as the faster centre shot.
        float offsets[3] = {-128.0f, 0.0f, 128.0f};
        for (int i = 0; i < 3; ++i) {
            float tx = loading_minigame_heart_x;
            float ty = loading_minigame_heart_y;
            if (edge == 0 || edge == 2) tx += offsets[i];
            else ty += offsets[i];
            uint8_t kind = i == 1 ? 1 : 0;
            float speed = kind ? ballSpeed : diamondSpeed + 20.0f;
            loading_minigame_add_bullet(now, edge, along, tx, ty,
                                        kind, (uint8_t)(variant + i), speed,
                                        (uint64_t)i * 65000ULL);
        }
        loading_minigame_next_shot_us = now + 640000ULL;
    } else {
        // Final rush (95-98%): four directions with staggered telegraphs. New
        // bullets stop completely at 98% so the encounter can finish cleanly.
        for (int i = 0; i < 4; ++i) {
            int rushEdge = (edge + i) & 3;
            float rushAlong = (rushEdge == 0 || rushEdge == 2) ?
                              150.0f + (float)(((r >> (i * 3)) & 255u) % 660u) :
                              90.0f + (float)(((r >> (i * 3)) & 255u) % 364u);
            uint8_t kind = (i & 1) ? 1 : 0;
            loading_minigame_add_bullet(now, rushEdge, rushAlong,
                                        loading_minigame_heart_x,
                                        loading_minigame_heart_y,
                                        kind, (uint8_t)(variant + i),
                                        kind ? ballSpeed + 22.0f : diamondSpeed + 35.0f,
                                        (uint64_t)i * 65000ULL);
        }
        loading_minigame_next_shot_us = now + 650000ULL;
    }
    loading_minigame_pattern_index++;
}

static bool loading_minigame_any_bullets_active(void) {
    for (int i = 0; i < LOADING_MINIGAME_MAX_BULLETS; ++i)
        if (loading_minigame_bullets[i].active) return true;
    return false;
}

static void loading_minigame_damage(uint64_t now) {
    if (now < loading_minigame_invincible_until_us) return;
    loading_minigame_invincible_until_us = now + 800000ULL;
    loading_minigame_damage_shake_until_us = now + 180000ULL;
    loading_minigame_hits++;
    loading_minigame_graze_combo = 0;
    loading_minigame_graze_visible = false;
    loading_direct_sound_play(loading_minigame_hurt_source,
                              loading_minigame_sfx_gain * 0.95f);
}

static void loading_minigame_register_graze(uint64_t now) {
    if (loading_minigame_last_graze_us != 0 &&
        now - loading_minigame_last_graze_us <= 1500000ULL)
        loading_minigame_graze_combo++;
    else
        loading_minigame_graze_combo = 1;
    loading_minigame_last_graze_us = now;
    loading_minigame_graze_count++;
    if (loading_minigame_graze_combo > loading_minigame_best_combo)
        loading_minigame_best_combo = loading_minigame_graze_combo;
    loading_direct_sound_play(loading_minigame_graze_source,
                              loading_minigame_sfx_gain * 0.85f);
}

static void loading_minigame_update(void) {
    uint64_t now = sceKernelGetProcessTimeWide();
    if (!loading_minigame_generation_seen) {
        loading_minigame_generation_seen = true;
        loading_minigame_last_us = now;
        loading_minigame_next_shot_us = now + 500000ULL;
    }
    float dt = (float)(now - loading_minigame_last_us) / 1000000.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.10f) dt = 0.10f;
    loading_minigame_last_us = now;

    SceCtrlData pad = {0};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    bool crossPressed = (pad.buttons & SCE_CTRL_CROSS) != 0 &&
                        (loading_minigame_prev_buttons & SCE_CTRL_CROSS) == 0;
    bool circlePressed = (pad.buttons & SCE_CTRL_CIRCLE) != 0 &&
                         (loading_minigame_prev_buttons & SCE_CTRL_CIRCLE) == 0;
    loading_minigame_prev_buttons = pad.buttons;

    if (loading_minigame_finish_state == 0) {
        if (!loading_minigame_active && crossPressed) {
            loading_minigame_active = true;
            loading_minigame_ever_played = true;
            loading_minigame_next_shot_us = now + 350000ULL;
            loading_direct_sound_play(loading_minigame_play_source,
                                      loading_minigame_sfx_gain * 0.85f);
            if (loading_texture_worker_thread_id < 0)
                g_vitaTexturePrepareAbortForMinigame = 1;
            log_line("LOADING_MINIGAME=play async_generation=requested_if_sync");
        } else if (loading_minigame_active && circlePressed) {
            loading_minigame_active = false;
            memset(loading_minigame_bullets, 0, sizeof(loading_minigame_bullets));
            loading_minigame_graze_visible = false;
            loading_minigame_graze_combo = 0;
            loading_direct_sound_play(loading_minigame_stop_source,
                                      loading_minigame_sfx_gain * 0.85f);
            if (loading_texture_worker_thread_id < 0)
                g_vitaTexturePrepareAbortForMinigame = 0;
            log_line("LOADING_MINIGAME=stop");
        }
    }

    bool simulate = loading_minigame_active || loading_minigame_finish_state == 1;
    if (simulate) {
        float ax = ((int)pad.lx - 128) / 127.0f;
        float ay = ((int)pad.ly - 128) / 127.0f;
        if (ax > -0.20f && ax < 0.20f) ax = 0.0f;
        if (ay > -0.20f && ay < 0.20f) ay = 0.0f;
        if (pad.buttons & SCE_CTRL_LEFT) ax = -1.0f;
        if (pad.buttons & SCE_CTRL_RIGHT) ax = 1.0f;
        if (pad.buttons & SCE_CTRL_UP) ay = -1.0f;
        if (pad.buttons & SCE_CTRL_DOWN) ay = 1.0f;
        const float moveSpeed = 235.0f;
        loading_minigame_heart_x += ax * moveSpeed * dt;
        loading_minigame_heart_y += ay * moveSpeed * dt;
        if (loading_minigame_heart_x < 10.0f) loading_minigame_heart_x = 10.0f;
        if (loading_minigame_heart_x > 950.0f) loading_minigame_heart_x = 950.0f;
        if (loading_minigame_heart_y < 10.0f) loading_minigame_heart_y = 10.0f;
        if (loading_minigame_heart_y > 534.0f) loading_minigame_heart_y = 534.0f;

        if (loading_minigame_active && loading_minigame_finish_state == 0 &&
            loading_overlay_ratio < 0.98f && now >= loading_minigame_next_shot_us)
            loading_minigame_spawn_pattern(now);

        bool bulletHit = false;
        bool anyGraze = false;
        for (int i = 0; i < LOADING_MINIGAME_MAX_BULLETS; ++i) {
            LoadingMinigameBullet* b = &loading_minigame_bullets[i];
            if (!b->active) continue;

            if (!b->armed) {
                if (now < b->activate_us) continue;
                b->armed = true;
            }

            b->x += b->vx * dt;
            b->y += b->vy * dt;
            if (b->x < -55.0f || b->x > 1015.0f || b->y < -55.0f || b->y > 599.0f) {
                b->active = false;
                b->grazing = false;
                continue;
            }

            const float hitHalf = b->kind == 1 ? 12.0f : 15.0f;
            bool touchingHeart = loading_minigame_overlap(
                loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                b->x - hitHalf, b->y - hitHalf, b->x + hitHalf, b->y + hitHalf);
            if (touchingHeart) {
                b->grazing = false;
                b->active = false;
                if (now >= loading_minigame_invincible_until_us)
                    bulletHit = true;
                continue;
            }

            const float grazeHalf = hitHalf + 12.0f;
            bool nearHeart = loading_minigame_overlap(
                loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                b->x - grazeHalf, b->y - grazeHalf,
                b->x + grazeHalf, b->y + grazeHalf);
            if (nearHeart) {
                if (!b->grazing) {
                    b->grazing = true;
                    loading_minigame_register_graze(now);
                }
                anyGraze = true;
            } else {
                b->grazing = false;
            }
        }
        loading_minigame_graze_visible = anyGraze;

        loading_minigame_heart_over_ui = loading_minigame_overlap(
                         loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                         loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                         330.0f, 268.0f, 630.0f, 312.0f) ||
                     loading_minigame_overlap(
                         loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                         loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                         365.0f, 318.0f, 595.0f, 356.0f) ||
                     loading_minigame_overlap(
                         loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                         loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                         298.0f, 404.0f, 662.0f, 438.0f) ||
                     loading_minigame_overlap(
                         loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                         loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                         410.0f, 504.0f, 530.0f, 542.0f) ||
                     loading_minigame_overlap(
                         loading_minigame_heart_x - 5.0f, loading_minigame_heart_y - 5.0f,
                         loading_minigame_heart_x + 5.0f, loading_minigame_heart_y + 5.0f,
                         10.0f, 8.0f, 150.0f, 80.0f);
        if (bulletHit && now >= loading_minigame_invincible_until_us)
            loading_minigame_damage(now);
    } else if (loading_minigame_finish_state == 0) {
        loading_minigame_heart_over_ui = false;
        loading_minigame_graze_visible = false;
    }
}

static void loading_minigame_draw_number(uint32_t value, float x, float y, float scale) {
    if (loading_labels_texture == 0) return;
    char text[16];
    snprintf(text, sizeof(text), "%u", (unsigned)value);
    const float glyphW = 32.0f * scale;
    const float glyphH = 64.0f * scale;
    for (int i = 0; text[i] != '\0'; ++i) {
        int glyph = text[i] >= '0' && text[i] <= '9' ? text[i] - '0' : -1;
        if (glyph < 0) continue;
        float u0 = (float)(glyph * 32) / 512.0f;
        float u1 = (float)((glyph + 1) * 32) / 512.0f;
        draw_loading_texture_region(loading_labels_texture,
                                    u0, 448.0f / 512.0f,
                                    u1, 1.0f,
                                    x, y, x + glyphW, y + glyphH);
        x += glyphW;
    }
}

static void loading_minigame_draw_label_cell(int cell,
                                             float left, float top,
                                             float right, float bottom) {
    if (loading_minigame_labels_texture == 0 || cell < 0 || cell > 3) return;
    float u0 = (float)(cell * 128) / 512.0f;
    float u1 = (float)((cell + 1) * 128) / 512.0f;
    draw_loading_texture_region(loading_minigame_labels_texture,
                                u0, 0.0f, u1, 1.0f,
                                left, top, right, bottom);
}

static void loading_minigame_draw_score(void) {
    if (!loading_minigame_ever_played ||
        (!loading_minigame_active && loading_minigame_finish_state == 0)) return;
    uint32_t combo = loading_minigame_finish_state == 2 ?
                     loading_minigame_best_combo : loading_minigame_graze_combo;
    loading_minigame_draw_label_cell(0, 14.0f, 12.0f, 78.0f, 44.0f);
    loading_minigame_draw_number(loading_minigame_graze_count, 84.0f, 12.0f, 0.5f);
    loading_minigame_draw_label_cell(1, 14.0f, 44.0f, 78.0f, 76.0f);
    loading_minigame_draw_number(combo, 84.0f, 44.0f, 0.5f);
}

static void loading_minigame_draw(void) {
    uint64_t now = sceKernelGetProcessTimeWide();
    bool drawEncounter = loading_minigame_active || loading_minigame_finish_state == 1;
    if (drawEncounter) {
        for (int i = 0; i < LOADING_MINIGAME_MAX_BULLETS; ++i) {
            const LoadingMinigameBullet* b = &loading_minigame_bullets[i];
            if (!b->active) continue;
            GLuint texture = b->kind == 1 ? loading_minigame_ball_texture :
                                               loading_minigame_diamond_texture;
            if (texture == 0) continue;
            const float diamondAngle = atan2f(b->vy, b->vx) - 1.57079632679f;

            if (!b->armed) {
                // Border telegraph: show the same incoming shape just inside the
                // screen for ~130 ms before it becomes dangerous.
                float tx = loading_minigame_clampf(b->x, 9.0f, 951.0f);
                float ty = loading_minigame_clampf(b->y, 9.0f, 535.0f);
                float pulse = ((now / 60000ULL) & 1ULL) ? 0.32f : 0.72f;
                glColor4f(1.0f, 1.0f, 1.0f, pulse);
                if (b->kind == 1)
                    draw_loading_texture(texture, tx - 7.0f, ty - 7.0f,
                                         tx + 7.0f, ty + 7.0f);
                else
                    draw_loading_texture_rotated(texture, tx, ty, 5.0f, 7.5f,
                                                 diamondAngle);
                glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                continue;
            }

            if (b->kind == 1) {
                draw_loading_texture(texture,
                                     b->x - 15.0f, b->y - 15.0f,
                                     b->x + 15.0f, b->y + 15.0f);
            } else {
                // One pixel-art rhombus for every diamond projectile. Rotate the
                // quad to follow its actual travel vector instead of swapping PNGs.
                draw_loading_texture_rotated(texture, b->x, b->y,
                                             13.5f, 19.5f, diamondAngle);
            }
        }
    }

    bool visible = now >= loading_minigame_invincible_until_us ||
                   ((now / 80000ULL) & 1ULL) == 0ULL;
    float heartX = loading_minigame_heart_x;
    float heartY = loading_minigame_heart_y;
    if (now < loading_minigame_damage_shake_until_us) {
        int shake = (int)((now / 30000ULL) & 3ULL);
        static const float shakeX[4] = {-2.0f, 2.0f, 0.0f, 0.0f};
        static const float shakeY[4] = {0.0f, 0.0f, -2.0f, 2.0f};
        heartX += shakeX[shake];
        heartY += shakeY[shake];
    }

    if (drawEncounter && loading_minigame_graze_visible &&
        loading_minigame_graze_texture != 0) {
        draw_loading_texture(loading_minigame_graze_texture,
                             heartX - 20.5f, heartY - 20.5f,
                             heartX + 20.5f, heartY + 20.5f);
    }
    if ((drawEncounter || loading_minigame_finish_state == 2) &&
        visible && loading_minigame_heart_texture != 0) {
        if (loading_minigame_heart_over_ui)
            glColor4f(1.0f, 1.0f, 1.0f, 0.45f);
        draw_loading_texture(loading_minigame_heart_texture,
                             heartX - 9.0f, heartY - 9.0f,
                             heartX + 9.0f, heartY + 9.0f);
        if (loading_minigame_heart_over_ui)
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }

    loading_minigame_draw_score();
    if (loading_minigame_finish_state == 2) {
        int resultCell = loading_minigame_hits == 0 ? 3 : 2;
        // Two-times nearest-neighbour presentation for a clear, readable result.
        loading_minigame_draw_label_cell(resultCell,
                                         352.0f, 408.0f, 608.0f, 536.0f);
    }
}

static void loading_minigame_draw_hint(void) {
    if (loading_minigame_finish_state != 0) return;
    // Row 7's final 160 pixels contain two 80x32 localized native-size cells:
    // PLAY at x=352 and STOP at x=432. Keep this footer on-screen for the full
    // Generating phase so the controls are always discoverable.
    const bool active = loading_minigame_active;
    GLuint icon = active ? loading_minigame_circle_texture : loading_minigame_cross_texture;
    if (icon != 0)
        draw_loading_texture(icon, 414.0f, 510.0f, 438.0f, 538.0f);
    if (loading_labels_texture != 0) {
        const float u0 = active ? 432.0f / 512.0f : 352.0f / 512.0f;
        const float u1 = active ? 1.0f : 432.0f / 512.0f;
        draw_loading_texture_region(loading_labels_texture,
                                    u0, 448.0f / 512.0f,
                                    u1, 480.0f / 512.0f,
                                    446.0f, 508.0f, 526.0f, 540.0f);
    }
}

static void loading_process_car_touch(float carLeft, float carTop,
                                      float carRight, float carBottom) {
    SceTouchData touch = {0};
    bool touching = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0 &&
                    touch.reportNum > 0;
    if (touching && !loading_car_touch_held) {
        for (unsigned i = 0; i < touch.reportNum; ++i) {
            float tx = (float)touch.report[i].x * (960.0f / 1920.0f);
            float ty = (float)touch.report[i].y * (544.0f / 1080.0f);
            if (tx >= carLeft && tx <= carRight && ty >= carTop && ty <= carBottom) {
                loading_touch_sound_play();
                break;
            }
        }
    }
    loading_car_touch_held = touching;
}

static void loading_screen_init(void) {
    // Parsing, room bootstrap and texture preparation share one uninterrupted
    // loading session. Do not reload its PNGs or restart the animation when a
    // later stage asks for the overlay again.
    if (loading_overlay_active && loading_labels_texture != 0) return;
    char languagePath[128];
    snprintf(languagePath, sizeof(languagePath),
             "app0:assets/loading/Lang/%s.png", loading_language_name);
    loading_labels_texture = load_loading_texture(languagePath);
    if (loading_labels_texture == 0 && strcmp(loading_language_name, "English") != 0) {
        snprintf(languagePath, sizeof(languagePath),
                 "app0:assets/loading/Lang/English.png");
        loading_labels_texture = load_loading_texture(languagePath);
    }
    snprintf(languagePath, sizeof(languagePath),
             "app0:assets/loading/LangMiniGame/%s.png", loading_language_name);
    loading_minigame_labels_texture = load_loading_texture(languagePath);
    if (loading_minigame_labels_texture == 0 && strcmp(loading_language_name, "English") != 0) {
        snprintf(languagePath, sizeof(languagePath),
                 "app0:assets/loading/LangMiniGame/English.png");
        loading_minigame_labels_texture = load_loading_texture(languagePath);
    }
    // Every loading/generating overlay keeps one of the larger Toby animations
    // above the title. The dedicated toby_car_load strip is loaded separately
    // and is rendered ONLY when a real progress bar is visible.
    static const char* animationFolders[] = {"toby_sleep", "toby_dance", "toby_walk", "toby_spin", "toby_turn"};
    static const char* animationNames[] = {"Toby Sleep", "Toby Dance", "Toby Walk", "Toby Spin", "Toby Turn"};
    static const int animationFrameCounts[] = {4, 18, 2, 8, 27};
    static const uint64_t animationFrameDurationsUs[] = {375000ULL, 125000ULL, 180000ULL, 125000ULL, 100000ULL};
    uint64_t randomSeed = sceKernelGetProcessTimeWide();
    int animation = (int)((randomSeed ^ (randomSeed >> 17) ^ (randomSeed >> 31)) % 5ULL);
    loading_animation_name = animationNames[animation];
    loading_animation_frame_count = animationFrameCounts[animation];
    loading_animation_frame_duration_us = animationFrameDurationsUs[animation];
    loading_animation_started_us = sceKernelGetProcessTimeWide();
    loading_animation_last_present_us = 0;
    loading_overlay_active = true;

    char path[160];
    for (int i = 0; i < loading_animation_frame_count; ++i) {
        snprintf(path, sizeof(path),
                 "app0:assets/loading/animations/%s/frame_%d.png",
                 animationFolders[animation], i);
        loading_frame_textures[i] = load_loading_texture(path);
    }
    for (int i = 0; i < LOADING_CAR_FRAME_COUNT; ++i) {
        snprintf(path, sizeof(path),
                 "app0:assets/loading/animations/toby_car_load/frame_%d.png", i);
        loading_car_frame_textures[i] = load_loading_texture(path);
    }
    loading_minigame_heart_texture = load_loading_texture(
        "app0:assets/loading/minigame/spr_heart_0.png");
    loading_minigame_graze_texture = load_loading_texture(
        "app0:assets/loading/minigame/spinheartmask.png");
    loading_minigame_cross_texture = load_loading_texture("app0:assets/ui/button_ps4_cross_0.png");
    loading_minigame_circle_texture = load_loading_texture("app0:assets/ui/button_ps4_circle_0.png");
    loading_minigame_diamond_texture = load_loading_texture(
        "app0:assets/loading/minigame/projectile_diamond.png");
    loading_minigame_ball_texture = load_loading_texture(
        "app0:assets/loading/minigame/projectile_ball.png");
    loading_minigame_reset();
    loading_touch_sound_init();
    char line[320];
    snprintf(line, sizeof(line),
             "LOADING_ASSETS language=%s labels=%u plain_animation=%s frames=%d plain0=%u car=%u,%u heart=%u",
             loading_language_name, loading_labels_texture, loading_animation_name,
             loading_animation_frame_count, loading_frame_textures[0],
             loading_car_frame_textures[0], loading_car_frame_textures[1],
             loading_minigame_heart_texture);
    log_line(line);
}

static void loading_screen_shutdown(void) {
    if (loading_labels_texture != 0) glDeleteTextures(1, &loading_labels_texture);
    for (int i = 0; i < MAX_LOADING_ANIMATION_FRAMES; i++) {
        if (loading_frame_textures[i] != 0) glDeleteTextures(1, &loading_frame_textures[i]);
    }
    for (int i = 0; i < LOADING_CAR_FRAME_COUNT; i++) {
        if (loading_car_frame_textures[i] != 0) glDeleteTextures(1, &loading_car_frame_textures[i]);
    }
    if (loading_minigame_heart_texture != 0) glDeleteTextures(1, &loading_minigame_heart_texture);
    if (loading_minigame_graze_texture != 0) glDeleteTextures(1, &loading_minigame_graze_texture);
    if (loading_minigame_labels_texture != 0) glDeleteTextures(1, &loading_minigame_labels_texture);
    if (loading_minigame_cross_texture != 0) glDeleteTextures(1, &loading_minigame_cross_texture);
    if (loading_minigame_circle_texture != 0) glDeleteTextures(1, &loading_minigame_circle_texture);
    if (loading_minigame_diamond_texture != 0) glDeleteTextures(1, &loading_minigame_diamond_texture);
    if (loading_minigame_ball_texture != 0) glDeleteTextures(1, &loading_minigame_ball_texture);
    loading_touch_sound_shutdown();
    loading_direct_sound_shutdown(&loading_minigame_hurt_buffer, &loading_minigame_hurt_source);
    loading_direct_sound_shutdown(&loading_minigame_graze_buffer, &loading_minigame_graze_source);
    loading_direct_sound_shutdown(&loading_minigame_play_buffer, &loading_minigame_play_source);
    loading_direct_sound_shutdown(&loading_minigame_stop_buffer, &loading_minigame_stop_source);
    // Do not cut tail cues that are still playing across the transition. If a
    // cue was never started, release its preloaded source normally.
    if (loading_minigame_cardrive_started)
        loading_minigame_collect_tail_sfx();
    else
        loading_direct_sound_shutdown(&loading_minigame_cardrive_buffer,
                                      &loading_minigame_cardrive_source);
    if (loading_minigame_won_started)
        loading_minigame_collect_tail_sfx();
    else
        loading_direct_sound_shutdown(&loading_minigame_won_buffer,
                                      &loading_minigame_won_source);
    loading_labels_texture = 0;
    memset(loading_frame_textures, 0, sizeof(loading_frame_textures));
    memset(loading_car_frame_textures, 0, sizeof(loading_car_frame_textures));
    loading_minigame_heart_texture = 0;
    loading_minigame_graze_texture = 0;
    loading_minigame_labels_texture = 0;
    loading_minigame_cross_texture = 0;
    loading_minigame_circle_texture = 0;
    loading_minigame_diamond_texture = 0;
    loading_minigame_ball_texture = 0;
    loading_minigame_reset();
    loading_animation_frame_count = 0;
    loading_overlay_active = false;
    loading_animation_started_us = 0;
    loading_animation_last_present_us = 0;
    loading_overlay_current = 0;
    loading_overlay_total = 0;
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

static void draw_loading_texture_rotated(GLuint texture, float cx, float cy,
                                         float halfW, float halfH, float angleRadians) {
    if (texture == 0) return;
    const float c = cosf(angleRadians);
    const float sn = sinf(angleRadians);
    const float lx[4] = {-halfW, halfW, halfW, -halfW};
    const float ly[4] = {-halfH, -halfH, halfH, halfH};
    const float u[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float v[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    LoadingVertex vertices[4];
    for (int i = 0; i < 4; ++i) {
        const float px = cx + lx[i] * c - ly[i] * sn;
        const float py = cy + lx[i] * sn + ly[i] * c;
        vertices[i].u = u[i];
        vertices[i].v = v[i];
        vertices[i].x = px / 480.0f - 1.0f;
        vertices[i].y = 1.0f - py / 272.0f;
    }
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
    size_t text_len = strlen(text);
    size_t needed = dev_log_buffer_size + text_len + 1;
    if (needed > dev_log_buffer_capacity) {
        size_t capacity = dev_log_buffer_capacity > 0 ? dev_log_buffer_capacity : 16384;
        while (capacity < needed) capacity *= 2;
        char* grown = (char*)realloc(dev_log_buffer, capacity);
        if (grown == NULL) return;
        dev_log_buffer = grown;
        dev_log_buffer_capacity = capacity;
    }
    memcpy(dev_log_buffer + dev_log_buffer_size, text, text_len);
    dev_log_buffer_size += text_len;
    dev_log_buffer[dev_log_buffer_size++] = '\n';
}

static void dev_log_stop(void) {
    if (dev_log_fd < 0) return;
    dev_log_write("DEV_CAPTURE=end");
    if (dev_log_buffer != NULL && dev_log_buffer_size > 0)
        sceIoWrite(dev_log_fd, dev_log_buffer, dev_log_buffer_size);
    sceIoClose(dev_log_fd);
    dev_log_fd = -1;
    free(dev_log_buffer);
    dev_log_buffer = NULL;
    dev_log_buffer_size = 0;
    dev_log_buffer_capacity = 0;
}

static unsigned int dev_log_next_sequence(const char* chapterDir) {
    unsigned int highest = 0;
    SceUID dir = sceIoDopen(chapterDir);
    if (dir < 0) return 1;
    SceIoDirent entry;
    memset(&entry, 0, sizeof(entry));
    while (sceIoDread(dir, &entry) > 0) {
        const char* name = entry.d_name;
        unsigned int number = 0;
        size_t i = 0;
        while (name[i] >= '0' && name[i] <= '9') {
            number = number * 10U + (unsigned int)(name[i] - '0');
            i++;
        }
        if (i > 0 && name[i] == '_' && number > highest) highest = number;
        memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(dir);
    return highest + 1U;
}

static void dev_log_start(const char* room) {
    dev_log_stop();
    dev_log_buffer_size = 0;
    sceIoMkdir(DEV_LOG_ROOT, 0777);
    char chapterDir[192];
    snprintf(chapterDir, sizeof(chapterDir), DEV_LOG_ROOT "/chapter %d", g_vitaActiveChapter);
    sceIoMkdir(chapterDir, 0777);
    unsigned int sequence = dev_log_next_sequence(chapterDir);
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
    char path[384];
    snprintf(path, sizeof(path), "%s/%u_%s_%04d%02d%02d-%02d%02d%02d.log",
             chapterDir, sequence, safeRoom,
             now.year, now.month, now.day, now.hour, now.minute, now.second);
    dev_log_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (dev_log_fd >= 0) {
        dev_log_write("DELTARUNE_VITA_DEV_CAPTURE=1");
        dev_log_write("BUILD_VERSION=" PORT_BUILD_VERSION);
        char line[512];
        snprintf(line, sizeof(line), "CHAPTER=%d LOG_SEQUENCE=%u START_ROOM=%s FILE=%s",
                 g_vitaActiveChapter, sequence, source, path);
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

static uint32_t preload_chapter3_intro_modern(DataWin* dw, GLRenderer* gl) {
    if (dw == NULL || gl == NULL) return 0;
    // Captured from the actual Modern-GL room_intro trace. These are the
    // pages first touched by obj_intro_ch3; lazy uploads were producing
    // 150-255 ms stalls in the middle of the opening animation.
    static const uint16_t introPages[] = {
        91, 107, 99, 236, 108, 106, 104, 234, 233,
        34, 98, 97, 210, 92, 96, 93, 95, 94
    };
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < sizeof(introPages) / sizeof(introPages[0]); ++i) {
        uint32_t page = introPages[i];
        if (page >= dw->txtr.count) continue;
        if (GLRenderer_ensureTextureLoaded(gl, page)) loaded++;
        // Keep upload command lists short on real Vita.
        if ((i & 3u) == 3u) glFinish();
    }
    glFinish();
    char line[128];
    snprintf(line, sizeof(line),
             "CH3_INTRO_MODERN_PREWARM=complete pages=%u requested=%u",
             loaded, (unsigned)(sizeof(introPages) / sizeof(introPages[0])));
    log_line(line);
    return loaded;
}

static void preload_chapter3_slide_atlases(DataWin* dw, GLLegacyRenderer* gl) {
    static const char* spriteNames[] = {
        "spr_krisd_slide", "spr_krisd_slide_heart", "spr_susie_slide",
        "spr_ralsei_slide", "spr_slidedust"
    };
    uint32_t loadedPages[16];
    uint32_t loadedCount = 0;
    for (uint32_t n = 0; n < sizeof(spriteNames) / sizeof(spriteNames[0]); ++n) {
        for (uint32_t i = 0; i < dw->sprt.count; ++i) {
            Sprite* sprite = &dw->sprt.sprites[i];
            if (!sprite->present || sprite->name == nullptr ||
                strcmp(sprite->name, spriteNames[n]) != 0) continue;
            for (uint32_t f = 0; f < sprite->textureCount; ++f) {
                int32_t tpagIndex = sprite->tpagIndices[f];
                if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
                int32_t page = dw->tpag.items[tpagIndex].texturePageId;
                if (page < 0 || (uint32_t)page >= gl->textureCount) continue;
                bool duplicate = false;
                for (uint32_t p = 0; p < loadedCount; ++p)
                    if (loadedPages[p] == (uint32_t)page) duplicate = true;
                if (duplicate) continue;
                if (GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t)page)) {
                    if (loadedCount < 16) loadedPages[loadedCount++] = (uint32_t)page;
                }
            }
            break;
        }
    }
    char line[96];
    snprintf(line, sizeof(line), "CH3_SLIDE_PRELOAD=complete pages=%u pinned=0", loadedCount);
    log_line(line);
}

static void preload_chapter3_finale_atlases(DataWin* dw, GLLegacyRenderer* gl) {
    static const char* spriteNames[] = {
        "spr_funnytext_amazing_01", "spr_funnytext_tears"
    };
    uint32_t pages[8];
    uint32_t pageCount = 0;

    // Include the default sprite used by every confetti instance.
    int32_t confettiSprite = -1;
    for (uint32_t i = 0; i < dw->objt.count; ++i) {
        GameObject* object = &dw->objt.objects[i];
        if (object->present && object->name != NULL &&
            strcmp(object->name, "obj_confetti_overworld") == 0) {
            confettiSprite = object->spriteId;
            break;
        }
    }

    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        Sprite* sprite = &dw->sprt.sprites[i];
        if (!sprite->present) continue;
        bool wanted = (int32_t)i == confettiSprite;
        for (uint32_t n = 0; !wanted && n < sizeof(spriteNames) / sizeof(spriteNames[0]); ++n)
            wanted = sprite->name != NULL && strcmp(sprite->name, spriteNames[n]) == 0;
        if (!wanted) continue;

        for (uint32_t f = 0; f < sprite->textureCount; ++f) {
            int32_t tpagIndex = sprite->tpagIndices[f];
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            int32_t page = dw->tpag.items[tpagIndex].texturePageId;
            if (page < 0 || (uint32_t)page >= gl->textureCount) continue;
            bool duplicate = false;
            for (uint32_t p = 0; p < pageCount; ++p)
                if (pages[p] == (uint32_t)page) duplicate = true;
            if (duplicate) continue;
            if (GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t)page)) {
                // This event caused a single 554 ms upload immediately before
                // the video. Its tiny page set is safe to retain; unlike the
                // former five-page slide pin it does not crowd out room art.
                if (pageCount < 8) pages[pageCount++] = (uint32_t)page;
            }
        }
    }

    char line[96];
    snprintf(line, sizeof(line), "CH3_FINALE_PRELOAD=complete pages=%u residency=normal", pageCount);
    log_line(line);
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
    if (!g_vitaProbeLoggingEnabled || text == NULL) return;
    SceUID fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

static int config_devmode_enabled(void) {
    FILE* config = fopen("ux0:data/deltarune/config.ini", "rb");
    if (config == NULL) return 0;
    char line[192];
    int enabled = 0;
    while (fgets(line, sizeof(line), config) != NULL) {
        char* key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (strncmp(key, "devmode=", 8) == 0) {
            enabled = atoi(key + 8) == 1;
            break;
        }
    }
    fclose(config);
    return enabled;
}

static bool config_get_dev_start_room(char* out, size_t outSize) {
    if (out == NULL || outSize == 0 || !config_devmode_enabled()) return false;
    FILE* config = fopen("ux0:data/deltarune/config.ini", "rb");
    if (config == NULL) return false;
    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), config) != NULL) {
        char* key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (strncmp(key, "dev_start_room=", 15) == 0) {
            char* value = key + 15;
            size_t n = strcspn(value, "\r\n");
            if (n >= outSize) n = outSize - 1;
            memcpy(out, value, n);
            out[n] = '\0';
            found = n > 0;
            break;
        }
    }
    fclose(config);
    return found;
}


static void draw_loading_texture_region(GLuint texture,
                                        float u0, float v0, float u1, float v1,
                                        float left, float top, float right, float bottom) {
    if (texture == 0) return;
    float x0 = left / 480.0f - 1.0f, x1 = right / 480.0f - 1.0f;
    float y0 = 1.0f - top / 272.0f, y1 = 1.0f - bottom / 272.0f;
    const LoadingVertex vertices[4] = {
        {u0, v0, x0, y0}, {u1, v0, x1, y0},
        {u1, v1, x1, y1}, {u0, v1, x0, y1}
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

static int loading_progress_glyph_index(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character == '/') return 10;
    return -1;
}

static float loading_progress_value_width(const char* value) {
    float width = 0.0f;
    if (value == NULL) return width;
    for (int i = 0; value[i] != '\0'; ++i)
        width += value[i] == ' ' ? 8.0f : 16.0f;
    return width;
}

static void draw_loading_progress_text(int labelType) {
    if (loading_labels_texture == 0 || loading_overlay_total == 0) return;

    const bool shaders = labelType == LOADING_LABEL_LOADING_SHADERS;
    char value[32];
    snprintf(value, sizeof(value), "%u / %u",
             (unsigned)loading_overlay_current, (unsigned)loading_overlay_total);

    // Row 6 contains two 160x64 localized cells: TEXTURES and SHADERS.
    // Draw at exactly half scale so the pixel font stays crisp inside the
    // 364x34 progress bar. Row 7 contains 0-9 and '/' in 32x64 cells.
    const float labelWidth = 80.0f;
    const float labelHeight = 32.0f;
    const float gap = 8.0f;
    const float valueWidth = loading_progress_value_width(value);
    const float totalWidth = labelWidth + gap + valueWidth;
    float x = 480.0f - totalWidth * 0.5f;
    const float y = 405.0f;
    const float rowHeight = 1.0f / 8.0f;
    const float labelU0 = shaders ? 160.0f / 512.0f : 0.0f;
    const float labelU1 = shaders ? 320.0f / 512.0f : 160.0f / 512.0f;

    draw_loading_texture_region(loading_labels_texture,
                                labelU0, 6.0f * rowHeight,
                                labelU1, 7.0f * rowHeight,
                                x, y, x + labelWidth, y + labelHeight);
    x += labelWidth + gap;

    for (int i = 0; value[i] != '\0'; ++i) {
        if (value[i] == ' ') {
            x += 8.0f;
            continue;
        }
        int glyph = loading_progress_glyph_index(value[i]);
        if (glyph < 0) continue;
        float u0 = (float)(glyph * 32) / 512.0f;
        float u1 = (float)((glyph + 1) * 32) / 512.0f;
        draw_loading_texture_region(loading_labels_texture,
                                    u0, 7.0f * rowHeight,
                                    u1, 8.0f * rowHeight,
                                    x, y, x + 16.0f, y + labelHeight);
        x += 16.0f;
    }
}

static int config_modern_gl_enabled(void) {
    FILE* config = fopen("ux0:data/deltarune/config.ini", "rb");
    if (config == NULL) return 0;
    char line[192];
    int enabled = 0;
    while (fgets(line, sizeof(line), config) != NULL) {
        char* key = line;
        while (*key == ' ' || *key == '\t') key++;
        if (strncmp(key, "renderer=", 9) == 0) {
            enabled = strncmp(key + 9, "modern-gl", 9) == 0;
            break;
        }
    }
    fclose(config);
    return enabled;
}

static void log_memory_snapshot(const char* phase, GLLegacyRenderer* renderer) {
    SceKernelFreeMemorySizeInfo systemMemory;
    memset(&systemMemory, 0, sizeof(systemMemory));
    systemMemory.size = sizeof(systemMemory);
    sceKernelGetFreeMemorySize(&systemMemory);

    VitaVideoMemoryStats videoMemory = {0};
    VitaVideo_getMemoryStats(&videoMemory);
    size_t vramTotal = vglMemTotal(VGL_MEM_VRAM);
    size_t vramFree = vglMemFree(VGL_MEM_VRAM);
    size_t ramTotal = vglMemTotal(VGL_MEM_RAM);
    size_t ramFree = vglMemFree(VGL_MEM_RAM);
    size_t phyTotal = vglMemTotal(VGL_MEM_SLOW);
    size_t phyFree = vglMemFree(VGL_MEM_SLOW);
    size_t budgetTotal = vglMemTotal(VGL_MEM_BUDGET);
    size_t budgetFree = vglMemFree(VGL_MEM_BUDGET);
    uint64_t atlasBytes = renderer != NULL ? renderer->residentTextureBytes : 0;
    uint64_t surfaceBytes = 0;
    uint64_t retiredTextureBytes = 0;
    uint32_t retiredTextureCount = 0;
    uint32_t activeSurfaces = 0;
    if (renderer != NULL) {
        for (uint32_t i = 0; i < renderer->surfaceCount; ++i) {
            if (renderer->surfaces[i] == 0 || renderer->surfaceTexture[i] == 0) continue;
            activeSurfaces++;
            surfaceBytes += (uint64_t)renderer->surfaceWidth[i] *
                            (uint64_t)renderer->surfaceHeight[i] * 4ULL;
        }
        for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i) {
            if (renderer->retiredTextures[i] == 0) continue;
            retiredTextureCount++;
            retiredTextureBytes += renderer->retiredTextureBytes[i];
        }
    }

    char line[768];
    snprintf(line, sizeof(line),
             "MEMORY phase=%s atlas=%llu surfaces=%llu surface_count=%u retired=%llu retired_count=%u vgl_vram_used=%llu vgl_vram_free=%llu vgl_vram_total=%llu vgl_ram_used=%llu vgl_ram_free=%llu vgl_ram_total=%llu vgl_phy_used=%llu vgl_phy_free=%llu vgl_phy_total=%llu vgl_budget_used=%llu vgl_budget_free=%llu vgl_budget_total=%llu video_cpu=%llu video_cpu_peak=%llu video_gpu=%llu video_gpu_peak=%llu system_user_free=%u system_cdram_free=%u system_phy_free=%u",
             phase != NULL ? phase : "unknown",
             (unsigned long long)atlasBytes,
             (unsigned long long)surfaceBytes, activeSurfaces,
             (unsigned long long)retiredTextureBytes, retiredTextureCount,
             (unsigned long long)(vramTotal >= vramFree ? vramTotal - vramFree : 0),
             (unsigned long long)vramFree, (unsigned long long)vramTotal,
             (unsigned long long)(ramTotal >= ramFree ? ramTotal - ramFree : 0),
             (unsigned long long)ramFree, (unsigned long long)ramTotal,
             (unsigned long long)(phyTotal >= phyFree ? phyTotal - phyFree : 0),
             (unsigned long long)phyFree, (unsigned long long)phyTotal,
             (unsigned long long)(budgetTotal >= budgetFree ? budgetTotal - budgetFree : 0),
             (unsigned long long)budgetFree, (unsigned long long)budgetTotal,
             (unsigned long long)videoMemory.cpuBytes,
             (unsigned long long)videoMemory.cpuPeakBytes,
             (unsigned long long)videoMemory.gpuBytes,
             (unsigned long long)videoMemory.gpuPeakBytes,
             systemMemory.size_user, systemMemory.size_cdram,
             systemMemory.size_phycont);
    log_line(line);
}

void VitaProbe_logLine(const char *text) {
    log_line(text);
}

static void progress(const char *chunk, int index, int total, DataWin *dw, void *user) {
    (void)dw; (void)user;
    char line[96];
    snprintf(line, sizeof(line), "LOAD chunk=%.4s %d/%d", chunk, index + 1, total);
    log_line(line);
    if (first_load_animation_active) {
        first_load_animation_pump(false);
    } else if (loading_overlay_active) {
        loading_overlay_label_type = LOADING_LABEL_LOADING_TEXTURES;
        loading_overlay_show_progress = true;
        loading_overlay_ratio = total > 0 ? (float)(index + 1) / (float)total : 0.0f;
        loading_overlay_current = index + 1;
        loading_overlay_total = total;
        draw_loading_screen(true, loading_overlay_ratio, loading_overlay_label_type);
    }
    sceKernelDelayThread(1000);
}

static void draw_loading_screen(bool showProgress, float ratio, int labelType) {
    if (loading_overlay_active) {
        loading_overlay_show_progress = showProgress;
        loading_overlay_ratio = ratio;
        loading_overlay_label_type = labelType;
    }
    // A cooperative redraw may happen between VM instructions. Preserve the
    // target and fixed-pipeline state so a GML bootstrap surface cannot become
    // the accidental destination of the loading overlay.
    GLint previousFramebuffer = 0, previousTexture = 0, previousMatrixMode = GL_MODELVIEW;
    GLint previousViewport[4] = {0, 0, 960, 544};
    GLint previousScissor[4] = {0, 0, 960, 544};
    GLfloat previousColor[4] = {1, 1, 1, 1};
    GLboolean hadScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLboolean hadBlend = glIsEnabled(GL_BLEND);
    GLboolean hadTexture = glIsEnabled(GL_TEXTURE_2D);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    glGetFloatv(GL_CURRENT_COLOR, previousColor);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 960, 544);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    // Runner_initFirstRoom configures GameMaker's 640x480 projection and can
    // leave the current fixed-pipeline colour black.  Loading vertices are
    // already expressed in normalized host coordinates; inheriting that game
    // state places the PNG outside the viewport or modulates it to invisible.
    // Give this small overlay its own identity matrices and neutral colour.
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (labelType == LOADING_LABEL_GENERATING_TEXTURES && !loading_car_exit_active)
        loading_minigame_update();
    else if (labelType != LOADING_LABEL_GENERATING_TEXTURES)
        loading_minigame_generation_seen = false;
    // The regular Toby animation is independent from progress and is always
    // shown above LOADING/GENERATING. toby_car_load is intentionally NOT part
    // of loading_frame_textures and remains exclusive to the progress bar.
    if (loading_animation_frame_count > 0 && loading_frame_textures[0] != 0) {
        uint64_t animationElapsed = sceKernelGetProcessTimeWide() - loading_animation_started_us;
        int animationFrame = (int)((animationElapsed /
            loading_animation_frame_duration_us) % (uint64_t)loading_animation_frame_count);
        if (strcmp(loading_animation_name, "Toby Turn") == 0) {
            // Toby Turn is the only upper animation that intentionally touches
            // the top edge. Its source frames are pre-scaled 300% (63x249), so
            // draw them at native size to preserve nearest-neighbour pixels.
            draw_loading_texture(loading_frame_textures[animationFrame],
                                 448.5f, 0.0f, 511.5f, 249.0f);
        } else {
            draw_loading_texture(loading_frame_textures[animationFrame], 352, 96, 608, 240);
        }
    }

    // The localized atlas is 512x512 with one 64px row per label.  Fixed
    // destination rectangles preserve the original 960x544 composition even
    // when translated strings have different lengths.
    const float labelRowHeight = 1.0f / 8.0f;
    float stateRowTop = 0.00f;
    if (labelType == LOADING_LABEL_GENERATING_TEXTURES)
        stateRowTop = 2.0f * labelRowHeight;
    else if (labelType == LOADING_LABEL_UPDATING_GAME_FILES)
        stateRowTop = 3.0f * labelRowHeight;
    draw_loading_texture_region(loading_labels_texture,
                                0.0f, stateRowTop, 1.0f, stateRowTop + labelRowHeight,
                                224, 258, 736, 322);
    if (labelType != LOADING_LABEL_PLAIN && loading_labels_texture != 0) {
        const float detailRowTop = labelType == LOADING_LABEL_UPDATING_GAME_FILES ?
                                   4.0f * labelRowHeight :
                                   (labelType == LOADING_LABEL_LOADING_SHADERS ?
                                    5.0f * labelRowHeight : 1.0f * labelRowHeight);
        draw_loading_texture_region(loading_labels_texture,
                                    0.0f, detailRowTop,
                                    1.0f, detailRowTop + labelRowHeight,
                                    224, 304, 736, 368);
    }
    if (showProgress) {
        // Keep the bar on scissored clears: glBegin here would compile an
        // unnecessary fixed-pipeline shader before the first game frame.
        // Screen-space bar: x=298..662, y=404..438 (34 px high).
        float clampedRatio = ratio;
        if (clampedRatio < 0.0f) clampedRatio = 0.0f;
        if (clampedRatio > 1.0f) clampedRatio = 1.0f;
        glEnable(GL_SCISSOR_TEST);
        glScissor(298, 106, 364, 34);
        glClearColor(0.25f, 0.25f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(302, 110, 356, 26);
        glClearColor(0.62f, 0.62f, 0.62f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        int fill = (int)(356.0f * clampedRatio + 0.5f);
        if (fill > 0) {
            glScissor(302, 110, fill, 26);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);

        // Black localized counter over both the white filled and grey pending
        // portions. The atlas itself stays white and is modulated here.
        glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
        draw_loading_progress_text(labelType);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        // Toby's car rides the filled edge of the bar. Its RIGHT edge, not
        // its centre, is anchored to the current inner progress boundary.
        if (loading_car_frame_textures[0] != 0) {
            uint64_t animationElapsed = sceKernelGetProcessTimeWide() - loading_animation_started_us;
            int animationFrame = (int)((animationElapsed / 125000ULL) % LOADING_CAR_FRAME_COUNT);
            float carRight = 302.0f + 356.0f * clampedRatio;
            if (loading_car_exit_active) {
                float exitT = (float)(sceKernelGetProcessTimeWide() - loading_car_exit_started_us) /
                              (float)LOADING_CAR_EXIT_DURATION_US;
                if (exitT < 0.0f) exitT = 0.0f;
                if (exitT > 1.0f) exitT = 1.0f;
                // Smooth acceleration from the 100% edge. Overshoot enough
                // that the last presented frame has the entire 40px car beyond
                // x=960, avoiding the one-pixel sliver seen on real hardware.
                float ease = exitT * exitT;
                carRight = 658.0f + (1048.0f - 658.0f) * ease;
            }
            float carLeft = carRight - 40.0f;
            // 40x42 native sprite: bottom touches the bar's y=404 top edge.
            const float carTop = 362.0f, carBottom = 404.0f;
            draw_loading_texture(loading_car_frame_textures[animationFrame],
                                 carLeft, carTop, carRight, carBottom);
            loading_process_car_touch(carLeft, carTop, carRight, carBottom);
        }
    }
    if (labelType == LOADING_LABEL_GENERATING_TEXTURES && !loading_car_exit_active) {
        loading_minigame_draw();
        loading_minigame_draw_hint();
    }
    vglSwapBuffers(GL_FALSE);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    glScissor(previousScissor[0], previousScissor[1],
              previousScissor[2], previousScissor[3]);
    if (hadScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (hadDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (hadCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (hadBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (hadTexture) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    glColor4f(previousColor[0], previousColor[1], previousColor[2], previousColor[3]);
    glMatrixMode((GLenum)previousMatrixMode);
}

static void loading_play_car_exit(int labelType) {
    if (!loading_overlay_active || loading_car_frame_textures[0] == 0) return;

    // If the player is still inside the Generating mini-game, stop creating new
    // projectiles, let the existing pattern clear for a short bounded window,
    // then present the result before Toby drives away.
    bool finishMinigame = labelType == LOADING_LABEL_GENERATING_TEXTURES &&
                          loading_minigame_active && loading_minigame_ever_played;
    if (finishMinigame) {
        loading_minigame_finish_state = 1;
        loading_minigame_graze_visible = false;
        uint64_t drainStarted = sceKernelGetProcessTimeWide();
        while (loading_minigame_any_bullets_active() &&
               sceKernelGetProcessTimeWide() - drainStarted < 500000ULL) {
            draw_loading_screen(true, 1.0f, labelType);
            sceKernelDelayThread(16667);
        }
        memset(loading_minigame_bullets, 0, sizeof(loading_minigame_bullets));
        loading_minigame_graze_visible = false;
        loading_minigame_finish_state = 2;
        loading_minigame_active = false;

        if (loading_minigame_hits == 0) {
            loading_minigame_won_started = true;
            loading_direct_sound_play(loading_minigame_won_source,
                                      loading_minigame_sfx_gain * 0.90f);
        }

        uint64_t resultStarted = sceKernelGetProcessTimeWide();
        while (sceKernelGetProcessTimeWide() - resultStarted < 650000ULL) {
            draw_loading_screen(true, 1.0f, labelType);
            sceKernelDelayThread(16667);
        }
        loading_minigame_finish_state = 0;
    }

    loading_minigame_active = false;
    loading_minigame_heart_over_ui = false;
    loading_minigame_graze_visible = false;
    memset(loading_minigame_bullets, 0, sizeof(loading_minigame_bullets));
    loading_car_exit_active = true;
    loading_car_exit_started_us = sceKernelGetProcessTimeWide();
    loading_minigame_cardrive_started = true;
    char carAudioLog[128];
    snprintf(carAudioLog, sizeof(carAudioLog),
             "LOADING_MINIGAME=cardrive_begin source=%u gain=%.2f",
             (unsigned)loading_minigame_cardrive_source,
             loading_minigame_sfx_gain * 0.72f);
    log_line(carAudioLog);
    loading_direct_sound_play(loading_minigame_cardrive_source,
                              loading_minigame_sfx_gain * 0.72f);
    while (sceKernelGetProcessTimeWide() - loading_car_exit_started_us <
           LOADING_CAR_EXIT_DURATION_US) {
        draw_loading_screen(true, 1.0f, labelType);
        sceKernelDelayThread(16667);
    }
    // Present one clamped t=1 frame: the car is guaranteed fully off-screen.
    draw_loading_screen(true, 1.0f, labelType);
    // snd_cardrive is intentionally NOT stopped here; it continues as a tail
    // cue across the transition and is collected only after OpenAL reports it
    // finished. The NO HIT cue, when applicable, started before the drive-away.
    loading_car_exit_active = false;
}

// Long global/Create scripts otherwise keep main() occupied until the selected
// room is ready. The VM invokes this pump cooperatively, keeping every VitaGL
// call on the main thread instead of racing SceGxm from a render thread.
void VitaProbe_pumpLoadingScreen(void) {
    if (first_load_animation_active) {
        first_load_animation_pump(false);
        return;
    }
    if (!loading_overlay_active || loading_labels_texture == 0) return;
    uint64_t now = sceKernelGetProcessTimeWide();
    if (loading_animation_last_present_us != 0 &&
        now - loading_animation_last_present_us < 33333ULL) return;
    loading_animation_last_present_us = now;
    draw_loading_screen(loading_overlay_show_progress, loading_overlay_ratio,
                        loading_overlay_label_type);
}

typedef struct FirstLoadAudioJob {
    short* pcm;
    int samples;
    int channels;
    int sample_rate;
    int volume;
} FirstLoadAudioJob;

static int first_load_audio_thread(SceSize args, void* argp) {
    (void)args;
    FirstLoadAudioJob* job = *(FirstLoadAudioJob**)argp;
    if (job == NULL) return sceKernelExitDeleteThread(0);

    const int grain = 1024;
    SceAudioOutMode mode = job->channels == 1 ?
                           SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grain,
                                   job->sample_rate, mode);
    if (port >= 0) {
        log_line("FIRST_LOAD_AUDIO=port_open");
        int volumes[2] = {job->volume, job->volume};
        sceAudioOutSetVolume(port,
            SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volumes);
        short* tail = NULL;
        for (int offset = 0; offset < job->samples; offset += grain) {
            int remaining = job->samples - offset;
            const short* block = job->pcm + offset * job->channels;
            if (remaining < grain) {
                size_t values = (size_t)grain * (size_t)job->channels;
                tail = (short*)calloc(values, sizeof(short));
                if (tail == NULL) break;
                memcpy(tail, block,
                       (size_t)remaining * (size_t)job->channels * sizeof(short));
                block = tail;
            }
            sceAudioOutOutput(port, block);
            free(tail);
            tail = NULL;
        }
        sceAudioOutReleasePort(port);
    } else {
        char line[96];
        snprintf(line, sizeof(line),
                 "FIRST_LOAD_AUDIO=port_failed result=0x%08X",
                 (unsigned int)port);
        log_line(line);
    }
    free(job->pcm);
    free(job);
    return sceKernelExitDeleteThread(0);
}

static void first_load_audio_start(void) {
    if (first_load_audio_started) return;
    first_load_audio_started = true;

    FirstLoadAudioJob* job = (FirstLoadAudioJob*)calloc(1, sizeof(*job));
    if (job == NULL) return;
    job->samples = stb_vorbis_decode_filename(
        "app0:assets/loading/loadtoby.ogg", &job->channels,
        &job->sample_rate, &job->pcm);
    job->volume = first_load_audio_volume;
    if (job->samples <= 0 || job->pcm == NULL ||
        (job->channels != 1 && job->channels != 2)) {
        free(job->pcm);
        free(job);
        log_line("FIRST_LOAD_AUDIO=decode_failed");
        return;
    }

    // SceAudioOut's native clock is 48 kHz; loadtoby.ogg is 44.1 kHz. A
    // compact linear conversion avoids the port rejecting the stream while
    // preserving the original asset in the project.
    if (job->sample_rate != 48000) {
        int sourceRate = job->sample_rate;
        int outputSamples = (int)(((int64_t)job->samples * 48000 +
                                  sourceRate / 2) / sourceRate);
        short* converted = (short*)malloc((size_t)outputSamples *
                           (size_t)job->channels * sizeof(short));
        if (converted == NULL) {
            free(job->pcm);
            free(job);
            log_line("FIRST_LOAD_AUDIO=resample_alloc_failed");
            return;
        }
        for (int output = 0; output < outputSamples; ++output) {
            uint64_t position = (uint64_t)output * (uint64_t)sourceRate;
            int source = (int)(position / 48000ULL);
            uint32_t fraction = (uint32_t)(position % 48000ULL);
            int next = source + 1 < job->samples ? source + 1 : source;
            for (int channel = 0; channel < job->channels; ++channel) {
                int a = job->pcm[source * job->channels + channel];
                int b = job->pcm[next * job->channels + channel];
                int sample = a + (int)(((int64_t)(b - a) * fraction) / 48000);
                converted[output * job->channels + channel] = (short)sample;
            }
        }
        free(job->pcm);
        job->pcm = converted;
        job->samples = outputSamples;
        job->sample_rate = 48000;
        log_line("FIRST_LOAD_AUDIO=resampled 44100_to_48000");
    }

    SceUID thread = sceKernelCreateThread("Deltarune first-load audio",
        first_load_audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (thread < 0 || sceKernelStartThread(thread, sizeof(job), &job) < 0) {
        free(job->pcm);
        free(job);
        log_line("FIRST_LOAD_AUDIO=thread_failed");
        return;
    }
    log_line("FIRST_LOAD_AUDIO=started frame=50");
}

static void draw_first_load_frame_tint(GLuint texture, float brightness) {
    if (texture == 0) return;
    GLint previousFramebuffer = 0, previousTexture = 0, previousMatrixMode = GL_MODELVIEW;
    GLint previousViewport[4] = {0, 0, 960, 544};
    GLint previousScissor[4] = {0, 0, 960, 544};
    GLfloat previousColor[4] = {1, 1, 1, 1};
    GLboolean hadScissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean hadDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLboolean hadBlend = glIsEnabled(GL_BLEND);
    GLboolean hadTexture = glIsEnabled(GL_TEXTURE_2D);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_SCISSOR_BOX, previousScissor);
    glGetFloatv(GL_CURRENT_COLOR, previousColor);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 960, 544);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glColor4f(brightness, brightness, brightness, 1.0f);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // first_load frames are authored at the Vita framebuffer resolution.
    // Draw 1:1 across the full 960x544 canvas; do not reinterpret a cropped
    // content size as right/bottom coordinates.
    draw_loading_texture(texture, 0, 0, 960, 544);
    vglSwapBuffers(GL_FALSE);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    glScissor(previousScissor[0], previousScissor[1],
              previousScissor[2], previousScissor[3]);
    if (hadScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (hadDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (hadCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (hadBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (hadTexture) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    glColor4fv(previousColor);
    glMatrixMode((GLenum)previousMatrixMode);
}

static void draw_first_load_frame(GLuint texture) {
    draw_first_load_frame_tint(texture, 1.0f);
}

static int first_load_decoder_worker(SceSize args, void* argp) {
    (void)args;
    (void)argp;
    for (int frame = 2; frame < FIRST_LOAD_FRAME_COUNT && !first_load_decoder_stop; ++frame) {
        const int slotIndex = (frame - 2) % FIRST_LOAD_DECODE_SLOTS;
        FirstLoadDecodedFrame* slot = &first_load_decode_slots[slotIndex];
        while (slot->state != 0 && !first_load_decoder_stop)
            sceKernelDelayThread(500);
        if (first_load_decoder_stop) break;

        char path[128];
        snprintf(path, sizeof(path),
                 "ux0:data/deltarune/deltarunevita/ui/first_load/frame_%03d.png",
                 frame);
        int w = 0, h = 0;
        unsigned char* pixels = load_loading_pixels_rgba(path, &w, &h);
        slot->frame = frame;
        slot->pixels = pixels;
        slot->width = w;
        slot->height = h;
        __sync_synchronize();
        slot->state = 1;
    }
    first_load_decoder_done = 1;
    return sceKernelExitDeleteThread(0);
}

static FirstLoadDecodedFrame* first_load_wait_decoded_frame(int frame) {
    const int slotIndex = (frame - 2) % FIRST_LOAD_DECODE_SLOTS;
    FirstLoadDecodedFrame* slot = &first_load_decode_slots[slotIndex];
    while (!first_load_decoder_stop) {
        __sync_synchronize();
        if (slot->state == 1 && slot->frame == frame)
            return slot;
        sceKernelDelayThread(250);
    }
    return NULL;
}

static void first_load_release_decode_slot(FirstLoadDecodedFrame* slot) {
    if (slot == NULL) return;
    if (slot->pixels != NULL) {
        stbi_image_free(slot->pixels);
        slot->pixels = NULL;
    }
    slot->width = 0;
    slot->height = 0;
    __sync_synchronize();
    slot->state = 0;
}

static void first_load_decoder_shutdown(void) {
    first_load_decoder_stop = 1;
    if (first_load_decoder_thread >= 0) {
        sceKernelWaitThreadEnd(first_load_decoder_thread, NULL, NULL);
        first_load_decoder_thread = -1;
    }
    for (int i = 0; i < FIRST_LOAD_DECODE_SLOTS; ++i)
        first_load_release_decode_slot(&first_load_decode_slots[i]);
    first_load_decoder_done = 0;
}

static bool first_load_video_play(float volume) {
    const char* path = "app0:first_load.mp4";
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0) {
        if (sceIoGetstat("ux0:data/deltarune/deltarunevita/vid/first_load.mp4", &st) >= 0)
            path = "ux0:data/deltarune/deltarunevita/vid/first_load.mp4";
        else if (sceIoGetstat("ux0:data/deltarune/deltarunevita/first_load.mp4", &st) >= 0)
            path = "ux0:data/deltarune/deltarunevita/first_load.mp4";
    }
    char logBuf[256];
    snprintf(logBuf, sizeof(logBuf), "FIRST_LOAD_VIDEO=init_begin path=%s eof_driven=1", path);
    log_line(logBuf);

    int initResult = VitaVideo_init();
    if (initResult < 0) {
        char line[128];
        snprintf(line, sizeof(line), "FIRST_LOAD_VIDEO=init_failed result=0x%08X fallback=png", (unsigned)initResult);
        log_line(line);
        VitaVideo_shutdown();
        return false;
    }

    VitaVideo_setLooping(false);
    VitaVideo_setVolume(volume);
    int openResult = VitaVideo_open(path);
    if (openResult < 0) {
        char line[128];
        snprintf(line, sizeof(line), "FIRST_LOAD_VIDEO=open_failed result=0x%08X fallback=png", (unsigned)openResult);
        log_line(line);
        VitaVideo_shutdown();
        return false;
    }

    // Pic0 remains visible until the hardware decoder has produced a real
    // frame. Only then do we perform the first VitaGL swap.
    vglWaitVblankStart(GL_TRUE);
    const uint64_t openedAt = sceKernelGetProcessTimeWide();
    uint64_t firstFrameAt = 0;
    bool presented = false;
    bool timedOut = false;

    for (;;) {
        bool active = VitaVideo_updateFrame();
        // Startup playback happens before Runner/GameMaker async events exist.
        // Discard AVPlayer start/end notifications here so they cannot leak into
        // Chapter 0 once the runner is created.
        (void)VitaVideo_consumeEvent();
        uint64_t now = sceKernelGetProcessTimeWide();

        if (VitaVideo_hasDecodedFrame()) {
            if (!presented) {
                presented = true;
                firstFrameAt = now;
                char line[128];
                snprintf(line, sizeof(line),
                         "FIRST_LOAD_VIDEO=first_frame wait_us=%llu",
                         (unsigned long long)(firstFrameAt - openedAt));
                log_line(line);
            }
            // first_load.mp4 already carries its AAC track. VitaVideo's
            // SceAvPlayer audio thread outputs it directly; loadtoby.ogg remains
            // exclusive to the legacy PNG fallback.
            VitaVideo_drawHost(0.0f, 0.0f, 960.0f, 544.0f);
            vglSwapBuffers(GL_FALSE);
        } else if (!presented) {
            // Do not leave the user stuck on Pic0 if SceAvPlayer accepts the
            // MP4 but never starts decoding (the Chapter 3 failure mode).
            if (now - openedAt >= 3000000ULL) {
                timedOut = true;
                log_line("FIRST_LOAD_VIDEO=first_frame_timeout timeout_us=3000000 fallback=png");
                break;
            }
            sceKernelDelayThread(1000);
        }

        if (!active || VitaVideo_getStatus() == VITA_VIDEO_STATUS_NONE)
            break;

        // Corrupt timestamps must never stall startup indefinitely.
        if (now - openedAt >= 30000000ULL) {
            timedOut = true;
            log_line("FIRST_LOAD_VIDEO=playback_timeout timeout_us=30000000");
            break;
        }
    }

    if (presented && !timedOut && VitaVideo_hasDecodedFrame()) {
        const uint64_t transitionStarted = sceKernelGetProcessTimeWide();
        const uint64_t transitionDuration = VitaMediaRules_firstLoadHoldUs() +
                                            VitaMediaRules_firstLoadFadeUs();
        log_line("FIRST_LOAD_VIDEO=last_frame_hold_begin hold_us=3000000 fade_us=500000");
        for (;;) {
            const uint64_t elapsed = sceKernelGetProcessTimeWide() - transitionStarted;
            const float brightness = VitaMediaRules_firstLoadBrightness(elapsed);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, 960, 544);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            VitaVideo_drawHostBrightness(0.0f, 0.0f, 960.0f, 544.0f, brightness);
            vglSwapBuffers(GL_FALSE);
            if (elapsed >= transitionDuration) break;
            sceKernelDelayThread(16000);
        }
        log_line("FIRST_LOAD_VIDEO=last_frame_fade_complete");
    }

    const uint64_t endedAt = sceKernelGetProcessTimeWide();
    VitaVideo_close();
    VitaVideo_shutdown();

    char line[160];
    snprintf(line, sizeof(line),
             "FIRST_LOAD_VIDEO=complete presented=%d timeout=%d elapsed_us=%llu",
             presented ? 1 : 0, timedOut ? 1 : 0,
             (unsigned long long)(endedAt - openedAt));
    log_line(line);
    return presented && !timedOut;
}

static void first_load_animation_begin(void) {
    first_load_animation_active = true;
    first_load_animation_next_frame = 1;
    first_load_animation_next_at_us = 0;
    first_load_animation_start_us = 0;
    first_load_decoder_stop = 0;
    first_load_decoder_done = 0;
    memset(first_load_decode_slots, 0, sizeof(first_load_decode_slots));
    if (first_load_stream_texture != 0) {
        glDeleteTextures(1, &first_load_stream_texture);
        first_load_stream_texture = 0;
    }
    if (first_load_frame1_pixels != NULL) {
        stbi_image_free(first_load_frame1_pixels);
        first_load_frame1_pixels = NULL;
    }
    first_load_frame1_width = first_load_frame1_height = 0;

    first_load_audio_ready = true;
    first_load_audio_started = false;
    log_line("FIRST_LOAD_ANIMATION=begin frames=133 total_us=4500000 scheduler=absolute async_decode=1 ring=6 persistent_texture=1 format=rgb_png before_datawin=1");

    // Decode frame 0 and frame 1 before the visible clock begins. Once frame 0
    // is on screen, all later disk/decode work happens on the worker thread.
    int firstW = 0, firstH = 0;
    unsigned char* firstPixels = load_loading_pixels_rgba(
        "ux0:data/deltarune/deltarunevita/ui/first_load/frame_000.png", &firstW, &firstH);
    first_load_frame1_pixels = load_loading_pixels_rgba(
        "ux0:data/deltarune/deltarunevita/ui/first_load/frame_001.png",
        &first_load_frame1_width, &first_load_frame1_height);
    if (firstPixels != NULL) {
        first_load_stream_texture = create_loading_texture_rgba(firstPixels, firstW, firstH);
        stbi_image_free(firstPixels);
    }
    if (first_load_stream_texture != 0) {
        draw_first_load_frame(first_load_stream_texture);
        first_load_animation_start_us = sceKernelGetProcessTimeWide();
    } else {
        log_line("FIRST_LOAD_ANIMATION=missing frame=0");
        first_load_animation_start_us = sceKernelGetProcessTimeWide();
    }

    bool firstLoadFourthCore = false;
    first_load_decoder_thread = VitaThread_createBackground(
        "Deltarune first-load decoder", first_load_decoder_worker,
        0x10000100, 0x18000, 0, &firstLoadFourthCore);
    log_line(firstLoadFourthCore
        ? "CAPUNLOCKER=first_load_decoder core=4 enabled=1"
        : "CAPUNLOCKER=first_load_decoder core=default enabled=0 fallback=1");
    if (first_load_decoder_thread >= 0) {
        if (sceKernelStartThread(first_load_decoder_thread, 0, NULL) < 0) {
            first_load_decoder_thread = -1;
            log_line("FIRST_LOAD_ANIMATION=decoder_thread_start_failed");
        }
    } else {
        log_line("FIRST_LOAD_ANIMATION=decoder_thread_create_failed");
    }

    first_load_animation_next_at_us = first_load_animation_start_us +
        (FIRST_LOAD_FRAMES_DURATION_US / FIRST_LOAD_FRAME_COUNT);
}

static void first_load_animation_play_to_end(void) {
    while (first_load_animation_next_frame < FIRST_LOAD_FRAME_COUNT)
        first_load_animation_pump(true);

    const uint64_t framePhaseEnd = first_load_animation_start_us + FIRST_LOAD_FRAMES_DURATION_US;
    const uint64_t now = sceKernelGetProcessTimeWide();
    if (now < framePhaseEnd)
        sceKernelDelayThread((unsigned int)(framePhaseEnd - now));
    log_line("FIRST_LOAD_ANIMATION=last_frame_held frame_phase_complete=1");
}

static void first_load_animation_pump(bool waitForDeadline) {
    if (!first_load_animation_active) return;
    if (first_load_animation_next_frame >= FIRST_LOAD_FRAME_COUNT) return;

    if (first_load_animation_next_frame == 50 && !first_load_audio_ready)
        return;

    uint64_t now = sceKernelGetProcessTimeWide();
    if (now < first_load_animation_next_at_us) {
        if (!waitForDeadline) return;
        sceKernelDelayThread((unsigned int)(first_load_animation_next_at_us - now));
    }

    const int frameToPresent = first_load_animation_next_frame;
    unsigned char* pixels = NULL;
    int w = 0, h = 0;
    FirstLoadDecodedFrame* slot = NULL;
    if (frameToPresent == 1) {
        pixels = first_load_frame1_pixels;
        w = first_load_frame1_width;
        h = first_load_frame1_height;
        first_load_frame1_pixels = NULL;
    } else {
        slot = first_load_wait_decoded_frame(frameToPresent);
        if (slot != NULL) {
            pixels = slot->pixels;
            w = slot->width;
            h = slot->height;
        }
    }

    if (pixels != NULL && first_load_stream_texture != 0 && w == 960 && h == 544) {
        glBindTexture(GL_TEXTURE_2D, first_load_stream_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (frameToPresent == 50)
            first_load_audio_start();
        draw_first_load_frame(first_load_stream_texture);
    } else {
        char line[160];
        snprintf(line, sizeof(line),
                 "FIRST_LOAD_ANIMATION=frame_unavailable frame=%d w=%d h=%d",
                 frameToPresent, w, h);
        log_line(line);
    }

    if (frameToPresent == 1) {
        if (pixels != NULL) stbi_image_free(pixels);
    } else if (slot != NULL) {
        first_load_release_decode_slot(slot);
    }

    ++first_load_animation_next_frame;
    if (first_load_animation_next_frame < FIRST_LOAD_FRAME_COUNT) {
        first_load_animation_next_at_us = first_load_animation_start_us +
            ((uint64_t)first_load_animation_next_frame * FIRST_LOAD_FRAMES_DURATION_US) /
            FIRST_LOAD_FRAME_COUNT;
    }
}

static void first_load_animation_finish(void) {
    if (!first_load_animation_active) return;
    first_load_audio_ready = true;
    while (first_load_animation_next_frame < FIRST_LOAD_FRAME_COUNT)
        first_load_animation_pump(true);

    first_load_decoder_shutdown();

    if (first_load_stream_texture != 0) {
        const uint64_t transitionStarted = sceKernelGetProcessTimeWide();
        const uint64_t transitionDuration = VitaMediaRules_firstLoadHoldUs() +
                                            VitaMediaRules_firstLoadFadeUs();
        log_line("FIRST_LOAD_ANIMATION=last_frame_hold_begin hold_us=3000000 fade_us=500000");
        for (;;) {
            const uint64_t elapsed = sceKernelGetProcessTimeWide() - transitionStarted;
            draw_first_load_frame_tint(first_load_stream_texture,
                                       VitaMediaRules_firstLoadBrightness(elapsed));
            if (elapsed >= transitionDuration) break;
            sceKernelDelayThread(16000);
        }
        log_line("FIRST_LOAD_ANIMATION=last_frame_fade_complete");
        glDeleteTextures(1, &first_load_stream_texture);
        first_load_stream_texture = 0;
    }
    first_load_animation_active = false;
    char durationLog[192];
    const uint64_t elapsed = sceKernelGetProcessTimeWide() - first_load_animation_start_us;
    snprintf(durationLog, sizeof(durationLog),
             "FIRST_LOAD_ANIMATION=complete elapsed_us=%llu target_us=%llu decoder_done=%d",
             (unsigned long long)elapsed,
             (unsigned long long)FIRST_LOAD_TOTAL_DURATION_US,
             (int)first_load_decoder_done);
    log_line(durationLog);
}

static void texture_loading_progress(uint32_t current, uint32_t total, void *user) {
    (void)user;
    loading_overlay_current = current;
    loading_overlay_total = total;
    float ratio = total > 0 ? (float)current / (float)total : 1.0f;
    loading_overlay_label_type = LOADING_LABEL_GENERATING_TEXTURES;
    loading_overlay_show_progress = true;
    loading_overlay_ratio = ratio;
    uint64_t now = sceKernelGetProcessTimeWide();
    // Before Play, 10 FPS is enough for the progress artwork and avoids making
    // framebuffer swaps compete with BC3 encode/write. Once Play is pressed,
    // keep the synchronous remainder responsive until the page-boundary handoff.
    uint64_t interval = loading_minigame_active ? 33333ULL : 100000ULL;
    if (current < total && loading_texture_progress_last_draw_us != 0 &&
        now - loading_texture_progress_last_draw_us < interval) return;
    loading_texture_progress_last_draw_us = now;
    draw_loading_screen(true, ratio, loading_overlay_label_type);
}


static void texture_loading_worker_progress(uint32_t current, uint32_t total, void *user) {
    LoadingTextureWorkerJob* job = (LoadingTextureWorkerJob*)user;
    if (job == NULL) return;
    // Never move the visible bar backwards when the resumed scan quickly
    // walks over pages already generated by the synchronous phase.
    if (current > job->current) job->current = current;
    job->total = total;
    __sync_synchronize();
}

static int loading_texture_worker_entry(SceSize args, void* argp) {
    (void)args;
    LoadingTextureWorkerJob* job = *(LoadingTextureWorkerJob**)argp;
    if (job == NULL || job->dw == NULL)
        return sceKernelExitDeleteThread(0);
    job->result = GLLegacyRenderer_prepareTextureCache(
        job->dw, texture_loading_worker_progress, job);
    __sync_synchronize();
    job->done = 1;
    return sceKernelExitDeleteThread(0);
}

static bool loading_texture_worker_start(DataWin* dw) {
    memset(&loading_texture_worker_job, 0, sizeof(loading_texture_worker_job));
    loading_texture_worker_job.dw = dw;
    loading_texture_worker_job.current = loading_overlay_current;
    loading_texture_worker_job.total = loading_overlay_total;
    LoadingTextureWorkerJob* job = &loading_texture_worker_job;
    bool textureWorkerFourthCore = false;
    loading_texture_worker_thread_id = VitaThread_createBackground(
        "Deltarune texture generator", loading_texture_worker_entry,
        0x10000100, 0x80000, 0, &textureWorkerFourthCore);
    log_line(textureWorkerFourthCore
        ? "CAPUNLOCKER=texture_generator core=4 enabled=1"
        : "CAPUNLOCKER=texture_generator core=default enabled=0 fallback=1");
    if (loading_texture_worker_thread_id < 0) return false;
    if (sceKernelStartThread(loading_texture_worker_thread_id, sizeof(job), &job) < 0) {
        loading_texture_worker_thread_id = -1;
        return false;
    }
    log_line("LOADING_MINIGAME=async_generation started=1");
    return true;
}

static uint32_t loading_texture_worker_present_until_done(void) {
    while (!loading_texture_worker_job.done) {
        __sync_synchronize();
        uint32_t current = loading_texture_worker_job.current;
        uint32_t total = loading_texture_worker_job.total;
        if (total == 0) total = loading_overlay_total;
        loading_overlay_current = current;
        loading_overlay_total = total;
        float ratio = total > 0 ? (float)current / (float)total : 0.0f;
        draw_loading_screen(true, ratio, LOADING_LABEL_GENERATING_TEXTURES);
        // Stop gives CPU back to texture generation while retaining a smooth
        // animated loader and a responsive persistent PLAY hint.
        sceKernelDelayThread(loading_minigame_active ? 16667 : 50000);
    }
    __sync_synchronize();
    uint32_t result = loading_texture_worker_job.result;
    if (loading_texture_worker_thread_id >= 0) {
        sceKernelWaitThreadEnd(loading_texture_worker_thread_id, NULL, NULL);
        loading_texture_worker_thread_id = -1;
    }
    log_line("LOADING_MINIGAME=async_generation complete=1");
    return result;
}

static void shader_loading_progress(uint32_t current, uint32_t total, void *user) {
    (void)user;
    loading_overlay_current = current;
    loading_overlay_total = total;
    float ratio = total > 0 ? (float)current / (float)total : 1.0f;
    loading_overlay_label_type = LOADING_LABEL_LOADING_SHADERS;
    loading_overlay_show_progress = true;
    loading_overlay_ratio = ratio;
    draw_loading_screen(true, ratio, loading_overlay_label_type);
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
        g_vitaHadExplicitChapterRequest = true;
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
    glFinish();
    sceKernelDelayThread(100000);
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
                char old_path[512];
                char new_path[512];
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

static void resolve_game_paths(int active_chapter, const VitaSettings* settings,
                               char* game_path, size_t game_path_size,
                               char* bundle_path, size_t bundle_path_size) {
    snprintf(game_path, game_path_size, DATA_ROOT "chapter%d/data.win",
             active_chapter);
    snprintf(bundle_path, bundle_path_size, DATA_ROOT "chapter%d/",
             active_chapter);
    if (active_chapter == 0) {
        SceIoStat st;
        if (sceIoGetstat(game_path, &st) < 0) {
            snprintf(game_path, game_path_size, "app0:assets/chapter0/data.win");
            snprintf(bundle_path, bundle_path_size, "app0:assets/chapter0/");
            log_line("LAUNCHER=app0_fallback_loaded");
        }
    }

    if (settings_language_uses_external_mod(settings)) {
        SceIoStat mod_stat;
        char mod_game[192];
        const char* modName = settings->modNames[settings->modIndex];
        snprintf(mod_game, sizeof(mod_game),
                 DATA_ROOT "mods/Lang/%s/chapter%d/data.win", modName,
                 active_chapter);
        if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
            snprintf(game_path, game_path_size, "%s", mod_game);
            log_line("MOD=alternate_game=enabled");
            return;
        }

        const char* alias = strcmp(modName, "Russkiy") == 0 ? "Russian" :
                            (strcmp(modName, "Russian") == 0 ? "Russkiy" : NULL);
        if (alias != NULL) {
            snprintf(mod_game, sizeof(mod_game),
                     DATA_ROOT "mods/Lang/%s/chapter%d/data.win", alias,
                     active_chapter);
            if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
                snprintf(game_path, game_path_size, "%s", mod_game);
                log_line("MOD=alternate_game=enabled_alias");
                return;
            }
        }
        if (active_chapter == 0) {
            const char* legacyName = strcmp(modName, "Portuguese-BR") == 0 ?
                                     "PTBR" : modName;
            snprintf(mod_game, sizeof(mod_game),
                     DATA_ROOT "mods/%s/chapter0/data.win", legacyName);
            if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
                snprintf(game_path, game_path_size, "%s", mod_game);
                log_line("MOD=alternate_game=legacy_chapter0_fallback");
            } else {
                log_line("MOD=alternate_game=missing_chapter0_using_english");
            }
        }
    }
}

static DataWinParserOptions make_datawin_options(bool worker) {
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
    options.progressCallback = worker ? NULL : progress;
    return options;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Record whether the deltarunevita root folder exists before we create it
    SceIoStat rootStat;
    memset(&rootStat, 0, sizeof(rootStat));
    g_initialRootExists = (sceIoGetstat("ux0:data/deltarune/deltarunevita", &rootStat) >= 0) &&
                          SCE_S_ISDIR(rootStat.st_mode);

    sceIoMkdir("ux0:data/deltarune", 0777);
    sceIoMkdir(DATA_ROOT, 0777);
    sceIoMkdir(SAVE_PATH, 0777);
    migrate_old_saves();
    g_vitaProbeLoggingEnabled = config_devmode_enabled();
    sceIoRemove(LOG_PATH);
    int active_chapter = consume_next_chapter();
    g_vitaActiveChapter = active_chapter;

    log_line("Deltarune Windows data + Butterscotch VitaRenderer " PORT_BUILD_VERSION);
    log_line("BACKEND=vitagl executable=app0:/vitagl.bin");
    log_line("MAIN_STACK=4194304");
    char startup_line[96];
    snprintf(startup_line, sizeof(startup_line), "AUDIO=openal ENTRY=chapter%d CONTROLS=vita+touch", active_chapter);
    log_line(startup_line);
    if (active_chapter == 5)
    log_line("CH5_OPT=legacy_lut_skipped sunshadows_krisyard_enabled original_atlas_min=2048 pvr_source=ux0_steam_v0.0.253 compression=bc3");

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    // The generated fixed-pipeline shader used by the legacy renderer trips the
    // aggressive runtime optimizer on Vita. O0 is slower only during the first
    // compilation and avoids that optimizer path entirely.
    vglSetupRuntimeShaderCompiler(SHARK_OPT_SLOW, 0, 0, 0);
    log_line("SHARK=opt_slow fastmath=0 fastprecision=0 fastint=0");
    log_line("RENDERER=vita_arrays_v2 immediate_bridge=all STDIO=unbuffered");
    // Keep the runner at the standard high-performance clocks used by mature
    // Vita ports. The standalone runner did not inherit this initialization
    // from the old loaders, leaving GPU-heavy legacy-GL rooms underclocked.
    int clock_arm = scePowerSetArmClockFrequency(444);
    int clock_bus = scePowerSetBusClockFrequency(222);
    int clock_gpu = scePowerSetGpuClockFrequency(222);
    int clock_xbar = scePowerSetGpuXbarClockFrequency(166);
    char clock_log[160];
    snprintf(clock_log, sizeof(clock_log),
             "CLOCKS=arm444:%d bus222:%d gpu222:%d xbar166:%d",
             clock_arm, clock_bus, clock_gpu, clock_xbar);
    log_line(clock_log);

    // vglInitExtended consumes nearly every heap it sees after applying only
    // one RAM threshold. On some relaunches that made its allocation depend on
    // system fragmentation and it stopped inside initialization before the
    // first framebuffer was shown. Use the stable heap totals measured in the
    // working builds, capped by the memory actually available at this launch.
    SceKernelFreeMemorySizeInfo preInitMemory;
    memset(&preInitMemory, 0, sizeof(preInitMemory));
    preInitMemory.size = sizeof(preInitMemory);
    sceKernelGetFreeMemorySize(&preInitMemory);
    // Chapter 3's hardware MP4 decoder allocates its output frames through
    // VGL_MEM_SLOW. A 48 MiB pool was already down to ~3 MiB in the video
    // room, while roughly 30 MiB of ordinary user RAM remained unreachable by
    // VitaGL. Give this chapter another 12 MiB without changing CDRAM/PHY.
    const int modernGlRequested = config_modern_gl_enabled();
    uint32_t ramPoolCap = modernGlRequested ? 64U * 1024U * 1024U :
                          (active_chapter == 5 ? 44U * 1024U * 1024U :
                          (active_chapter == 3 ? 60U * 1024U * 1024U :
                                                 48U * 1024U * 1024U));
    uint32_t ramPool = preInitMemory.size_user > 40U * 1024U * 1024U ?
                       preInitMemory.size_user - (modernGlRequested ? 20U : 32U) * 1024U * 1024U :
                       8U * 1024U * 1024U;
    if (ramPool > ramPoolCap) ramPool = ramPoolCap;
    uint32_t cdramPool = preInitMemory.size_cdram > 16U * 1024U * 1024U ?
                         preInitMemory.size_cdram - 16U * 1024U * 1024U :
                         preInitMemory.size_cdram / 2U;
    if (cdramPool > 96U * 1024U * 1024U) cdramPool = 96U * 1024U * 1024U;
    uint32_t phyPool = 0; // Leave PHYCONT free for SceAvPlayer hardware video decoding
    char vitaglInitLog[192];
    snprintf(vitaglInitLog, sizeof(vitaglInitLog),
             "VITAGL=init_begin fixed ram=%u cdram=%u phy=%u free_user=%u free_cdram=%u free_phy=%u",
             (unsigned int)ramPool, (unsigned int)cdramPool, (unsigned int)phyPool,
             (unsigned int)preInitMemory.size_user,
             (unsigned int)preInitMemory.size_cdram,
             (unsigned int)preInitMemory.size_phycont);
    log_line(vitaglInitLog);
    vglInitWithCustomSizes(0, 960, 544, ramPool, cdramPool, phyPool, 0,
                           SCE_GXM_MULTISAMPLE_NONE);
    log_line("VITAGL=init_complete");

    // vitaShaRK loads this module from the fixed ur0:data path when the first
    // runtime shader is linked. Report the missing dependency here, while the
    // native dialog can still be rendered, instead of crashing later with no
    // actionable information for the player.
    if (!is_file_valid("ur0:data/libshacccg.suprx")) {
        log_line("STARTUP_FATAL=libshacccg_missing");
        show_shader_dependency_error_and_exit();
    }

    // Feature 2: fail fast with a readable report if the game data is not
    // installed, instead of proceeding and crashing later in the boot path.
    verify_essential_data_or_report(active_chapter);

    VitaSettings settings;
    VitaSettings_load(&settings);
    float firstLoadGain = settings.audioDisabled ? 0.0f :
        ((float)settings.masterVolume / 10.0f) *
        ((float)settings.musicVolume / 10.0f);
    if (firstLoadGain < 0.0f) firstLoadGain = 0.0f;
    if (firstLoadGain > 1.0f) firstLoadGain = 1.0f;
    first_load_audio_volume = (int)(firstLoadGain * SCE_AUDIO_VOLUME_0DB);

    char riskyUsbPlugin[128] = {0};
    const bool hasRiskyUsbPlugin = detect_risky_usb_plugin(
        riskyUsbPlugin, sizeof(riskyUsbPlugin));
    VitaVideo_setHardwareDisabled(hasRiskyUsbPlugin, riskyUsbPlugin);
    if (hasRiskyUsbPlugin) {
        char pluginLog[192];
        snprintf(pluginLog, sizeof(pluginLog),
                 "VIDEO_GUARD=usb_capture_plugin_detected plugin=%s avplayer=disabled",
                 riskyUsbPlugin);
        log_line(pluginLog);
    }

    VitaBackgroundWork* background_work = NULL;
    VitaStartupPreload startup_preload;
    memset(&startup_preload, 0, sizeof(startup_preload));
    char preload_game_path[192] = {0};
    char preload_bundle_path[128] = {0};
    bool startup_preload_started = false;
    if (active_chapter == 0 && !g_vitaHadExplicitChapterRequest) {
        resolve_game_paths(active_chapter, &settings,
                           preload_game_path, sizeof(preload_game_path),
                           preload_bundle_path, sizeof(preload_bundle_path));
        background_work =
            VitaBackgroundWork_create(VITA_BACKGROUND_DEFAULT_BUDGET);
        if (background_work != NULL) {
            DataWinParserOptions preload_options = make_datawin_options(true);
            startup_preload_started = VitaStartupPreload_begin(
                &startup_preload, background_work, preload_game_path,
                preload_options, 1U);
        }
        char preloadLog[320];
        snprintf(preloadLog, sizeof(preloadLog),
                 "STARTUP_PRELOAD=begin submitted=%d core4=%d path=%s",
                 startup_preload_started ? 1 : 0,
                 background_work != NULL &&
                     VitaBackgroundWork_usedUnlockedCore(background_work) ? 1 : 0,
                 preload_game_path);
        log_line(preloadLog);
    }

    // Cold LiveArea launch: the validated SceAvPlayer loader needs a small
    // VitaGL PHYCONT heap for its presentation shader while keeping the rest of
    // PHYCONT available to the decoder's dedicated raw frame buffers.
    const bool coldLauncherVideo = active_chapter == 0 && !g_vitaHadExplicitChapterRequest;
    if (coldLauncherVideo) {
        if (hasRiskyUsbPlugin) {
            log_line("FIRST_LOAD_VIDEO=disabled_plugin_conflict fallback=png");
            show_startup_warning(
                "Plugin USB de captura de audio/video detectado.\n\n"
                "A reproducao MP4 por hardware foi desativada para evitar crash. "
                "A abertura por imagens sera usada automaticamente.\n\n"
                "Para reativar os videos, desative UAC_UVC/UDCD_UVC "
                "no taiHEN e reinicie o Vita.");
            log_line("FIRST_LOAD_VIDEO=fallback_png_begin");
            first_load_animation_begin();
            first_load_animation_play_to_end();
            first_load_animation_finish();
        } else {
            vglPhycontMemLazyInit(4U * 1024U * 1024U);
            char firstPhyLog[128];
            snprintf(firstPhyLog, sizeof(firstPhyLog),
                     "VITAGL=first_load_phycont size=%u free_slow=%u",
                     4U * 1024U * 1024U, (unsigned int)vglMemFree(VGL_MEM_SLOW));
            log_line(firstPhyLog);
            if (!first_load_video_play(firstLoadGain)) {
                log_line("FIRST_LOAD_VIDEO=fallback_png_begin");
                first_load_animation_begin();
                first_load_animation_play_to_end();
                first_load_animation_finish();
            }
        }
    }

    // Lazy PHYCONT init is one-shot in this VitaGL. Explicit chapter processes
    // skip first_load and retain the larger gameplay pool; the cold launcher
    // keeps only the 4 MiB presentation heap used above.
    if (!coldLauncherVideo) {
        SceKernelFreeMemorySizeInfo postVideoMemory;
        memset(&postVideoMemory, 0, sizeof(postVideoMemory));
        postVideoMemory.size = sizeof(postVideoMemory);
        sceKernelGetFreeMemorySize(&postVideoMemory);
        // Chapters 3 and 5 open MP4s much later from GML. Keep almost all PHYCONT
        // outside VitaGL so the validated raw SceAvPlayer allocator can still
        // obtain contiguous decode buffers in room_dw_couch_video. Four MiB is
        // enough for the video presentation shader/handles, matching the
        // standalone loader that reproduced video+AAC on real hardware.
        uint32_t lazyPhy;
        if (active_chapter == 3 || active_chapter == 5) {
            lazyPhy = postVideoMemory.size_phycont >= 4U * 1024U * 1024U ?
                      4U * 1024U * 1024U : (uint32_t)postVideoMemory.size_phycont;
        } else {
            lazyPhy = postVideoMemory.size_phycont > 4U * 1024U * 1024U ?
                      postVideoMemory.size_phycont - 2U * 1024U * 1024U : 0;
            if (lazyPhy > 26U * 1024U * 1024U) lazyPhy = 26U * 1024U * 1024U;
        }
        if (lazyPhy > 0) {
            vglPhycontMemLazyInit(lazyPhy);
            char phyLog[128];
            snprintf(phyLog, sizeof(phyLog), "VITAGL=lazy_phycont_initialized size=%u free_slow=%u",
                     (unsigned int)lazyPhy, (unsigned int)vglMemFree(VGL_MEM_SLOW));
            log_line(phyLog);
        }
    }

    // The chapter process used to remain black through trophy setup and the
    // complete data.win parse. VitaGL is ready here, so establish the same
    // chapter Loading presentation before either blocking stage begins.
    const char* selectedLoadingLanguage =
        (settings.modIndex >= 0 && settings.modIndex < settings.modCount) ?
        settings.modNames[settings.modIndex] : "English";
    if (strcmp(selectedLoadingLanguage, "Russkiy") == 0)
        selectedLoadingLanguage = "Russian";
    else if (strcmp(selectedLoadingLanguage, "Nihongo") == 0)
        selectedLoadingLanguage = "Japanese";
    else if (strncmp(selectedLoadingLanguage, "Italian", 7) == 0)
        selectedLoadingLanguage = "Italian";
    snprintf(loading_language_name, sizeof(loading_language_name), "%s",
             selectedLoadingLanguage);
    if (active_chapter > 0) {
        loading_screen_init();
        loading_overlay_current = 0;
        loading_overlay_total = 31;
        draw_loading_screen(true, 0.0f, LOADING_LABEL_LOADING_TEXTURES);
        log_line("CHAPTER_LOADING=visible phase=pre_trophies_datawin");
    }

    bool nativeTrophies = VitaTrophies_init();
    VitaSettings_setNativeTrophiesActive(&settings, nativeTrophies);
    char trophyLog[192];
    snprintf(trophyLog, sizeof(trophyLog),
             "TROPHIES_NATIVE=%s stage=%s result=0x%08X %s",
             nativeTrophies ? "ready" : "unavailable",
             VitaTrophies_lastStage(), (unsigned int)VitaTrophies_lastResult(),
             nativeTrophies ? "notrpdrm_pack_available" : "local_fallback_active");
    log_line(trophyLog);
    log_memory_snapshot("after_vitagl_init", NULL);

    // vglSwapBuffers()'s boolean argument controls Common Dialog updates, not
    // synchronization.  Configure VitaGL's real swap interval explicitly.
    vglWaitVblankStart(settings.vsyncEnabled ? GL_TRUE : GL_FALSE);
    char settingsLog[256];
    snprintf(settingsLog, sizeof(settingsLog),
             "SETTINGS_LOAD master=%d music=%d sfx=%d disabled=%d vsync=%d mod=%s",
             settings.masterVolume, settings.musicVolume, settings.sfxVolume,
             settings.audioDisabled ? 1 : 0, settings.vsyncEnabled ? 1 : 0,
             (settings.modIndex >= 0 && settings.modIndex < settings.modCount) ? settings.modNames[settings.modIndex] : "English");
    log_line(settingsLog);
    // Reconcile the persistent in-game trophy file with the Vita database on
    // every boot. Existing system trophies are skipped by VitaTrophies, while
    // local-only unlocks are queued without requiring the player to earn them
    // again.
    uint32_t localTrophyMask = 0;
    unsigned int localTrophyCount = 0;
    for (int trophy = 0; trophy < 30; ++trophy) {
        if (!settings.trophiesUnlocked[trophy]) continue;
        localTrophyMask |= 1U << trophy;
        ++localTrophyCount;
    }
    unsigned int trophiesQueued = VitaTrophies_syncMask(localTrophyMask);
    snprintf(trophyLog, sizeof(trophyLog),
             "TROPHIES_SYNC=boot local=%u queued_native=%u native=%s",
    localTrophyCount, trophiesQueued, nativeTrophies ? "ready" : "unavailable");
    log_line(trophyLog);
    VitaSettings_setActiveChapter(active_chapter);
    g_vitaExperimentalGlActive = settings.rendererMode == VITA_RENDERER_EXPERIMENTAL;
    VitaSettings_forceLegacyRenderer(&settings);
    if (settings.rendererMode == VITA_RENDERER_AUTOMATIC) {
        char rendererSelectionLog[160];
        snprintf(rendererSelectionLog, sizeof(rendererSelectionLog),
                 "RENDERER_AUTO_SELECT chapter=%d selected=%s reason=%s",
                 active_chapter,
                 settings.modernGlEnabled ? "modern-gl" : "legacy-gl",
                 active_chapter == 3 ? "chapter3_round2_gpu_stability" : "chapter_default");
        log_line(rendererSelectionLog);
    }
    VitaSettings_setLauncherMode(active_chapter == 0);
    VitaBorders_init(active_chapter);

    int next_chapter = -1;
    char game_path[192];
    char bundle_path[128];
    snprintf(game_path, sizeof(game_path), DATA_ROOT "chapter%d/data.win", active_chapter);
    snprintf(bundle_path, sizeof(bundle_path), DATA_ROOT "chapter%d/", active_chapter);
    if (active_chapter == 0) {
        SceIoStat st;
        if (sceIoGetstat(game_path, &st) < 0) {
            snprintf(game_path, sizeof(game_path), "app0:assets/chapter0/data.win");
            snprintf(bundle_path, sizeof(bundle_path), "app0:assets/chapter0/");
            log_line("LAUNCHER=app0_fallback_loaded");
        }
    }
    // A mod can provide a dedicated launcher translation at
    // mods/Lang/<name>/chapter0/data.win. Keep the original launcher as fallback
    // when that file is absent; never reuse another chapter's data here.
    if (settings_language_uses_external_mod(&settings)) {
        SceIoStat mod_stat;
        char mod_game[160];
        const char* modName = settings.modNames[settings.modIndex];
        snprintf(mod_game, sizeof(mod_game), DATA_ROOT "mods/Lang/%s/chapter%d/data.win", modName, active_chapter);
        if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
            snprintf(game_path, sizeof(game_path), "%s", mod_game);
            log_line("MOD=alternate_game=enabled");
        } else {
            const char* alias = strcmp(modName, "Russkiy") == 0 ? "Russian" :
                                (strcmp(modName, "Russian") == 0 ? "Russkiy" : NULL);
            if (alias != NULL) {
                snprintf(mod_game, sizeof(mod_game), DATA_ROOT "mods/Lang/%s/chapter%d/data.win", alias, active_chapter);
                if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
                    snprintf(game_path, sizeof(game_path), "%s", mod_game);
                    log_line("MOD=alternate_game=enabled_alias");
                }
            }
            if (active_chapter == 0 && strncmp(game_path, DATA_ROOT "mods/", 10) != 0) {
                const char* legacyName = strcmp(modName, "Portuguese-BR") == 0 ? "PTBR" : modName;
                snprintf(mod_game, sizeof(mod_game), DATA_ROOT "mods/%s/chapter0/data.win", legacyName);
                if (sceIoGetstat(mod_game, &mod_stat) >= 0) {
                    snprintf(game_path, sizeof(game_path), "%s", mod_game);
                    log_line("MOD=alternate_game=legacy_chapter0_fallback");
                } else {
                    log_line("MOD=alternate_game=missing_chapter0_using_english");
                }
            }
        }
    }

    SceIoStat gameStat;
    memset(&gameStat, 0, sizeof(gameStat));
    if (sceIoGetstat(game_path, &gameStat) < 0) {
        char failure[160];
        snprintf(failure, sizeof(failure),
                 "Arquivo obrigatorio ausente: chapter%d/data.win", active_chapter);
        log_line("STARTUP_FATAL=missing_data_win");
        show_startup_error_and_exit(failure, game_path);
    }
    if (gameStat.st_size < 1024) {
        char failure[160];
        snprintf(failure, sizeof(failure),
                 "Arquivo invalido ou incompleto: chapter%d/data.win", active_chapter);
        log_line("STARTUP_FATAL=invalid_data_win_size");
        show_startup_error_and_exit(failure, game_path);
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
    DataWin *dw = NULL;
    if (startup_preload_started) {
        dw = VitaStartupPreload_join(&startup_preload, game_path);
        log_line(dw != NULL ? "STARTUP_PRELOAD=adopted" :
                              "STARTUP_PRELOAD=fallback");
        char preloadResult[320];
        snprintf(preloadResult, sizeof(preloadResult),
                 "STARTUP_PRELOAD=%s parse_us=%llu overlap_us=%llu join_us=%llu",
                 dw != NULL ? "adopted" : "fallback",
                 (unsigned long long)(startup_preload.stats.parseEndUs -
                                      startup_preload.stats.parseBeginUs),
                 (unsigned long long)(startup_preload.stats.joinBeginUs >
                                      startup_preload.stats.parseBeginUs ?
                                      startup_preload.stats.joinBeginUs -
                                      startup_preload.stats.parseBeginUs : 0),
                 (unsigned long long)(startup_preload.stats.joinEndUs -
                                      startup_preload.stats.joinBeginUs));
        log_line(preloadResult);
    }
    if (dw == NULL) {
        log_line("STARTUP_PRELOAD=fallback synchronous=1");
        dw = DataWin_parse(game_path, options);
    }
    if (dw == NULL) {
        log_line("STARTUP_FATAL=data_win_parse_failed");
        show_startup_error_and_exit(
            "Nao foi possivel interpretar data.win. O arquivo pode estar corrompido ou ser de uma versao incompativel.",
            game_path);
    }
    log_line("DATAWIN=parse_complete");
    first_load_animation_pump(false);
    // Keep the normal room payload. The alternate "original" room was once
    // swapped in solely to obtain draw_path, but that also replaced collision
    // and instance data. draw_path is implemented by the runner now.
    VMContext *vm = VM_create(dw);
    log_line("VM=create_complete");
    first_load_animation_pump(false);
    OverlayFileSystem *fs = OverlayFileSystem_create(bundle_path, SAVE_PATH);
    char mod_path[160];
    if (settings_language_uses_external_mod(&settings)) {
        const char* modName = settings.modNames[settings.modIndex];
        snprintf(mod_path, sizeof(mod_path), DATA_ROOT "mods/Lang/%s/chapter%d/", modName, active_chapter);
        SceIoStat mod_dir_stat;
        if (sceIoGetstat(mod_path, &mod_dir_stat) < 0) {
            const char* alias = strcmp(modName, "Russkiy") == 0 ? "Russian" :
                                (strcmp(modName, "Russian") == 0 ? "Russkiy" : NULL);
            if (alias != NULL) {
                snprintf(mod_path, sizeof(mod_path), DATA_ROOT "mods/Lang/%s/chapter%d/", alias, active_chapter);
            }
        }
        OverlayFileSystem_setModPath(fs, mod_path);
        log_line("MOD=overlay=enabled");
    } else {
        log_line("MOD=Original overlay=disabled");
    }
    Renderer *renderer = NULL;
    if (settings.modernGlEnabled) {
        // At this point fixed-pipeline state is still isolated and safe. The
        // framebuffer remains visible while Modern GL initializes lazily.
        if (active_chapter > 0) {
            loading_screen_init();
            draw_loading_screen(true, 0.0f,
                                g_vitaBc3OnlyEnabled ? LOADING_LABEL_LOADING_TEXTURES :
                                                       LOADING_LABEL_GENERATING_TEXTURES);
            loading_screen_shutdown();
            log_line("CHAPTER_LOADING=visible renderer=modern-gl phase=pre_renderer");
        }
        char modernReason[128] = {0};
        bool modernReady = GLRenderer_preflight(dw, modernReason, sizeof(modernReason));
        char modernLog[192];
        snprintf(modernLog, sizeof(modernLog),
                 "MODERN_GL_PREFLIGHT=%s reason=%s",
                 modernReady ? "pass" : "fail", modernReason);
        log_line(modernLog);
        renderer = modernReady ? GLRenderer_create() : NULL;
        if (renderer != NULL) {
            g_vitaModernGlActive = true;
            log_line("RENDERER_ACTIVE=modern-gl bootstrap=legacy_loading_overlay_disabled");
        } else {
            log_line("MODERN_GL=create_failed fallback=legacy-gl");
        }
    }
    if (renderer == NULL) {
        renderer = GLLegacyRenderer_create();
        g_vitaModernGlActive = false;
        if (g_vitaExperimentalGlActive) {
            char experimentalLog[96];
            snprintf(experimentalLog, sizeof(experimentalLog),
                     "RENDERER=experimental-gl backend=legacy shaders=%d",
                     g_vitaShadersEnabled ? 1 : 0);
            log_line(experimentalLog);
        } else {
            log_line("RENDERER_ACTIVE=legacy-gl");
        }
    }
    // Initialize the Chapter 3 hardware-video presentation objects before
    // room atlases consume the auxiliary VitaGL RAM pool. The actual decoder
    // and MP4 source remain closed until GML calls video_open(), so normal
    // chapter startup does not begin playback or allocate decoded frames.
    if (active_chapter == 3) {
        log_line("VIDEO_BOOTSTRAP=init_begin");
        int videoInitResult = VitaVideo_init();
        char videoBootstrapLog[96];
        snprintf(videoBootstrapLog, sizeof(videoBootstrapLog),
                 "VIDEO_BOOTSTRAP=init_complete result=0x%08X",
                 (unsigned)videoInitResult);
        log_line(videoBootstrapLog);
    }
    AudioSystem *audio = (AudioSystem *)AlAudioSystem_create();
    if (settings.audioDisabled) {
        AlAudioSystem_setDisabled((AlAudioSystem*)audio, true);
        log_line("AUDIO_BACKEND=disabled_before_runner_init");
    }
    VitaVideo_setGameAudioSystem(audio);
    log_line("SUBSYSTEMS=create_complete");
    first_load_animation_pump(false);

    log_line("RUNNER=create_begin");
    if (background_work == NULL)
        background_work =
            VitaBackgroundWork_create(VITA_BACKGROUND_DEFAULT_BUDGET);
    VitaRoomPrefetch_init(background_work, dw);
    Runner *runner = Runner_create(dw, vm, renderer, (FileSystem *)fs, audio);
    log_line("RUNNER=create_returned");
    first_load_audio_ready = true;
    first_load_animation_pump(false);
    const bool spanishModActive = settings_language_uses_external_mod(&settings) &&
                                  strcmp(settings.modNames[settings.modIndex], "Spanish") == 0;
    g_vitaSpanishModActive = spanishModActive && active_chapter > 0 ? 1 : 0;
    if (spanishModActive && active_chapter > 0) {
        // The Spanish package includes PC-oriented trophy and console-border
        // overlays. Vita supplies both facilities itself; allowing the mod
        // objects to run changes the application-surface layout and squeezes
        // the game into the upper/right PC coordinates.
        Runner_setObjectDisabled(runner, "obj_trophy_manager", true);
        Runner_setObjectDisabled(runner, "obj_border_manager", true);
        Runner_setObjectDisabled(runner, "obj_custom_config", true);
        Runner_setObjectDisabled(runner, "obj_spanish_config", true);
        Runner_setObjectDisabled(runner, "obj_border_controller", true);
        log_line("SPANISH_OVERLAYS=disabled trophy_manager,border_manager,border_controller,custom_config,spanish_config");
    }
    // The Steam launcher requires the Windows branch to emit game_change for
    // chapter selection. Reporting os_psvita leaves it in PLACE_CHAPTER_SELECT.
    runner->osType = OS_WINDOWS;
    log_line("PLATFORM=os_windows steam_launcher_compatible");
    VitaSettings_applyAudio(&settings, audio);
    loading_minigame_audio = audio;
    loading_minigame_sfx_gain = settings.audioDisabled ? 0.0f :
        ((float)settings.masterVolume / 10.0f) * ((float)settings.sfxVolume / 10.0f);
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
    // Some official chapter data keeps ordinary effects in audiogroup1.dat.
    // The PT-BR data happens to request that group early, while the original
    // scripts can play an effect before their deferred group-load event. Load
    // every declared auxiliary group lazily now; AUDO payloads remain on disk
    // until the individual SFX is actually used.
    if (audio->vtable->groupLoad != NULL && dw->agrp.count > 1) {
        for (uint32_t group = 1; group < dw->agrp.count; ++group) {
            if (audio->vtable->groupIsLoaded == NULL ||
                !audio->vtable->groupIsLoaded(audio, (int32_t)group))
                audio->vtable->groupLoad(audio, (int32_t)group);
        }
        log_line("AUDIO_GROUPS=auxiliary_groups_loaded_lazy");
    }
    if (active_chapter > 0) {
        // Chapter bootstrap can create and replace a music stream before the
        // first playable/menu room is presented. Keep that work inaudible;
        // otherwise the loading screen exposes a short start/stop/start pop.
        AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, 0.0f, 0.0f);
        log_line("STARTUP_AUDIO=muted_until_first_presented_frame");
        // Always replace the black chapter-start gap with the loading artwork.
        // The progress bar remains exclusive to actual cache preparation.
        if (!g_vitaModernGlActive) {
            loading_screen_init();
            if (settings.shadersEnabled) {
                loading_overlay_current = 0;
                loading_overlay_total = 1;
                draw_loading_screen(true, 0.0f, LOADING_LABEL_LOADING_SHADERS);
                uint64_t shaderStartedUs = sceKernelGetProcessTimeWide();
                uint32_t shaderCount = GLLegacyRenderer_prepareShaders(
                    (GLLegacyRenderer*)renderer, shader_loading_progress, NULL);
                uint64_t shaderElapsedUs = sceKernelGetProcessTimeWide() - shaderStartedUs;
                char shaderLine[160];
                snprintf(shaderLine, sizeof(shaderLine),
                         "SHADER_PRELOAD=boot enabled=1 ready=%u chapter=%d duration_us=%llu",
                         shaderCount, active_chapter, (unsigned long long)shaderElapsedUs);
                log_line(shaderLine);
            } else {
                log_line("SHADER_PRELOAD=boot enabled=0 skipped");
            }
            draw_loading_screen(false, 0.0f, 0);
            log_line("CHAPTER_LOADING=visible bar=hidden renderer=legacy-gl");
        } else {
            // The custom loader uses fixed-pipeline matrices/client arrays and
            // swaps VitaGL directly. Running it inside the programmable
            // renderer corrupted its state and hard-locked the GPU.
            log_line("CHAPTER_LOADING=skipped renderer=modern-gl reason=fixed_pipeline_overlay");
        }
    }
    Runner_initFirstRoom(runner);
    log_line("RUNNER=first_room_complete");
    if (settings.devMode) {
        char devStartRoom[128];
        if (config_get_dev_start_room(devStartRoom, sizeof(devStartRoom))) {
            int32_t targetRoom = -1;
            for (uint32_t i = 0; i < dw->room.count; ++i) {
                const char* roomName = dw->room.rooms[i].name;
                if (roomName != NULL && strcmp(roomName, devStartRoom) == 0) {
                    targetRoom = (int32_t)i;
                    break;
                }
            }
            if (targetRoom >= 0 && targetRoom != runner->currentRoomIndex) {
                char line[192];
                snprintf(line, sizeof(line), "DEV_START_ROOM=request name=%s index=%d", devStartRoom, targetRoom);
                log_line(line);
                runner->pendingRoom = targetRoom;
                Runner_handlePendingRoomChange(runner);
                snprintf(line, sizeof(line), "DEV_START_ROOM=applied current=%d name=%s",
                         runner->currentRoomIndex,
                         runner->currentRoom != NULL && runner->currentRoom->name != NULL ? runner->currentRoom->name : "<null>");
                log_line(line);
            } else {
                char line[192];
                snprintf(line, sizeof(line), "DEV_START_ROOM=not_found name=%s", devStartRoom);
                log_line(line);
            }
        }
    }
    bool startup_audio_paused = false;
    if (active_chapter > 0 && audio->vtable->pauseAll != NULL) {
        // Category gain alone muted the chapter bootstrap but allowed voices
        // to keep advancing asynchronously. Chapter 5's spoken "Deltarune"
        // therefore resumed at "arune". Freeze active sources until the first
        // rendered frame, then resume them at the beginning of the fade.
        audio->vtable->pauseAll(audio);
        AlAudioSystem_setTransitionHold((AlAudioSystem*)audio, true);
        startup_audio_paused = true;
        log_line("STARTUP_AUDIO=paused_until_first_presented_frame");
    }
    if (!g_vitaModernGlActive && active_chapter == 1 && runner->currentRoom != NULL && runner->currentRoom->name != NULL &&
        strstr(runner->currentRoom->name, "PLACE_CONTACT") != NULL) {
        preload_place_contact_atlases(dw, (GLLegacyRenderer*)renderer);
    }
    if (settings.devMode && settings.debugDevEnabled) {
        dev_log_start(runner->currentRoom != NULL ? runner->currentRoom->name : NULL);
        Profiler_setEnabled(&runner->vmContext->profiler, true);
        char shaderCatalog[128];
        snprintf(shaderCatalog, sizeof(shaderCatalog), "SHADER_CATALOG count=%u renderer=%s",
                 dw->shdr.count, g_vitaModernGlActive ? "modern-gl" : "legacy-gl");
        dev_log_write(shaderCatalog);
        for (uint32_t i = 0; i < dw->shdr.count; ++i) {
            char shaderLine[256];
            snprintf(shaderLine, sizeof(shaderLine), "SHADER_DEF index=%u name=%s version=%d",
                     i, dw->shdr.shaders[i].name != NULL ? dw->shdr.shaders[i].name : "<unnamed>",
                     dw->shdr.shaders[i].version);
            dev_log_write(shaderLine);
        }
    }
    if (active_chapter > 0) {
        log_line("AUDIO_PRELOAD=begin scope=active_chapter");
        // Chapter 2's Mansion battle logs show an 18 ms first-use snd_play
        // spike. Its current Steam SOND layout has been validated by the
        // selective priority list, so decode those small battle/UI effects
        // while the loading screen is still visible. Other chapter variants
        // remain on the conservative deferred path.
        bool preloadSfxBuffers = false;
        uint32_t audioCached = AlAudioSystem_preloadChapterSfx((AlAudioSystem*)audio,
                                                               preloadSfxBuffers);
        audioCached += AlAudioSystem_preloadBattleIntroSfx((AlAudioSystem*)audio);
        char audioCacheLine[96];
        snprintf(audioCacheLine, sizeof(audioCacheLine),
                 "AUDIO_PRELOAD=complete cached=%u capacity=%u decode=%s", audioCached,
                 (unsigned)MAX_SFX_BUFFER_CACHE, preloadSfxBuffers ? "enabled" : "deferred");
        log_line(audioCacheLine);
        // The VitaGL fixed-pipeline bridge and the runner must exist before the
        // loading screen draws. Running this directly after DataWin_parse made
        // the first progress frame touch an uninitialized GL pipeline.
        log_line("TEXTURE_PRELOAD=begin stage=runner_ready");
        
        // Select the artwork from the actual cache state. Previously every
        // mixed/RGBA4444 profile displayed Generating even when complete.vtc
        // was valid and prepareTextureCache returned immediately.
        bool textureCacheComplete = !g_vitaModernGlActive &&
            GLLegacyRenderer_textureCacheIsComplete(dw);
        bool textureGenerationRequired = !g_vitaModernGlActive &&
            !textureCacheComplete;
        if (textureGenerationRequired)
            loading_minigame_prepare_game_sfx();
        uint64_t textureLoadingStartedUs = sceKernelGetProcessTimeWide();
        loading_texture_progress_last_draw_us = 0;
        if (!g_vitaModernGlActive) {
            loading_overlay_current = 0;
            loading_overlay_total = dw->txtr.count;
            draw_loading_screen(true, 0.0f,
                                textureGenerationRequired ? LOADING_LABEL_GENERATING_TEXTURES :
                                                            LOADING_LABEL_LOADING_TEXTURES);
        }
        
        VitaTexturePrepareProgress textureProgress = g_vitaModernGlActive ? NULL :
            (textureGenerationRequired ? texture_loading_progress : NULL);
        uint32_t cached = 0;
        if (g_vitaModernGlActive) {
            // Modern GL keeps the chapter lazy, but Chapter 3's opening has a
            // measured 18-page burst whose on-demand uploads freeze individual
            // animation frames for 150-255 ms. Warm only that proven set.
            log_line("TEXTURE_PRELOAD=skipped renderer=modern-gl mode=lazy_on_demand");
            if (active_chapter == 3)
                preload_chapter3_intro_modern(dw, (GLRenderer*)renderer);
        } else {
            g_vitaTexturePrepareAbortForMinigame = 0;
            cached = GLLegacyRenderer_prepareTextureCache(dw, textureProgress, NULL);
            // Normal generation remains entirely synchronous. Only an explicit
            // X press requests the switch, and the renderer returns here at the
            // next completed texture page so no half-written cache file moves
            // across threads. The worker rescans from page 0, cheaply skipping
            // files already completed by this phase.
            if (textureGenerationRequired && loading_minigame_active &&
                g_vitaTexturePrepareAbortForMinigame && cached < dw->txtr.count) {
                g_vitaTexturePrepareAbortForMinigame = 0;
                if (loading_texture_worker_start(dw)) {
                    cached = loading_texture_worker_present_until_done();
                } else {
                    log_line("LOADING_MINIGAME=async_generation start_failed=1 fallback=synchronous");
                    cached = GLLegacyRenderer_prepareTextureCache(dw, texture_loading_progress, NULL);
                }
            }
            uint32_t battleCorePages =
                GLLegacyRenderer_preloadChapterBattleCore((GLLegacyRenderer*)renderer);
            if (battleCorePages > 0) {
                char battleCoreLine[96];
                snprintf(battleCoreLine, sizeof(battleCoreLine),
                         "BATTLE_PRELOAD=complete chapter=%d pages=%u",
                         active_chapter, battleCorePages);
                log_line(battleCoreLine);
            }
        }
        char cache_line[96];
        snprintf(cache_line, sizeof(cache_line), "TEXTURE_PRELOAD=complete prepared=%u total=%u",
                 cached, dw->txtr.count);
        log_line(cache_line);
        if (!g_vitaModernGlActive && active_chapter == 3) {
            preload_chapter3_slide_atlases(dw, (GLLegacyRenderer*)renderer);
            preload_chapter3_finale_atlases(dw, (GLLegacyRenderer*)renderer);
        }
        // Existing cache validation is intentionally O(1) (completion marker),
        // so there is no per-page callback to drive the Loading bar. Use the
        // already-required one-second presentation window to advance X / total
        // smoothly without reintroducing the old O(page count) filesystem scan.
        if (!g_vitaModernGlActive) {
            const uint64_t minimumLoadingUs = 1000000ULL;
            do {
                if (textureGenerationRequired) {
                    draw_loading_screen(true, 1.0f, LOADING_LABEL_GENERATING_TEXTURES);
                } else {
                    uint64_t elapsedUs = sceKernelGetProcessTimeWide() - textureLoadingStartedUs;
                    float existingRatio = (float)elapsedUs / (float)minimumLoadingUs;
                    if (existingRatio > 1.0f) existingRatio = 1.0f;
                    loading_overlay_total = dw->txtr.count;
                    loading_overlay_current = (uint32_t)((float)dw->txtr.count * existingRatio + 0.5f);
                    if (loading_overlay_current > loading_overlay_total)
                        loading_overlay_current = loading_overlay_total;
                    draw_loading_screen(true, existingRatio, LOADING_LABEL_LOADING_TEXTURES);
                }
                sceKernelDelayThread(16667);
            } while (sceKernelGetProcessTimeWide() - textureLoadingStartedUs < minimumLoadingUs);
            loading_overlay_current = dw->txtr.count;
            loading_overlay_total = dw->txtr.count;
            if (!textureGenerationRequired)
                draw_loading_screen(true, 1.0f, LOADING_LABEL_LOADING_TEXTURES);
            // Finish the texture stage with the car driving off the right edge.
            loading_play_car_exit(textureGenerationRequired ?
                                  LOADING_LABEL_GENERATING_TEXTURES :
                                  LOADING_LABEL_LOADING_TEXTURES);
            loading_screen_shutdown();
        }
        if (textureGenerationRequired)
            AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, 0.0f, 0.0f);
    }
    char display_line[160];
    snprintf(display_line, sizeof(display_line), "DISPLAY=gen8_%dx%d applied_%dx%d host_960x544 mode=native_centered",
             (int)dw->gen8.defaultWindowWidth, (int)dw->gen8.defaultWindowHeight,
             (int)dw->gen8.defaultWindowWidth, (int)dw->gen8.defaultWindowHeight);
    log_line(display_line);

    bool previous[sizeof(KEY_MAP) / sizeof(KEY_MAP[0])] = {0};
    bool gp_previous[20] = {0};
    uint32_t frame = 0;
    uint64_t last_time = sceKernelGetProcessTimeWide();
    VitaFramePacer frame_pacer;
    VitaFramePacer_init(&frame_pacer, 0, last_time);
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
    float settings_touch_last_y = 0.0f;
    int32_t dev_room_target = runner->currentRoomIndex;
    bool dev_force_move = false;
    uint32_t failure_black_frames = 0;
    bool failure_exit_held = false;

    while (!exit_requested && !runner->shouldExit) {
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
        runner->gamepads->connectedCount = 1;
        runner->gamepads->slots[0].connected = true;
        strcpy(runner->gamepads->slots[0].description, "Sony DualShock 4");
        RunnerMouse_beginFrame(runner->mouse);
        SceCtrlData pad = {0};
        sceCtrlPeekBufferPositive(0, &pad, 1);

        // The Steam scripts only expose the shortened game-over exit path on
        // PS4/PS5/Switch.  The Vita identifies as Windows to retain normal
        // filesystem/save behavior, so "Give up" otherwise waits on a black
        // screen until the long AUDIO_DARKNESS timeout expires.  Once the
        // failure controller explicitly reaches Give up (EVENT=28), accept a face button
        // like the console builds and keep a bounded fallback for audio streams
        // that never report completion. Primitive counts alone also match the
        // white Continue fade and must never trigger a chapter reload there.
        const char* input_room = runner->currentRoom != NULL ? runner->currentRoom->name : NULL;
        uint32_t submittedPrimitives = g_vitaModernGlActive ?
            ((GLRenderer*)renderer)->frameSubmittedPrimitives :
            ((GLLegacyRenderer*)renderer)->frameSubmittedPrimitives;
        bool failure_black = active_chapter > 0 && input_room != NULL &&
                             strcmp(input_room, "PLACE_FAILURE") == 0 &&
                             Runner_isFailureGivingUp(runner) &&
                             submittedPrimitives <= 2;
        if (failure_black) {
            if (failure_black_frames < UINT32_MAX) failure_black_frames++;
            bool face_down = (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE |
                                             SCE_CTRL_TRIANGLE | SCE_CTRL_SQUARE)) != 0;
            if (failure_black_frames >= 90 && face_down && !failure_exit_held) {
                // Restart the active chapter at its save/menu flow. Returning
                // through runner->shouldExit always launched Chapter 0 and
                // discarded DELTARUNE's intended Continue behavior.
                log_line("GAME_CHANGE=failure_face_button_restart_active_chapter");
                if (restart_into_chapter(active_chapter)) return 0;
            } else if (failure_black_frames >= 2100) {
                log_line("GAME_CHANGE=failure_timeout_restart_active_chapter");
                if (restart_into_chapter(active_chapter)) return 0;
            }
            failure_exit_held = face_down;
        } else {
            failure_black_frames = 0;
            failure_exit_held = false;
        }

        // The settings UI remains touch-capable even when gameplay touch is
        // disabled. Vita touch coordinates are twice the 960x544 UI space.
        if (settings.open) {
            SceTouchData menu_touch = {0};
            bool touching = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &menu_touch, 1) > 0 &&
                            menu_touch.reportNum > 0;
            if (touching && settings.trophiesMenuOpen) {
                float ty = (float)menu_touch.report[0].y * 0.5f;
                if (!settings_touch_held) {
                    settings_touch_last_y = ty;
                    if (ty >= 190.0f && ty <= 420.0f) {
                        int row = (int)((ty - 190.0f) / 72.0f);
                        int item = settings.trophiesScroll + row;
                        if (row >= 0 && row < 3 && item < 30) settings.trophiesSelected = item;
                    }
                } else {
                    float delta = ty - settings_touch_last_y;
                    if (delta <= -34.0f) {
                        settings.trophiesSelected = (settings.trophiesSelected + 1) % 30;
                        settings_touch_last_y = ty;
                    } else if (delta >= 34.0f) {
                        settings.trophiesSelected = (settings.trophiesSelected + 29) % 30;
                        settings_touch_last_y = ty;
                    }
                }
            } else if (touching && settings.controlEditMode) {
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
            } else if (touching) {
                float tx = (float)menu_touch.report[0].x * 0.5f;
                float ty = (float)menu_touch.report[0].y * 0.5f;
                if (!settings_touch_held && ty >= 118.0f && ty <= 166.0f && tx >= 120.0f && tx <= 840.0f) {
                    int categoryCount = settings.devMode ? 5 : 4;
                    float categorySpacing = categoryCount == 5 ? 140.0f : 180.0f;
                    float categoryStart = 480.0f - categorySpacing * (categoryCount - 1) * 0.5f;
                    int category = (int)(((tx - categoryStart) / categorySpacing) + 0.5f);
                    if (category < 0) category = 0;
                    if (category >= categoryCount) category = categoryCount - 1;
                    settings.category = category;
                    settings.selected = 0;
                } else if (tx >= 180.0f && tx <= 780.0f && ty >= 168.0f && ty <= 420.0f) {
                    int count = VitaSettings_itemCount(settings.category);
                    float firstY = count == 5 ? 184.0f : (count == 4 ? 200.0f : (count == 3 ? 206.0f : 214.0f));
                    float spacing = count == 5 ? 38.0f : (count == 4 ? 42.0f : (count == 3 ? 50.0f : 64.0f));
                    int item = (int)((ty - firstY + spacing * 0.5f) / spacing);
                    if (item >= 0 && item < count) {
                        settings.selected = item;
                        int logicalCategory = settings.category + (!settings.devMode ? 1 : 0);
                        int dataIndex = logicalCategory == 2 ?
                            (item >= 3 && settings.consoleBorderMode != 1 ? item + 1 : item) : item;
                        bool slider = (logicalCategory == 3 && dataIndex < 3) ||
                                      (logicalCategory == 2 && dataIndex == 4);
                        if (slider) {
                            VitaSettings_setSliderFromTouch(&settings, audio,
                                                            logicalCategory, dataIndex, tx);
                        } else if (!settings_touch_held) {
                            // Route buttons through the regular input path so
                            // persistence and SFX remain identical to X.
                            pad.buttons |= SCE_CTRL_CROSS;
                        }
                    }
                }
            }
            settings_touch_held = touching;
        } else {
            settings_touch_held = false;
        }

        if (!settings.open && settings.devMode && settings.devRoomNavEnabled) {
            bool confirmRoom = (pad.buttons & SCE_CTRL_LTRIGGER) &&
                               (pad.buttons & SCE_CTRL_CROSS);
            bool navInput = (pad.buttons & (SCE_CTRL_UP | SCE_CTRL_DOWN |
                                            SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) != 0 ||
                            confirmRoom;
            if (navInput && !dev_room_nav_held) {
                if (pad.buttons & SCE_CTRL_UP) dev_room_target--;
                else if (pad.buttons & SCE_CTRL_DOWN) dev_room_target++;
                else if (pad.buttons & SCE_CTRL_LEFT) dev_room_target -= 10;
                else if (pad.buttons & SCE_CTRL_RIGHT) dev_room_target += 10;
                while (dev_room_target < 0) dev_room_target += (int32_t)dw->room.count;
                while ((uint32_t)dev_room_target >= dw->room.count)
                    dev_room_target -= (int32_t)dw->room.count;
                if (confirmRoom) {
                    const char* targetName = dw->room.rooms[dev_room_target].name;
                    char jump[256];
                    snprintf(jump, sizeof(jump), "DEV_ROOM_JUMP from=%d to=%d name=%s",
                             runner->currentRoomIndex, dev_room_target,
                             targetName != NULL ? targetName : "<null>");
                    log_line(jump);
                    dev_log_write(jump);
                    // Room Navigator can bypass the normal path between two
                    // unrelated, GPU-heavy scenes. Fence the outgoing command
                    // list and collect already-retired allocations before the
                    // runner starts destroying surfaces and replacing atlas
                    // pages. The Flowery -> cliff capture otherwise submitted
                    // the destination while work from 36 old surfaces was
                    // still pending, then GPU-crashed on the following jump.
                    renderer->vtable->flush(renderer);
                    glFinish();
                    if (!g_vitaModernGlActive)
                        GLLegacyRenderer_collectRetiredTexturesForRoomChange(
                            (GLLegacyRenderer*)renderer);
                    vglForceGarbageCollection();
                    log_line("DEV_ROOM_JUMP_GPU_BARRIER=complete");
                    runner->pendingRoom = dev_room_target;
                    dev_force_move = true;
                }
            }
            dev_room_nav_held = navInput;
            // Debug navigation must not leak into the game controls.
            pad.buttons &= ~(SCE_CTRL_UP | SCE_CTRL_DOWN |
                             SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
            if (confirmRoom)
                pad.buttons &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_CROSS);
        } else {
            dev_room_nav_held = false;
            if (!settings.devMode || !settings.devRoomNavEnabled) {
                dev_room_target = runner->currentRoomIndex;
                dev_force_move = false;
            }
        }

        if (!settings.open && settings.debugCollisionMasks) {
            SceTouchData game_touch = {0};
            if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &game_touch, 1) > 0 && game_touch.reportNum > 0) {
                float sx = (float)game_touch.report[0].x * (640.0f / 1920.0f);
                float sy = (float)game_touch.report[0].y * (480.0f / 1080.0f);
                float wx = sx + (float)runner->defaultCameras[0].viewX;
                float wy = sy + (float)runner->defaultCameras[0].viewY;
                DebugOverlay_selectInstanceAtPoint(runner, wx, wy);
            }
            static bool l_trigger_was_held = false;
            bool l_pressed = (pad.buttons & SCE_CTRL_LTRIGGER) != 0;
            if (l_pressed) {
                static bool obj_inspect_held = false;
                bool navInput = (pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_SQUARE)) != 0;
                if (navInput && !obj_inspect_held) {
                    if (pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_UP)) DebugOverlay_selectNextInstance(runner, -1);
                    else if (pad.buttons & (SCE_CTRL_RIGHT | SCE_CTRL_DOWN | SCE_CTRL_SQUARE)) DebugOverlay_selectNextInstance(runner, 1);
                }
                obj_inspect_held = navInput;
                // Prevent L + D-Pad object inspection inputs from moving the player character
                pad.buttons &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_SQUARE | SCE_CTRL_CROSS);
            } else if (l_trigger_was_held) {
                // Save object log on releasing L-TRIGGER
                DebugOverlay_saveSelectedObjectLog(runner, active_chapter);
            }
            l_trigger_was_held = l_pressed;
        }
        
        bool r_trigger = (pad.buttons & SCE_CTRL_RTRIGGER);
        bool border_cycled = false;
        if (!settings.open && r_trigger && settings.devMode) {
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
        
        // Snapshot the current room before Game Settings can rotate the probe log.
        if (runner->currentRoom != NULL && runner->currentRoom->name != NULL)
            snprintf(g_vitaProbeCurrentRoom, sizeof(g_vitaProbeCurrentRoom), "%s", runner->currentRoom->name);
        else
            snprintf(g_vitaProbeCurrentRoom, sizeof(g_vitaProbeCurrentRoom), "unknown_room");
        bool settingsWasOpen = settings.open;
        bool restart_for_settings = VitaSettings_handleInput(&settings, &pad, audio);
        if (!settingsWasOpen && settings.open)
            log_line(g_vitaModernGlActive ?
                     "GAME_SETTINGS=open renderer=modern-gl icons=suppressed" :
                     "GAME_SETTINGS=open renderer=legacy-gl icons=enabled");
        else if (settingsWasOpen && !settings.open)
            log_line("GAME_SETTINGS=closed");
        // Resolution and complete texture profiles remain live. Replace only
        // the resident atlas set, preserving the Runner, current room,
        // instances, audio and unsaved gameplay state. An incomplete texture
        // profile restarts into the pre-game generation stage below.
        extern int g_vitaTextureCacheInvalidate;
        if (g_vitaTextureCacheInvalidate && !restart_for_settings) {
            // Texture encoding is a chapter-bootstrap operation. Generating
            // R444/BC3 while a room is live competes with rendering/audio for
            // the small VitaGL RAM pool and leaves partially replaced atlas
            // sets visible when the encoder needs several frames. If the new
            // profile is incomplete, restart the selected chapter so the
            // normal GENERATING TEXTURES stage can finish it before gameplay.
            bool liveGenerationRequired = !g_vitaModernGlActive &&
                !GLLegacyRenderer_textureCacheIsComplete(dw);
            if (liveGenerationRequired) {
                log_line("SETTINGS=texture_profile_restart reason=cache_generation_required stage=chapter_bootstrap");
                g_vitaTextureCacheInvalidate = 0;
                restart_for_settings = true;
            }
        }
        if (g_vitaTextureCacheInvalidate && !restart_for_settings) {
            loading_screen_init();
            loading_overlay_current = 0;
            loading_overlay_total = 1;
            draw_loading_screen(true, 0.0f, LOADING_LABEL_LOADING_TEXTURES);
            if (!g_vitaModernGlActive) {
                GLLegacyRenderer* liveGl = (GLLegacyRenderer*)renderer;
                GLLegacyRenderer_trimTextureCacheForRoomChange(liveGl, 0, true);
                GLLegacyRenderer_collectRetiredTexturesForRoomChange(liveGl);
                if (runner->currentRoom != NULL) {
                    GLLegacyRenderer_prepareRoomTextureSet(liveGl, runner->currentRoom);
                    uint32_t reloaded = GLLegacyRenderer_preloadRoomTextureSet(liveGl);
                    char reloadLine[128];
                    snprintf(reloadLine, sizeof(reloadLine),
                             "SETTINGS=texture_profile_live_apply room=%s preloaded=%u",
                             runner->currentRoom->name != NULL ? runner->currentRoom->name : "<null>",
                             reloaded);
                    log_line(reloadLine);
                }
            } else {
                GLRenderer_trimTextureCacheForRoomChange((GLRenderer*)renderer, 0);
                log_line("SETTINGS=texture_profile_live_apply renderer=modern-gl");
            }
            g_vitaTextureCacheInvalidate = 0;
            loading_screen_shutdown();
        }
        if (settings.shaderSceneReloadRequested && !restart_for_settings) {
            settings.shaderSceneReloadRequested = false;
            // This runtime overlay uses fixed-pipeline CPU vertices. Keep the
            // game's shader, sampler unit and VBO out of its PNG draw calls.
            GLint shaderLoadingProgram = 0, shaderLoadingUnit = GL_TEXTURE0;
            GLint shaderLoadingClient = GL_TEXTURE0, shaderLoadingBuffer = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &shaderLoadingProgram);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &shaderLoadingUnit);
            glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &shaderLoadingClient);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &shaderLoadingBuffer);
            glUseProgram(0);
            glActiveTexture(GL_TEXTURE0);
            glClientActiveTexture(GL_TEXTURE0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            loading_screen_init();
            loading_overlay_current = 0;
            loading_overlay_total = 1;
            draw_loading_screen(true, 0.0f, LOADING_LABEL_LOADING_SHADERS);
            uint32_t shaderCount = 0;
            uint64_t shaderStartedUs = sceKernelGetProcessTimeWide();
            if (!g_vitaModernGlActive && settings.shadersEnabled) {
                shaderCount = GLLegacyRenderer_prepareShaders(
                    (GLLegacyRenderer*)renderer, shader_loading_progress, NULL);
            }
            uint64_t shaderElapsedUs = sceKernelGetProcessTimeWide() - shaderStartedUs;
            renderer->vtable->gpuResetShader(renderer);
            int32_t reloadRoom = runner->currentRoomIndex;
            if (reloadRoom >= 0) runner->pendingRoom = reloadRoom;
            char shaderLine[160];
            snprintf(shaderLine, sizeof(shaderLine),
                     "SHADER_MODE=apply enabled=%d ready=%u reload_room=%d keep_settings=1 duration_us=%llu",
                     settings.shadersEnabled ? 1 : 0, shaderCount, reloadRoom,
                     (unsigned long long)shaderElapsedUs);
            log_line(shaderLine);
            glUseProgram(0);
            draw_loading_screen(true, 1.0f, LOADING_LABEL_LOADING_SHADERS);
            loading_screen_shutdown();
            glBindBuffer(GL_ARRAY_BUFFER, (GLuint)shaderLoadingBuffer);
            glClientActiveTexture((GLenum)shaderLoadingClient);
            glActiveTexture((GLenum)shaderLoadingUnit);
            glUseProgram((GLuint)shaderLoadingProgram);
        }
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
            if (settings_target == 0)
                VitaBorders_prepareLauncherReturn();
            if (restart_into_chapter(settings_target)) {
                // Release the OpenAL device/SceAudioOut port before the process is
                // replaced. Without this, the next process's alcOpenDevice() can
                // crash inside alcResetDeviceSOFT on the still-open hardware port.
                audio->vtable->destroy(audio);
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
                int interactRadius = stickRadius * 3; // Much more tolerant
                int stickDx = tx - stickCenterX;
                int stickDy = ty - stickCenterY;
                if (stickDx * stickDx + stickDy * stickDy <= interactRadius * interactRadius) {
                    int tdx = stickDx;
                    int tdy = stickDy;
                    if (tdx < -stickRadius) tdx = -stickRadius;
                    if (tdx > stickRadius) tdx = stickRadius;
                    if (tdy < -stickRadius) tdy = -stickRadius;
                    if (tdy > stickRadius) tdy = stickRadius;
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
        
        bool gp_up = controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_UP) || dy < -48 || touch_up);
        bool gp_down = controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_DOWN) || dy > 48 || touch_down);
        bool gp_left = controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_LEFT) || dx < -48 || touch_left);
        bool gp_right = controls_enabled && !dev_force_move && ((pad.buttons & SCE_CTRL_RIGHT) || dx > 48 || touch_right);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_PADU, gp_up, &gp_previous[0]);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_PADD, gp_down, &gp_previous[1]);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_PADL, gp_left, &gp_previous[2]);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_PADR, gp_right, &gp_previous[3]);
        
        bool gp_cross = controls_enabled && ((pad.buttons & SCE_CTRL_CROSS) || touch_confirm);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_FACE1, gp_cross, &gp_previous[4]);
        
        bool gp_circle = controls_enabled && ((pad.buttons & (SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE)) || touch_cancel);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_FACE2, gp_circle, &gp_previous[5]);
        
        bool gp_triangle = controls_enabled && ((pad.buttons & SCE_CTRL_TRIANGLE) || touch_menu);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_FACE4, gp_triangle, &gp_previous[6]);
        
        bool gp_l = controls_enabled && (pad.buttons & SCE_CTRL_LTRIGGER);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_SHOULDERL, gp_l, &gp_previous[7]);
        
        bool gp_r = controls_enabled && (pad.buttons & SCE_CTRL_RTRIGGER);
        RunnerGamepad_setButton(runner->gamepads, 0, GP_SHOULDERR, gp_r, &gp_previous[8]);

        RunnerGamepad_setAxis(runner->gamepads, 0, GP_AXIS_LH, controls_enabled ? visual_x : 0.0f);
        RunnerGamepad_setAxis(runner->gamepads, 0, GP_AXIS_LV, controls_enabled ? visual_y : 0.0f);

        set_key(runner->keyboard, VK_UP, gp_up, &previous[0]);
        set_key(runner->keyboard, VK_DOWN, gp_down, &previous[1]);
        set_key(runner->keyboard, VK_LEFT, gp_left, &previous[2]);
        set_key(runner->keyboard, VK_RIGHT, gp_right, &previous[3]);
        set_key(runner->keyboard, 'Z', gp_cross, &previous[4]);
        set_key(runner->keyboard, 'X', gp_circle, &previous[5]);
        
        if (settings.shortcutSkipDialogs) {
            set_key(runner->keyboard, 'C', gp_triangle, &previous[7]);
        } else {
            set_key_pulse(runner->keyboard, 'C', gp_triangle, &previous[7]);
        }
        
        set_key(runner->keyboard, VK_PAGEDOWN, gp_l, &previous[8]);
        set_key(runner->keyboard, VK_PAGEUP, gp_r, &previous[9]);

        uint64_t frame_begin = sceKernelGetProcessTimeWide();
        uint64_t now = frame_begin;
        runner->deltaTime = (double)(now - last_time);
        last_time = now;
        uint64_t step_begin = sceKernelGetProcessTimeWide();
        bool profiler_sample_frame = dev_log_fd >= 0 &&
            runner->vmContext->profiler != NULL && (frame % 120U) == 0U;
        if (runner->vmContext->profiler != NULL)
            Profiler_setSampling(runner->vmContext->profiler, profiler_sample_frame);
        if (profiler_sample_frame)
            Profiler_reset(runner->vmContext->profiler);
        // Screen calibration is a visual pause: keep rendering the current
        // frame while preventing dialogue, movement, timers and battles from
        // advancing behind the adjustment overlay.
        if (!settings.open && !settings.adjustMode) {
            if (frame == 0) log_line("FRAME0=step_begin");
            Runner_step(runner);
            // DELTARUNE only enables its event-manager trophies on PS4/PS5.
            // Keep os_windows for launcher compatibility and bridge the same
            // event list to Vita's persistent trophy state instead.
            // trophies_queue can be transient and may be consumed by the game's
            // event manager in fewer than 30 frames. Observe it every frame so
            // no legitimate in-game unlock can disappear between polls. Keep
            // the enable write at the old low cadence; sync only touches disk
            // when Runner_getGameTrophies reports something not already local.
            if ((frame % 30U) == 0U) Runner_enableGameTrophies(runner);
            {
                bool gameTrophies[30];
                Runner_getGameTrophies(runner, gameTrophies);
                bool hasNewTrophy = false;
                for (int trophy = 0; trophy < 30; ++trophy) {
                    if (gameTrophies[trophy] && !settings.trophiesUnlocked[trophy]) {
                        hasNewTrophy = true;
                        break;
                    }
                }
                if (hasNewTrophy) {
                    uint32_t unlockedTrophies = VitaSettings_syncGameTrophies(&settings, gameTrophies);
                    for (int trophy = 0; trophy < 30; ++trophy)
                        if ((unlockedTrophies & (1U << trophy)) != 0) VitaTrophies_unlock(trophy);
                }
            }
            int video_event = VitaVideo_consumeEvent();
            if (video_event == VITA_VIDEO_EVENT_START) {
                // Preserve GameMaker's video contract.  DELTARUNE enables
                // obj_ch3_couch_video only after this async callback, then
                // calls video_draw() and draws the returned surface itself.
                // The Vita backend exposes its decoded frame through the
                // reserved VITA_VIDEO_SURFACE_ID handled by draw_surface().
                Runner_dispatchVideoAsync(runner, "video_start");
                log_line("VIDEO_ASYNC=video_start_dispatched");
            } else if (video_event == VITA_VIDEO_EVENT_END) {
                log_line("VIDEO_ASYNC=video_end");
                Runner_dispatchVideoAsync(runner, "video_end");
            }
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
                    audio->vtable->destroy(audio);
                    sceKernelDelayThread(500000);
                    sceKernelExitProcess(0);
                }
                log_line("GAME_CHANGE=loadexec_failed");
            } else if (active_chapter > 0) {
                next_chapter = 0;
                log_line("GAME_CHANGE=chapter_select_fallback_launcher");
                if (restart_into_chapter(0)) {
                    audio->vtable->destroy(audio);
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
        loading_minigame_collect_tail_sfx();
        uint64_t audio_us = sceKernelGetProcessTimeWide() - audio_begin;

        uint64_t render_begin = sceKernelGetProcessTimeWide();
        uint64_t render_phase_begin = render_begin;
        uint64_t render_pre_us = 0, render_begin_frame_us = 0;
        uint64_t render_views_us = 0, render_composite_us = 0;
        uint64_t render_gui_us = 0, render_overlay_us = 0, render_swap_us = 0;
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
        render_pre_us = sceKernelGetProcessTimeWide() - render_phase_begin;
        render_phase_begin = sceKernelGetProcessTimeWide();
        if (frame == 0) log_line("FRAME0=draw_pre_complete");
        Runner_beginFrame(runner, game_w, game_h, 960, 544, 960, 544);
        render_begin_frame_us = sceKernelGetProcessTimeWide() - render_phase_begin;
        render_phase_begin = sceKernelGetProcessTimeWide();
        if (frame == 0) log_line("FRAME0=begin_frame_complete");
        Runner_drawViews(runner, game_w, game_h, settings.debugCollisionMasks);
        render_views_us = sceKernelGetProcessTimeWide() - render_phase_begin;
        render_phase_begin = sceKernelGetProcessTimeWide();
        if (settings.debugCollisionMasks && (frame % 30U) == 0U)
            log_puzzle_diagnostics(runner);
        if (dev_log_fd >= 0 && (frame % 30U) == 0U) {
            char shaderActivity[512];
            if (VMBuiltins_takeShaderDiagnostic(shaderActivity, sizeof(shaderActivity)))
                dev_log_write(shaderActivity);
        }
        if (frame == 0) log_line("FRAME0=draw_views_complete");
        vitaBorderSampleGameState(runner);
        VitaBorders_updateRoom(runner->currentRoom != NULL ? runner->currentRoom->name : NULL);
        // Keep the active gameplay border behind Game Settings. The overlay
        // does not replace or resize the game framebuffer, so suppressing the
        // border here only produced an unnecessary black flash. Calibration
        // still uses black margins while the user adjusts the viewport.
        // Keep the current border visible until the room change is actually
        // processed. Some Chapter 3 cutscenes queue pendingRoom while dialogue
        // is still visible; suppressing on the request alone made the side art
        // disappear early. VitaBorders_prepareRoomChange fences with glFinish.
        VitaBorders_setSuppressed(settings.adjustMode);
        // Mixed Chapter 5 pages contain characters, fonts and scenery. They
        // cannot be downscaled or evicted without corruption or an immediate
        // reload. Prefer black side areas when this mandatory set is already
        // too close to the physical graphics-memory limit.
        uint64_t borderPressureMiB = g_vitaActiveChapter == 5 ? 44ULL : 105ULL;
        size_t borderVramFree = vglMemFree(VGL_MEM_VRAM);
        size_t borderMappedFree = borderVramFree +
                                  vglMemFree(VGL_MEM_RAM) +
                                  vglMemFree(VGL_MEM_SLOW);
        uint64_t borderResidentBytes = g_vitaModernGlActive ?
            ((GLRenderer*)renderer)->residentTextureBytes :
            ((GLLegacyRenderer*)renderer)->residentTextureBytes;
        VitaBorders_setMemoryPressure(
            borderResidentBytes > borderPressureMiB * 1024ULL * 1024ULL ||
            borderMappedFree < 2ULL * 1024ULL * 1024ULL);
        // Border textures and room surfaces are outside residentTextureBytes.
        // Retire atlases left by PLACE_MENU before the first border frame so
        // Chapter 5 keeps headroom for its framebuffer and border allocation.
        // Pinned fonts and dialogue pages survive this stale-page trim.
        if (VitaBorders_needsMemoryTrim() && !g_vitaModernGlActive) {
            GLLegacyRenderer* borderLegacy = (GLLegacyRenderer*)renderer;
            // Match the Chapter 5 renderer ceiling; borders and five room
            // surfaces live outside its resident-atlas accounting.
            // Use the same ceiling as the Chapter 2 renderer. The previous
            // 88 MiB border preparation repeatedly discarded page 2/8 while
            // the border was waiting to upload, causing visible room art to
            // vanish and be recreated on following frames.
            uint64_t borderTrimMiB = g_vitaActiveChapter == 5 ? 44ULL :
                (g_vitaActiveChapter == 3 ? 80ULL :
                 (g_vitaActiveChapter == 2 ? 104ULL : 88ULL));
            GLLegacyRenderer_trimTextureCacheForRoomChange(
                (GLLegacyRenderer*)renderer,
                borderTrimMiB * 1024ULL * 1024ULL, false);
            VitaBorders_setMemoryPressure(
                borderLegacy->residentTextureBytes > borderPressureMiB * 1024ULL * 1024ULL ||
                vglMemFree(VGL_MEM_VRAM) + vglMemFree(VGL_MEM_RAM) +
                    vglMemFree(VGL_MEM_SLOW) < 8ULL * 1024ULL * 1024ULL);
            char borderTrimLog[128];
            snprintf(borderTrimLog, sizeof(borderTrimLog),
                     "CONSOLE_BORDER=preload_texture_trim target_mb=%llu resident=%llu",
                     (unsigned long long)borderTrimMiB,
                     (unsigned long long)borderLegacy->residentTextureBytes);
            log_line(borderTrimLog);
        }
        /* Loading/settings overlays temporarily request a full-screen host
         * viewport.  Never let that state leak into the application-surface
         * composite after a save/room load, which looked like the game
         * shrinking once when the first normal frame was presented. */
        g_vitaPortOverlayFullScreen = 0;
        renderer->vtable->endFrameInit(renderer);
        if (frame == 0) log_line("FRAME0=end_frame_init_complete");
        if (frame == 0) log_line("FRAME0=draw_post_begin");
        Runner_drawPost(runner, 960, 544);
        if (frame == 0) log_line("FRAME0=draw_post_complete");
        renderer->vtable->endFrameEnd(renderer);
        render_composite_us = sceKernelGetProcessTimeWide() - render_phase_begin;
        render_phase_begin = sceKernelGetProcessTimeWide();
        if (frame == 0) log_line("FRAME0=end_frame_end_complete");
        draw_ch5_sakura_host(runner, renderer);
        Runner_drawGUI(runner, 960, 544, game_w, game_h);
        render_gui_us = sceKernelGetProcessTimeWide() - render_phase_begin;
        render_phase_begin = sceKernelGetProcessTimeWide();
        if (frame == 0) log_line("FRAME0=draw_gui_complete");
        // Port-owned overlays must not inherit a GameMaker room shader under
        // Modern GL. Preserve the logical GameMaker shader across the overlay
        // pass, but render Settings/DevMode/Navigator with the default program.
        int32_t portOverlaySavedShader = renderer->currentShader;
        if (g_vitaModernGlActive)
            renderer->vtable->gpuResetShader(renderer);

        // Advance SceAvPlayer even while it is preparing its first frame.
        // Once video_start has been dispatched, DELTARUNE presents the frame
        // through video_draw()/draw_surface() in its normal Draw event.
        VitaVideo_updateFrame();
        draw_ch5_flowey_video_host(runner, renderer);
        // Apply after the game, GUI and console border have been composited,
        // but before Game Settings so the brightness control remains readable.
        VitaSettings_drawBrightness(&settings, renderer);
        VitaSettings_drawTouchControls(&settings, renderer);
        VitaSettings_updateTrophies(&settings);
        VitaSettings_drawTrophyNotification(&settings, renderer);
        if (frame == 0) log_line("FRAME0=touch_overlay_complete");
        VitaSettings_draw(&settings, renderer);
        if (frame == 0) log_line("FRAME0=settings_overlay_complete");
        VitaSettings_drawCalibration(&settings, renderer);
        uint64_t rendererGpuBytes = 0;
        uint32_t rendererEvictions = 0, rendererDeferred = 0, rendererRamHits = 0;
        uint32_t rendererPrimitives = 0, rendererFlushes = 0;
        GLLegacyRenderer* legacy = (GLLegacyRenderer*)renderer;
        if (g_vitaModernGlActive) {
            GLRenderer* modern = (GLRenderer*)renderer;
            rendererGpuBytes = modern->residentTextureBytes;
            rendererEvictions = modern->vitaTextureEvictions;
            rendererDeferred = modern->vitaTextureDeferred;
            rendererRamHits = modern->vitaTextureRamHits;
            rendererPrimitives = modern->frameSubmittedPrimitives;
            rendererFlushes = modern->frameFlushCount;
        } else {
            rendererGpuBytes = legacy->residentTextureBytes;
            rendererEvictions = legacy->vitaTextureEvictions;
            rendererDeferred = legacy->vitaTextureDeferred;
            rendererRamHits = legacy->vitaTextureRamHits;
            rendererPrimitives = legacy->frameSubmittedPrimitives;
            rendererFlushes = legacy->frameFlushCount;
        }
        uint64_t dev_total_us = sceKernelGetProcessTimeWide() - frame_begin;
        uint64_t dev_render_us = sceKernelGetProcessTimeWide() - render_begin;
        float dev_fps = dev_total_us > 0 ? 1000000.0f / (float)dev_total_us : 0.0f;
        VitaSettings_drawDevOverlay(&settings, renderer,
            runner->currentRoom != NULL ? runner->currentRoom->name : NULL,
            dev_fps, step_us, audio_us, dev_render_us, rendererGpuBytes,
            rendererEvictions, rendererDeferred, rendererRamHits,
            dev_room_target >= 0 && (uint32_t)dev_room_target < dw->room.count ? dw->room.rooms[dev_room_target].name : NULL,
            dev_room_target);
        render_overlay_us = sceKernelGetProcessTimeWide() - render_phase_begin;
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
                // Draw the fade in a fresh host-framebuffer GUI pass. Reusing
                // DELTARUNE's active 4:3 viewport produced a smaller black
                // rectangle offset inside the 960x544 Vita display.
                extern int g_vitaPortOverlayFullScreen;
                int previousOverlayMode = g_vitaPortOverlayFullScreen;
                g_vitaPortOverlayFullScreen = 1;
                renderer->vtable->beginGUI(renderer, 960, 544, 0, 0, 960, 544,
                                            RENDER_TARGET_HOST_FRAMEBUFFER);
                renderer->vtable->drawRectangle(renderer, 0.0f, 0.0f, 960.0f, 544.0f,
                                                 0x000000, 1.0f - progress, false);
                renderer->vtable->endGUI(renderer);
                g_vitaPortOverlayFullScreen = previousOverlayMode;
            }
        }
        draw_screen_filter(&settings, renderer);
        if (g_vitaModernGlActive && portOverlaySavedShader >= 0)
            renderer->vtable->gpuSetShader(renderer, portOverlaySavedShader);
        if (runner->pendingRoom == -1) {
            if (frame == 0) log_line("FRAME0=swap_begin");
            // Frame pacing below already enforces the game's 30 FPS cadence.
            // Waiting for VitaGL's 60 Hz swap as well quantized heavier Chapter
            // 5 scenes to ~50 ms (20 FPS). Submit immediately and keep a single
            // pacing mechanism instead of stacking two waits.
            uint64_t swap_begin = sceKernelGetProcessTimeWide();
            // vglSwapBuffers() takes "has common dialog", not a VSync flag.
            // Update the actual VitaGL swap interval here so a live change to
            // FPS Limit takes effect immediately: capped modes synchronize;
            // A 40 Hz presentation cadence cannot divide evenly into Vita's
            // 60 Hz VBlank. Waiting here produced an alternating 16/33 ms
            // cadence while the software limiter also tried to hold 25 ms.
            // At 30 FPS a late vblank also adds up to 16.7 ms before the
            // deadline wait. Use one software pacer for both 30 and 40 FPS.
            bool waitForVblank = settings.vsyncEnabled && settings.fpsTargetMode >= 2;
            vglWaitVblankStart(waitForVblank ? GL_TRUE : GL_FALSE);
            vglSwapBuffers(GL_FALSE);
            render_swap_us = sceKernelGetProcessTimeWide() - swap_begin;
            if (frame == 0) log_line("FRAME0=swap_complete");
            if (runner->vitaAudioTransitionWaitFrames > 0) {
                runner->vitaAudioTransitionWaitFrames--;
                if (runner->vitaAudioTransitionWaitFrames == 0) {
                    AlAudioSystem_setTransitionHold((AlAudioSystem*)audio, false);
                    log_line("ROOM_AUDIO=new_streams_released_after_presented_frame");
                }
            }
            if (audio_present_wait_frames > 0) {
                audio_present_wait_frames--;
                if (audio_present_wait_frames == 0) {
                    AlAudioSystem_setTransitionHold((AlAudioSystem*)audio, false);
                    // Save loading can finish with a freshly primed stream in
                    // AL_STOPPED even though the transition hold was released
                    // one presented frame earlier. Reassert playback for every
                    // ready active stream before raising the category gain;
                    // this is idempotent for music that is already running.
                    if (audio->vtable->resumeAll != NULL)
                        audio->vtable->resumeAll(audio);
                    bool initialPresentation = startup_audio_paused;
                    if (startup_audio_paused && audio->vtable->resumeAll != NULL) {
                        startup_audio_paused = false;
                        log_line("STARTUP_AUDIO=resumed_at_first_presented_frame");
                    }
                    save_load_fade = !initialPresentation;
                    save_load_fade_start = sceKernelGetProcessTimeWide();
                    if (initialPresentation) {
                        AlAudioSystem_setCategoryGains((AlAudioSystem*)audio,
                            (float)settings.musicVolume / 10.0f,
                            (float)settings.sfxVolume / 10.0f);
                        log_line("STARTUP_AUDIO=full_gain_after_present");
                    } else {
                        log_line("AUDIO_FADE=begin_after_present duration_ms=1000");
                    }
                }
            }
        }
        // Report after Step and Draw so GPU-heavy rooms identify the GML Draw
        // code that submitted their primitives. Previously this ran directly
        // after Step and therefore omitted every Draw event from GML_TOP.
        if (profiler_sample_frame) {
            char* gmlReport = Profiler_createReport(runner->vmContext->profiler, 10, 1);
            if (gmlReport != NULL) {
                char header[64];
                snprintf(header, sizeof(header), "GML_TOP_STEP_DRAW frame=%u", frame);
                dev_log_write(header);
                dev_log_write(gmlReport);
                free(gmlReport);
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
        bool queue_new_room_audio = active_chapter > 0 && runner->pendingRoom >= 0 &&
                                    !starting_save_load && !entering_place_menu;
        // A forced/debug room jump can bypass obj_ch3_couch_video's normal
        // video_end/video_close path. Tear AVPlayer down before the room is
        // destroyed so its last decoded frame and audio thread cannot leak into
        // the destination room. Normal movie completion still remains owned by
        // the GameMaker video lifecycle.
        if (active_chapter == 3 && runner->pendingRoom >= 0 &&
            previous_room_name != NULL &&
            strcmp(previous_room_name, "room_dw_couch_video") == 0 &&
            (pending_room_name == NULL || strcmp(pending_room_name, previous_room_name) != 0)) {
            VitaVideo_close();
            log_line("VIDEO=room_exit_close room=room_dw_couch_video");
        }
        if (starting_save_load || entering_place_menu) {
            // Mute before Room End/room construction/Room Start. Music created by
            // the destination room therefore starts silent instead of playing a
            // short burst and being muted one frame later.
            AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, 0.0f,
                                           (float)settings.sfxVolume / 10.0f);
            AlAudioSystem_setTransitionHold((AlAudioSystem*)audio, true);
            log_line("SAVE_LOAD_FADE=muted_before_room_change");
            
            // Clear the host framebuffer directly. Drawing this mask through
            // beginGUI inherited DELTARUNE's adjusted 4:3 viewport, so the
            // temporary black save-load frame appeared offset on the Vita.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDisable(GL_SCISSOR_TEST);
            glViewport(0, 0, 960, 544);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            vglSwapBuffers(GL_FALSE);
        }
        if (queue_new_room_audio) {
            // Runner_handlePendingRoomChange owns this hold and its two-frame
            // release countdown. Setting transitionHold here first prevented
            // Runner from claiming it, leaving the hold enabled indefinitely;
            // later dialogue/SFX were queued but never started.
            log_line("ROOM_AUDIO=hold_delegated_to_runner");
        }
        Runner_handlePendingRoomChange(runner);
        if (runner->currentRoomIndex != room_before_pending)
            VitaFramePacer_reset(&frame_pacer, sceKernelGetProcessTimeWide());
        // Runner_handlePendingRoomChange keeps destination streams queued
        // until two complete frames of the new room have reached the display.
        // Releasing here made music play during late texture uploads, underrun
        // under the black fade and restart from the beginning afterwards.
        if ((starting_save_load || entering_place_menu) && runner->currentRoomIndex != room_before_pending) {
            // Wait for the destination to render and swap once. Starting the
            // fade here lets room construction/first-use texture uploads consume
            // most of it, causing a short audible burst before PLACE_MENU.
            save_load_fade = false;
            // The first swapped frame can still be followed by late atlas
            // uploads. Keep music muted until three destination frames have
            // actually reached the display so it does not start, underrun and
            // restart during the transition.
            audio_present_wait_frames = 3;
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
            log_memory_snapshot("room_change_complete",
                                g_vitaModernGlActive ? NULL : (GLLegacyRenderer*)renderer);
            logged_room_index = runner->currentRoomIndex;
        }
        if (dev_log_fd >= 0 && (frame % 30U) == 0U) {
            char dev_sample[512];
            snprintf(dev_sample, sizeof(dev_sample),
                     "DEV_SAMPLE frame=%u room=%s fps=%.2f total_us=%llu step_us=%llu audio_us=%llu render_us=%llu gpu_bytes=%llu evictions=%u deferred=%u ram_hits=%u quads=%u flushes=%u masks=%d events_us=%llu alarms_us=%llu spatial_us=%llu collision_us=%llu other_us=%llu",
                     frame, runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>",
                     work_us > 0 ? 1000000.0 / (double)work_us : 0.0,
                     (unsigned long long)work_us, (unsigned long long)step_us,
                     (unsigned long long)audio_us, (unsigned long long)render_us,
                     (unsigned long long)rendererGpuBytes,
                     rendererEvictions,
                     rendererDeferred,
                     rendererRamHits,
                     rendererPrimitives,
                     rendererFlushes,
                     settings.debugCollisionMasks ? 1 : 0,
                     (unsigned long long)runner->profStepEventsUs,
                     (unsigned long long)runner->profStepAlarmsUs,
                     (unsigned long long)runner->profStepSpatialUs,
                     (unsigned long long)runner->profStepCollisionUs,
                     (unsigned long long)runner->profStepOtherUs);
            dev_log_write(dev_sample);
            char render_phase[256];
            snprintf(render_phase, sizeof(render_phase),
                     "RENDER_PHASE frame=%u pre_us=%llu begin_us=%llu views_us=%llu composite_us=%llu gui_us=%llu overlay_us=%llu swap_us=%llu",
                     frame,
                     (unsigned long long)render_pre_us,
                     (unsigned long long)render_begin_frame_us,
                     (unsigned long long)render_views_us,
                     (unsigned long long)render_composite_us,
                     (unsigned long long)render_gui_us,
                     (unsigned long long)render_overlay_us,
                     (unsigned long long)render_swap_us);
            dev_log_write(render_phase);
        }
        if (work_us > 50000 && frame_begin - last_slow_log > 1000000ULL) {
            char slow[384];
            snprintf(slow, sizeof(slow), "PERF_SLOW frame=%u total_us=%llu step_us=%llu audio_us=%llu render_us=%llu gpu_bytes=%llu events_us=%llu alarms_us=%llu collision_us=%llu other_us=%llu room=%s index=%d",
                     frame, (unsigned long long)work_us, (unsigned long long)step_us,
                     (unsigned long long)audio_us, (unsigned long long)render_us,
                     (unsigned long long)rendererGpuBytes,
                     (unsigned long long)runner->profStepEventsUs,
                     (unsigned long long)runner->profStepAlarmsUs,
                     (unsigned long long)runner->profStepCollisionUs,
                     (unsigned long long)runner->profStepOtherUs,
                     runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>",
                     runner->currentRoomIndex);
            log_line(slow);
            // Persist the breakdown without requiring the developer overlay.
            // These CPU intervals include any driver waits; they are not GPU timers.
            char detail[512];
            snprintf(detail, sizeof(detail),
                     "PERF_RENDER frame=%u pre_us=%llu begin_us=%llu views_us=%llu composite_us=%llu gui_us=%llu overlay_us=%llu swap_us=%llu draw_max_us=%llu draw_object=%s",
                     frame, (unsigned long long)render_pre_us,
                     (unsigned long long)render_begin_frame_us,
                     (unsigned long long)render_views_us,
                     (unsigned long long)render_composite_us,
                     (unsigned long long)render_gui_us,
                     (unsigned long long)render_overlay_us,
                     (unsigned long long)render_swap_us,
                     (unsigned long long)runner->profDrawMaxUs,
                     runner->profDrawMaxObject >= 0 && (uint32_t)runner->profDrawMaxObject < dw->objt.count
                         ? dw->objt.objects[runner->profDrawMaxObject].name : "none");
            log_line(detail);
            if (g_vitaModernGlActive) {
                GLRenderer* modern = (GLRenderer*)renderer;
                uint32_t liveSurfaces = 0;
                uint64_t surfaceBytes = 0;
                for (uint32_t si = 0; si < modern->surfaceCount; ++si) {
                    if (!modern->surfaceTexture[si]) continue;
                    liveSurfaces++;
                    surfaceBytes += (uint64_t)modern->surfaceWidth[si] * modern->surfaceHeight[si] * 4;
                }
                snprintf(detail, sizeof(detail),
                         "PERF_MODERN frame=%u batches=%u primitives=%u buffer_us=%llu submit_us=%llu target_calls=%u surfaces=%u surface_color_bytes=%llu",
                         frame, modern->frameFlushCount, modern->frameSubmittedPrimitives,
                         (unsigned long long)modern->frameBufferUploadUs,
                         (unsigned long long)modern->frameDrawSubmitUs,
                         modern->frameTargetSwitches, liveSurfaces, (unsigned long long)surfaceBytes);
                log_line(detail);
            }
            last_slow_log = frame_begin;
        }
        if (frame_begin - perf_window_start >= 5000000ULL && perf_frames > 0) {
            char summary[192];
            snprintf(summary, sizeof(summary), "PERF_SUMMARY frames=%u avg_us=%llu max_us=%llu drops40=%u severe80=%u room=%s",
                     perf_frames, (unsigned long long)(perf_total_us / perf_frames),
                     (unsigned long long)perf_max_us, perf_drops, perf_severe,
                     runner->currentRoom && runner->currentRoom->name ? runner->currentRoom->name : "<null>");
            log_line(summary);
            if (active_chapter == 3 && runner->currentRoom && runner->currentRoom->name &&
                (strcmp(runner->currentRoom->name,"room_dw_teevie_stealth_d")==0 ||
                 strcmp(runner->currentRoom->name,"room_dw_teevie_maze_quiz")==0)) {
                snprintf(summary,sizeof(summary),
                    "CH3_TV_STATS getters=%llu candidates=%llu culled=%llu signature_miss=%llu shader_skip=%llu",
                    (unsigned long long)runner->vmContext->selfGetterFastCalls,
                    (unsigned long long)runner->vmContext->tvPanelCandidates,
                    (unsigned long long)runner->vmContext->tvPanelCulledDraws,
                    (unsigned long long)runner->vmContext->tvPanelSignatureMisses,
                    (unsigned long long)runner->vmContext->tvPanelShaderSkips);
                log_line(summary);
            }
            log_memory_snapshot("perf_summary",
                                g_vitaModernGlActive ? NULL : (GLLegacyRenderer*)renderer);
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
        uint32_t targetFps = 0;
        switch (settings.fpsTargetMode) {
            case 0: targetFps = 30; break;
            case 1: targetFps = 40; break;
            case 2: targetFps = 60; break;
            case 3: targetFps = 0;  break; // Unlock / Uncapped
            default: targetFps = 30; break;
        }

        uint64_t pacing_now = sceKernelGetProcessTimeWide();
        VitaFramePacer_setRate(&frame_pacer, targetFps, pacing_now);
        VitaFramePacerPlan pacing_plan =
            VitaFramePacer_plan(&frame_pacer, pacing_now);
        if (pacing_plan.enabled) {
            if (pacing_plan.coarseSleepUs != 0) {
                uint64_t sleep_begin = sceKernelGetProcessTimeWide();
                sceKernelDelayThread(pacing_plan.coarseSleepUs);
                VitaFramePacer_noteSleep(&frame_pacer,
                                         pacing_plan.coarseSleepUs,
                                         sceKernelGetProcessTimeWide() -
                                             sleep_begin);
            }
            while (sceKernelGetProcessTimeWide() < pacing_plan.deadlineUs) {
                // Adaptive sleep leaves at most a short, bounded tail.
            }
            VitaFramePacer_finish(&frame_pacer,
                                  sceKernelGetProcessTimeWide());
        }
    }

    if (runner->shouldExit && active_chapter > 0) {
        log_line("GAME_CHANGE=game_end_return_to_launcher");
        if (restart_into_chapter(0)) {
            audio->vtable->destroy(audio);
            sceKernelDelayThread(500000);
            sceKernelExitProcess(0);
        }
        log_line("GAME_CHANGE=launcher_loadexec_failed");
    }
    log_line(next_chapter >= 0 ? "EXIT=chapter_switch" : "EXIT=runner_requested");
    Runner_free(runner);
    runner = NULL;
    dev_log_stop();
    loading_direct_sound_shutdown(&loading_minigame_won_buffer, &loading_minigame_won_source);
    audio->vtable->destroy(audio);
    VitaBorders_shutdown();
    VitaVideo_shutdown();
    renderer->vtable->destroy(renderer);
    OverlayFileSystem_destroy(fs);
    VM_free(vm);
    VitaRoomPrefetch_shutdown();
    DataWin_free(dw);
    VitaBackgroundWork_destroy(background_work);
    log_line("PROCESS=exit_clean");
    sceKernelExitProcess(0);
    return 0;
}


