// vita_video.h - SceAvPlayer video playback for Deltarune Vita
// Implements GML video_open / video_draw / video_close / video_get_status / video_get_duration
// using the Vita hardware H.264 decoder via sceAvPlayer.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// video_status_* constants (must match GML constants)
#define VITA_VIDEO_STATUS_NONE    0
#define VITA_VIDEO_STATUS_PREPARING 1
#define VITA_VIDEO_STATUS_PLAYING 2
#define VITA_VIDEO_STATUS_PAUSED  3

#define VITA_VIDEO_EVENT_NONE  0
#define VITA_VIDEO_EVENT_START 1
#define VITA_VIDEO_EVENT_END   2

#define VITA_VIDEO_SURFACE_ID (-700001)

typedef struct VitaVideoMemoryStats {
    uint64_t cpuBytes;
    uint64_t cpuPeakBytes;
    uint64_t gpuBytes;
    uint64_t gpuPeakBytes;
} VitaVideoMemoryStats;

// Initialise the sceAvPlayer subsystem. Call once after vitaGL is ready.
int VitaVideo_init(void);
// Release all video resources.
void VitaVideo_shutdown(void);
// Attach the live game mixer. Chapter movies temporarily release it so SceAvPlayer owns audio exclusively.
void VitaVideo_setGameAudioSystem(void* audio);
// Prevent SceAvPlayer from being initialized when a known USB streaming
// kernel plugin is active. Opening a movie then publishes video_end so GML can
// continue instead of entering the unsafe decoder path.
void VitaVideo_setHardwareDisabled(bool disabled, const char* reason);
// Open and start playing an MP4.
int VitaVideo_open(const char* path);
bool VitaVideo_updateFrame(void);
bool VitaVideo_hasDecodedFrame(void);
// Draw the latest decoded frame into the rectangle.
void VitaVideo_draw(float x, float y, float w, float h);
void VitaVideo_drawHost(float x, float y, float w, float h);
void VitaVideo_drawHostBrightness(float x, float y, float w, float h, float brightness);
float VitaVideo_getWidth(void);
float VitaVideo_getHeight(void);
// Chapter 5 Flowey intro uses the console full-screen movie presentation.
int VitaVideo_isChapter5Intro(void);
// The Vita streaming backend rebuilds the queue on seek; allow only one hard resync for this intro.
int VitaVideo_shouldApplyAudioResync(void);
// Stop and release.
void VitaVideo_close(void);
// Pause/resume.
void VitaVideo_pause(void);
void VitaVideo_resume(void);
// Returns VITA_VIDEO_STATUS_*.
int VitaVideo_getStatus(void);
// Returns duration/position in milliseconds, matching GameMaker's video API.
float VitaVideo_getDuration(void);
float VitaVideo_getPosition(void);
// Returns and clears a pending GameMaker async video event.
int VitaVideo_consumeEvent(void);
void VitaVideo_setLooping(bool enabled);
void VitaVideo_setVolume(float volume);
// Memory owned by sceAvPlayer callbacks. This is separate from vitaGL's pools.
void VitaVideo_getMemoryStats(VitaVideoMemoryStats* stats);
