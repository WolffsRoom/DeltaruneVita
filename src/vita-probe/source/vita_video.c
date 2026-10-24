// vita_video.c - SceAvPlayer-based MP4 video playback for Deltarune Vita
// Ported from vitaGL samples/video_playback/main.c
#include "vita_video.h"
#include "vita_media_rules.h"
#include "vita_thread_policy.h"
#include "audio/openal/al_audio_system.h"
#include <vitasdk.h>
#include <vitaGL.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "stb_image.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

// sceAvPlayer returns a two-plane YUV texture.  Feeding that texture through
// the ordinary GameMaker/VitaGL sprite program produces a black frame on the
// Vita.  We use runtime GLSL shaders compiled via VitaGL/ShaRK that perform the
// texture sampling expected by SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1.

// Two decoded frames are sufficient for SceAvPlayer's producer/consumer
// hand-off and match YoYo Loader's shipping backend. Five buffers consumed
// scarce Vita graphics/user memory before the first Chapter 3 frame.
#define VIDEO_BUFFERS 5
#define VIDEO_MEM_ALIGNMENT 0x40000
#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#define VIDEO_TRACKED_ALLOCS 64
typedef struct VideoTrackedAllocation {
    void* address;
    uint32_t size;
} VideoTrackedAllocation;

static VideoTrackedAllocation g_cpu_allocations[VIDEO_TRACKED_ALLOCS];
static VideoTrackedAllocation g_gpu_allocations[VIDEO_TRACKED_ALLOCS];
typedef struct VideoRawGpuAllocation {
    void* address;
    SceUID uid;
    uint32_t size;
    int mapped;
} VideoRawGpuAllocation;
static VideoRawGpuAllocation g_raw_gpu_allocations[VIDEO_TRACKED_ALLOCS];
static volatile int g_memory_lock = 0;
static uint64_t g_video_cpu_bytes = 0;
static uint64_t g_video_cpu_peak = 0;
static uint64_t g_video_gpu_bytes = 0;
static uint64_t g_video_gpu_peak = 0;

static void video_log(const char* state, const char* path, int result);

static void video_memory_lock(void) {
    while (__sync_lock_test_and_set(&g_memory_lock, 1)) sceKernelDelayThread(50);
}

static void video_memory_unlock(void) {
    __sync_lock_release(&g_memory_lock);
}

static void video_track_allocation(VideoTrackedAllocation* entries,
                                   void* address, uint32_t size,
                                   uint64_t* current, uint64_t* peak) {
    if (address == NULL) return;
    video_memory_lock();
    for (int i = 0; i < VIDEO_TRACKED_ALLOCS; ++i) {
        if (entries[i].address != NULL) continue;
        entries[i].address = address;
        entries[i].size = size;
        *current += size;
        if (*current > *peak) *peak = *current;
        break;
    }
    video_memory_unlock();
}

static void video_untrack_allocation(VideoTrackedAllocation* entries,
                                     void* address, uint64_t* current) {
    if (address == NULL) return;
    video_memory_lock();
    for (int i = 0; i < VIDEO_TRACKED_ALLOCS; ++i) {
        if (entries[i].address != address) continue;
        uint32_t size = entries[i].size;
        entries[i].address = NULL;
        entries[i].size = 0;
        *current = *current > size ? *current - size : 0;
        break;
    }
    video_memory_unlock();
}

static int video_raw_gpu_free(void* address) {
    if (address == NULL) return 0;
    for (int i = 0; i < VIDEO_TRACKED_ALLOCS; ++i) {
        if (g_raw_gpu_allocations[i].address != address) continue;
        if (g_raw_gpu_allocations[i].mapped)
            sceGxmUnmapMemory(address);
        if (g_raw_gpu_allocations[i].uid >= 0)
            sceKernelFreeMemBlock(g_raw_gpu_allocations[i].uid);
        g_raw_gpu_allocations[i].address = NULL;
        g_raw_gpu_allocations[i].uid = -1;
        g_raw_gpu_allocations[i].size = 0;
        g_raw_gpu_allocations[i].mapped = 0;
        return 1;
    }
    return 0;
}

static void video_release_failed_init_allocations(void) {
    // sceAvPlayerInit may return an error without calling the replacement
    // deallocators. Release those orphaned blocks before GML/fallback retries.
    for (int i = 0; i < VIDEO_TRACKED_ALLOCS; ++i) {
        void* address = g_gpu_allocations[i].address;
        if (address != NULL) {
            video_untrack_allocation(g_gpu_allocations, address, &g_video_gpu_bytes);
            video_raw_gpu_free(address);
        }
    }
    for (int i = 0; i < VIDEO_TRACKED_ALLOCS; ++i) {
        void* address = g_cpu_allocations[i].address;
        if (address != NULL) {
            video_untrack_allocation(g_cpu_allocations, address, &g_video_cpu_bytes);
            free(address);
        }
    }
}

// ===[ Memory callbacks for sceAvPlayer ]===
static void* avp_alloc_cpu(void* p, uint32_t align, uint32_t size) {
    (void)p;
    void* result = memalign(align, size);
    {
        char detail[160];
        snprintf(detail, sizeof(detail), "kind=cpu align=%u size=%u ptr=0x%08X",
                 (unsigned)align, (unsigned)size, (unsigned)(uintptr_t)result);
        video_log(result != NULL ? "alloc_cpu" : "alloc_cpu_failed", detail, result != NULL ? 0 : -1);
    }
    video_track_allocation(g_cpu_allocations, result, size,
                           &g_video_cpu_bytes, &g_video_cpu_peak);
    return result;
}
static void avp_free_cpu(void* p, void* ptr) {
    (void)p;
    video_untrack_allocation(g_cpu_allocations, ptr, &g_video_cpu_bytes);
    free(ptr);
}
static void* avp_alloc_gpu(void* p, uint32_t align, uint32_t size) {
    (void)p;
    if (align < VIDEO_MEM_ALIGNMENT) align = VIDEO_MEM_ALIGNMENT;
    size = ALIGN_MEM(size, align);
    uint32_t blockSize = ALIGN_MEM(size, 0x100000u);
    SceUID uid = sceKernelAllocMemBlock("deltarune_avp_phy",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, blockSize, NULL);
    if (uid < 0) {
        char detail[160];
        snprintf(detail, sizeof(detail), "kind=gpu_raw align=%u size=%u block=%u",
                 (unsigned)align, (unsigned)size, (unsigned)blockSize);
        video_log("alloc_gpu_raw_failed", detail, uid);
        return NULL;
    }
    void* res = NULL;
    int baseResult = sceKernelGetMemBlockBase(uid, &res);
    if (baseResult < 0 || res == NULL) {
        sceKernelFreeMemBlock(uid);
        video_log("alloc_gpu_raw_base_failed", "sceKernelGetMemBlockBase", baseResult);
        return NULL;
    }
    int mapResult = sceGxmMapMemory(res, blockSize, SCE_GXM_MEMORY_ATTRIB_RW);
    if (mapResult < 0) {
        sceKernelFreeMemBlock(uid);
        video_log("alloc_gpu_raw_map_failed", "sceGxmMapMemory", mapResult);
        return NULL;
    }
    for (int i = 0; i < VIDEO_TRACKED_ALLOCS; ++i) {
        if (g_raw_gpu_allocations[i].address != NULL) continue;
        g_raw_gpu_allocations[i].address = res;
        g_raw_gpu_allocations[i].uid = uid;
        g_raw_gpu_allocations[i].size = blockSize;
        g_raw_gpu_allocations[i].mapped = 1;
        break;
    }
    video_track_allocation(g_gpu_allocations, res, blockSize,
                           &g_video_gpu_bytes, &g_video_gpu_peak);
    char detail[192];
    snprintf(detail, sizeof(detail),
             "kind=gpu_raw align=%u request=%u block=%u ptr=0x%08X uid=0x%08X",
             (unsigned)align, (unsigned)size, (unsigned)blockSize,
             (unsigned)(uintptr_t)res, (unsigned)uid);
    video_log("alloc_gpu_raw", detail, 0);
    return res;
}
static void avp_free_gpu(void* p, void* addr) {
    (void)p;
    if (addr == NULL) return;
    glFinish();
    video_untrack_allocation(g_gpu_allocations, addr, &g_video_gpu_bytes);
    if (!video_raw_gpu_free(addr))
        video_log("free_gpu_raw_unknown", "untracked pointer", (int)(uintptr_t)addr);
}

// ===[ Video state ]===
typedef enum {
    VS_INACTIVE = 0,
    VS_PREPARING = 1,
    VS_PLAYING   = 2,
    VS_PAUSED    = 3,
} VideoState;

typedef enum {
    VIDEO_BACKEND_NONE = 0,
    VIDEO_BACKEND_AVPLAYER = 1,
    VIDEO_BACKEND_FRAMES = 2,
} VideoBackend;

#define FRAME_BACKEND_RING 4
#define FRAME_BACKEND_TOTAL 1225
#define FRAME_BACKEND_FPS_NUM 30000U
#define FRAME_BACKEND_FPS_DEN 1001U
#define FRAME_BACKEND_AUDIO_RATE 48000U
typedef struct FrameDecodedSlot {
    volatile int state;
    int frame;
    unsigned char* pixels;
    int width;
    int height;
} FrameDecodedSlot;

static volatile VideoState g_state = VS_INACTIVE;
static SceAvPlayerHandle   g_player;
static SceUID              g_audio_thread_id = -1;
static int                 g_audio_port      = -1;
static AlAudioSystem*     g_game_audio      = NULL;
static int                 g_audio_port_new  = 0;
static int                 g_audio_old_len   = 0;
static int                 g_audio_old_freq  = 0;
static int                 g_audio_old_mode  = 0;

static GLuint           g_frame_tex[VIDEO_BUFFERS];
static SceGxmTexture*   g_frame_gxm[VIDEO_BUFFERS];
static GLuint           g_movie_vs              = 0;
static GLuint           g_movie_fs              = 0;
static GLuint           g_movie_program         = 0;
static int              g_frame_idx            = 0;
static int              g_first_frame_decoded  = 0;
static int              g_initialized          = 0;
static int              g_hardware_disabled    = 0;
static char             g_hardware_disabled_reason[64] = {0};
static uint64_t         g_started_us           = 0;
static volatile int     g_pending_event        = VITA_VIDEO_EVENT_NONE;
static int              g_looping              = 0;
static float            g_volume               = 1.0f;
static float            g_duration_ms          = 40874.167f;
static float            g_video_width          = 640.0f;
static float            g_video_height         = 480.0f;
static int              g_is_first_load_video  = 0;
static int              g_is_tenna_video       = 0;
static int              g_is_ch5_intro_video   = 0;
static int              g_ch5_audio_resync_applied = 0;
static int              g_ch5_audio_resync_suppressed_logged = 0;
static int              g_ch5_eof_held = 0;
static int              g_ch5_final_pause_seen = 0;
static int              g_ch5_final_resume_seen = 0;
static uint64_t         g_ch5_eof_release_us = 0;
static uint64_t         g_next_video_frame_us  = 0;
#define TENNA_FRAME_INTERVAL_US 33367ULL
#define CH5_INTRO_FRAME_INTERVAL_US 41667ULL
static uint64_t         g_paused_started_us    = 0;
static uint64_t         g_paused_total_us      = 0;
static int              g_first_frame_logged   = 0;
static int              g_first_draw_logged    = 0;
static int              g_decoder_wait_logged  = 0;
static SceUID           g_video_file           = -1;
static uint64_t         g_video_file_size      = 0;
static int              g_video_file_reads     = 0;
static volatile int     g_video_file_lock      = 0;

static volatile VideoBackend g_backend = VIDEO_BACKEND_NONE;
static FrameDecodedSlot g_frame_slots[FRAME_BACKEND_RING];
static volatile int g_frame_decode_stop = 0;
static volatile int g_frame_decode_done = 0;
static SceUID g_frame_decode_thread = -1;
static int g_frame_current = -1;
static char g_frame_dir[384];
static char g_frame_audio_path[384];
static char g_video_source_path[384];
static volatile uint32_t g_frame_audio_samples = 0;
static volatile int g_frame_audio_done = 0;

static void avp_file_lock(void) {
    while (__sync_lock_test_and_set(&g_video_file_lock, 1)) sceKernelDelayThread(50);
}

static void avp_file_unlock(void) {
    __sync_lock_release(&g_video_file_lock);
}

// Retail sceAvPlayer returns its controller object as an address in user RAM.
// SceAvPlayerHandle is an int, so valid 0x81xxxxxx..0x9xxxxxxx handles appear
// negative in a signed comparison. Treating them as errors prevented every
// video from reaching sceAvPlayerAddSource.
static bool video_player_handle_valid(SceAvPlayerHandle handle) {
    uint32_t value = (uint32_t)handle;
    return value >= 0x81000000u && value < 0xA0000000u && (value & 3u) == 0;
}

static void video_log(const char* state, const char* path, int result) {
    extern int g_vitaProbeLoggingEnabled;
    if (!g_vitaProbeLoggingEnabled) return;
    FILE* f = fopen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log", "a");
    if (f != NULL) {
        fprintf(f, "VIDEO=%s result=0x%08X path=%s\n", state,
                (unsigned)result, path != NULL ? path : "<null>");
        fclose(f);
    }
}

// Use explicit file callbacks for ux0 sources. The stock sceAvPlayer path can
// accept AddSource yet never start decoding external files on some firmware;
// YoYo Loader's proven implementation supplies the file operations directly.
static int avp_file_open(void* object, const char* path) {
    (void)object;
    if (g_video_file >= 0) sceIoClose(g_video_file);
    g_video_file = sceIoOpen(path, SCE_O_RDONLY, 0);
    g_video_file_size = 0;
    g_video_file_reads = 0;
    if (g_video_file < 0) {
        video_log("file_callback_open_failed", path, g_video_file);
        return g_video_file;
    }
    SceIoStat st;
    memset(&st, 0, sizeof(st));
    if (sceIoGetstat(path, &st) >= 0) g_video_file_size = (uint64_t)st.st_size;
    video_log("file_callback_open", path, (int)g_video_file_size);
    return 0;
}

static int avp_file_close(void* object) {
    (void)object;
    int result = 0;
    if (g_video_file >= 0) result = sceIoClose(g_video_file);
    g_video_file = -1;
    return result;
}

static int avp_file_read(void* object, uint8_t* buffer, uint64_t offset, uint32_t length) {
    (void)object;
    if (g_video_file < 0) return -1;
    avp_file_lock();
    SceOff seek = sceIoLseek(g_video_file, (SceOff)offset, SCE_SEEK_SET);
    if (seek < 0) {
        avp_file_unlock();
        return (int)seek;
    }
    int result = sceIoRead(g_video_file, buffer, length);
    avp_file_unlock();
    if (result > 0) ++g_video_file_reads;
    if (g_video_file_reads <= 12) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                 "n=%d off=%llu len=%u got=%d seek=%lld",
                 g_video_file_reads, (unsigned long long)offset,
                 (unsigned)length, result, (long long)seek);
        video_log("file_read", detail, result);
    }
    return result;
}

static uint64_t avp_file_size(void* object) {
    (void)object;
    return g_video_file_size;
}


// ===[ Frame-sequence fallback for Chapter 3 ]===
static unsigned char* frame_backend_load_rgba(const char* path, int* outW, int* outH) {
    if (outW != NULL) *outW = 0;
    if (outH != NULL) *outH = 0;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return NULL;
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0) { sceIoClose(fd); return NULL; }
    unsigned char* encoded = (unsigned char*)malloc((size_t)size);
    if (encoded == NULL || sceIoRead(fd, encoded, (unsigned int)size) != size) {
        free(encoded);
        sceIoClose(fd);
        return NULL;
    }
    sceIoClose(fd);
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(encoded, (int)size, &w, &h, &channels, 4);
    free(encoded);
    if (pixels == NULL) return NULL;
    if (outW != NULL) *outW = w;
    if (outH != NULL) *outH = h;
    return pixels;
}

static void frame_backend_release_slot(FrameDecodedSlot* slot) {
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

static int frame_backend_decoder_thread(SceSize args, void* argp) {
    (void)args;
    (void)argp;
    for (int frame = 0; frame < FRAME_BACKEND_TOTAL && !g_frame_decode_stop; ++frame) {
        FrameDecodedSlot* slot = &g_frame_slots[frame % FRAME_BACKEND_RING];
        while (slot->state != 0 && !g_frame_decode_stop)
            sceKernelDelayThread(500);
        if (g_frame_decode_stop) break;

        char path[512];
        snprintf(path, sizeof(path), "%s/frame_%04d.png", g_frame_dir, frame);
        int w = 0, h = 0;
        unsigned char* pixels = frame_backend_load_rgba(path, &w, &h);
        slot->frame = frame;
        slot->pixels = pixels;
        slot->width = w;
        slot->height = h;
        __sync_synchronize();
        slot->state = 1;
    }
    g_frame_decode_done = 1;
    return sceKernelExitDeleteThread(0);
}

static int frame_backend_audio_thread(SceSize args, void* argp) {
    (void)args;
    (void)argp;
    FILE* file = fopen(g_frame_audio_path, "rb");
    if (file == NULL) {
        video_log("frame_audio_open_failed", g_frame_audio_path, -1);
        g_frame_audio_done = 1;
        return sceKernelExitDeleteThread(0);
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* compressed = length > 0 ? (unsigned char*)malloc((size_t)length) : NULL;
    if (compressed == NULL || fread(compressed, 1, (size_t)length, file) != (size_t)length) {
        free(compressed);
        fclose(file);
        video_log("frame_audio_read_failed", g_frame_audio_path, -2);
        g_frame_audio_done = 1;
        return sceKernelExitDeleteThread(0);
    }
    fclose(file);

    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(compressed, (int)length, &error, NULL);
    if (vorbis == NULL) {
        free(compressed);
        video_log("frame_audio_decode_open_failed", g_frame_audio_path, error);
        g_frame_audio_done = 1;
        return sceKernelExitDeleteThread(0);
    }
    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    int channels = info.channels == 1 ? 1 : 2;
    int sampleRate = (int)info.sample_rate;

    g_audio_port = -1;
    g_audio_port_new = 0;
    for (int i = 0; i < 8; ++i) {
        if (sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_LEN) < 0) continue;
        g_audio_port = i;
        g_audio_old_len = sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_LEN);
        g_audio_old_freq = sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ);
        g_audio_old_mode = sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_MODE);
        break;
    }
    if (g_audio_port < 0) {
        g_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024,
                                           sampleRate,
                                           channels == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
        g_audio_port_new = g_audio_port >= 0 ? 1 : 0;
    }
    if (g_audio_port >= 0)
        sceAudioOutSetConfig(g_audio_port, 1024, sampleRate,
                             channels == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);

    short pcm[1024 * 2];
    while (g_state != VS_INACTIVE && g_backend == VIDEO_BACKEND_FRAMES) {
        if (g_state != VS_PLAYING) {
            sceKernelDelayThread(2000);
            continue;
        }
        memset(pcm, 0, sizeof(pcm));
        int samples = stb_vorbis_get_samples_short_interleaved(vorbis, channels,
                                                                pcm, 1024 * channels);
        if (samples <= 0) break;
        if (g_audio_port >= 0) {
            int volume = (int)(g_volume * SCE_AUDIO_VOLUME_0DB);
            int volumes[2] = { volume, volume };
            sceAudioOutSetVolume(g_audio_port,
                SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volumes);
            sceAudioOutOutput(g_audio_port, pcm);
        }
        __sync_fetch_and_add(&g_frame_audio_samples, (uint32_t)samples);
    }

    if (g_audio_port >= 0) {
        if (g_audio_port_new) sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }
    stb_vorbis_close(vorbis);
    free(compressed);
    g_frame_audio_done = 1;
    return sceKernelExitDeleteThread(0);
}

static bool frame_backend_assets_for(const char* sourcePath) {
    const char* stem = NULL;
    if (sourcePath != NULL && strstr(sourcePath, "tennaIntroJPf1") != NULL)
        stem = "tennaIntroJPf1";
    else if (sourcePath != NULL && strstr(sourcePath, "tennaIntroF1") != NULL)
        stem = "tennaIntroF1";
    else
        return false;

    // Prefer frame/audio companions next to the MP4 selected by the overlay
    // filesystem (for example mods/Lang/Portuguese-BR/chapter3/vid). This
    // allows translations to ship their own movie assets. Fall back to the
    // shared deltarunevita/vid package so one common frame pack can still be
    // used by English and translations that do not override the movie.
    if (sourcePath != NULL) {
        const char* slash = strrchr(sourcePath, '/');
        const char* backslash = strrchr(sourcePath, '\\');
        const char* separator = slash;
        if (backslash != NULL && (separator == NULL || backslash > separator))
            separator = backslash;
        if (separator != NULL) {
            size_t dirLen = (size_t)(separator - sourcePath);
            if (dirLen > 0 && dirLen < 300) {
                char sourceDir[320];
                memcpy(sourceDir, sourcePath, dirLen);
                sourceDir[dirLen] = '\0';
                snprintf(g_frame_dir, sizeof(g_frame_dir), "%s/%s_frames", sourceDir, stem);
                snprintf(g_frame_audio_path, sizeof(g_frame_audio_path), "%s/%s_audio.ogg", sourceDir, stem);
                char firstFrame[512];
                struct stat st;
                snprintf(firstFrame, sizeof(firstFrame), "%s/frame_0000.png", g_frame_dir);
                if (stat(firstFrame, &st) == 0 && stat(g_frame_audio_path, &st) == 0)
                    return true;
            }
        }
    }

    snprintf(g_frame_dir, sizeof(g_frame_dir),
             "ux0:data/deltarune/deltarunevita/vid/%s_frames", stem);
    snprintf(g_frame_audio_path, sizeof(g_frame_audio_path),
             "ux0:data/deltarune/deltarunevita/vid/%s_audio.ogg", stem);
    char firstFrame[512];
    snprintf(firstFrame, sizeof(firstFrame), "%s/frame_0000.png", g_frame_dir);
    struct stat st;
    return stat(firstFrame, &st) == 0 && stat(g_frame_audio_path, &st) == 0;
}

static bool frame_backend_start(const char* sourcePath) {
    if (!frame_backend_assets_for(sourcePath)) return false;

    memset(g_frame_slots, 0, sizeof(g_frame_slots));
    g_backend = VIDEO_BACKEND_FRAMES;
    g_state = VS_PREPARING;
    g_first_frame_decoded = 0;
    g_first_frame_logged = 0;
    g_first_draw_logged = 0;
    g_decoder_wait_logged = 0;
    g_pending_event = VITA_VIDEO_EVENT_NONE;
    g_started_us = 0;
    g_paused_started_us = 0;
    g_paused_total_us = 0;
    g_frame_current = -1;
    g_frame_decode_stop = 0;
    g_frame_decode_done = 0;
    g_frame_audio_samples = 0;
    g_frame_audio_done = 0;
    g_video_width = 640.0f;
    g_video_height = 480.0f;
    g_duration_ms = 40874.167f;

    for (int i = 0; i < VIDEO_BUFFERS; ++i) {
        glBindTexture(GL_TEXTURE_2D, g_frame_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    g_frame_idx = 0;

    bool videoDecoderFourthCore = false;
    g_frame_decode_thread = VitaThread_createBackground(
        "Deltarune video frame decoder", frame_backend_decoder_thread,
        0x10000100, 0x20000, 0, &videoDecoderFourthCore);
    video_log(videoDecoderFourthCore
        ? "CAPUNLOCKER=video_frame_decoder core=4 enabled=1"
        : "CAPUNLOCKER=video_frame_decoder core=default enabled=0 fallback=1",
        sourcePath, g_frame_decode_thread);
    if (g_frame_decode_thread < 0 || sceKernelStartThread(g_frame_decode_thread, 0, NULL) < 0) {
        video_log("frame_decoder_thread_failed", sourcePath, g_frame_decode_thread);
        g_backend = VIDEO_BACKEND_NONE;
        g_state = VS_INACTIVE;
        return false;
    }
    g_audio_thread_id = sceKernelCreateThread("Deltarune video frame audio",
        frame_backend_audio_thread, 0x10000100 - 10, 0x10000, 0, 0, NULL);
    if (g_audio_thread_id < 0 || sceKernelStartThread(g_audio_thread_id, 0, NULL) < 0) {
        video_log("frame_audio_thread_failed", g_frame_audio_path, g_audio_thread_id);
        g_frame_decode_stop = 1;
        g_backend = VIDEO_BACKEND_NONE;
        g_state = VS_INACTIVE;
        return false;
    }
    video_log("frame_backend_open", g_frame_dir, FRAME_BACKEND_TOTAL);
    return true;
}

static bool frame_backend_slot_ready(int frame, FrameDecodedSlot** outSlot) {
    FrameDecodedSlot* slot = &g_frame_slots[frame % FRAME_BACKEND_RING];
    __sync_synchronize();
    if (slot->state == 1 && slot->frame == frame) {
        if (outSlot != NULL) *outSlot = slot;
        return true;
    }
    return false;
}

static bool frame_backend_update(void) {
    if (g_backend != VIDEO_BACKEND_FRAMES || g_state == VS_INACTIVE) return false;

    if (!g_first_frame_decoded) {
        FrameDecodedSlot* first = NULL;
        if (!frame_backend_slot_ready(0, &first)) return true;
        if (first->pixels == NULL || first->width != 640 || first->height != 480) {
            video_log("frame_backend_first_frame_invalid", g_frame_dir, -1);
            frame_backend_release_slot(first);
            return true;
        }
        glBindTexture(GL_TEXTURE_2D, g_frame_tex[0]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480,
                        GL_RGBA, GL_UNSIGNED_BYTE, first->pixels);
        frame_backend_release_slot(first);
        g_frame_idx = 0;
        g_frame_current = 0;
        g_first_frame_decoded = 1;
        g_state = VS_PLAYING;
        g_started_us = sceKernelGetProcessTimeWide();
        g_pending_event = VITA_VIDEO_EVENT_START;
        video_log("frame_backend_first_frame", "640x480", 0);
        return true;
    }

    if (g_state == VS_PAUSED) return true;

    uint32_t samples = g_frame_audio_samples;
    int desired = (int)(((uint64_t)samples * FRAME_BACKEND_FPS_NUM) /
                        ((uint64_t)FRAME_BACKEND_AUDIO_RATE * FRAME_BACKEND_FPS_DEN));
    if (desired >= FRAME_BACKEND_TOTAL) desired = FRAME_BACKEND_TOTAL - 1;
    if (desired <= g_frame_current) {
        if (g_frame_audio_done && g_frame_current >= FRAME_BACKEND_TOTAL - 1) {
            g_pending_event = VITA_VIDEO_EVENT_END;
            g_state = VS_INACTIVE;
            return false;
        }
        return true;
    }

    int scanFrame = g_frame_current;
    FrameDecodedSlot* uploadSlot = NULL;
    while (scanFrame < desired) {
        int next = scanFrame + 1;
        FrameDecodedSlot* slot = NULL;
        if (!frame_backend_slot_ready(next, &slot)) break;
        if (uploadSlot != NULL) frame_backend_release_slot(uploadSlot);
        uploadSlot = slot;
        scanFrame = next;
    }
    if (uploadSlot != NULL) {
        if (uploadSlot->pixels != NULL && uploadSlot->width == 640 && uploadSlot->height == 480) {
            g_frame_idx = (g_frame_idx + 1) % VIDEO_BUFFERS;
            glBindTexture(GL_TEXTURE_2D, g_frame_tex[g_frame_idx]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 640, 480,
                            GL_RGBA, GL_UNSIGNED_BYTE, uploadSlot->pixels);
            g_frame_current = scanFrame;
        }
        frame_backend_release_slot(uploadSlot);
    }

    if (g_frame_audio_done && g_frame_current >= FRAME_BACKEND_TOTAL - 1) {
        g_pending_event = VITA_VIDEO_EVENT_END;
        g_state = VS_INACTIVE;
        return false;
    }
    return true;
}

static void frame_backend_stop(void) {
    g_frame_decode_stop = 1;
    if (g_frame_decode_thread >= 0) {
        SceUInt timeout = 3000000;
        sceKernelWaitThreadEnd(g_frame_decode_thread, NULL, &timeout);
        g_frame_decode_thread = -1;
    }
    for (int i = 0; i < FRAME_BACKEND_RING; ++i)
        frame_backend_release_slot(&g_frame_slots[i]);
    if (g_audio_thread_id >= 0) {
        SceUInt timeout = 3000000;
        sceKernelWaitThreadEnd(g_audio_thread_id, NULL, &timeout);
        g_audio_thread_id = -1;
    }
    for (int i = 0; i < VIDEO_BUFFERS; ++i) {
        glBindTexture(GL_TEXTURE_2D, g_frame_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        g_frame_gxm[i] = vglGetGxmTexture(GL_TEXTURE_2D);
        void* ptr = vglGetTexDataPointer(GL_TEXTURE_2D);
        if (ptr != NULL) vglFree(ptr);
    }
    g_backend = VIDEO_BACKEND_NONE;
    g_frame_current = -1;
}


static bool video_find_adjacent_ogg(char* out, size_t capacity) {
    if (!g_is_tenna_video || g_video_source_path[0] == '\0' || out == NULL || capacity < 8)
        return false;
    snprintf(out, capacity, "%s", g_video_source_path);
    char* dot = strrchr(out, '.');
    if (dot == NULL) return false;
    size_t used = (size_t)(dot - out);
    if (used + 5 >= capacity) return false;
    snprintf(dot, capacity - used, ".ogg");
    SceIoStat st;
    return sceIoGetstat(out, &st) >= 0;
}

static bool video_audio_play_ogg_fallback(void) {
    char oggPath[384];
    if (!video_find_adjacent_ogg(oggPath, sizeof(oggPath))) {
        video_log("audio_ogg_fallback_missing", g_video_source_path, -1);
        return false;
    }

    FILE* file = fopen(oggPath, "rb");
    if (file == NULL) {
        video_log("audio_ogg_fallback_open_failed", oggPath, -1);
        return false;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* compressed = length > 0 ? (unsigned char*)malloc((size_t)length) : NULL;
    if (compressed == NULL || fread(compressed, 1, (size_t)length, file) != (size_t)length) {
        free(compressed);
        fclose(file);
        video_log("audio_ogg_fallback_read_failed", oggPath, -2);
        return false;
    }
    fclose(file);

    int decodeError = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(compressed, (int)length, &decodeError, NULL);
    if (vorbis == NULL) {
        free(compressed);
        video_log("audio_ogg_fallback_decode_failed", oggPath, decodeError);
        return false;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    int channels = info.channels == 1 ? 1 : 2;
    int sampleRate = (int)info.sample_rate;
    SceAudioOutMode mode = channels == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO;

    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, 1024, sampleRate, mode);
    const char* route = "voice";
    if (port < 0 && sampleRate == 48000) {
        port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, sampleRate, mode);
        route = "main";
    }
    if (port < 0) {
        port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024, sampleRate, mode);
        route = "bgm";
    }
    if (port < 0) {
        char detail[160];
        snprintf(detail, sizeof(detail), "%s rate=%d channels=%d", oggPath, sampleRate, channels);
        video_log("audio_ogg_fallback_port_failed", detail, port);
        stb_vorbis_close(vorbis);
        free(compressed);
        return false;
    }

    int volume = (int)(g_volume * SCE_AUDIO_VOLUME_0DB);
    int volumes[2] = { volume, volume };
    sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volumes);
    char selected[192];
    snprintf(selected, sizeof(selected), "%s route=%s port=%d rate=%d channels=%d",
             oggPath, route, port, sampleRate, channels);
    video_log("audio_source_ogg_fallback", selected, 0);

    short pcm[1024 * 2];
    int firstOutputLogged = 0;
    while (g_state != VS_INACTIVE) {
        if (g_state == VS_PAUSED) {
            sceKernelDelayThread(2000);
            continue;
        }
        memset(pcm, 0, sizeof(pcm));
        int samples = stb_vorbis_get_samples_short_interleaved(vorbis, channels,
                                                                pcm, 1024 * channels);
        if (samples <= 0) break;
        int outputResult = sceAudioOutOutput(port, pcm);
        if (!firstOutputLogged) {
            video_log(outputResult >= 0 ? "audio_ogg_fallback_output_started" :
                                         "audio_ogg_fallback_output_failed",
                      oggPath, outputResult);
            firstOutputLogged = 1;
        }
        if (outputResult < 0) break;
    }

    sceAudioOutReleasePort(port);
    stb_vorbis_close(vorbis);
    free(compressed);
    video_log("audio_ogg_fallback_complete", oggPath, 0);
    return true;
}

// ===[ Audio thread ]===
static int audio_thread_func(SceSize args, void* argp) {
    (void)args; (void)argp;
    // YoYo Loader reuses an already-open SceAudioOut port when the game has
    // consumed all hardware ports. Pick the first open port that actually
    // accepts the movie frame format, save its config, and restore it at end.
    g_audio_port = -1;
    g_audio_port_new = 0;
    g_audio_old_len = 0;
    g_audio_old_freq = 0;
    g_audio_old_mode = 0;
    int audio_config_logged = 0;
    int audio_source_logged = 0;
    char audio_route[24] = "none";

    while (g_state != VS_INACTIVE) {
        if ((g_state == VS_PREPARING || g_state == VS_PLAYING) && sceAvPlayerIsActive(g_player)) {
            SceAvPlayerFrameInfo frame;
            memset(&frame, 0, sizeof(frame));
            if (sceAvPlayerGetAudioData(g_player, &frame)) {
                int mode = frame.details.audio.channelCount == 1 ?
                           SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO;                if (g_audio_port < 0) {
                    int bgmPort = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024,
                                                      frame.details.audio.sampleRate,
                                                      (SceAudioOutMode)mode);
                    if (bgmPort >= 0) {
                        g_audio_port = bgmPort;
                        g_audio_port_new = 1;
                        snprintf(audio_route, sizeof(audio_route), "exclusive_bgm");
                        video_log("audio_port_exclusive_selected", "openal_handoff", g_audio_port);
                    } else {
                        video_log("audio_native_port_failed", "openal_released_but_bgm_unavailable", bgmPort);
                        if (g_is_tenna_video) {
                            video_audio_play_ogg_fallback();
                            return sceKernelExitDeleteThread(0);
                        }
                    }
                }

                if (g_audio_port < 0) {
                    sceKernelDelayThread(1000);
                    continue;
                }

                int cfg = sceAudioOutSetConfig(g_audio_port, 1024,
                                                frame.details.audio.sampleRate,
                                                (SceAudioOutMode)mode);
                if (!audio_config_logged) {
                    char detail[128];
                    snprintf(detail, sizeof(detail), "rate=%u channels=%u bytes=%u cfg=0x%08X",
                             (unsigned)frame.details.audio.sampleRate,
                             (unsigned)frame.details.audio.channelCount,
                             (unsigned)frame.details.audio.size, (unsigned)cfg);
                    video_log(cfg >= 0 ? "audio_config_ready" : "audio_config_failed", detail, cfg);
                    audio_config_logged = 1;
                }
                if (cfg < 0) { sceKernelDelayThread(1000); continue; }

                int volume = (int)(g_volume * SCE_AUDIO_VOLUME_0DB);
                int volumes[2] = { volume, volume };
                sceAudioOutSetVolume(g_audio_port,
                    SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, volumes);
                int outputResult = sceAudioOutOutput(g_audio_port, frame.pData);
                if (!audio_source_logged) {
                    char detail[160];
                    snprintf(detail, sizeof(detail),
                             "route=%s port=%d rate=%u channels=%u bytes=%u",
                             audio_route, g_audio_port,
                             (unsigned)frame.details.audio.sampleRate,
                             (unsigned)frame.details.audio.channelCount,
                             (unsigned)frame.details.audio.size);
                    video_log(outputResult >= 0 ? "audio_source_mp4_native" :
                                                 "audio_mp4_native_output_failed",
                              detail, outputResult);
                    audio_source_logged = 1;
                }
                if (outputResult < 0 && g_is_tenna_video) {
                    if (g_audio_port_new) sceAudioOutReleasePort(g_audio_port);
                    else if (g_audio_old_len > 0 && g_audio_old_freq > 0)
                        sceAudioOutSetConfig(g_audio_port, g_audio_old_len, g_audio_old_freq,
                                             (SceAudioOutMode)g_audio_old_mode);
                    g_audio_port = -1;
                    video_audio_play_ogg_fallback();
                    return sceKernelExitDeleteThread(0);
                }
            } else {
                sceKernelDelayThread(1000);
            }
        } else {
            sceKernelDelayThread(5000);
        }
    }

    if (g_audio_port >= 0) {
        if (g_audio_port_new) {
            sceAudioOutReleasePort(g_audio_port);
        } else if (g_audio_old_len > 0 && g_audio_old_freq > 0) {
            sceAudioOutSetConfig(g_audio_port, g_audio_old_len, g_audio_old_freq,
                                 (SceAudioOutMode)g_audio_old_mode);
            video_log("audio_port_restored", "reused_port", g_audio_port);
        }
        g_audio_port = -1;
    }
    video_log("audio_thread_exit", "vita_video_audio", 0);
    return sceKernelExitDeleteThread(0);
}

// The audio worker may still be executing inside sceAvPlayerGetAudioData() when
// the main thread detects end-of-stream. Always signal it first and wait for it
// to exit before destroying the SceAvPlayer controller.
static bool video_stop_avplayer_workers(const char* reason) {
    g_state = VS_INACTIVE;

    if (g_audio_thread_id >= 0) {
        SceUID audioThread = g_audio_thread_id;
        video_log("audio_thread_join_begin", reason, audioThread);
        int waitResult = sceKernelWaitThreadEnd(audioThread, NULL, NULL);
        if (waitResult < 0) {
            // Keep the handle valid and, crucially, do not close the player
            // while a worker may still dereference its internal state.
            video_log("audio_thread_join_failed", reason, waitResult);
            return false;
        }
        g_audio_thread_id = -1;
        video_log("audio_thread_joined", reason, waitResult);
    }

    if (video_player_handle_valid(g_player)) {
        video_log("player_close_begin", reason, 0);
        int stopResult = sceAvPlayerStop(g_player);
        int closeResult = sceAvPlayerClose(g_player);
        g_player = (SceAvPlayerHandle)0;
        char detail[128];
        snprintf(detail, sizeof(detail), "%s stop=0x%08X close=0x%08X",
                 reason != NULL ? reason : "unknown",
                 (unsigned)stopResult, (unsigned)closeResult);
        video_log("player_close_complete", detail, closeResult);
    }
    return true;
}

static GLuint compileVideoShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info[512] = {0};
        glGetShaderInfoLog(shader, sizeof(info) - 1, NULL, info);
        video_log("shader_compile_failed", info, (int)type);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// ===[ Public API ]===
int VitaVideo_init(void) {
    if (g_initialized) return 0;
    video_log("init_begin", "sceAvPlayer", 0);
    int r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (r < 0 && r != (int)0x80540101 /* already loaded */) {
        video_log("sysmodule_failed", "sceAvPlayer", r);
        return r;
    }
    video_log("sysmodule_ready", "sceAvPlayer", r);
    glGenTextures(VIDEO_BUFFERS, g_frame_tex);
    for (int i = 0; i < VIDEO_BUFFERS; i++) {
        glBindTexture(GL_TEXTURE_2D, g_frame_tex[i]);
        // Allocate a tiny placeholder; the actual data pointer will come from sceAvPlayer
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        g_frame_gxm[i] = vglGetGxmTexture(GL_TEXTURE_2D);
        vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
        // The placeholder storage was explicitly released above. Clear VitaGL's
        // ownership pointer before this texture handle is rebound to external
        // SceAvPlayer frame memory, otherwise deletion can touch it twice.
        vglOverloadTexDataPointer(GL_TEXTURE_2D, NULL);
    }
    video_log("frame_handles_ready", "sceAvPlayer", VIDEO_BUFFERS);

    static const char* vertexSource =
        "#version 100\n"
        "attribute vec2 inPos;\n"
        "attribute vec2 inTex;\n"
        "varying vec2 texcoord;\n"
        "void main() {\n"
        "    texcoord = inTex;\n"
        "    gl_Position = vec4(inPos, 0.0, 1.0);\n"
        "}\n";
    static const char* fragmentSource =
        "#version 100\n"
        "precision mediump float;\n"
        "varying vec2 texcoord;\n"
        "uniform sampler2D tex;\n"
        "uniform float brightness;\n"
        "void main() {\n"
        "    gl_FragColor = texture2D(tex, texcoord) * vec4(brightness, brightness, brightness, 1.0);\n"
        "}\n";

    g_movie_vs = compileVideoShader(GL_VERTEX_SHADER, vertexSource);
    g_movie_fs = compileVideoShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (g_movie_vs == 0 || g_movie_fs == 0) {
        video_log("shader_create_failed", "sceAvPlayer", -1);
        if (g_movie_vs != 0) { glDeleteShader(g_movie_vs); g_movie_vs = 0; }
        if (g_movie_fs != 0) { glDeleteShader(g_movie_fs); g_movie_fs = 0; }
        return -1;
    }

    g_movie_program = glCreateProgram();
    glAttachShader(g_movie_program, g_movie_vs);
    glAttachShader(g_movie_program, g_movie_fs);
    glBindAttribLocation(g_movie_program, 0, "inPos");
    glBindAttribLocation(g_movie_program, 1, "inTex");
    glLinkProgram(g_movie_program);
    glDeleteShader(g_movie_vs);
    glDeleteShader(g_movie_fs);
    g_movie_vs = g_movie_fs = 0;

    GLint linked = GL_FALSE;
    glGetProgramiv(g_movie_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char info[512] = {0};
        glGetProgramInfoLog(g_movie_program, sizeof(info) - 1, NULL, info);
        video_log("program_link_failed", info, -1);
        glDeleteProgram(g_movie_program);
        g_movie_program = 0;
        return -1;
    }

    glUseProgram(g_movie_program);
    glUniform1i(glGetUniformLocation(g_movie_program, "tex"), 0);
    glUniform1f(glGetUniformLocation(g_movie_program, "brightness"), 1.0f);
    glUseProgram(0);
    video_log("presentation_shader_ready", "sceAvPlayer", (int)g_movie_program);
    g_initialized = 1;
    video_log("init_complete", "sceAvPlayer", 0);
    return 0;
}

void VitaVideo_shutdown(void) {
    VitaVideo_close();
    if (g_initialized) {
        if (g_movie_program != 0) glDeleteProgram(g_movie_program);
        if (g_movie_vs != 0) glDeleteShader(g_movie_vs);
        if (g_movie_fs != 0) glDeleteShader(g_movie_fs);
        g_movie_program = g_movie_vs = g_movie_fs = 0;
        glDeleteTextures(VIDEO_BUFFERS, g_frame_tex);
        g_initialized = 0;
    }
}

void VitaVideo_setGameAudioSystem(void* audio) {
    g_game_audio = (AlAudioSystem*)audio;
}

void VitaVideo_setHardwareDisabled(bool disabled, const char* reason) {
    g_hardware_disabled = disabled ? 1 : 0;
    snprintf(g_hardware_disabled_reason, sizeof(g_hardware_disabled_reason), "%s",
             reason != NULL ? reason : "runtime_guard");
    if (g_hardware_disabled) {
        VitaVideo_close();
        video_log("hardware_disabled", g_hardware_disabled_reason, 0);
    }
}

int VitaVideo_open(const char* path) {
    if (g_hardware_disabled) {
        g_state = VS_INACTIVE;
        g_backend = VIDEO_BACKEND_NONE;
        g_first_frame_decoded = 0;
        // DELTARUNE waits for the asynchronous end event to leave movie rooms.
        // Publishing it here keeps those rooms playable without touching
        // SceAvPlayer, whose kernel path conflicts with UAC/UVC plugins.
        g_pending_event = VITA_VIDEO_EVENT_END;
        video_log("video_disabled", g_hardware_disabled_reason, -1);
        return -1;
    }
    if (!g_initialized) {
        int initResult = VitaVideo_init();
        if (initResult < 0) {
            video_log("init_failed", path, initResult);
            return initResult;
        }
    }
    VitaVideo_close(); // stop any previous video

    // Keep the path already selected by the overlay filesystem whenever it
    // exists. A translation may provide its own MP4 under mods/Lang/<lang>/...
    // and must not be silently replaced by the shared English movie. Only use
    // deltarunevita/vid/<basename> as a fallback when the resolved mod/chapter
    // path is absent.
    char resolvedPath[384];
    char chapterPath[384];
    const char* sourcePath = path;
    extern int g_vitaActiveChapter;
    SceIoStat sourceInfo;
    bool isApp0 = path != NULL && strncmp(path, "app0:", 5) == 0;
    bool sourceExists = isApp0 || (path != NULL && sceIoGetstat(path, &sourceInfo) >= 0);

    const char* videoName = NULL;
    if (path != NULL) {
        const char* slash = strrchr(path, '/');
        const char* backslash = strrchr(path, '\\');
        const char* separator = slash;
        if (backslash != NULL && (separator == NULL || backslash > separator))
            separator = backslash;
        videoName = separator != NULL ? separator + 1 : path;
    }
    if (!sourceExists && videoName != NULL && strstr(videoName, ".mp4") != NULL) {
        snprintf(resolvedPath, sizeof(resolvedPath),
                 "ux0:data/deltarune/deltarunevita/vid/%s", videoName);
        SceIoStat fileInfo;
        if (sceIoGetstat(resolvedPath, &fileInfo) >= 0) {
            sourcePath = resolvedPath;
            sourceExists = true;
        }
    }
    if (!sourceExists && !isApp0 && path != NULL && strchr(path, ':') == NULL && path[0] != '/') {
        snprintf(resolvedPath, sizeof(resolvedPath),
                 "ux0:data/deltarune/deltarunevita/%s", path);
        SceIoStat fileInfo;
        if (sceIoGetstat(resolvedPath, &fileInfo) >= 0) {
            sourcePath = resolvedPath;
        } else {
            snprintf(chapterPath, sizeof(chapterPath),
                     "ux0:data/deltarune/deltarunevita/chapter%d/%s",
                     g_vitaActiveChapter, path);
            sourcePath = chapterPath;
        }
    }

    // MP4 is the authoritative Chapter 3/5 path. The frame-sequence backend is
    // retained only as research/fallback code and is deliberately not selected
    // automatically: thousands of PNGs caused I/O stalls and complicated room
    // lifecycle/audio synchronization on real Vita hardware.
    g_is_first_load_video = sourcePath != NULL && strstr(sourcePath, "first_load.mp4") != NULL;
    g_is_tenna_video = sourcePath != NULL &&
        (strstr(sourcePath, "tennaIntroF1_compressed_28.mp4") != NULL ||
         strstr(sourcePath, "tennaIntroJPf1_compressed_28.mp4") != NULL);
    g_is_ch5_intro_video = g_vitaActiveChapter == 5 && VitaMediaRules_isChapter5Intro(sourcePath);
    g_ch5_audio_resync_applied = 0;
    g_ch5_audio_resync_suppressed_logged = 0;
    g_ch5_eof_held = 0;
    g_ch5_final_pause_seen = 0;
    g_ch5_final_resume_seen = 0;
    g_ch5_eof_release_us = 0;
    // Chapter 5's MP4 is video-only. GML plays ch5_intro_audio.ogg through
    // OpenAL, so releasing that device here also discards its soundtrack.
    const bool gameSoundtrack = g_is_ch5_intro_video != 0;
    if (!g_is_first_load_video && !gameSoundtrack && g_game_audio != NULL) {
        if (!AlAudioSystem_beginVideoHandoff(g_game_audio)) {
            video_log("audio_handoff_failed", sourcePath, -1);
        } else {
            video_log("audio_handoff_begin", sourcePath, 0);
        }
    }
    snprintf(g_video_source_path, sizeof(g_video_source_path), "%s", sourcePath != NULL ? sourcePath : "");
    g_next_video_frame_us = 0;
    g_backend = VIDEO_BACKEND_AVPLAYER;
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    // Match the validated stock-SceAvPlayer loader: generic allocations use
    // memalign and decoded video frames use dedicated raw PHYCONT blocks.
    init.memoryReplacement.allocate          = avp_alloc_cpu;
    init.memoryReplacement.deallocate        = avp_free_cpu;
    init.memoryReplacement.allocateTexture   = avp_alloc_gpu;
    init.memoryReplacement.deallocateTexture = avp_free_gpu;
    init.basePriority = 0xA0;
    // Keep the already validated five-buffer setup for the 960x544 cold-boot
    // movie. Chapter videos follow YoYo Loader's shipping SceAvPlayer backend
    // and use two producer/consumer output buffers.
    init.numOutputVideoFrameBuffers = g_is_first_load_video ? VIDEO_BUFFERS : 2;
    init.autoStart = GL_TRUE;

    g_player = sceAvPlayerInit(&init);
    if (!video_player_handle_valid(g_player)) {
        video_log("player_init_failed", sourcePath, (int)g_player);
        video_release_failed_init_allocations();
        return (int)g_player;
    }

    // GameMaker keeps a newly opened video in video_status_preparing until
    // the decoder has produced its first displayable frame.  DELTARUNE waits
    // for the matching video_start async event before enabling its video Draw
    // event, so advertising PLAYING here races the decoder and yields captions
    // over a black surface on Vita.
    g_state = VS_PREPARING;
    g_frame_idx = 0;
    g_first_frame_decoded = 0;
    g_first_frame_logged = 0;
    g_first_draw_logged = 0;
    g_decoder_wait_logged = 0;
    g_started_us = sceKernelGetProcessTimeWide();
    g_paused_started_us = 0;
    g_paused_total_us = 0;
    g_video_width = 640.0f;
    g_video_height = 480.0f;
    g_duration_ms = g_is_first_load_video ? 4500.0f :
                    (strstr(sourcePath, "ch5_intro_") != NULL ? 30333.333f : 40874.167f);

    int r = sceAvPlayerAddSource(g_player, sourcePath);
    if (r < 0) {
        video_log("open_failed", sourcePath, r);
        VitaVideo_close();
        return r;
    }
    video_log("open_ok_native_local", sourcePath, r);
    sceAvPlayerSetLooping(g_player, g_looping ? GL_TRUE : GL_FALSE);
    g_pending_event = VITA_VIDEO_EVENT_NONE;

    // Only movies with their own soundtrack need the AvPlayer audio worker.
    g_audio_thread_id = gameSoundtrack ? -1 : sceKernelCreateThread("vita_video_audio", audio_thread_func,
                                               0x10000100 - 10, 0x4000, 0, 0, NULL);
    if (g_audio_thread_id >= 0)
        sceKernelStartThread(g_audio_thread_id, 0, NULL);

    return 0;
}

static bool video_prepare_timeout_end(void) {
    const uint64_t elapsed = sceKernelGetProcessTimeWide() - g_started_us;
    char detail[128];
    snprintf(detail, sizeof(detail),
             "elapsed_us=%llu reads=%d size=%llu",
             (unsigned long long)elapsed, g_video_file_reads,
             (unsigned long long)g_video_file_size);
    video_log("prepare_timeout_end", detail, 0);

    // Stop worker activity before closing AVPlayer. Preserve an END event so
    // DELTARUNE's original async handler can clear the VHS recovery marker,
    // clean up the room and continue to room_dw_tv_cutscene1g.
    video_stop_avplayer_workers("prepare_timeout");
    g_backend = VIDEO_BACKEND_NONE;
    g_first_frame_decoded = 0;
    g_pending_event = VITA_VIDEO_EVENT_END;
    return false;
}

bool VitaVideo_updateFrame(void) {
    if (g_state == VS_INACTIVE || !g_initialized) return false;

    if (g_is_ch5_intro_video && g_ch5_eof_held) {
        uint64_t nowUs = sceKernelGetProcessTimeWide();
        if (g_ch5_eof_release_us != 0 && nowUs >= g_ch5_eof_release_us) {
            video_log("ch5_eof_release_after_final_pause", g_video_source_path, 0);
            video_stop_avplayer_workers("ch5_final_pause_complete");
            g_backend = VIDEO_BACKEND_NONE;
            g_pending_event = VITA_VIDEO_EVENT_END;
            g_ch5_eof_held = 0;
            return false;
        }
        return true;
    }
    if (g_backend == VIDEO_BACKEND_FRAMES) return frame_backend_update();

    if (g_state == VS_PREPARING || g_state == VS_PLAYING) {
        int active = sceAvPlayerIsActive(g_player);
        if (active) {
            uint64_t nowUs = sceKernelGetProcessTimeWide();
            bool pacedVideo = g_is_tenna_video || g_is_ch5_intro_video;
            bool fetchFrame = !pacedVideo || !g_first_frame_decoded ||
                              g_next_video_frame_us == 0 || nowUs >= g_next_video_frame_us;
            SceAvPlayerFrameInfo frame;
            if (fetchFrame && sceAvPlayerGetVideoData(g_player, &frame)) {
                if (frame.pData != NULL && frame.details.video.width > 0 && frame.details.video.height > 0) {
                    g_frame_idx = (g_frame_idx + 1) % VIDEO_BUFFERS;
                    sceGxmTextureInitLinear(g_frame_gxm[g_frame_idx],
                        frame.pData,
                        SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
                        frame.details.video.width,
                        frame.details.video.height, 0);
                    sceGxmTextureSetMinFilter(g_frame_gxm[g_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
                    sceGxmTextureSetMagFilter(g_frame_gxm[g_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
                    g_video_width = (float)frame.details.video.width;
                    g_video_height = (float)frame.details.video.height;
                    if (pacedVideo) {
                        uint64_t interval = g_is_ch5_intro_video ?
                            CH5_INTRO_FRAME_INTERVAL_US : TENNA_FRAME_INTERVAL_US;
                        uint64_t base = g_next_video_frame_us != 0 ? g_next_video_frame_us : nowUs;
                        g_next_video_frame_us = base + interval;
                        if (g_next_video_frame_us <= nowUs)
                            g_next_video_frame_us = nowUs + interval;
                    }
                    if (!g_first_frame_decoded) {
                        g_first_frame_decoded = 1;
                        g_state = VS_PLAYING;
                        g_pending_event = VITA_VIDEO_EVENT_START;
                    }
                }
                if (!g_first_frame_logged) {
                    char dimensions[64];
                    snprintf(dimensions, sizeof(dimensions), "%ux%u",
                             frame.details.video.width, frame.details.video.height);
                    video_log("first_frame_decoded", dimensions, 0);
                    g_first_frame_logged = 1;
                }
            }
            if (!g_first_frame_decoded && !g_decoder_wait_logged &&
                sceKernelGetProcessTimeWide() - g_started_us > 2000000ULL) {
                char detail[96];
                snprintf(detail, sizeof(detail), "active=1 reads=%d size=%llu",
                         g_video_file_reads, (unsigned long long)g_video_file_size);
                video_log("waiting_first_frame", detail, 0);
                g_decoder_wait_logged = 1;
            }
        } else if (g_first_frame_decoded) {
            // Chapter 5 deliberately pauses the movie near its physical EOF
            // for ~90 GameMaker frames while the black fade completes. Some
            // Vita AvPlayer runs report inactive a few frames before that
            // scripted pause/resume sequence. Preserve the last decoded frame
            // and fake PLAYING/PAUSED status until the scripted resume occurs.
            if (g_is_ch5_intro_video) {
                g_ch5_eof_held = 1;
                g_state = VS_PLAYING;
                video_log("ch5_eof_held_for_final_pause", g_video_source_path, 0);
                if (g_ch5_final_resume_seen) {
                    g_ch5_eof_release_us = sceKernelGetProcessTimeWide() + 700000ULL;
                    video_log("ch5_eof_release_scheduled", "eof_after_video_resume_700ms", 0);
                }
                return true;
            }
            // Startup owns its final-frame hold/fade. Keep AVPlayer and its
            // external frame memory alive until first_load_video_play finishes
            // presenting that sequence, then VitaVideo_close performs the
            // synchronized worker shutdown.
            if (g_is_first_load_video) {
                g_state = VS_PAUSED;
                g_pending_event = VITA_VIDEO_EVENT_END;
                video_log("first_load_eof_held_for_fade", g_video_source_path, 0);
                return false;
            }
            // Video finished. The audio worker can still be inside AVPlayer,
            // so it must be joined before Stop/Close destroys the controller.
            video_log("playback_complete_begin", g_video_source_path, 0);
            video_stop_avplayer_workers("end_of_stream");
            g_backend = VIDEO_BACKEND_NONE;
            g_pending_event = VITA_VIDEO_EVENT_END;
            return false;
        } else if (!g_decoder_wait_logged &&
                   sceKernelGetProcessTimeWide() - g_started_us > 2000000ULL) {
            char detail[96];
            snprintf(detail, sizeof(detail), "active=0 reads=%d size=%llu",
                     g_video_file_reads, (unsigned long long)g_video_file_size);
            video_log("waiting_first_frame", detail, 0);
            g_decoder_wait_logged = 1;
        }

        if (g_state == VS_PREPARING && !g_first_frame_decoded &&
            sceKernelGetProcessTimeWide() - g_started_us >= 3000000ULL) {
            return video_prepare_timeout_end();
        }
    }

    return g_state != VS_INACTIVE;
}

bool VitaVideo_hasDecodedFrame(void) {
    return g_state != VS_INACTIVE && g_first_frame_decoded != 0;
}

#include "vita_video_draw_gl.h"

void VitaVideo_draw(float x, float y, float w, float h) {
    if (g_state == VS_INACTIVE || !g_initialized || !g_first_frame_decoded) return;

    VitaVideo_drawGL(g_frame_tex[g_frame_idx], g_movie_program, g_is_first_load_video, x, y, w, h);

    if (!g_first_draw_logged) {
        video_log("first_frame_presented", "gml_surface", 0);
        g_first_draw_logged = 1;
    }
}

void VitaVideo_drawHostBrightness(float x, float y, float w, float h, float brightness) {
    if (!VitaVideo_hasDecodedFrame()) return;
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;

    GLint previousProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glUseProgram(g_movie_program);
    glUniform1f(glGetUniformLocation(g_movie_program, "brightness"), brightness);
    glUseProgram((GLuint)previousProgram);

    glViewport(0, 0, 960, 544);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrthof(0.0f, 960.0f, 544.0f, 0.0f, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    // Host-framebuffer video uses the same top-origin mapping validated by the
    // first-load movie. The GameMaker surface path uses the YoYo-flipped UVs.
    VitaVideo_drawGL(g_frame_tex[g_frame_idx], g_movie_program, true, x, y, w, h);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    if (!g_first_draw_logged) {
        video_log("first_frame_presented", "host_overlay", 0);
        g_first_draw_logged = 1;
    }
}

void VitaVideo_drawHost(float x, float y, float w, float h) {
    VitaVideo_drawHostBrightness(x, y, w, h, 1.0f);
}

void VitaVideo_close(void) {
    g_is_ch5_intro_video = 0;
    g_ch5_audio_resync_applied = 0;
    g_ch5_audio_resync_suppressed_logged = 0;
    g_ch5_eof_held = 0;
    g_ch5_final_pause_seen = 0;
    g_ch5_final_resume_seen = 0;
    g_ch5_eof_release_us = 0;
    if (g_backend == VIDEO_BACKEND_FRAMES) {
        g_state = VS_INACTIVE;
        frame_backend_stop();
        g_first_frame_decoded = 0;
        return;
    }
    if (g_state == VS_INACTIVE && g_audio_thread_id < 0 &&
        !video_player_handle_valid(g_player)) {
        if (g_game_audio != NULL && g_game_audio->videoAudioHandoff)
            AlAudioSystem_endVideoHandoff(g_game_audio);
        return;
    }
    video_stop_avplayer_workers("explicit_close");
    g_first_frame_decoded = 0;
    g_backend = VIDEO_BACKEND_NONE;
    if (g_game_audio != NULL && g_game_audio->videoAudioHandoff) {
        bool restored = AlAudioSystem_endVideoHandoff(g_game_audio);
        video_log(restored ? "audio_handoff_end" : "audio_handoff_restore_failed",
                  "openal", restored ? 0 : -1);
    }
}

void VitaVideo_pause(void) {
    if (g_state == VS_PLAYING) {
        if (!g_ch5_eof_held)
            sceAvPlayerPause(g_player);
        g_state = VS_PAUSED;
        g_paused_started_us = sceKernelGetProcessTimeWide();
        if (g_is_ch5_intro_video) {
            g_ch5_final_pause_seen = 1;
            video_log("ch5_final_pause_seen", "video_pause", 0);
        }
    }
}

void VitaVideo_resume(void) {
    if (g_state == VS_PAUSED) {
        uint64_t nowUs = sceKernelGetProcessTimeWide();
        if (!g_ch5_eof_held)
            sceAvPlayerResume(g_player);
        g_state = VS_PLAYING;
        if (g_paused_started_us != 0) {
            g_paused_total_us += nowUs - g_paused_started_us;
            g_paused_started_us = 0;
        }
        if (g_is_ch5_intro_video && g_ch5_eof_held && g_ch5_final_pause_seen) {
            // The original MP4 has roughly 0.6-0.7 s left after the scripted
            // pause point. Keep the fully-faded last frame for that grace
            // before publishing video_end to GML.
            g_ch5_eof_release_us = nowUs + 700000ULL;
            video_log("ch5_eof_release_scheduled", "after_video_resume_700ms", 0);
        }
        // The Chapter 5 cutscene intentionally pauses only the movie for about
        // 90 frames while its OGG keeps running. Permit one new alignment after
        // each explicit resume, then suppress repeated queue rebuilds again.
        if (g_is_ch5_intro_video) {
            if (g_ch5_final_pause_seen)
                g_ch5_final_resume_seen = 1;
            g_ch5_audio_resync_applied = 0;
            g_ch5_audio_resync_suppressed_logged = 0;
            video_log("ch5_audio_resync_rearmed", "video_resume", 0);
        }
    }
}

int VitaVideo_getStatus(void) {
    if (g_state == VS_PREPARING) return VITA_VIDEO_STATUS_PREPARING;
    if (g_state == VS_PLAYING) return VITA_VIDEO_STATUS_PLAYING;
    if (g_state == VS_PAUSED) return VITA_VIDEO_STATUS_PAUSED;
    return VITA_VIDEO_STATUS_NONE;
}

float VitaVideo_getDuration(void) {
    return g_state == VS_INACTIVE ? 0.0f : g_duration_ms;
}

float VitaVideo_getPosition(void) {
    if (g_backend == VIDEO_BACKEND_FRAMES)
        return ((float)g_frame_audio_samples * 1000.0f) / (float)FRAME_BACKEND_AUDIO_RATE;
    if (g_backend == VIDEO_BACKEND_AVPLAYER && g_state != VS_INACTIVE &&
        video_player_handle_valid(g_player)) {
        uint64_t mediaMs = sceAvPlayerCurrentTime(g_player);
        if (mediaMs <= (uint64_t)(g_duration_ms + 5000.0f)) return (float)mediaMs;
    }
    if (g_state == VS_INACTIVE || g_started_us == 0) return 0.0f;
    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t paused = g_paused_total_us;
    if (g_state == VS_PAUSED && g_paused_started_us != 0) paused += now - g_paused_started_us;
    uint64_t elapsed = now > g_started_us + paused ? now - g_started_us - paused : 0;
    return (float)elapsed / 1000.0f;
}

float VitaVideo_getWidth(void) { return g_video_width; }
float VitaVideo_getHeight(void) { return g_video_height; }

int VitaVideo_isChapter5Intro(void) {
    return g_is_ch5_intro_video;
}

int VitaVideo_shouldApplyAudioResync(void) {
    if (!g_is_ch5_intro_video) return 1;
    if (VitaMediaRules_allowHardAudioResync(g_ch5_audio_resync_applied)) {
        g_ch5_audio_resync_applied = 1;
        video_log("ch5_audio_resync_allowed", "first_only", 0);
        return 1;
    }
    if (!g_ch5_audio_resync_suppressed_logged) {
        video_log("ch5_audio_resync_suppressed", "queue_rebuild_avoided", 0);
        g_ch5_audio_resync_suppressed_logged = 1;
    }
    return 0;
}

int VitaVideo_consumeEvent(void) {
    int event = g_pending_event;
    g_pending_event = VITA_VIDEO_EVENT_NONE;
    return event;
}

void VitaVideo_setLooping(bool enabled) {
    g_looping = enabled ? 1 : 0;
    if (g_state != VS_INACTIVE && g_backend == VIDEO_BACKEND_AVPLAYER)
        sceAvPlayerSetLooping(g_player, enabled ? GL_TRUE : GL_FALSE);
}

void VitaVideo_setVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    g_volume = volume;
}

void VitaVideo_getMemoryStats(VitaVideoMemoryStats* stats) {
    if (stats == NULL) return;
    video_memory_lock();
    stats->cpuBytes = g_video_cpu_bytes;
    stats->cpuPeakBytes = g_video_cpu_peak;
    stats->gpuBytes = g_video_gpu_bytes;
    stats->gpuPeakBytes = g_video_gpu_peak;
    video_memory_unlock();
}

