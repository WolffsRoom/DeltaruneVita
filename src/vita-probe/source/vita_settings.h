#ifndef DELTARUNE_VITA_SETTINGS_H
#define DELTARUNE_VITA_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <psp2/ctrl.h>

#include "audio_system.h"
#include "renderer.h"

typedef struct {
    bool open;
    bool touchEnabled;
    bool ptbrEnabled;
    bool widescreenEnabled;
    bool adjustMode;
    bool restartOnClose;
    int volume;
    int displayOffsetX;
    int displayOffsetY;
    int displayZoom;
    int selected;
    uint32_t previousButtons;
} VitaSettings;

void VitaSettings_load(VitaSettings* settings);
void VitaSettings_applyAudio(VitaSettings* settings, AudioSystem* audio);
bool VitaSettings_handleInput(VitaSettings* settings, const SceCtrlData* pad, AudioSystem* audio);
void VitaSettings_draw(VitaSettings* settings, Renderer* renderer);
void VitaSettings_drawTouchControls(VitaSettings* settings, Renderer* renderer);
void VitaSettings_drawCalibration(VitaSettings* settings, Renderer* renderer);

#endif
