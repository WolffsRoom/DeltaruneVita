#ifndef DELTARUNE_VITA_SETTINGS_H
#define DELTARUNE_VITA_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <psp2/ctrl.h>

#include "audio_system.h"
#include "renderer.h"

#define MAX_MODS 16
#define MOD_NAME_MAX 32

typedef struct {
    bool open;
    bool touchEnabled;
    bool ptbrEnabled;
    bool widescreenEnabled;
    bool adjustMode;
    bool restartOnClose;
    bool returnToChapterSelect;
    bool confirmChapterSelect;
    int musicVolume;
    int sfxVolume;
    int masterVolume;
    bool audioDisabled;
    bool devMode;
    bool showSettings;
    bool vsyncEnabled;
    bool debugDevEnabled;
    bool debugDevChanged;
    bool devRoomNavEnabled;
    bool shortcutSkipDialogs;
    int graphicsQuality;
    int pendingGraphicsQuality;
    int modIndex;
    int pendingModIndex;
    int modCount;
    char modNames[MAX_MODS][MOD_NAME_MAX];
    bool showRestartPrompt;
    int promptSelection; // 0 = Yes, 1 = No
    int displayOffsetX;
    int displayOffsetY;
    int displayZoom;
    float visualStickX;
    float visualStickY;
    bool visualConfirm;
    bool visualCancel;
    bool visualMenu;
    int category;
    int selected;
    int inputCooldown;
    int borderWarningFrames;
    bool controlEditMode;
    int selectedTouchControl;
    int touchControlX[4];
    int touchControlY[4];
    int touchControlScale[4];
    uint32_t previousButtons;
} VitaSettings;

extern int g_vitaGraphicsQuality;

void VitaSettings_load(VitaSettings* settings);
void VitaSettings_applyAudio(VitaSettings* settings, AudioSystem* audio);
bool VitaSettings_handleInput(VitaSettings* settings, const SceCtrlData* pad, AudioSystem* audio);
void VitaSettings_draw(VitaSettings* settings, Renderer* renderer);
void VitaSettings_drawTouchControls(VitaSettings* settings, Renderer* renderer);
void VitaSettings_drawCalibration(VitaSettings* settings, Renderer* renderer);
void VitaSettings_drawDevOverlay(VitaSettings* settings, Renderer* renderer,
                                 const char* room, float fps, uint64_t stepUs,
                                 uint64_t audioUs, uint64_t renderUs,
                                 uint64_t gpuBytes, uint32_t evictions,
                                 uint32_t deferred, uint32_t ramHits,
                                 const char* devTargetRoom, int32_t devTargetIndex);
void VitaSettings_setTouchVisuals(VitaSettings* settings, float stickX, float stickY,
                                  bool confirm, bool cancel, bool menu);
void VitaSettings_setLauncherMode(bool launcherMode);
int VitaSettings_itemCount(int category);

#endif
