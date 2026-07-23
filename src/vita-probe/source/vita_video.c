// vita_video.c - SceAvPlayer-based MP4 video playback for Deltarune Vita
// Ported from vitaGL samples/video_playback/main.c
#include "vita_video.h"
#include <vitasdk.h>
#include <vitaGL.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define VIDEO_BUFFERS 5
#define PHYCONT_MEM_ALIGNMENT (1024 * 1024)
#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

// ===[ Memory callbacks for sceAvPlayer ]===
static void* avp_alloc_cpu(void* p, uint32_t align, uint32_t size) {
    (void)p;
    return memalign(align, size);
}
static void avp_free_cpu(void* p, void* ptr) {
    (void)p;
    free(ptr);
}
static void* avp_alloc_gpu(void* p, uint32_t align, uint32_t size) {
    (void)p;
    size = ALIGN_MEM(size, PHYCONT_MEM_ALIGNMENT);
    SceUID blk = sceKernelAllocMemBlock("avp_gpu", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, size, NULL);
    if (blk < 0) return NULL;
    void* res;
    sceKernelGetMemBlockBase(blk, &res);
    sceGxmMapMemory(res, size, SCE_GXM_MEMORY_ATTRIB_RW);
    return res;
}
static void avp_free_gpu(void* p, void* addr) {
    (void)p;
    glFinish();
    SceUID blk = sceKernelFindMemBlockByAddr(addr, 0);
    sceGxmUnmapMemory(addr);
    sceKernelFreeMemBlock(blk);
}

// ===[ Video state ]===
typedef enum {
    VS_INACTIVE = 0,
    VS_PLAYING  = 1,
    VS_PAUSED   = 2,
} VideoState;

static volatile VideoState g_state = VS_INACTIVE;
static SceAvPlayerHandle   g_player;
static SceUID              g_audio_thread_id = -1;
static int                 g_audio_port      = -1;

static GLuint           g_frame_tex[VIDEO_BUFFERS];
static SceGxmTexture*   g_frame_gxm[VIDEO_BUFFERS];
static int              g_frame_idx            = 0;
static int              g_first_frame_decoded  = 0;
static int              g_initialized          = 0;

// ===[ Audio thread ]===
static int audio_thread_func(SceSize args, void* argp) {
    (void)args; (void)argp;
    g_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
    while (g_state != VS_INACTIVE) {
        if (g_state == VS_PLAYING && sceAvPlayerIsActive(g_player)) {
            SceAvPlayerFrameInfo frame;
            if (sceAvPlayerGetAudioData(g_player, &frame)) {
                sceAudioOutSetConfig(g_audio_port, 1024, frame.details.audio.sampleRate,
                    frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
                sceAudioOutOutput(g_audio_port, frame.pData);
            } else {
                sceKernelDelayThread(1000);
            }
        } else {
            sceKernelDelayThread(5000);
        }
    }
    if (g_audio_port >= 0) {
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }
    return sceKernelExitDeleteThread(0);
}

// ===[ Public API ]===
int VitaVideo_init(void) {
    if (g_initialized) return 0;
    int r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if (r < 0 && r != (int)0x80540101 /* already loaded */) return r;
    glGenTextures(VIDEO_BUFFERS, g_frame_tex);
    for (int i = 0; i < VIDEO_BUFFERS; i++) {
        glBindTexture(GL_TEXTURE_2D, g_frame_tex[i]);
        // Allocate a tiny placeholder; the actual data pointer will come from sceAvPlayer
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        g_frame_gxm[i] = vglGetGxmTexture(GL_TEXTURE_2D);
        vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
    }
    g_initialized = 1;
    return 0;
}

void VitaVideo_shutdown(void) {
    VitaVideo_close();
    if (g_initialized) {
        glDeleteTextures(VIDEO_BUFFERS, g_frame_tex);
        g_initialized = 0;
    }
}

int VitaVideo_open(const char* path) {
    if (!g_initialized) VitaVideo_init();
    VitaVideo_close(); // stop any previous video

    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate          = avp_alloc_cpu;
    init.memoryReplacement.deallocate        = avp_free_cpu;
    init.memoryReplacement.allocateTexture   = avp_alloc_gpu;
    init.memoryReplacement.deallocateTexture = avp_free_gpu;
    init.basePriority                = 0xA0;
    init.numOutputVideoFrameBuffers  = VIDEO_BUFFERS;
    init.autoStart                   = GL_TRUE;

    g_player = sceAvPlayerInit(&init);
    if ((int)g_player < 0) return (int)g_player;

    g_state = VS_PLAYING;
    g_frame_idx = 0;
    g_first_frame_decoded = 0;

    int r = sceAvPlayerAddSource(g_player, path);
    if (r < 0) { VitaVideo_close(); return r; }

    // Start audio thread
    g_audio_thread_id = sceKernelCreateThread("vita_video_audio", audio_thread_func,
                                               0x10000100 - 10, 0x4000, 0, 0, NULL);
    if (g_audio_thread_id >= 0)
        sceKernelStartThread(g_audio_thread_id, 0, NULL);

    return 0;
}

void VitaVideo_draw(float x, float y, float w, float h) {
    if (g_state == VS_INACTIVE || !g_initialized) return;

    // Pump a new video frame
    if (g_state == VS_PLAYING) {
        if (sceAvPlayerIsActive(g_player)) {
            SceAvPlayerFrameInfo frame;
            if (sceAvPlayerGetVideoData(g_player, &frame)) {
                g_frame_idx = (g_frame_idx + 1) % VIDEO_BUFFERS;
                sceGxmTextureInitLinear(g_frame_gxm[g_frame_idx],
                    frame.pData,
                    SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
                    frame.details.video.width,
                    frame.details.video.height, 0);
                sceGxmTextureSetMinFilter(g_frame_gxm[g_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
                sceGxmTextureSetMagFilter(g_frame_gxm[g_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
                g_first_frame_decoded = 1;
            }
        } else if (g_first_frame_decoded) {
            // Video finished
            sceAvPlayerStop(g_player);
            sceAvPlayerClose(g_player);
            g_state = VS_INACTIVE;
            return;
        }
    }

    if (!g_first_frame_decoded) return;

    // Draw the last decoded frame as a full-screen quad inside the given rect
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, g_frame_tex[g_frame_idx]);
    glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y    );
        glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y    );
        glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y + h);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
    glEnd();
    glEnable(GL_BLEND);
}

void VitaVideo_close(void) {
    if (g_state == VS_INACTIVE) return;
    g_state = VS_INACTIVE; // signals audio thread to stop
    if (g_audio_thread_id >= 0) {
        SceUInt timeout = 3000000;
        sceKernelWaitThreadEnd(g_audio_thread_id, NULL, &timeout);
        g_audio_thread_id = -1;
    }
    if ((int)g_player >= 0) {
        sceAvPlayerStop(g_player);
        sceAvPlayerClose(g_player);
    }
    g_first_frame_decoded = 0;
}

void VitaVideo_pause(void) {
    if (g_state == VS_PLAYING) {
        sceAvPlayerPause(g_player);
        g_state = VS_PAUSED;
    }
}

void VitaVideo_resume(void) {
    if (g_state == VS_PAUSED) {
        sceAvPlayerResume(g_player);
        g_state = VS_PLAYING;
    }
}

int VitaVideo_getStatus(void) {
    return (int)g_state;  // 0=none 1=playing 2=paused, matches GML video_status_*
}

float VitaVideo_getDuration(void) {
    if (g_state == VS_INACTIVE) return 0.0f;
    // sceAvPlayer doesn't expose total duration easily; return -1 so GML treats it as "ongoing"
    return -1.0f;
}
