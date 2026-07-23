#include "vita_settings.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#include "runner.h"
#include "audio/openal/al_audio_system.h"
#include "vita_borders.h"

#define SETTINGS_PATH "ux0:data/deltarune/config.ini"
#define SETTINGS_CATEGORIES 5
#define MODS_ROOT "ux0:data/deltarune/deltarunevita/mods/"

static bool g_devMode = false;

static int settingsItemCount(int category) {
    if (g_devMode) {
        if (category == 0) return 2; // DEV
        if (category == 1) return 3; // CONTROLES
        if (category == 2) return 3; // TELA
        if (category == 3) return 4; // AUDIO
        if (category == 4) return 4; // SISTEMA
    } else {
        if (category == 0) return 3; // CONTROLES
        if (category == 1) return 3; // TELA
        if (category == 2) return 4; // AUDIO
        if (category == 3) return 4; // SISTEMA
    }
    return 0;
}

int VitaSettings_itemCount(int category) { return settingsItemCount(category); }

int g_vitaDisplayOffsetX = 0;
int g_vitaDisplayOffsetY = 0;
int g_vitaDisplayZoom = 100;
int g_vitaPortOverlayFullScreen = 0;
int g_vitaTouchEnabled = 0;
int g_vitaConsoleBordersEnabled = 0;
int g_vitaGraphicsQuality = 0;
static bool g_launcherMode = false;

static void resetTouchLayout(VitaSettings* s) {
    const int x[4] = {155, 850, 755, 755};
    const int y[4] = {420, 385, 455, 340};
    for (int i = 0; i < 4; ++i) {
        s->touchControlX[i] = x[i];
        s->touchControlY[i] = y[i];
        s->touchControlScale[i] = 100;
    }
}

static void resetDefaults(VitaSettings* s) {
    s->touchEnabled = false;
    s->ptbrEnabled = false;
    s->widescreenEnabled = false;
    s->musicVolume = 10;
    s->sfxVolume = 10;
    s->masterVolume = 10;
    s->audioDisabled = false;
    s->devMode = false;
    s->showSettings = true;
    g_devMode = false;
    s->vsyncEnabled = true;
    s->displayOffsetX = 0;
    s->displayOffsetY = 0;
    s->displayZoom = 100;
    s->graphicsQuality = 0;
    s->pendingGraphicsQuality = 0;
    s->modIndex = 0;
    s->pendingModIndex = 0;
    s->showRestartPrompt = false;
    s->devRoomNavEnabled = false;
    s->controlEditMode = false;
    s->selectedTouchControl = 0;
    resetTouchLayout(s);
}

static void discoverMods(VitaSettings* s) {
    s->modCount = 0;
    strncpy(s->modNames[0], "Original", MOD_NAME_MAX - 1);
    s->modCount = 1;
    SceUID dd = sceIoDopen(MODS_ROOT);
    if (dd < 0) return;
    SceIoDirent entry;
    while (sceIoDread(dd, &entry) > 0 && s->modCount < MAX_MODS) {
        if (entry.d_name[0] == '.') continue;
        SceIoStat stat;
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s%s", MODS_ROOT, entry.d_name);
        if (sceIoGetstat(fullpath, &stat) >= 0 && SCE_S_ISDIR(stat.st_mode)) {
            size_t nlen = strlen(entry.d_name);
            if (nlen >= MOD_NAME_MAX) nlen = MOD_NAME_MAX - 1;
            memcpy(s->modNames[s->modCount], entry.d_name, nlen);
            s->modNames[s->modCount][nlen] = '\0';
            s->modCount++;
        }
    }
    sceIoDclose(dd);
}

static void applyDisplaySettings(const VitaSettings* s) {
    g_vitaDisplayOffsetX = g_launcherMode ? 0 : s->displayOffsetX;
    g_vitaDisplayOffsetY = g_launcherMode ? 0 : s->displayOffsetY;
    g_vitaDisplayZoom = g_launcherMode ? 100 : s->displayZoom;
    g_vitaTouchEnabled = s->touchEnabled ? 1 : 0;
    g_vitaConsoleBordersEnabled = (!g_launcherMode && s->widescreenEnabled) ? 1 : 0;
    g_vitaGraphicsQuality = s->graphicsQuality;
}

static void saveSettings(const VitaSettings* s) {
    const char* modName = (s->modIndex > 0 && s->modIndex < s->modCount) ? s->modNames[s->modIndex] : "Original";
    char text[1024];
    int length = snprintf(text, sizeof(text), "touch=%d\nroom_nav=%d\ndevmode=%d\nshowsettings=%d\nmod=%s\nwidescreen=%d\nmaster_volume=%d\nmusic_volume=%d\nsfx_volume=%d\naudio_disabled=%d\nvsync=%d\ngraphics=%d\nscreen_profile=3\noffset_x=%d\noffset_y=%d\nzoom=%d\ntouch_stick=%d,%d,%d\ntouch_z=%d,%d,%d\ntouch_x=%d,%d,%d\ntouch_c=%d,%d,%d\nshortcut_skip=%d\n",
                           s->touchEnabled ? 1 : 0, s->devRoomNavEnabled ? 1 : 0,
                           s->devMode ? 1 : 0, s->showSettings ? 1 : 0, modName,
                          s->widescreenEnabled ? 1 : 0, s->masterVolume, s->musicVolume, s->sfxVolume, s->audioDisabled ? 1 : 0, s->vsyncEnabled ? 1 : 0,
                          s->graphicsQuality, s->displayOffsetX, s->displayOffsetY, s->displayZoom,
                          s->touchControlX[0], s->touchControlY[0], s->touchControlScale[0],
                          s->touchControlX[1], s->touchControlY[1], s->touchControlScale[1],
                          s->touchControlX[2], s->touchControlY[2], s->touchControlScale[2],
                          s->touchControlX[3], s->touchControlY[3], s->touchControlScale[3],
                          s->shortcutSkipDialogs ? 1 : 0);
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, length);
        sceIoClose(fd);
    }
}

void VitaSettings_load(VitaSettings* s) {
    memset(s, 0, sizeof(*s));
    resetDefaults(s);
    discoverMods(s);
    bool migrateScreenProfile = false;
    char text[1024] = {0};
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        int read = sceIoRead(fd, text, sizeof(text) - 1);
        sceIoClose(fd);
        if (read > 0) text[read] = '\0';
        char* touchSetting = strstr(text, "touch=");
        s->touchEnabled = touchSetting != NULL && strstr(touchSetting, "touch=1") == touchSetting;
        s->devRoomNavEnabled = strstr(text, "room_nav=1") != NULL;
        s->devMode = strstr(text, "devmode=1") != NULL;
        s->showSettings = strstr(text, "showsettings=0") == NULL;
        char* shortcutSetting = strstr(text, "shortcut_skip=");
        if (shortcutSetting != NULL) s->shortcutSkipDialogs = strstr(shortcutSetting, "shortcut_skip=1") == shortcutSetting;
        s->ptbrEnabled = strstr(text, "mod=PTBR") != NULL;
        s->widescreenEnabled = strstr(text, "widescreen=1") != NULL;
        s->vsyncEnabled = strstr(text, "vsync=0") == NULL;
        s->audioDisabled = strstr(text, "audio_disabled=1") != NULL;
        char* musicVolume = strstr(text, "music_volume=");
        char* sfxVolume = strstr(text, "sfx_volume=");
        char* masterVolume = strstr(text, "master_volume=");
        if (masterVolume != NULL) sscanf(masterVolume + 14, "%d", &s->masterVolume);
        if (musicVolume != NULL) sscanf(musicVolume + 13, "%d", &s->musicVolume);
        if (sfxVolume != NULL) sscanf(sfxVolume + 11, "%d", &s->sfxVolume);
        char* offsetX = strstr(text, "offset_x=");
        char* offsetY = strstr(text, "offset_y=");
        char* zoom = strstr(text, "zoom=");
        char* graphics = strstr(text, "graphics=");
        if (offsetX != NULL) sscanf(offsetX + 9, "%d", &s->displayOffsetX);
        if (offsetY != NULL) sscanf(offsetY + 9, "%d", &s->displayOffsetY);
        if (zoom != NULL) sscanf(zoom + 5, "%d", &s->displayZoom);
        if (graphics != NULL) sscanf(graphics + 9, "%d", &s->graphicsQuality);
        const char* touchKeys[4] = {"touch_stick=", "touch_z=", "touch_x=", "touch_c="};
        for (int i = 0; i < 4; ++i) {
            char* value = strstr(text, touchKeys[i]);
            if (value != NULL) sscanf(value + strlen(touchKeys[i]), "%d,%d,%d",
                                      &s->touchControlX[i], &s->touchControlY[i], &s->touchControlScale[i]);
        }
        char modValue[MOD_NAME_MAX] = {0};
        char* modSetting = strstr(text, "mod=");
        if (modSetting != NULL) sscanf(modSetting + 4, "%31s", modValue);
        if (modValue[0] != '\0') {
            s->modIndex = 0;
            for (int i = 1; i < s->modCount; i++) {
                if (strcmp(s->modNames[i], modValue) == 0) { s->modIndex = i; break; }
            }
        } else if (s->ptbrEnabled) {
            s->modIndex = 0;
            for (int i = 1; i < s->modCount; i++) {
                if (strcmp(s->modNames[i], "PTBR") == 0) { s->modIndex = i; break; }
            }
        }
        s->ptbrEnabled = (s->modIndex > 0);
        if (strstr(text, "screen_profile=3") == NULL) {
            s->displayOffsetX = 0;
            s->displayOffsetY = 0;
            s->displayZoom = 100;
            s->widescreenEnabled = false;
            migrateScreenProfile = true;
        }
    }
    if (s->musicVolume < 0) s->musicVolume = 0;
    if (s->musicVolume > 10) s->musicVolume = 10;
    if (s->sfxVolume < 0) s->sfxVolume = 0;
    if (s->sfxVolume > 10) s->sfxVolume = 10;
    if (s->masterVolume < 0) s->masterVolume = 0;
    if (s->masterVolume > 10) s->masterVolume = 10;
    if (s->displayZoom < 50) s->displayZoom = 50;
    if (s->displayZoom > 160) s->displayZoom = 160;
    if (s->graphicsQuality < 0 || s->graphicsQuality > 2) s->graphicsQuality = 0;
    for (int i = 0; i < 4; ++i) {
        if (s->touchControlX[i] < 40) s->touchControlX[i] = 40;
        if (s->touchControlX[i] > 920) s->touchControlX[i] = 920;
        if (s->touchControlY[i] < 40) s->touchControlY[i] = 40;
        if (s->touchControlY[i] > 504) s->touchControlY[i] = 504;
        if (s->touchControlScale[i] < 60) s->touchControlScale[i] = 60;
        if (s->touchControlScale[i] > 160) s->touchControlScale[i] = 160;
    }
    s->pendingGraphicsQuality = s->graphicsQuality;
    s->pendingModIndex = s->modIndex;
    applyDisplaySettings(s);
    g_devMode = s->devMode;
    if (migrateScreenProfile) saveSettings(s);
}

void VitaSettings_applyAudio(VitaSettings* s, AudioSystem* audio) {
    audio->vtable->setMasterGain(audio, (float)s->masterVolume / 10.0f);
    AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, (float)s->musicVolume / 10.0f, (float)s->sfxVolume / 10.0f);
    AlAudioSystem_setDisabled((AlAudioSystem*)audio, s->audioDisabled);
}

static int32_t snd_index_menumove = -1;
static int32_t snd_index_select = -1;
static DataWin* snd_index_dw = NULL;

static void playSettingSound(AudioSystem* audio, int type) {
    if (!audio || !audio->dw) return;
    if (snd_index_dw != audio->dw) {
        snd_index_menumove = -1;
        snd_index_select = -1;
        snd_index_dw = audio->dw;
        for (uint32_t i = 0; i < audio->dw->sond.count; i++) {
            if (audio->dw->sond.sounds[i].name) {
                if (strcmp(audio->dw->sond.sounds[i].name, "snd_menumove") == 0) snd_index_menumove = i;
                if (strcmp(audio->dw->sond.sounds[i].name, "snd_select") == 0) snd_index_select = i;
            }
        }
    }
    int idx = (type == 0) ? snd_index_menumove : snd_index_select;
    if (idx >= 0) audio->vtable->playSound(audio, idx, 50, false);
}

bool VitaSettings_handleInput(VitaSettings* s, const SceCtrlData* pad, AudioSystem* audio) {
    if (s->inputCooldown > 0) s->inputCooldown--;
    uint32_t pressed = pad->buttons & ~s->previousButtons;
    s->previousButtons = pad->buttons;
    bool requestRestart = false;

    if (s->controlEditMode) {
        if (pressed & SCE_CTRL_LTRIGGER) s->selectedTouchControl = (s->selectedTouchControl + 3) % 4;
        if (pressed & SCE_CTRL_RTRIGGER) s->selectedTouchControl = (s->selectedTouchControl + 1) % 4;
        int i = s->selectedTouchControl;
        int lx = (int)pad->lx - 128, ly = (int)pad->ly - 128, ry = (int)pad->ry - 128;
        if (lx < -40) s->touchControlX[i] -= 2;
        if (lx > 40) s->touchControlX[i] += 2;
        if (ly < -40) s->touchControlY[i] -= 2;
        if (ly > 40) s->touchControlY[i] += 2;
        if (ry < -40) s->touchControlScale[i]++;
        if (ry > 40) s->touchControlScale[i]--;
        if (s->touchControlX[i] < 40) s->touchControlX[i] = 40;
        if (s->touchControlX[i] > 920) s->touchControlX[i] = 920;
        if (s->touchControlY[i] < 40) s->touchControlY[i] = 40;
        if (s->touchControlY[i] > 504) s->touchControlY[i] = 504;
        if (s->touchControlScale[i] < 60) s->touchControlScale[i] = 60;
        if (s->touchControlScale[i] > 160) s->touchControlScale[i] = 160;
        if (pressed & SCE_CTRL_TRIANGLE) resetTouchLayout(s);
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SELECT)) {
            s->controlEditMode = false;
            saveSettings(s);
            playSettingSound(audio, 1);
        }
        return false;
    }

    if (s->showRestartPrompt) {
        if (pressed & SCE_CTRL_LEFT) s->promptSelection = 0;
        else if (pressed & SCE_CTRL_RIGHT) s->promptSelection = 1;
        else if (pressed & SCE_CTRL_CROSS) {
            if (s->promptSelection == 0) {
                // Apply pending changes
                s->graphicsQuality = s->pendingGraphicsQuality;
                s->modIndex = s->pendingModIndex;
                s->ptbrEnabled = (s->modIndex > 0);
                applyDisplaySettings(s);
                saveSettings(s);
                s->showRestartPrompt = false;
                s->open = false;
                requestRestart = true;
            } else {
                // Cancel pending changes
                s->pendingGraphicsQuality = s->graphicsQuality;
                s->pendingModIndex = s->modIndex;
                s->showRestartPrompt = false;
            }
        } else if (pressed & SCE_CTRL_CIRCLE) {
            // Cancel pending changes
            s->pendingGraphicsQuality = s->graphicsQuality;
            s->pendingModIndex = s->modIndex;
            s->showRestartPrompt = false;
        }
        return requestRestart;
    }

    if (s->confirmChapterSelect) {
        if (pressed & SCE_CTRL_CROSS) {
            s->confirmChapterSelect = false;
            s->returnToChapterSelect = true;
            s->open = false;
            requestRestart = true;
        } else if (pressed & SCE_CTRL_CIRCLE) {
            s->confirmChapterSelect = false;
            s->inputCooldown = 2;
        }
        return requestRestart;
    }

    if (s->adjustMode) {
        int lx = (int)pad->lx - 128;
        int ly = (int)pad->ly - 128;
        int ry = (int)pad->ry - 128;
        if (lx < -48) s->displayOffsetX -= 2;
        if (lx > 48) s->displayOffsetX += 2;
        if (ly < -48) s->displayOffsetY -= 2;
        if (ly > 48) s->displayOffsetY += 2;
        if (ry < -48 && s->displayZoom < 160) s->displayZoom++;
        if (ry > 48 && s->displayZoom > 50) s->displayZoom--;
        applyDisplaySettings(s);
        if (pressed & SCE_CTRL_CIRCLE) {
            s->displayOffsetX = 0;
            s->displayOffsetY = 0;
            s->displayZoom = 100;
            applyDisplaySettings(s);
        }
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SELECT)) {
            s->adjustMode = false;
            saveSettings(s);
        }
        return false;
    }

    if (pressed & SCE_CTRL_SELECT) {
        if (!s->showSettings && !s->open) return false;
        if (!s->open && s->debugDevEnabled) {
            s->debugDevEnabled = false;
            s->debugDevChanged = true;
        }
        if (s->open && s->restartOnClose) requestRestart = true;
        s->open = !s->open;
        playSettingSound(audio, s->open ? 1 : 0);
    }
    if (!s->open) return requestRestart;

    int numCategories = s->devMode ? 5 : 4;
    if (pressed & SCE_CTRL_LTRIGGER) {
        s->category = (s->category + numCategories - 1) % numCategories;
        s->selected = 0;
        playSettingSound(audio, 0);
    }
    if (pressed & SCE_CTRL_RTRIGGER) {
        s->category = (s->category + 1) % numCategories;
        s->selected = 0;
        playSettingSound(audio, 0);
    }
    int itemCount = settingsItemCount(s->category);
    if (pressed & SCE_CTRL_UP) { s->selected = (s->selected + itemCount - 1) % itemCount; playSettingSound(audio, 0); }
    if (pressed & SCE_CTRL_DOWN) { s->selected = (s->selected + 1) % itemCount; playSettingSound(audio, 0); }

    bool activate = (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) != 0;
    if (activate) {
        if (pressed & SCE_CTRL_CROSS) playSettingSound(audio, 1);
        else playSettingSound(audio, 0);
        
        int logicalCategory = s->category;
        if (!s->devMode) logicalCategory += 1;

        if (logicalCategory == 0 && s->selected == 0) {
            s->devRoomNavEnabled = !s->devRoomNavEnabled;
            saveSettings(s);
        } else if (logicalCategory == 0 && s->selected == 1) {
            s->debugDevEnabled = !s->debugDevEnabled;
            s->debugDevChanged = true;
            saveSettings(s);
        } else if (logicalCategory == 1 && s->selected == 0) {
            s->touchEnabled = !s->touchEnabled;
            saveSettings(s);
        } else if (logicalCategory == 1 && s->selected == 1) {
            s->controlEditMode = true;
            s->touchEnabled = true;
            s->selectedTouchControl = 0;
        } else if (logicalCategory == 1 && s->selected == 2) {
            s->shortcutSkipDialogs = !s->shortcutSkipDialogs;
            saveSettings(s);
        } else if (logicalCategory == 2 && s->selected == 0) {
            s->widescreenEnabled = !s->widescreenEnabled;
            if (s->widescreenEnabled && !VitaBorders_filesAvailable()) s->borderWarningFrames = 240;
            s->displayOffsetX = 0;
            s->displayOffsetY = 0;
            s->displayZoom = s->widescreenEnabled ? 90 : 100;
            applyDisplaySettings(s);
        } else if (logicalCategory == 2 && s->selected == 1) {
            s->adjustMode = true;
            s->open = false;
        } else if (logicalCategory == 2 && s->selected == 2) {
            if (pressed & SCE_CTRL_LEFT) {
                s->pendingGraphicsQuality--;
                if (s->pendingGraphicsQuality < 0) s->pendingGraphicsQuality = 2;
            } else if (pressed & SCE_CTRL_RIGHT) {
                s->pendingGraphicsQuality++;
                if (s->pendingGraphicsQuality > 2) s->pendingGraphicsQuality = 0;
            } else if (pressed & SCE_CTRL_CROSS) {
                if (s->pendingGraphicsQuality != s->graphicsQuality) {
                    s->showRestartPrompt = true;
                    s->promptSelection = 0;
                }
            }
        } else if (logicalCategory == 3 && s->selected < 3) {
            int* value = s->selected == 0 ? &s->masterVolume :
                         (s->selected == 1 ? &s->sfxVolume : &s->musicVolume);
            if (pressed & SCE_CTRL_LEFT) (*value)--;
            else if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_CROSS)) (*value)++;
            if (*value < 0) *value = 0;
            if (*value > 10) *value = 10;
            VitaSettings_applyAudio(s, audio);
            saveSettings(s);
        } else if (logicalCategory == 3 && s->selected == 3) {
            s->audioDisabled = !s->audioDisabled;
            VitaSettings_applyAudio(s, audio);
            saveSettings(s);
        } else if (logicalCategory == 4 && s->selected == 0) {
            s->vsyncEnabled = !s->vsyncEnabled;
            saveSettings(s);
        } else if (logicalCategory == 4 && s->selected == 1) {
            if (pressed & SCE_CTRL_LEFT) {
                s->pendingModIndex--;
                if (s->pendingModIndex < 0) s->pendingModIndex = s->modCount - 1;
            } else if (pressed & SCE_CTRL_RIGHT) {
                s->pendingModIndex++;
                if (s->pendingModIndex >= s->modCount) s->pendingModIndex = 0;
            } else if (pressed & SCE_CTRL_CROSS) {
                if (s->pendingModIndex != s->modIndex) {
                    s->showRestartPrompt = true;
                    s->promptSelection = 0;
                }
            }
        } else if (logicalCategory == 4 && s->selected == 2) {
            s->confirmChapterSelect = true;
        } else if (logicalCategory == 4 && s->selected == 3) {
            resetDefaults(s);
            applyDisplaySettings(s);
            VitaSettings_applyAudio(s, audio);
            saveSettings(s);
        }
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        playSettingSound(audio, 0);
        s->open = false;
        s->inputCooldown = 2;
        requestRestart = s->restartOnClose;
    }
    return requestRestart;
}

static void drawLabel(Renderer* r, const char* text, float x, float y, uint32_t color, float scale) {
    r->vtable->drawTextColor(r, text, x, y, scale, scale, 0.0f,
                             color, color, color, color, 1.0f, -1.0f);
}

static int findSettingsFont(Renderer* r) {
    for (uint32_t i = 0; i < r->dataWin->font.count; ++i) {
        Font* font = &r->dataWin->font.fonts[i];
        if (font->present && font->name != nullptr && strcmp(font->name, "fnt_main") == 0) return (int)i;
    }
    return r->dataWin->font.count > 0 ? 0 : -1;
}

static void drawControl(Renderer* r, const char* name, float centerX, float centerY, float targetSize, float alpha);

static void drawCenteredText(Renderer* r, const char* text, float centerX, float y,
                             float scale, uint32_t color) {
    int oldHalign = r->drawHalign;
    r->drawHalign = 1;
    r->vtable->drawTextColor(r, text, centerX, y, scale, scale, 0.0f,
                             color, color, color, color, 1.0f, -1.0f);
    r->drawHalign = oldHalign;
}

static void drawRightText(Renderer* r, const char* text, float rightX, float y,
                          float scale, uint32_t color) {
    int oldHalign = r->drawHalign;
    r->drawHalign = 2;
    r->vtable->drawTextColor(r, text, rightX, y, scale, scale, 0.0f,
                             color, color, color, color, 1.0f, -1.0f);
    r->drawHalign = oldHalign;
}

static Sprite* findSettingsSprite(Renderer* r, const char* name) {
    for (uint32_t i = 0; i < r->dataWin->sprt.count; ++i) {
        Sprite* sprite = &r->dataWin->sprt.sprites[i];
        if (sprite->present && sprite->name != nullptr && strcmp(sprite->name, name) == 0 &&
            sprite->textureCount > 0 && sprite->tpagIndices[0] >= 0) return sprite;
    }
    return nullptr;
}

static void drawDialogBorder(Renderer* r, float left, float top, float right, float bottom) {
    Sprite* corner = findSettingsSprite(r, "spr_textbox_topleft");
    Sprite* edgeTop = findSettingsSprite(r, "spr_textbox_top");
    Sprite* edgeLeft = findSettingsSprite(r, "spr_textbox_left");
    if (corner == nullptr || edgeTop == nullptr || edgeLeft == nullptr ||
        corner->width <= 0 || corner->height <= 0 || edgeTop->width <= 0 || edgeLeft->height <= 0) {
        r->vtable->drawRectangle(r, left, top, right, bottom, 0xFFFFFF, 1.0f, true);
        r->vtable->drawRectangle(r, left + 6, top + 6, right - 6, bottom - 6, 0xFFFFFF, 1.0f, true);
        return;
    }

    float s_scale = 2.0f;
    float cw = (float) corner->width * s_scale;
    float ch = (float) corner->height * s_scale;
    float horizontalScale = (right - left - cw * 2.0f) / (float) edgeTop->width;
    float verticalScale = (bottom - top - ch * 2.0f) / (float) edgeLeft->height;
    r->vtable->drawSprite(r, corner->tpagIndices[0], left, top, 0, 0, s_scale, s_scale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, corner->tpagIndices[0], right, top, 0, 0, -s_scale, s_scale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, corner->tpagIndices[0], left, bottom, 0, 0, s_scale, -s_scale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, corner->tpagIndices[0], right, bottom, 0, 0, -s_scale, -s_scale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeTop->tpagIndices[0], left + cw, top, 0, 0, horizontalScale, s_scale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeTop->tpagIndices[0], left + cw, bottom, 0, 0, horizontalScale, -s_scale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeLeft->tpagIndices[0], left, top + ch, 0, 0, s_scale, verticalScale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeLeft->tpagIndices[0], right, top + ch, 0, 0, -s_scale, verticalScale, 0, 0xFFFFFF, 1.0f);
}


void VitaSettings_draw(VitaSettings* s, Renderer* r) {
    if (!s->open) return;
    if (s->borderWarningFrames > 0) s->borderWarningFrames--;
    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r);

    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    const float panelLeft = 108.0f, panelTop = 48.0f, panelRight = 852.0f, panelBottom = 486.0f;
    r->vtable->drawRectangle(r, panelLeft, panelTop, panelRight, panelBottom, 0x000000, 0.96f, false);
    drawDialogBorder(r, panelLeft, panelTop, panelRight, panelBottom);
    drawCenteredText(r, s->ptbrEnabled ? "CONFIGURACOES" : "GAME SETTINGS", 480, 79, 1.6f, 0xFFFFFF);

    if (s->controlEditMode) {
        const char* namesPt[4] = {"DIRECIONAL", "CONFIRMAR (Z)", "CANCELAR (X)", "MENU (C)"};
        const char* namesEn[4] = {"D-PAD", "CONFIRM (Z)", "CANCEL (X)", "MENU (C)"};
        const char** names = s->ptbrEnabled ? namesPt : namesEn;
        drawCenteredText(r, s->ptbrEnabled ? "EDITAR CONTROLES TOUCH" : "EDIT TOUCH CONTROLS", 480, 120, 1.35f, 0x00FFFF);
        drawCenteredText(r, names[s->selectedTouchControl], 480, 158, 1.15f, 0xFFFFFF);
        for (int i = 0; i < 4; ++i) {
            float size = (i == 0 ? 170.0f : 82.0f) * ((float)s->touchControlScale[i] / 100.0f);
            const char* sprite = i == 0 ? "spr_joybase" : (i == 1 ? "spr_control_zkey" : (i == 2 ? "spr_control_xkey" : "spr_control_ckey"));
            drawControl(r, sprite, (float)s->touchControlX[i], (float)s->touchControlY[i], size, i == s->selectedTouchControl ? 0.95f : 0.5f);
            if (i == s->selectedTouchControl)
                r->vtable->drawRectangle(r, s->touchControlX[i] - size * 0.55f, s->touchControlY[i] - size * 0.55f,
                                         s->touchControlX[i] + size * 0.55f, s->touchControlY[i] + size * 0.55f,
                                         0x00FFFF, 1.0f, true);
        }
        drawCenteredText(r, s->ptbrEnabled ? "TOQUE/ANALOGICO ESQ: MOVER   ANALOGICO DIR: TAMANHO" : "TOUCH/LEFT STICK: MOVE   RIGHT STICK: SIZE", 480, 436, 0.9f, 0xA0A0A0);
        drawCenteredText(r, s->ptbrEnabled ? "L/R: ITEM   X: SALVAR   TRIANGULO: RESET" : "L/R: ITEM   X: SAVE   TRIANGLE: RESET", 480, 462, 0.9f, 0xA0A0A0);
        r->vtable->endGUI(r);
        g_vitaPortOverlayFullScreen = 0;
        r->drawFont = oldFont;
        return;
    }

    const char* touch = s->touchEnabled ? (s->ptbrEnabled ? "Ligado" : "On") : (s->ptbrEnabled ? "Desligado" : "Off");
    const char* mod = (s->pendingModIndex >= 0 && s->pendingModIndex < s->modCount) ? s->modNames[s->pendingModIndex] : "Original";
    const char* screen = s->widescreenEnabled ? (s->ptbrEnabled ? "Ligadas" : "On") : (s->ptbrEnabled ? "Desligadas" : "Off");
    char masterVolume[32], sfxVolume[32], musicVolume[32];
    snprintf(masterVolume, sizeof(masterVolume), "%d%%", s->masterVolume * 10);
    snprintf(sfxVolume, sizeof(sfxVolume), "%d%%", s->sfxVolume * 10);
    snprintf(musicVolume, sizeof(musicVolume), "%d%%", s->musicVolume * 10);
    const char* categoriesPt[SETTINGS_CATEGORIES] = {"DEV", "CONTROLES", "TELA", "AUDIO", "SISTEMA"};
    const char* categoriesEn[SETTINGS_CATEGORIES] = {"DEV", "CONTROLS", "SCREEN", "AUDIO", "SYSTEM"};
    const char** categories = s->ptbrEnabled ? categoriesPt : categoriesEn;
    
    int numCategories = s->devMode ? 5 : 4;
    int logicalSelectedCategory = s->category + (!s->devMode ? 1 : 0);
    
    float spacing = numCategories == 5 ? 140.0f : 180.0f;
    float startX = 480.0f - (spacing * (numCategories - 1)) / 2.0f;

    for (int i = 0; i < numCategories; ++i) {
        int logicalIndex = i + (!s->devMode ? 1 : 0);
        float centerX = startX + i * spacing;
        if (logicalIndex == logicalSelectedCategory) {
            r->vtable->drawRectangle(r, centerX - 65, 126, centerX + 65, 156, 0x181818, 1.0f, false);
            r->vtable->drawRectangle(r, centerX - 65, 126, centerX + 65, 156, 0x00FFFF, 0.72f, true);
        }
        drawCenteredText(r, categories[logicalIndex], centerX, 132, 1.15f,
                         logicalIndex == logicalSelectedCategory ? 0x00FFFF : 0x808080);
    }
    char adjustment[64];
    snprintf(adjustment, sizeof(adjustment), "%d,%d  %d%%", s->displayOffsetX, s->displayOffsetY, s->displayZoom);
    const char* vsync = s->vsyncEnabled ? (s->ptbrEnabled ? "Ligado" : "On") : (s->ptbrEnabled ? "Desligado" : "Off");
    const char* graphics = s->pendingGraphicsQuality == 0 ? "Original" :
                           (s->pendingGraphicsQuality == 1 ? (s->ptbrEnabled ? "Medio" : "Medium") :
                                                      (s->ptbrEnabled ? "Baixo" : "Low"));
    const char* labelsPt[SETTINGS_CATEGORIES][4] = {
        {"Navegador de salas", "Debug DEV", "", ""},
        {"Controles touch", "Editar controles", "Pular falas?", ""}, 
        {"Bordas de console", "Ajustar tela", "Graficos", ""},
        {"Volume mestre", "Efeitos sonoros", "Musica", "Desabilitar audio"}, 
        {"VSync / 30 FPS", "Mod / Idioma", "Voltar aos capitulos", ""}
    };
    const char* labelsEn[SETTINGS_CATEGORIES][4] = {
        {"Room navigator", "Debug DEV", "", ""},
        {"Touch controls", "Edit controls", "Skip dialogs?", ""}, 
        {"Console borders", "Adjust screen", "Graphics", ""},
        {"Master volume", "Sound effects", "Music", "Disable audio"}, 
        {"VSync / 30 FPS", "Mod / Language", "Chapter Select", ""}
    };
    const char* values[SETTINGS_CATEGORIES][4] = {
        {s->devRoomNavEnabled ? "On" : "Off", s->debugDevEnabled ? "On" : "Off", "", ""},
        {touch, s->controlEditMode ? (s->ptbrEnabled ? "Aberto" : "Open") : ">", s->shortcutSkipDialogs ? "On" : "Off", ""}, 
        {screen, adjustment, graphics, ""}, 
        {masterVolume, sfxVolume, musicVolume, s->audioDisabled ? (s->ptbrEnabled ? "Ligado" : "On") : (s->ptbrEnabled ? "Desligado" : "Off")}, 
        {vsync, mod, "", ""}
    };
    int visibleItems = settingsItemCount(s->category);
    for (int i = 0; i < visibleItems; ++i) {
        float y = visibleItems == 4 ? 200.0f + i * 42.0f : (visibleItems == 3 ? 206.0f + i * 50.0f : 214.0f + i * 64.0f);
        uint32_t textColor = i == s->selected ? 0x00FFFF : 0xFFFFFF;
        drawLabel(r, i == s->selected ? ">" : " ", 214, y, textColor, 1.84f);
        const char* label = (i == 3 && logicalSelectedCategory == 4)
            ? (s->ptbrEnabled ? "Restaurar padrao" : "Restore defaults")
            : (s->ptbrEnabled ? labelsPt[logicalSelectedCategory][i] : labelsEn[logicalSelectedCategory][i]);
        drawLabel(r, label, 252, y, textColor, 1.84f);
        if (logicalSelectedCategory == 3 && i < 3) {
            int sliderValue = i == 0 ? s->masterVolume : (i == 1 ? s->sfxVolume : s->musicVolume);
            float sliderLeft = 570.0f, sliderRight = 698.0f, sliderY = y + 10.0f;
            r->vtable->drawRectangle(r, sliderLeft, sliderY, sliderRight, sliderY + 8.0f,
                                     0x505050, 1.0f, false);
            r->vtable->drawRectangle(r, sliderLeft, sliderY,
                                     sliderLeft + (sliderRight - sliderLeft) * ((float)sliderValue / 10.0f),
                                     sliderY + 8.0f, i == s->selected ? 0x00FFFF : 0xFFFFFF, 1.0f, false);
            drawRightText(r, values[logicalSelectedCategory][i], 742, y, 1.25f,
                          i == s->selected ? 0x00FFFF : 0xFFFFFF);
        } else {
            drawRightText(r, values[logicalSelectedCategory][i], 742, y, 1.6f,
                          i == s->selected ? 0x00FFFF : 0xFFFFFF);
        }
    }
    drawCenteredText(r,
        s->ptbrEnabled ? "L/R: CATEGORIA    X: ALTERAR    O: FECHAR" : "L/R: CATEGORY    X: CHANGE    O: CLOSE",
        480, 426, 1.1f, 0x808080);

    if (s->borderWarningFrames > 0) {
        r->vtable->drawRectangle(r, 176, 354, 784, 414, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 354, 784, 414);
        drawCenteredText(r,
            s->ptbrEnabled ? "ARQUIVOS DE BORDA NAO ENCONTRADOS." : "BORDER FILES WERE NOT FOUND.",
            480, 372, 1.05f, 0xFFFF00);
    }

    if (s->confirmChapterSelect) {
        r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 174, 784, 370);
        drawCenteredText(r, s->ptbrEnabled ? "VOLTAR AOS CAPITULOS?" : "RETURN TO CHAPTER SELECT?",
                         480, 208, 1.35f, 0xFFFFFF);
        drawCenteredText(r, s->ptbrEnabled ? "O PROGRESSO NAO SALVO SERA PERDIDO." : "UNSAVED PROGRESS WILL BE LOST.",
                         480, 260, 1.05f, 0xFFFF00);
        drawCenteredText(r, s->ptbrEnabled ? "X: CONFIRMAR       O: CANCELAR" : "X: CONFIRM       O: CANCEL",
                         480, 316, 1.05f, 0xA0A0A0);
    }
    
    if (s->showRestartPrompt) {
        r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 174, 784, 370);
        drawCenteredText(r, s->ptbrEnabled ? "REINICIAR PARA APLICAR AS MUDANCAS?" : "RESTART TO APPLY CHANGES?",
                         480, 208, 1.25f, 0xFFFFFF);
                         
        drawCenteredText(r, s->ptbrEnabled ? "Sim" : "Yes", 380, 280, 1.3f, s->promptSelection == 0 ? 0x00FFFF : 0x808080);
        drawCenteredText(r, s->ptbrEnabled ? "Nao" : "No", 580, 280, 1.3f, s->promptSelection == 1 ? 0x00FFFF : 0x808080);
        
        drawCenteredText(r, s->ptbrEnabled ? "L/R: SELECIONAR    X: CONFIRMAR" : "L/R: SELECT    X: CONFIRM",
                         480, 330, 1.0f, 0xA0A0A0);
    }
    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
    r->drawFont = oldFont;
}

void VitaSettings_drawDevOverlay(VitaSettings* s, Renderer* r,
                                 const char* room, float fps, uint64_t stepUs,
                                 uint64_t audioUs, uint64_t renderUs,
                                 uint64_t gpuBytes, uint32_t evictions,
                                 uint32_t deferred, uint32_t ramHits,
                                 const char* devTargetRoom, int32_t devTargetIndex) {
    if (!s->devMode || (!s->debugDevEnabled && !s->devRoomNavEnabled) || s->open) return;
    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r);
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    if (s->devRoomNavEnabled) {
        r->vtable->drawRectangle(r, 8, 466, 952, 538, 0x000000, 0.90f, false);
        char navLine[192];
        snprintf(navLine, sizeof(navLine), "CURRENT ROOM  %s", room != NULL ? room : "<none>");
        r->vtable->drawTextColor(r, navLine, 24, 476, 2.0f, 2.0f, 0,
                                 0xFFFF00, 0xFFFF00, 0xFFFF00, 0xFFFF00, 1, -1);
    }
    if (!s->debugDevEnabled) {
        r->vtable->endGUI(r);
        r->drawFont = oldFont;
        return;
    }
    r->vtable->drawRectangle(r, 8, 8, 940, 238, 0x000000, 0.82f, false);
    char line[192];
    snprintf(line, sizeof(line), "DEV  ROOM: %s", room != NULL ? room : "<null>");
    r->vtable->drawTextColor(r, line, 18, 20, 1.85f, 1.85f, 0, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1, -1);
    snprintf(line, sizeof(line), "FPS %.1f  STEP %lluus  AUDIO %lluus  RENDER %lluus",
             fps, (unsigned long long)stepUs, (unsigned long long)audioUs, (unsigned long long)renderUs);
    r->vtable->drawTextColor(r, line, 18, 74, 1.45f, 1.45f, 0, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1, -1);
    snprintf(line, sizeof(line), "GPU CACHE %.1f MiB  EVICT %u  DEFER %u  RAM HIT %u",
             (double)gpuBytes / 1048576.0, evictions, deferred, ramHits);
    r->vtable->drawTextColor(r, line, 18, 128, 1.45f, 1.45f, 0, 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 1, -1);
    snprintf(line, sizeof(line), "L + UP/DOWN: ROOM  X: GO  [%d] %s",
             devTargetIndex, devTargetRoom != NULL ? devTargetRoom : "<none>");
    r->vtable->drawTextColor(r, line, 18, 182, 1.2f, 1.2f, 0, 0xFFFF00, 0xFFFF00, 0xFFFF00, 0xFFFF00, 1, -1);
    r->vtable->endGUI(r);
    r->drawFont = oldFont;
}

static Sprite* findSprite(Renderer* r, const char* name) {
    for (uint32_t i = 0; i < r->dataWin->sprt.count; ++i) {
        Sprite* sprite = &r->dataWin->sprt.sprites[i];
        if (sprite->present && sprite->name != nullptr && strcmp(sprite->name, name) == 0) return sprite;
    }
    return nullptr;
}

static bool drawGameControl(Renderer* r, const char* name, float centerX, float centerY, float targetSize, float alpha) {
    Sprite* sprite = findSprite(r, name);
    if (sprite == nullptr || sprite->textureCount == 0 || sprite->tpagIndices[0] < 0 || sprite->width == 0 || sprite->height == 0) return false;
    float largest = sprite->width > sprite->height ? (float)sprite->width : (float)sprite->height;
    float scale = targetSize / largest;
    r->vtable->drawSprite(r, sprite->tpagIndices[0], centerX, centerY,
                          (float)sprite->width * 0.5f, (float)sprite->height * 0.5f,
                          scale, scale, 0.0f, 0xFFFFFF, alpha);
    return true;
}

typedef struct {
    const char* name;
    const char* path;
    GLuint texture;
    int width;
    int height;
} BundledControl;

typedef struct {
    GLfloat u, v;
    GLfloat r, g, b, a;
    GLfloat x, y;
} TouchControlVertex;

static void touchControlLog(const char* phase, const char* name) {
    char line[128];
    int length = snprintf(line, sizeof(line), "TOUCH=%s control=%s\n", phase, name);
    SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, (SceSize)length);
        sceIoClose(fd);
    }
}

static void drawTouchControlQuad(const TouchControlVertex vertices[4]) {
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(TouchControlVertex), &vertices[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(TouchControlVertex), &vertices[0].r);
    glVertexPointer(2, GL_FLOAT, sizeof(TouchControlVertex), &vertices[0].x);
    glUseProgram(0);
#ifdef __vita__
    glBindVertexArray(0);
#endif
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static BundledControl bundledControls[] = {
    {"spr_joybase", "app0:assets/touch/spr_joybase_frame0.png", 0, 0, 0},
    {"spr_joystick", "app0:assets/touch/spr_joystick_frame0.png", 0, 0, 0},
    {"spr_control_zkey", "app0:assets/touch/spr_control_zkey_frame0.png", 0, 0, 0},
    {"spr_control_xkey", "app0:assets/touch/spr_control_xkey_frame0.png", 0, 0, 0},
    {"spr_control_ckey", "app0:assets/touch/spr_control_ckey_frame0.png", 0, 0, 0},
};

static bool drawBundledControl(const char* name, float centerX, float centerY, float targetSize, float alpha) {
    BundledControl* control = nullptr;
    for (unsigned i = 0; i < sizeof(bundledControls) / sizeof(bundledControls[0]); ++i) {
        if (strcmp(bundledControls[i].name, name) == 0) { control = &bundledControls[i]; break; }
    }
    if (control == nullptr) return false;
    if (control->texture == 0) {
        touchControlLog("asset_load_begin", name);
        int channels = 0;
        SceUID fd = sceIoOpen(control->path, SCE_O_RDONLY, 0);
        if (fd < 0) return false;
        SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);
        if (fileSize <= 0) { sceIoClose(fd); return false; }
        unsigned char* fileData = (unsigned char*)malloc((size_t)fileSize);
        if (fileData == nullptr) { sceIoClose(fd); return false; }
        int bytesRead = sceIoRead(fd, fileData, (unsigned int)fileSize);
        sceIoClose(fd);
        if (bytesRead != fileSize) { free(fileData); return false; }
        unsigned char* pixels = stbi_load_from_memory(fileData, bytesRead, &control->width, &control->height, &channels, 4);
        free(fileData);
        if (pixels == nullptr) return false;
        glGenTextures(1, &control->texture);
        glBindTexture(GL_TEXTURE_2D, control->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, control->width, control->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        stbi_image_free(pixels);
        touchControlLog("asset_upload_complete", name);
    }
    float largest = control->width > control->height ? (float)control->width : (float)control->height;
    float scale = targetSize / largest;
    float halfW = control->width * scale * 0.5f;
    float halfH = control->height * scale * 0.5f;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, control->texture);
    const TouchControlVertex vertices[4] = {
        {0.0f, 0.0f, 1.0f, 1.0f, 1.0f, alpha, centerX - halfW, centerY - halfH},
        {1.0f, 0.0f, 1.0f, 1.0f, 1.0f, alpha, centerX + halfW, centerY - halfH},
        {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha, centerX + halfW, centerY + halfH},
        {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, alpha, centerX - halfW, centerY + halfH},
    };
    drawTouchControlQuad(vertices);
    return true;
}

static void drawControl(Renderer* r, const char* name, float centerX, float centerY, float targetSize, float alpha) {
    if (!drawGameControl(r, name, centerX, centerY, targetSize, alpha)) {
        drawBundledControl(name, centerX, centerY, targetSize, alpha);
    }
}

void VitaSettings_drawTouchControls(VitaSettings* s, Renderer* r) {
    static bool firstOverlay = true;
    if (!s->touchEnabled || s->open || s->adjustMode) return;
    if (firstOverlay) touchControlLog("overlay_begin", "all");
    int oldFont = r->drawFont;
    if (r->drawFont < 0 || (uint32_t)r->drawFont >= r->dataWin->font.count) r->drawFont = 0;
    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    float stickScale = (float)s->touchControlScale[0] / 100.0f;
    drawControl(r, "spr_joybase", s->touchControlX[0], s->touchControlY[0], 205 * stickScale, 0.48f);
    drawControl(r, "spr_joystick", s->touchControlX[0] + s->visualStickX * 38.0f * stickScale, s->touchControlY[0] + s->visualStickY * 38.0f * stickScale,
                (125 + (s->visualStickX != 0.0f || s->visualStickY != 0.0f ? 8 : 0)) * stickScale, 0.68f);
    drawControl(r, "spr_control_zkey", s->touchControlX[1], s->touchControlY[1] + (s->visualConfirm ? 4 : 0), (s->visualConfirm ? 108 : 92) * s->touchControlScale[1] / 100.0f, s->visualConfirm ? 0.92f : 0.58f);
    drawControl(r, "spr_control_xkey", s->touchControlX[2], s->touchControlY[2] + (s->visualCancel ? 4 : 0), (s->visualCancel ? 108 : 92) * s->touchControlScale[2] / 100.0f, s->visualCancel ? 0.92f : 0.58f);
    drawControl(r, "spr_control_ckey", s->touchControlX[3], s->touchControlY[3] + (s->visualMenu ? 4 : 0), (s->visualMenu ? 108 : 92) * s->touchControlScale[3] / 100.0f, s->visualMenu ? 0.92f : 0.58f);
    r->vtable->endGUI(r);
    if (firstOverlay) {
        touchControlLog("overlay_complete", "all");
        firstOverlay = false;
    }
    g_vitaPortOverlayFullScreen = 0;
    r->drawFont = oldFont;
}

void VitaSettings_drawCalibration(VitaSettings* s, Renderer* r) {
    if (!s->adjustMode) return;
    int gameW = r->runner->renderGameW;
    int gameH = r->runner->renderGameH;
    if (gameW <= 0 || gameH <= 0) return;
    int width, height;
    if ((gameW * 544) / gameH < 960) {
        width = (gameW * 544) / gameH;
        height = 544;
    } else {
        width = 960;
        height = (gameH * 960) / gameW;
    }
    width = width * g_vitaDisplayZoom / 100;
    height = height * g_vitaDisplayZoom / 100;
    int x = (960 - width) / 2 + g_vitaDisplayOffsetX;
    int y = (544 - height) / 2 + g_vitaDisplayOffsetY;
    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    r->vtable->drawRectangle(r, (float)x, (float)y, (float)(x + width - 1), (float)(y + height - 1), 0xFFFFFF, 1.0f, true);
    drawLabel(r, s->ptbrEnabled ? "ESQ: MOVER  DIR: ZOOM  X: SALVAR  O: RESET" : "LEFT: MOVE  RIGHT: ZOOM  X: SAVE  O: RESET", 125, 18, 0xFFFFFF, 1.6f);
    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
}

void VitaSettings_setTouchVisuals(VitaSettings* s, float stickX, float stickY,
                                  bool confirm, bool cancel, bool menu) {
    if (stickX < -1.0f) stickX = -1.0f;
    if (stickX > 1.0f) stickX = 1.0f;
    if (stickY < -1.0f) stickY = -1.0f;
    if (stickY > 1.0f) stickY = 1.0f;
    s->visualStickX += (stickX - s->visualStickX) * 0.82f;
    s->visualStickY += (stickY - s->visualStickY) * 0.82f;
    if (s->visualStickX > -0.02f && s->visualStickX < 0.02f) s->visualStickX = 0.0f;
    if (s->visualStickY > -0.02f && s->visualStickY < 0.02f) s->visualStickY = 0.0f;
    s->visualConfirm = confirm;
    s->visualCancel = cancel;
    s->visualMenu = menu;
}

void VitaSettings_setLauncherMode(bool launcherMode) {
    g_launcherMode = launcherMode;
    if (launcherMode) {
        g_vitaDisplayOffsetX = 0;
        g_vitaDisplayOffsetY = 0;
        g_vitaDisplayZoom = 100;
    }
}
