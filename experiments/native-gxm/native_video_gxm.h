#pragma once
#include <stdbool.h>
#include "nanovg.h"
#define VITA_VIDEO_SURFACE_ID (-700001)
#define VITA_VIDEO_EVENT_NONE 0
#define VITA_VIDEO_EVENT_START 1
#define VITA_VIDEO_EVENT_END 2
int VitaVideo_init(void);
void VitaVideo_attachRenderer(NVGcontext*);
int VitaVideo_open(const char*);
void VitaVideo_close(void);
void VitaVideo_afterPresent(void);
void VitaVideo_shutdown(void);
bool VitaVideo_updateFrame(void);
bool VitaVideo_hasDecodedFrame(void);
void VitaVideo_draw(float,float,float,float);
void VitaVideo_drawHost(float,float,float,float);
void VitaVideo_drawHostBrightness(float,float,float,float,float);
float VitaVideo_getWidth(void);
float VitaVideo_getHeight(void);
int VitaVideo_isChapter5Intro(void);
int VitaVideo_shouldApplyAudioResync(void);
void VitaVideo_pause(void);
void VitaVideo_resume(void);
int VitaVideo_getStatus(void);
float VitaVideo_getDuration(void);
float VitaVideo_getPosition(void);
int VitaVideo_consumeEvent(void);
void VitaVideo_setLooping(bool);
void VitaVideo_setVolume(float);
int VitaVideo_image(void);
