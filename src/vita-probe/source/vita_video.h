// vita_video.h - SceAvPlayer video playback for Deltarune Vita
// Implements GML video_open / video_draw / video_close / video_get_status / video_get_duration
// using the Vita hardware H.264 decoder via sceAvPlayer.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// video_status_* constants (must match GML constants)
#define VITA_VIDEO_STATUS_NONE    0
#define VITA_VIDEO_STATUS_PLAYING 1
#define VITA_VIDEO_STATUS_PAUSED  2

// Initialise the sceAvPlayer subsystem. Call once after vitaGL is ready.
int VitaVideo_init(void);
// Release all video resources.
void VitaVideo_shutdown(void);
// Open and start playing an MP4.
int VitaVideo_open(const char* path);
// Draw the latest decoded frame into the rectangle.
void VitaVideo_draw(float x, float y, float w, float h);
// Stop and release.
void VitaVideo_close(void);
// Pause/resume.
void VitaVideo_pause(void);
void VitaVideo_resume(void);
// Returns VITA_VIDEO_STATUS_*.
int VitaVideo_getStatus(void);
// Returns total duration in seconds.
float VitaVideo_getDuration(void);
