#include "vita_settings.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "stb_image.h"
#include <AL/al.h>
#include <AL/alc.h>

#include "runner.h"
#include "audio/openal/al_audio_system.h"
#include "vita_borders.h"
#include "text_utils.h"
#include "gl/gl_renderer.h"

extern bool g_vitaModernGlActive;
static Renderer* activeSettingsRenderer = NULL;

#define NATIVE_LANGUAGE_CONFIG "ux0:data/deltarune_saves/true_config.ini"

static void drawTextAndIconsExt(Renderer* r, const char* fmt, float x, float y, float scale, uint32_t color, bool center);
static void drawCenteredText(Renderer* r, const char* text, float centerX, float y,
                             float scale, uint32_t color);
static bool drawUIIconExt(Renderer* r, const char* name, float centerX, float centerY, float w, float h, float alpha, bool flipX, bool flipY);
static void drawQueuedUIIconPass(Renderer* r);
static bool drawBundledControl(const char* name, float centerX, float centerY, float targetSize, float alpha);
static bool drawLanguageIcon(const char* language, float centerX, float centerY, float alpha);
static void drawSettingsControlFooter(Renderer* r, VitaSettings* s);
static void drawConfirmControlFooter(Renderer* r, VitaSettings* s);
static void drawApplyCancelFooter(Renderer* r, VitaSettings* s);
static void drawRestartControlFooter(Renderer* r, VitaSettings* s);
static void drawControlEditorFooter(Renderer* r, VitaSettings* s);

static const char* settingsText(const VitaSettings* s, const char* en, const char* pt, const char* es) {
    if (strcmp(s->activeLanguage, "Portuguese-BR") == 0) return pt;
    if (strcmp(s->activeLanguage, "Spanish") == 0) return es;
    return en;
}

static const char* languageDisplayName(const char* id) {
    if (id == NULL) return "English";
    if (strcmp(id, "English") == 0) return "English";
    // The settings overlay is drawn with the current chapter's embedded
    // GameMaker bitmap font.  The original Latin font has no kana/kanji or
    // Cyrillic glyphs, so keep these two selector labels representable.  The
    // language icon still identifies the native language visually.
    if (strcmp(id, "Japanese") == 0) return "Nihongo";
    if (strcmp(id, "Portuguese-BR") == 0) return "Português-BR";
    if (strcmp(id, "Spanish") == 0) return "Español";
    if (strcmp(id, "Italian") == 0) return "Italiano";
    if (strcmp(id, "Turkish") == 0) return "Türkçe";
    if (strcmp(id, "German") == 0) return "Deutsch";
    if (strcmp(id, "Russian") == 0 || strcmp(id, "Russkiy") == 0) return "Russkiy";
    if (strcmp(id, "French") == 0) return "Français";
    return id;
}

// Deltarune persists its own English/Japanese choice in true_config.ini.
// That value has precedence over os_get_language(), so changing only the Vita
// port configuration left an older LANG="en" active after selecting Japanese.
// Rewrite only the LANG section and retain settings owned by the game (SCREEN,
// save migration flags, and any fields introduced by future builds).
static void syncNativeGameLanguage(const char* modName) {
    const char* desired = modName != NULL && strcmp(modName, "Japanese") == 0 ? "ja" : "en";
    char oldText[4096] = {0};
    FILE* input = fopen(NATIVE_LANGUAGE_CONFIG, "rb");
    if (input != NULL) {
        fread(oldText, 1, sizeof(oldText) - 1, input);
        fclose(input);
    }

    char output[4352];
    int written = snprintf(output, sizeof(output), "[LANG]\nLANG=\"%s\"\n", desired);
    bool skippingLang = false;
    const char* cursor = oldText;
    while (*cursor != '\0' && written > 0 && (size_t)written < sizeof(output) - 1) {
        const char* end = strchr(cursor, '\n');
        size_t lineLength = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        size_t trimmed = lineLength;
        if (trimmed > 0 && cursor[trimmed - 1] == '\r') trimmed--;
        if (trimmed > 1 && cursor[0] == '[' && cursor[trimmed - 1] == ']') {
            skippingLang = trimmed == 6 && strncmp(cursor, "[LANG]", 6) == 0;
        }
        if (!skippingLang) {
            if ((size_t)written + lineLength + 2 >= sizeof(output)) break;
            memcpy(output + written, cursor, lineLength);
            written += (int)lineLength;
            output[written++] = '\n';
            output[written] = '\0';
        }
        if (end == NULL) break;
        cursor = end + 1;
    }

    FILE* target = fopen(NATIVE_LANGUAGE_CONFIG, "wb");
    if (target != NULL) {
        fwrite(output, 1, (size_t)written, target);
        fclose(target);
        fprintf(stderr, "VITA_LANGUAGE_SYNC mod=%s native=%s\n",
                modName != NULL ? modName : "English", desired);
    }
}

static const char* restartWarningText(const VitaSettings* s) {
    if (strcmp(s->activeLanguage, "Portuguese-BR") == 0)
        return "Ao confirmar, o capítulo selecionado será reiniciado.";
    if (strcmp(s->activeLanguage, "Spanish") == 0)
        return "Al confirmar, el capítulo seleccionado se reiniciará.";
    if (strcmp(s->activeLanguage, "Italian") == 0)
        return "Confermando, il capitolo selezionato verrà riavviato.";
    if (strcmp(s->activeLanguage, "Turkish") == 0)
        return "Onaylanınca seçili bölüm yeniden başlatılır.";
    if (strcmp(s->activeLanguage, "German") == 0)
        return "Das gewählte Kapitel wird neu gestartet.";
    if (strcmp(s->activeLanguage, "Russian") == 0 || strcmp(s->activeLanguage, "Russkiy") == 0)
        return "Vybrannaya glava budet perezapushchena.";
    return "The selected chapter will restart.";
}

static const char* restartSaveWarningText(const VitaSettings* s) {
    if (strcmp(s->activeLanguage, "Portuguese-BR") == 0) return "Salve seu progresso antes.";
    if (strcmp(s->activeLanguage, "Spanish") == 0) return "Guarda tu progreso antes.";
    if (strcmp(s->activeLanguage, "Italian") == 0) return "Salva prima i progressi.";
    if (strcmp(s->activeLanguage, "Turkish") == 0) return "Önce ilerlemeni kaydet.";
    if (strcmp(s->activeLanguage, "German") == 0) return "Speichere vorher deinen Fortschritt.";
    if (strcmp(s->activeLanguage, "Russian") == 0 || strcmp(s->activeLanguage, "Russkiy") == 0) return "Snachala sokhranite progress.";
    return "Save your progress first.";
}

// Game Settings uses UTF-8 localized strings.  ASCII-only toupper() would
// leave Portuguese/Spanish accents lowercase, so handle the Latin characters
// used by the bundled translations explicitly while copying the text.
static void settingsUppercase(const char* input, char* output, size_t outputSize) {
    if (outputSize == 0) return;
    size_t i = 0, o = 0;
    while (input != NULL && input[i] != '\0' && o + 1 < outputSize) {
        unsigned char a = (unsigned char)input[i];
        unsigned char b = (unsigned char)input[i + 1];
        if (a == 0xC3 && b != 0 && o + 2 < outputSize) {
            // à-ö and ø-þ map to their uppercase form by subtracting 0x20,
            // except multiplication/division signs which are not letters.
            output[o++] = (char)a;
            output[o++] = (char)(((b >= 0xA0 && b <= 0xB6 && b != 0xB7) ||
                                  (b >= 0xB8 && b <= 0xBE)) ? b - 0x20 : b);
            i += 2;
            continue;
        }
        if (a == 0xC4 && b == 0xB1 && o + 1 < outputSize) { // Turkish dotless ı
            output[o++] = 'I'; i += 2; continue;
        }
        if (((a == 0xC4 && b == 0x9F) || (a == 0xC5 && b == 0x9F)) && o + 2 < outputSize) {
            output[o++] = (char)a; output[o++] = (char)(b - 1); i += 2; continue;
        }
        output[o++] = (char)(a < 0x80 ? toupper(a) : a);
        i++;
    }
    output[o] = '\0';
}

static void drawCenteredUpperText(Renderer* r, const char* text, float x, float y,
                                  float scale, uint32_t color) {
    char upper[512];
    settingsUppercase(text, upper, sizeof(upper));
    drawCenteredText(r, upper, x, y, scale, color);
}


// Frame used by the Spanish trophy artwork (bronze through platinum).
static const uint8_t trophyTier[30] = {
    4, 3, 3, 3, 3, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 1, 3, 2, 2
};

static void rebuildTrophyDisplayOrder(VitaSettings* s) {
    int out = 0;
    if (s->trophiesUnlockedFirst) {
        for (int i = 0; i < 30; ++i)
            if (s->trophiesUnlocked[i]) s->trophyDisplayOrder[out++] = i;
        for (int i = 0; i < 30; ++i)
            if (!s->trophiesUnlocked[i]) s->trophyDisplayOrder[out++] = i;
    } else {
        for (int i = 0; i < 30; ++i) s->trophyDisplayOrder[out++] = i;
    }
}

static bool readTrophyUnlocks(bool unlocked[30], bool* enabled) {
    memset(unlocked, 0, sizeof(bool) * 30);
    if (enabled != NULL) *enabled = true;
    FILE* f = fopen("ux0:data/deltarune_saves/trophies.ini", "rb");
    if (f == NULL) return false;
    char line[160];
    bool trophySection = false, settingsSection = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '[') {
            trophySection = strncmp(line, "[TROPHY]", 8) == 0;
            settingsSection = strncmp(line, "[SETTINGS]", 10) == 0;
            continue;
        }
        int key = -1, value = 0;
        if (trophySection && sscanf(line, "%d=%d", &key, &value) == 2 && key >= 0 && key < 30)
            unlocked[key] = value != 0;
        if (enabled != NULL && settingsSection && sscanf(line, "enabled=%d", &value) == 1)
            *enabled = value != 0;
    }
    fclose(f);
    return true;
}

static bool writeTrophyUnlocks(const bool unlocked[30], bool enabled) {
    sceIoMkdir("ux0:data/deltarune_saves", 0777);
    const char* tempPath = "ux0:data/deltarune_saves/trophies.tmp";
    const char* finalPath = "ux0:data/deltarune_saves/trophies.ini";
    FILE* f = fopen(tempPath, "wb");
    if (f == NULL) return false;
    fprintf(f, "[SETTINGS]\nenabled=%d\n\n[TROPHY]\n", enabled ? 1 : 0);
    for (int i = 0; i < 30; ++i) fprintf(f, "%d=%d\n", i, unlocked[i] ? 1 : 0);
    if (fclose(f) != 0) return false;
    sceIoRemove(finalPath);
    return sceIoRename(tempPath, finalPath) >= 0;
}

static void loadTrophyState(VitaSettings* s) {
    memset(s->trophiesUnlocked, 0, sizeof(s->trophiesUnlocked));
    s->trophiesEnabled = true;
            
    const char* catalog = strcmp(s->activeLanguage, "Portuguese-BR") == 0 ? "catalog_ptbr.txt" : (strcmp(s->activeLanguage, "Spanish") == 0 ? "catalog_es.txt" : "catalog_en.txt");
    char catalogPath[256];
    snprintf(catalogPath, sizeof(catalogPath),
             "ux0:data/deltarune/deltarunevita/mods/Trophies/%s", catalog);
    FILE* catalogFile = fopen(catalogPath, "rb");
    if (catalogFile != NULL) {
        char row[320];
        while (fgets(row, sizeof(row), catalogFile) != NULL) {
            char* first = strchr(row, '|');
            char* second = first != NULL ? strchr(first + 1, '|') : NULL;
            if (first == NULL || second == NULL) continue;
            *first = '\0';
            *second = '\0';
            int id = atoi(row);
            if (id < 0 || id >= 30) continue;
            char* end = strpbrk(second + 1, "\r\n");
            if (end != NULL) *end = '\0';
            snprintf(s->trophyTitles[id], sizeof(s->trophyTitles[id]), "%s", first + 1);
            snprintf(s->trophyDescriptions[id], sizeof(s->trophyDescriptions[id]), "%s", second + 1);
        }
        fclose(catalogFile);
    }
    readTrophyUnlocks(s->trophiesUnlocked, &s->trophiesEnabled);
    rebuildTrophyDisplayOrder(s);
}

#define SETTINGS_PATH "ux0:data/deltarune/config.ini"
#define SETTINGS_CATEGORIES 5
#define MODS_ROOT "ux0:data/deltarune/deltarunevita/mods/Lang/"

static bool g_devMode = false;
static uint32_t queuedUIIconCount = 0;
static bool collectingUIIcons = false;
extern int g_vitaConsoleBordersEnabled;
extern int g_vitaConsoleBorderMode;
extern void VitaProbe_rotateLog(void);
extern int VitaProbe_sendCurrentLog(char* status, size_t statusSize);
static char g_probeUploadStatus[96] = "Send the active probe log over Wi-Fi.";

static int getVideoItemCount() {
    if (g_vitaConsoleBorderMode == 1) return 5;
    return 4;
}

// All video items are now direct indices - no remapping needed.
// When widescreen (borders) is OFF, "Better borders" is hidden (item count = 4).
// Items visible:
//   widescreen ON:  0=Borders, 1=Better borders, 2=Adjust, 3=Graphics, 4=Brightness  (5 items)
//   widescreen OFF: 0=Borders,                  1=Adjust,  2=Graphics, 3=Brightness  (4 items)
static int getVideoDataIndex(const VitaSettings* s, int i) {
    if (s->consoleBorderMode == 1) {
        return i; // direct: 0, 1, 2, 3, 4
    }
    // Borders off: skip index 1 (Better borders)
    if (i == 0) return 0; // Borders
    return i + 1;          // 1->2(Adjust), 2->3(Graphics), 3->4(Brightness)
}

static int settingsItemCount(int category) {
    if (g_devMode) {
        if (category == 0) return 5; // DEV
        if (category == 1) return 3; // CONTROLES TOUCH
        if (category == 2) return getVideoItemCount(); // TELA
        if (category == 3) return 4; // AUDIO
        if (category == 4) return 5; // SISTEMA
    } else {
        if (category == 0) return 3; // CONTROLES TOUCH
        if (category == 1) return getVideoItemCount(); // TELA
        if (category == 2) return 4; // AUDIO
        if (category == 3) return 5; // SISTEMA
    }
    return 0;
}

int VitaSettings_itemCount(int category) { return settingsItemCount(category); }

int g_vitaDisplayOffsetX = 0;
int g_vitaDisplayOffsetY = 0;
int g_vitaDisplayZoom = 100;
int g_vitaDisplayStretchToFill = 0;
int g_vitaPortOverlayFullScreen = 0;
int g_vitaTouchEnabled = 0;
int g_vitaConsoleBordersEnabled = 0;
int g_vitaConsoleBorderMode = 0;
int g_vitaBetterBordersEnabled = 0;
int g_vitaPvrEnabled = 1;
int g_vitaBc3OnlyEnabled = 1;
int g_vitaTextureFormatProfile = 0; // 0 = Otimizado, 1 = Nativo, 2 = Low
int g_vitaShadersEnabled = 0; // opt-in while the Vita shader compatibility layer is experimental
int g_vitaTextureLinearFilter = 0;
int g_vitaTextureFilterRevision = 1;
// Phase 5: raised to 1 when the Texture Compression mode changes in-game so the
// renderer drops and reloads format-dependent resident pages next frame.
int g_vitaTextureCacheInvalidate = 0;
static bool g_launcherMode = false;
static int g_activeChapter = 0;

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
    s->widescreenEnabled = false;
    s->consoleBorderMode = 0;
    s->betterBordersEnabled = false;
    s->musicVolume = 10;
    s->sfxVolume = 10;
    s->masterVolume = 10;
    s->audioDisabled = false;
    s->devMode = false;
    s->showSettings = true;
    g_devMode = false;
    s->vsyncEnabled = true;
    s->fpsTargetMode = 0; // 0 = 30 FPS, 1 = 40 FPS, 2 = 60 FPS, 3 = Unlock
    s->screenFilterMode = 0;
    s->textureFormatProfile = 0; // 0 = Otimizado, 1 = Nativo, 2 = Low
    s->pvrEnabled = true;
    s->bc3Enabled = true;
    s->rendererMode = VITA_RENDERER_AUTOMATIC;
    s->modernGlEnabled = true;
    s->shadersEnabled = false;
    g_vitaShadersEnabled = 0;
    s->displayOffsetX = 0;
    s->displayOffsetY = 0;
    s->displayZoom = 100;
    s->displayStretchToFill = false;
    s->brightness = 100;
    s->pendingPvrEnabled = false;
    s->pendingBc3Enabled = false;
    s->pendingRendererMode = VITA_RENDERER_AUTOMATIC;
    s->pendingShadersEnabled = false;
    s->modIndex = 0;
    s->pendingModIndex = 0;
    s->showRestartPrompt = false;
    s->confirmShaderChange = false;
    s->shaderSceneReloadRequested = false;
    s->devRoomNavEnabled = false;
    s->debugCollisionMasks = false;
    s->controlEditMode = false;
    s->graphicsMenuOpen = false;
    s->trophiesMenuOpen = false;
    s->trophiesSelected = 0;
    s->trophiesScroll = 0;
    s->trophiesEnabled = true;
    // Local popup is the fallback when native NP trophies are unavailable.
    // When NoTrpDrm/native trophies are active, an unset preference defaults
    // this popup to Off; the user can explicitly re-enable it in System.
    s->internalTrophyNotifications = true;
    s->internalTrophyNotificationsConfigured = false;
    s->nativeTrophiesActive = false;
    s->trophyNotificationId = -1;
    s->graphicsMenuSelected = 0;
    s->selectedTouchControl = 0;
    resetTouchLayout(s);
}

static void addModName(VitaSettings* s, const char* name) {
    if (name == NULL || name[0] == '\0' || strcmp(name, "Original") == 0 || strcmp(name, "Borders") == 0 || strcmp(name, "English") == 0) return;
    for (int i = 0; i < s->modCount; ++i) {
        if (strcmp(s->modNames[i], name) == 0) return;
    }
    if (s->modCount < MAX_MODS) {
        size_t nlen = strlen(name);
        if (nlen >= MOD_NAME_MAX) nlen = MOD_NAME_MAX - 1;
        memcpy(s->modNames[s->modCount], name, nlen);
        s->modNames[s->modCount][nlen] = '\0';
        s->modCount++;
    }
}

static void discoverMods(VitaSettings* s) {
    s->modCount = 0;
    strncpy(s->modNames[0], "English", MOD_NAME_MAX - 1);
    strncpy(s->modNames[1], "Japanese", MOD_NAME_MAX - 1);
    s->modCount = 2;

    // 1. Discover bundled language INI files from app0:assets/ui/Lang/
    SceUID dd_app = sceIoDopen("app0:assets/ui/Lang/");
    if (dd_app >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dd_app, &entry) > 0) {
            if (entry.d_name[0] == '.') continue;
            char* dot = strrchr(entry.d_name, '.');
            if (dot != NULL && strcmp(dot, ".ini") == 0) {
                char langName[MOD_NAME_MAX];
                size_t len = (size_t)(dot - entry.d_name);
                if (len >= sizeof(langName)) len = sizeof(langName) - 1;
                memcpy(langName, entry.d_name, len);
                langName[len] = '\0';
                addModName(s, langName);
            }
        }
        sceIoDclose(dd_app);
    }

    // 2. Discover external mod directories from ux0:data/deltarune/deltarunevita/mods/Lang/
    SceUID dd_user_lang = sceIoDopen("ux0:data/deltarune/deltarunevita/mods/Lang/");
    if (dd_user_lang >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dd_user_lang, &entry) > 0) {
            if (entry.d_name[0] == '.') continue;
            if (strcmp(entry.d_name, "Borders") == 0) continue;
            SceIoStat stat;
            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "ux0:data/deltarune/deltarunevita/mods/Lang/%s", entry.d_name);
            if (sceIoGetstat(fullpath, &stat) >= 0 && SCE_S_ISDIR(stat.st_mode)) {
                addModName(s, entry.d_name);
            }
        }
        sceIoDclose(dd_user_lang);
    }

    // 3. Discover external mod directories from ux0:data/deltarune/deltarunevita/mods/
    SceUID dd_user_root = sceIoDopen(MODS_ROOT);
    if (dd_user_root >= 0) {
        SceIoDirent entry;
        while (sceIoDread(dd_user_root, &entry) > 0) {
            if (entry.d_name[0] == '.') continue;
            if (strcmp(entry.d_name, "Borders") == 0 || strcmp(entry.d_name, "Lang") == 0) continue;
            SceIoStat stat;
            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "%s%s", MODS_ROOT, entry.d_name);
            if (sceIoGetstat(fullpath, &stat) >= 0 && SCE_S_ISDIR(stat.st_mode)) {
                addModName(s, entry.d_name);
            }
        }
        sceIoDclose(dd_user_root);
    }
}


static void applyDisplaySettings(const VitaSettings* s) {
    g_vitaDisplayOffsetX = g_launcherMode ? 0 : s->displayOffsetX;
    g_vitaDisplayOffsetY = g_launcherMode ? 0 : s->displayOffsetY;
    g_vitaDisplayZoom = g_launcherMode ? 100 : s->displayZoom;
    g_vitaDisplayStretchToFill = g_launcherMode ? 0 : (s->displayStretchToFill ? 1 : 0);
    g_vitaTouchEnabled = s->touchEnabled ? 1 : 0;
    g_vitaConsoleBorderMode = g_launcherMode ? 0 : s->consoleBorderMode;
    // Both visual modes use the regular border texture pipeline.  "Simple"
    // is border_line_1080_0 for every gameplay room, not an immediate-mode
    // GL outline (which could overwrite the composited framebuffer).
    g_vitaConsoleBordersEnabled = g_vitaConsoleBorderMode != 0 ? 1 : 0;
    g_vitaBetterBordersEnabled = (g_vitaConsoleBorderMode == 1 && s->betterBordersEnabled) ? 1 : 0;
    g_vitaPvrEnabled = s->pvrEnabled ? 1 : 0;
    g_vitaBc3OnlyEnabled = s->bc3Enabled ? 1 : 0;
    // Sharp Bilinear uses VitaGL's hardware filtering directly.  It does not
    // allocate another surface or add a post-processing render pass.
    int requestedLinearFilter = s->screenFilterMode == 2 ? 1 : 0;
    if (g_vitaTextureLinearFilter != requestedLinearFilter) {
        g_vitaTextureLinearFilter = requestedLinearFilter;
        g_vitaTextureFilterRevision++;
        if (g_vitaTextureFilterRevision <= 0) g_vitaTextureFilterRevision = 1;
    }
}

static void saveSettings(const VitaSettings* s) {
    const char* modName = (s->modIndex >= 0 && s->modIndex < s->modCount) ? s->modNames[s->modIndex] : "English";
    char text[1024];
    int length = snprintf(text, sizeof(text), "touch=%d\nroom_nav=%d\ndevmode=%d\nshowsettings=%d\ncollision_masks=%d\nmod=%s\nwidescreen=%d\nborder_mode=%s\nbetter_borders=%d\nmaster_volume=%d\nmusic_volume=%d\nsfx_volume=%d\naudio_disabled=%d\nvsync=%d\nfps_mode=%d\nscreen_filter=%s\npvr=%d\ntexture_format=%s\nrenderer=%s\nrenderer_selection_version=1\nshaders=%d\nbrightness=%d\nscreen_profile=3\noffset_x=%d\noffset_y=%d\nzoom=%d\nstretch_fill=%d\ntouch_stick=%d,%d,%d\ntouch_z=%d,%d,%d\ntouch_x=%d,%d,%d\ntouch_c=%d,%d,%d\nshortcut_skip=%d\ntrophy_notifications=%d\ntrophy_notifications_configured=%d\n",
                           s->touchEnabled ? 1 : 0, 0,
                           s->devMode ? 1 : 0, s->showSettings ? 1 : 0,
                           0, modName,
                          s->consoleBorderMode != 0 ? 1 : 0,
                          s->consoleBorderMode == 1 ? "dynamic" : (s->consoleBorderMode == 2 ? "simple" : "off"),
                          s->betterBordersEnabled ? 1 : 0,
                          s->masterVolume, s->musicVolume, s->sfxVolume, s->audioDisabled ? 1 : 0, s->vsyncEnabled ? 1 : 0,
                          s->fpsTargetMode,
                          s->screenFilterMode == 1 ? "scanlines" :
                          (s->screenFilterMode == 2 ? "sharp-bilinear" :
                          (s->screenFilterMode == 3 ? "dithering-blending" :
                          (s->screenFilterMode == 4 ? "vhs" : "none"))),
                           s->pvrEnabled ? 1 : 0,
                           s->textureFormatProfile == 1 ? "native" : (s->textureFormatProfile == 2 ? "low" : "optimized"),
                          s->rendererMode == VITA_RENDERER_EXPERIMENTAL ? "experimental-gl" :
                          (s->rendererMode == VITA_RENDERER_MODERN ? "modern-gl" :
                          (s->rendererMode == VITA_RENDERER_LEGACY ? "legacy-gl" : "automatic")),
                          s->shadersEnabled ? 1 : 0,
                          s->brightness, s->displayOffsetX, s->displayOffsetY, s->displayZoom,
                           s->displayStretchToFill ? 1 : 0,
                          s->touchControlX[0], s->touchControlY[0], s->touchControlScale[0],
                          s->touchControlX[1], s->touchControlY[1], s->touchControlScale[1],
                          s->touchControlX[2], s->touchControlY[2], s->touchControlScale[2],
                          s->touchControlX[3], s->touchControlY[3], s->touchControlScale[3],
                          s->shortcutSkipDialogs ? 1 : 0,
                          s->internalTrophyNotifications ? 1 : 0,
                          s->internalTrophyNotificationsConfigured ? 1 : 0);
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, length);
        sceIoClose(fd);
    }
    syncNativeGameLanguage(modName);
}


void VitaSettings_loadLanguage(VitaSettings* s) {
    const char* langName = (s->modIndex >= 0 && s->modIndex < s->modCount) ? s->modNames[s->modIndex] : "English";
    strncpy(s->activeLanguage, langName, sizeof(s->activeLanguage) - 1);
    
    char path[256];
    // Settings and trophy labels shipped with the runner are authoritative.
    // Older data packages may still contain the provisional trophy catalog
    // (for example CLAIM instead of RING), so prefer the VPK copy and retain
    // ux0 only as compatibility fallback for unbundled languages.
    const char* uiLanguage = strcmp(langName, "Japanese") == 0 ? "English" : langName;
    if (strcmp(uiLanguage, "Russkiy") == 0) uiLanguage = "Russian";
    snprintf(path, sizeof(path), "app0:assets/ui/Lang/%s.ini", uiLanguage);
    FILE* f = fopen(path, "r");
    if (!f) {
        snprintf(path, sizeof(path), "ux0:data/deltarune/deltarunevita/ui/Lang/%s.ini", uiLanguage);
        f = fopen(path, "r");
    }
    if (!f && strcmp(uiLanguage, "Russian") != 0 && (strcmp(langName, "Russian") == 0 || strcmp(langName, "Russkiy") == 0)) {
        snprintf(path, sizeof(path), "app0:assets/ui/Lang/Russian.ini");
        f = fopen(path, "r");
    }
    if (!f) {
        snprintf(path, sizeof(path), "app0:assets/ui/Lang/English.ini");
        f = fopen(path, "r");
    }
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char key[128];
            char value[128];
            if (sscanf(line, "%127[^=]=%127[^\r\n]", key, value) == 2) {
                if (strncmp(key, "CAT_", 4) == 0) {
                    int idx = atoi(key + 4);
                    if (idx >= 0 && idx < 7) strncpy(s->uiCategories[idx], value, 63);
                } else if (strncmp(key, "LBL_", 4) == 0) {
                    int catIdx, lblIdx;
                    if (sscanf(key, "LBL_%d_%d", &catIdx, &lblIdx) == 2) {
                        if (catIdx >= 0 && catIdx < 7 && lblIdx >= 0 && lblIdx < 8) {
                            strncpy(s->uiLabels[catIdx][lblIdx], value, 63);
                        }
                    }
                } else if (strncmp(key, "FOOTER_", 7) == 0) {
                    const char* footers[] = {"CATEGORY", "CHANGE", "CLOSE", "CONFIRM", "CANCEL", "CHOOSE", "APPLY", "SAVE"};
                    for(int i=0; i<8; i++) {
                        if (strcmp(key + 7, footers[i]) == 0) {
                            strncpy(s->uiFooters[i], value, 63);
                            break;
                        }
                    }
                } else if (strncmp(key, "TROPHY_TITLE_", 13) == 0) {
                    int idx = atoi(key + 13);
                    if (idx >= 0 && idx < 30) strncpy(s->trophyTitles[idx], value, 63);
                } else if (strncmp(key, "TROPHY_DESC_", 12) == 0) {
                    int idx = atoi(key + 12);
                    if (idx >= 0 && idx < 30) strncpy(s->trophyDescriptions[idx], value, 127);
                }
            }
        }
        fclose(f);
    }
}

bool VitaSettings_forceLegacyRenderer(VitaSettings* s) {
    if (s == NULL) return false;
    if (s->rendererMode == VITA_RENDERER_LEGACY ||
        s->rendererMode == VITA_RENDERER_EXPERIMENTAL) {
        s->modernGlEnabled = false;
    } else if (s->rendererMode == VITA_RENDERER_MODERN) {
        s->modernGlEnabled = true;
    } else {
        // Round 2 consistently GPU-crashes on Vita even after shader, batching,
        // residency and synchronization fallbacks. Select the proven backend
        // before renderer creation; other chapters retain Modern-GL shaders.
        s->modernGlEnabled = g_activeChapter != 3;
    }
    return !s->modernGlEnabled;
}


static inline int getIniCategoryIndex(int logicalCat) {
    if (logicalCat == 2) return 3; // TELA/VIDEO -> CAT_3
    if (logicalCat == 3) return 2; // AUDIO/SOM -> CAT_2
    if (logicalCat == 4) return 5; // SISTEMA -> CAT_5
    return logicalCat;
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
        // DevMode visibility may persist, but active diagnostic overlays are
        // session-only. A fresh process must never reopen Navigator/collision
        // tools just because the previous session ended with them enabled.
        s->devRoomNavEnabled = false;
        s->debugDevEnabled = false;
        s->debugCollisionMasks = false;
        s->devMode = strstr(text, "devmode=1") != NULL;
        s->showSettings = strstr(text, "showsettings=0") == NULL;
        char* shortcutSetting = strstr(text, "shortcut_skip=");
        if (shortcutSetting != NULL) s->shortcutSkipDialogs = strstr(shortcutSetting, "shortcut_skip=1") == shortcutSetting;
        char* trophyNotifySetting = strstr(text, "trophy_notifications=");
        if (trophyNotifySetting != NULL)
            s->internalTrophyNotifications =
                strstr(trophyNotifySetting, "trophy_notifications=1") == trophyNotifySetting;
        char* trophyNotifyConfigured = strstr(text, "trophy_notifications_configured=");
        s->internalTrophyNotificationsConfigured = trophyNotifyConfigured != NULL &&
            strstr(trophyNotifyConfigured, "trophy_notifications_configured=1") == trophyNotifyConfigured;
        
        s->widescreenEnabled = strstr(text, "widescreen=1") != NULL;
        s->consoleBorderMode = s->widescreenEnabled ? 1 : 0;
        if (strstr(text, "border_mode=simple") != NULL) s->consoleBorderMode = 2;
        else if (strstr(text, "border_mode=off") != NULL) s->consoleBorderMode = 0;
        else if (strstr(text, "border_mode=dynamic") != NULL) s->consoleBorderMode = 1;
        s->widescreenEnabled = s->consoleBorderMode != 0;
        s->betterBordersEnabled = strstr(text, "better_borders=1") != NULL;
        s->fpsTargetMode = 0;
        char* fpsModeStr = strstr(text, "fps_mode=");
        if (fpsModeStr != NULL) {
            sscanf(fpsModeStr + 9, "%d", &s->fpsTargetMode);
            if (s->fpsTargetMode < 0 || s->fpsTargetMode > 3) s->fpsTargetMode = 0;
        } else if (strstr(text, "vsync=0") != NULL) {
            s->fpsTargetMode = 3; // Unlock if vsync=0 in older config
        }
        // VSync is part of the FPS-limit policy: capped modes synchronize,
        // while Unlock submits immediately.  This also migrates old configs.
        s->vsyncEnabled = s->fpsTargetMode != 3;
        s->screenFilterMode = 0;
        if (strstr(text, "screen_filter=scanlines") != NULL) s->screenFilterMode = 1;
        else if (strstr(text, "screen_filter=sharp-bilinear") != NULL) s->screenFilterMode = 2;
        else if (strstr(text, "screen_filter=dithering-blending") != NULL) s->screenFilterMode = 3;
        else if (strstr(text, "screen_filter=old-tv") != NULL || strstr(text, "screen_filter=vhs") != NULL) s->screenFilterMode = 4;
        // Removed filters migrate safely to None instead of retaining an
        // orphan numeric value from an older config.ini.
        else if (strstr(text, "screen_filter=crt-curvature") != NULL ||
                 strstr(text, "screen_filter=depth-of-field") != NULL ||
                 strstr(text, "screen_filter=crt") != NULL) s->screenFilterMode = 0;
        g_vitaTextureLinearFilter = s->screenFilterMode == 2 ? 1 : 0;
        s->pvrEnabled = strstr(text, "pvr=1") != NULL;
        s->textureFormatProfile = 0;
        if (strstr(text, "texture_format=native") != NULL) {
            s->textureFormatProfile = 1;
        } else if (strstr(text, "texture_format=low") != NULL) {
            s->textureFormatProfile = 2;
        } else if (strstr(text, "texture_format=optimized") != NULL || strstr(text, "texture_format=mixed") != NULL) {
            s->textureFormatProfile = 0;
        }
        g_vitaTextureFormatProfile = s->textureFormatProfile;
        // Texture Compression modes: 0 Otimizado (BC3 on 2048 atlases only),
        // 1 Nenhuma (RGBA4444 everywhere), 2 Agressiva (BC3 on all atlases).
        // Compression, when enabled, always uses BC3; the profile decides WHICH
        // pages are compressed (Otimizado limits it to the unbreakable 2048
        // atlases via vitaPvrTextureAllowed()).
        s->pvrEnabled = (s->textureFormatProfile != 1);
        s->bc3Enabled = (s->textureFormatProfile != 1);
        // v0.76 introduces Automatic as the safe default. Migrate older INIs
        // once, including installations that previously selected Modern-GL.
        // The marker preserves every explicit choice made in the new menu.
        if (strstr(text, "renderer_selection_version=1") == NULL) {
            s->rendererMode = VITA_RENDERER_AUTOMATIC;
        } else if (strstr(text, "renderer=experimental-gl") != NULL) {
            s->rendererMode = VITA_RENDERER_EXPERIMENTAL;
        } else if (strstr(text, "renderer=modern-gl") != NULL) {
            s->rendererMode = VITA_RENDERER_MODERN;
        } else if (strstr(text, "renderer=legacy-gl") != NULL) {
            s->rendererMode = VITA_RENDERER_LEGACY;
        } else {
            s->rendererMode = VITA_RENDERER_AUTOMATIC;
        }
        s->shadersEnabled = strstr(text, "shaders=1") != NULL;
        g_vitaShadersEnabled = s->shadersEnabled ? 1 : 0;
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
        char* brightness = strstr(text, "brightness=");
        if (offsetX != NULL) sscanf(offsetX + 9, "%d", &s->displayOffsetX);
        if (offsetY != NULL) sscanf(offsetY + 9, "%d", &s->displayOffsetY);
        if (zoom != NULL) sscanf(zoom + 5, "%d", &s->displayZoom);
        s->displayStretchToFill = strstr(text, "stretch_fill=1") != NULL;
        if (brightness != NULL) sscanf(brightness + 11, "%d", &s->brightness);
        const char* touchKeys[4] = {"touch_stick=", "touch_z=", "touch_x=", "touch_c="};
        for (int i = 0; i < 4; ++i) {
            char* value = strstr(text, touchKeys[i]);
            if (value != NULL) sscanf(value + strlen(touchKeys[i]), "%d,%d,%d",
                                      &s->touchControlX[i], &s->touchControlY[i], &s->touchControlScale[i]);
        }
        char modValue[MOD_NAME_MAX] = {0};
        char* modSetting = strstr(text, "mod=");
        if (modSetting != NULL) sscanf(modSetting + 4, "%31s", modValue);
        // Migrate configurations written before languages moved under
        // mods/Lang and PTBR was renamed to Portuguese-BR.
        if (strcmp(modValue, "PTBR") == 0) {
            snprintf(modValue, sizeof(modValue), "%s", "Portuguese-BR");
        }
        if (strcmp(modValue, "Original") == 0) {
            snprintf(modValue, sizeof(modValue), "%s", "English");
        }
        if (modValue[0] != '\0') {
            s->modIndex = 0;
            for (int i = 0; i < s->modCount; i++) {
                if (strcmp(s->modNames[i], modValue) == 0) { s->modIndex = i; break; }
            }
        } else if (strcmp(s->activeLanguage, "Portuguese-BR") == 0) {
            s->modIndex = 0;
            for (int i = 1; i < s->modCount; i++) {
                if (strcmp(s->modNames[i], "Portuguese-BR") == 0) { s->modIndex = i; break; }
            }
        }
        
        
        if (strstr(text, "screen_profile=3") == NULL) {
            s->displayOffsetX = 0;
            s->displayOffsetY = 0;
            s->displayZoom = 100;
            s->displayStretchToFill = false;
            s->widescreenEnabled = false;
            s->consoleBorderMode = 0;
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
    if (s->brightness < 10) s->brightness = 10;
    if (s->brightness > 100) s->brightness = 100;
    for (int i = 0; i < 4; ++i) {
        if (s->touchControlX[i] < 40) s->touchControlX[i] = 40;
        if (s->touchControlX[i] > 920) s->touchControlX[i] = 920;
        if (s->touchControlY[i] < 40) s->touchControlY[i] = 40;
        if (s->touchControlY[i] > 504) s->touchControlY[i] = 504;
        if (s->touchControlScale[i] < 60) s->touchControlScale[i] = 60;
        if (s->touchControlScale[i] > 160) s->touchControlScale[i] = 160;
    }
    s->pendingPvrEnabled = s->pvrEnabled;
    s->pendingBc3Enabled = s->bc3Enabled;
    s->pendingRendererMode = s->rendererMode;
    s->pendingShadersEnabled = s->shadersEnabled;
    s->pendingModIndex = s->modIndex;
    s->pendingTextureFormatProfile = s->textureFormatProfile;
    s->pendingFpsTargetMode = s->fpsTargetMode;
    s->pendingScreenFilterMode = s->screenFilterMode;
    applyDisplaySettings(s);
    g_devMode = s->devMode;
    loadTrophyState(s);
    s->trophyStateInitialized = true;
    VitaSettings_loadLanguage(s);
    if (migrateScreenProfile) saveSettings(s);
}

void VitaSettings_applyAudio(VitaSettings* s, AudioSystem* audio) {
    audio->vtable->setMasterGain(audio, (float)s->masterVolume / 10.0f);
    AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, (float)s->musicVolume / 10.0f, (float)s->sfxVolume / 10.0f);
    AlAudioSystem_setDisabled((AlAudioSystem*)audio, s->audioDisabled);
}

void VitaSettings_setSliderFromTouch(VitaSettings* s, AudioSystem* audio,
                                     int logicalCategory, int item, float touchX) {
    int value = (int)(((touchX - 570.0f) * 10.0f / 128.0f) + 0.5f);
    if (value < 0) value = 0;
    if (value > 10) value = 10;
    if (logicalCategory == 3 && item >= 0 && item < 3) {
        int oldValue = item == 0 ? s->masterVolume : (item == 1 ? s->sfxVolume : s->musicVolume);
        if (oldValue == value) return;
        if (item == 0) s->masterVolume = value;
        else if (item == 1) s->sfxVolume = value;
        else s->musicVolume = value;
        VitaSettings_applyAudio(s, audio);
        saveSettings(s);
    } else if (logicalCategory == 2 && item == 4) {
        int brightness = value == 0 ? 10 : value * 10;
        if (s->brightness == brightness) return;
        s->brightness = brightness;
        saveSettings(s);
    }
}

// Native WAV parsing types matching butterscotch's wave.h
typedef struct {
  char riff_id[5];
  uint32_t file_size;
  char wave_id[5];
  char fmt_id[5];
  uint32_t fmt_size;
  uint16_t audio_format;
  uint16_t number_of_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t block_align;
  uint16_t bits_per_sample;
  char data_id[5];
  uint32_t data_size;
} NativeWAVHeader;

typedef struct {
  NativeWAVHeader header;
  uint8_t* data;
  uint32_t data_length;
} NativeWAVFile;

extern int stb_vorbis_decode_filename(const char *filename, int *channels, int *sample_rate, short **output);
extern NativeWAVFile WAV_ParseFileData(uint8_t const* data);

static ALuint native_al_buffers[3] = {0};
static ALuint native_al_sources[3] = {0};

static ALuint loadNativeWAV(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44) { fclose(f); return 0; }
    uint8_t* fileData = (uint8_t*)malloc(size);
    if (!fileData) { fclose(f); return 0; }
    fread(fileData, 1, size, f);
    fclose(f);
    
    NativeWAVFile wav = WAV_ParseFileData(fileData);
    free(fileData);
    if (!wav.data) return 0;
    
    ALuint buf = 0;
    alGenBuffers(1, &buf);
    ALenum format = AL_FORMAT_MONO16;
    if (wav.header.number_of_channels == 1) {
        format = wav.header.bits_per_sample == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16;
    } else {
        format = wav.header.bits_per_sample == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16;
    }
    alBufferData(buf, format, wav.data, wav.data_length, wav.header.sample_rate);
    free(wav.data);
    return buf;
}

static ALuint loadNativeOGG(const char* path) {
    int channels = 0, sampleRate = 0;
    short* pcm = nullptr;
    int samples = stb_vorbis_decode_filename(path, &channels, &sampleRate, &pcm);
    if (samples <= 0 || !pcm) return 0;
    
    ALuint buf = 0;
    alGenBuffers(1, &buf);
    ALenum format = (channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    alBufferData(buf, format, pcm, samples * channels * sizeof(short), sampleRate);
    free(pcm);
    return buf;
}

static void initNativeSounds() {
    if (native_al_sources[0] != 0) return;
    native_al_buffers[0] = loadNativeOGG("app0:assets/sounds/snd_menumove.ogg");
    native_al_buffers[1] = loadNativeOGG("app0:assets/sounds/snd-select.ogg");
    native_al_buffers[2] = loadNativeOGG("app0:assets/sounds/snd_swing.ogg");
    if (native_al_buffers[0] == 0) native_al_buffers[0] = loadNativeOGG("ux0:data/deltarune/deltarunevita/sounds/snd_menumove.wav");
    if (native_al_buffers[1] == 0) native_al_buffers[1] = loadNativeOGG("ux0:data/deltarune/deltarunevita/sounds/snd-select.ogg");
    if (native_al_buffers[2] == 0) native_al_buffers[2] = loadNativeOGG("ux0:data/deltarune/deltarunevita/sounds/snd_swing.wav");
    alGenSources(3, native_al_sources);
    for (int i = 0; i < 3; ++i) {
        if (native_al_sources[i] != 0 && native_al_buffers[i] != 0)
            alSourcei(native_al_sources[i], AL_BUFFER, native_al_buffers[i]);
    }
}

static void playSettingSound(VitaSettings* s, int type) {
    initNativeSounds();
    if (type < 0 || type >= 3 || native_al_sources[type] == 0 ||
        !alIsSource(native_al_sources[type])) return;
    ALuint buf = native_al_buffers[type];
    if (buf == 0) return;
    ALuint source = native_al_sources[type];
    alSourceStop(source);
    float gain = 1.0f;
    if (s) {
        gain = ((float)s->sfxVolume / 10.0f) * ((float)s->masterVolume / 10.0f);
    }
    alSourcef(source, AL_GAIN, gain);
    alSourcePlay(source);
}

bool VitaSettings_handleInput(VitaSettings* s, const SceCtrlData* pad, AudioSystem* audio) {
    if (s->inputCooldown > 0) s->inputCooldown--;
    uint32_t pressed = pad->buttons & ~s->previousButtons;
    s->previousButtons = pad->buttons;
    bool requestRestart = false;

    if (s->trophiesMenuOpen) {
        bool analogUp = pad->ly < 72 && s->inputCooldown == 0;
        bool analogDown = pad->ly > 184 && s->inputCooldown == 0;
        if ((pressed & SCE_CTRL_UP) || analogUp) {
            s->trophiesSelected = (s->trophiesSelected + 29) % 30;
            s->inputCooldown = analogUp ? 7 : 2;
            playSettingSound(s, 0);
        }
        if ((pressed & SCE_CTRL_DOWN) || analogDown) {
            s->trophiesSelected = (s->trophiesSelected + 1) % 30;
            s->inputCooldown = analogDown ? 7 : 2;
            playSettingSound(s, 0);
        }
        if (pressed & SCE_CTRL_TRIANGLE) {
            int selectedId = s->trophyDisplayOrder[s->trophiesSelected];
            s->trophiesUnlockedFirst = !s->trophiesUnlockedFirst;
            rebuildTrophyDisplayOrder(s);
            for (int i = 0; i < 30; ++i) {
                if (s->trophyDisplayOrder[i] == selectedId) {
                    s->trophiesSelected = i;
                    break;
                }
            }
            s->trophiesScroll = s->trophiesSelected;
            if (s->trophiesScroll > 27) s->trophiesScroll = 27;
            playSettingSound(s, 0);
        }
        if (s->trophiesSelected < s->trophiesScroll) s->trophiesScroll = s->trophiesSelected;
        if (s->trophiesSelected >= s->trophiesScroll + 3) s->trophiesScroll = s->trophiesSelected - 2;
        if (pressed & SCE_CTRL_CIRCLE) {
            s->trophiesMenuOpen = false;
            s->inputCooldown = 2;
            playSettingSound(s, 2);
        }
        return false;
    }

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
            playSettingSound(s, 1);
        }
        return false;
    }

    if (s->showRestartPrompt) {
        if (pressed & SCE_CTRL_CROSS) {
                if (s->pendingResetDefaults) {
                    bool nativeTrophiesActive = s->nativeTrophiesActive;
                    resetDefaults(s);
                    VitaSettings_setNativeTrophiesActive(s, nativeTrophiesActive);
                    applyDisplaySettings(s);
                    VitaSettings_applyAudio(s, audio);
                    saveSettings(s);
                    s->pendingResetDefaults = false;
                } else {
                    // Apply pending changes
                    s->pvrEnabled = s->pendingPvrEnabled;
                    s->bc3Enabled = s->pendingBc3Enabled;
                    s->rendererMode = s->pendingRendererMode;
                    s->shadersEnabled = s->pendingShadersEnabled;
                    g_vitaShadersEnabled = s->shadersEnabled ? 1 : 0;
                    s->modIndex = s->pendingModIndex;
                    s->textureFormatProfile = s->pendingTextureFormatProfile;
                    s->screenFilterMode = s->pendingScreenFilterMode;
                    s->fpsTargetMode = s->pendingFpsTargetMode;
                    s->vsyncEnabled = s->fpsTargetMode != 3;
                    g_vitaTextureFormatProfile = s->textureFormatProfile;
                    g_vitaTextureLinearFilter = s->screenFilterMode == 2 ? 1 : 0;
                    applyDisplaySettings(s);
                    saveSettings(s);
                }
                s->showRestartPrompt = false;
                s->open = false;
                requestRestart = true;
                playSettingSound(s, 1);
        } else if (pressed & SCE_CTRL_CIRCLE) {
            // Cancel pending changes
                    s->pendingPvrEnabled = s->pvrEnabled;
            s->pendingBc3Enabled = s->bc3Enabled;
            s->pendingRendererMode = s->rendererMode;
            s->pendingShadersEnabled = s->shadersEnabled;
            s->pendingModIndex = s->modIndex;
            s->pendingTextureFormatProfile = s->textureFormatProfile;
            s->pendingFpsTargetMode = s->fpsTargetMode;
            s->pendingScreenFilterMode = s->screenFilterMode;
            s->pendingResetDefaults = false;
            s->showRestartPrompt = false;
            playSettingSound(s, 2);
        }
        return requestRestart;
    }

    if (s->confirmShaderChange) {
        if (pressed & SCE_CTRL_CROSS) {
            // The Legacy-GL compatibility layer now has audited Chapter 4/5
            // programs.  Apply the user's choice in every chapter and reload
            // only the current scene so existing Draw state cannot retain a
            // program from the previous mode.
            s->shadersEnabled = s->pendingShadersEnabled;
            g_vitaShadersEnabled = s->shadersEnabled ? 1 : 0;
            s->shaderSceneReloadRequested = true;
            saveSettings(s);
            s->confirmShaderChange = false;
            s->graphicsMenuOpen = true;
            s->graphicsMenuSelected = 3;
            s->open = true;
            s->inputCooldown = 2;
            playSettingSound(s, 1);
        } else if (pressed & SCE_CTRL_CIRCLE) {
            s->pendingShadersEnabled = s->shadersEnabled;
            s->confirmShaderChange = false;
            s->inputCooldown = 2;
            playSettingSound(s, 2);
        }
        return requestRestart;
    }

    if (s->graphicsMenuOpen) {
        if (pressed & SCE_CTRL_UP) {
            s->graphicsMenuSelected = (s->graphicsMenuSelected + 4) % 5;
            playSettingSound(s, 0);
        }
        if (pressed & SCE_CTRL_DOWN) {
            s->graphicsMenuSelected = (s->graphicsMenuSelected + 1) % 5;
            playSettingSound(s, 0);
        }
        bool left = (pressed & SCE_CTRL_LEFT) != 0;
        bool right = (pressed & SCE_CTRL_RIGHT) != 0;
        bool confirm = (pressed & SCE_CTRL_CROSS) != 0;
        if (s->graphicsMenuSelected == 0 && (left || right)) {
            int step = right ? 1 : -1;
            s->pendingTextureFormatProfile = (s->pendingTextureFormatProfile + step + 3) % 3;
            s->pendingPvrEnabled = (s->pendingTextureFormatProfile != 1);
            s->pendingBc3Enabled = (s->pendingTextureFormatProfile != 1);
            playSettingSound(s, 0);
        } else if (s->graphicsMenuSelected == 1 && (left || right)) {
            int step = right ? 1 : -1;
            s->pendingFpsTargetMode = (s->pendingFpsTargetMode + step + 4) % 4;
            s->fpsTargetMode = s->pendingFpsTargetMode;
            s->vsyncEnabled = s->fpsTargetMode != 3;
            saveSettings(s);
            playSettingSound(s, 0);
        } else if (s->graphicsMenuSelected == 2 && (left || right)) {
            int step = right ? 1 : -1;
            s->pendingScreenFilterMode = (s->pendingScreenFilterMode + step + 5) % 5;
            s->screenFilterMode = s->pendingScreenFilterMode;
            g_vitaTextureLinearFilter = s->screenFilterMode == 2 ? 1 : 0;
            g_vitaTextureFilterRevision++;
            if (g_vitaTextureFilterRevision <= 0) g_vitaTextureFilterRevision = 1;
            saveSettings(s);
            playSettingSound(s, 0);
        } else if (s->graphicsMenuSelected == 3 && (left || right)) {
            s->pendingShadersEnabled = !s->shadersEnabled;
            s->confirmShaderChange = true;
            playSettingSound(s, 0);
            return requestRestart;
        } else if (s->graphicsMenuSelected == 4 && (left || right)) {
            if (right) {
                s->pendingRendererMode = (s->pendingRendererMode + 1) % 4;
            } else {
                s->pendingRendererMode = (s->pendingRendererMode + 3) % 4;
            }
            playSettingSound(s, 0);
        }
        if (confirm) {
            if (s->graphicsMenuSelected == 3) {
                s->pendingShadersEnabled = !s->shadersEnabled;
                s->confirmShaderChange = true;
                playSettingSound(s, 1);
                return requestRestart;
            }
            const bool rendererChanged = s->pendingRendererMode != s->rendererMode;
            const bool textureResidencyChanged =
                s->pendingPvrEnabled != s->pvrEnabled ||
                s->pendingBc3Enabled != s->bc3Enabled ||
                s->pendingTextureFormatProfile != s->textureFormatProfile;
            if (rendererChanged) {
                s->showRestartPrompt = true;
                s->promptSelection = 0;
            } else {
                s->pvrEnabled = s->pendingPvrEnabled;
                s->bc3Enabled = s->pendingBc3Enabled;
                s->textureFormatProfile = s->pendingTextureFormatProfile;
                s->fpsTargetMode = s->pendingFpsTargetMode;
                s->screenFilterMode = s->pendingScreenFilterMode;
                s->vsyncEnabled = s->fpsTargetMode != 3;
                g_vitaTextureFormatProfile = s->textureFormatProfile;
                g_vitaTextureLinearFilter = s->screenFilterMode == 2 ? 1 : 0;
                applyDisplaySettings(s);
                if (textureResidencyChanged) g_vitaTextureCacheInvalidate = 1;
                saveSettings(s);
                s->graphicsMenuOpen = false;
            }
            playSettingSound(s, 1);
        }
        if (pressed & SCE_CTRL_CIRCLE) {
            s->pendingPvrEnabled = s->pvrEnabled;
            s->pendingBc3Enabled = s->bc3Enabled;
            s->pendingRendererMode = s->rendererMode;
            s->pendingShadersEnabled = s->shadersEnabled;
            s->pendingTextureFormatProfile = s->textureFormatProfile;
            s->pendingFpsTargetMode = s->fpsTargetMode;
            s->pendingScreenFilterMode = s->screenFilterMode;
            s->graphicsMenuOpen = false;
            playSettingSound(s, 2);
        }
        return requestRestart;
    }

    if (s->confirmChapterSelect) {
        if (pressed & SCE_CTRL_CROSS) {
            s->confirmChapterSelect = false;
            s->returnToChapterSelect = true;
            s->open = false;
            requestRestart = true;
            playSettingSound(s, 1);
        } else if (pressed & SCE_CTRL_CIRCLE) {
            s->confirmChapterSelect = false;
            s->inputCooldown = 2;
            playSettingSound(s, 2);
        }
        return requestRestart;
    }

    if (s->confirmRotateLog) {
        if (pressed & SCE_CTRL_CROSS) {
            s->confirmRotateLog = false;
            VitaProbe_rotateLog();
            playSettingSound(s, 1);
        } else if (pressed & SCE_CTRL_CIRCLE) {
            s->confirmRotateLog = false;
            s->inputCooldown = 2;
            playSettingSound(s, 2);
        }
        return requestRestart;
    }

    if (s->adjustMode) {
        int lx = (int)pad->lx - 128;
        int ly = (int)pad->ly - 128;
        int ry = (int)pad->ry - 128;
        // Analog stick remains the fast adjustment. D-Pad is fine 1 px control.
        if (lx < -48) s->displayOffsetX -= 2;
        if (lx > 48) s->displayOffsetX += 2;
        if (ly < -48) s->displayOffsetY -= 2;
        if (ly > 48) s->displayOffsetY += 2;
        if (pressed & SCE_CTRL_LEFT)  s->displayOffsetX -= 1;
        if (pressed & SCE_CTRL_RIGHT) s->displayOffsetX += 1;
        if (pressed & SCE_CTRL_UP)    s->displayOffsetY -= 1;
        if (pressed & SCE_CTRL_DOWN)  s->displayOffsetY += 1;
        if (ry < -48 && s->displayZoom < 160) s->displayZoom++;
        if (ry > 48 && s->displayZoom > 50) s->displayZoom--;
        if (pressed & SCE_CTRL_TRIANGLE) {
            s->displayStretchToFill = !s->displayStretchToFill;
            playSettingSound(s, 0);
        }
        applyDisplaySettings(s);
        if (pressed & SCE_CTRL_CIRCLE) {
            s->displayOffsetX = 0;
            s->displayOffsetY = 0;
            s->displayZoom = 100;
            s->displayStretchToFill = false;
            applyDisplaySettings(s);
            playSettingSound(s, 2);
        }
        if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SELECT)) {
            s->adjustMode = false;
            saveSettings(s);
            playSettingSound(s, 1);
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
        playSettingSound(s, s->open ? 1 : 2);
    }
    if (!s->open) return requestRestart;

    int numCategories = s->devMode ? 5 : 4;
    if (pressed & SCE_CTRL_LTRIGGER) {
        s->category = (s->category + numCategories - 1) % numCategories;
        s->selected = 0;
        playSettingSound(s, 0);
    }
    if (pressed & SCE_CTRL_RTRIGGER) {
        s->category = (s->category + 1) % numCategories;
        s->selected = 0;
        playSettingSound(s, 0);
    }
    int itemCount = settingsItemCount(s->category);
    if (pressed & SCE_CTRL_UP) { s->selected = (s->selected + itemCount - 1) % itemCount; playSettingSound(s, 0); }
    if (pressed & SCE_CTRL_DOWN) { s->selected = (s->selected + 1) % itemCount; playSettingSound(s, 0); }

    bool activate = (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) != 0;
    if (activate) {
        if (pressed & SCE_CTRL_CROSS) playSettingSound(s, 1);
        else playSettingSound(s, 0);
        
        int logicalCategory = s->category;
        if (!s->devMode) logicalCategory += 1;

        if (logicalCategory == 0 && s->selected == 0) {
            s->devRoomNavEnabled = !s->devRoomNavEnabled;
            saveSettings(s);
        } else if (logicalCategory == 0 && s->selected == 1) {
            s->debugDevEnabled = !s->debugDevEnabled;
            s->debugDevChanged = true;
            saveSettings(s);
        } else if (logicalCategory == 0 && s->selected == 2) {
            s->debugCollisionMasks = !s->debugCollisionMasks;
            saveSettings(s);
        } else if (logicalCategory == 0 && s->selected == 3) {
            s->confirmRotateLog = true;
        } else if (logicalCategory == 0 && s->selected == 4) {
            snprintf(g_probeUploadStatus, sizeof(g_probeUploadStatus), "Sending current log over Wi-Fi...");
            VitaProbe_sendCurrentLog(g_probeUploadStatus, sizeof(g_probeUploadStatus));
        } else if (logicalCategory == 1 && s->selected == 0) {
            s->shortcutSkipDialogs = !s->shortcutSkipDialogs;
            saveSettings(s);
        } else if (logicalCategory == 1 && s->selected == 1) {
            s->touchEnabled = !s->touchEnabled;
            saveSettings(s);
        } else if (logicalCategory == 1 && s->selected == 2) {
            s->controlEditMode = true;
            s->touchEnabled = true;
            s->selectedTouchControl = 0;
        } else if (logicalCategory == 2) {
            int dataIndex = getVideoDataIndex(s, s->selected);
            if (dataIndex == 0) {
                // Console borders: Off -> Dynamic -> Simple.
                int step = (pressed & SCE_CTRL_LEFT) ? -1 : 1;
                s->consoleBorderMode = (s->consoleBorderMode + step + 3) % 3;
                s->widescreenEnabled = s->consoleBorderMode != 0;
                if (s->consoleBorderMode == 1) {
                    if (!VitaBorders_filesAvailable()) s->borderWarningFrames = 240;
                    s->displayZoom = 88;
                } else if (s->consoleBorderMode == 2) {
                    s->displayZoom = 88;
                } else {
                    s->displayZoom = 100;
                }
                s->displayOffsetX = 0;
                s->displayOffsetY = 0;
                applyDisplaySettings(s);
                saveSettings(s);
            } else if (dataIndex == 1) {
                // Bordas melhores
                s->betterBordersEnabled = !s->betterBordersEnabled;
                applyDisplaySettings(s);
                saveSettings(s);
            } else if (dataIndex == 2) {
                // Ajustar tela
                s->adjustMode = true;
                s->open = false;
            } else if (dataIndex == 3) {
                // Config. graficas
                if (pressed & SCE_CTRL_CROSS) {
                    s->graphicsMenuOpen = true;
                    s->graphicsMenuSelected = 0;
                    s->pendingPvrEnabled = s->pvrEnabled;
                    s->pendingBc3Enabled = s->bc3Enabled;
                    s->pendingRendererMode = s->rendererMode;
                    s->pendingShadersEnabled = s->shadersEnabled;
                    s->pendingTextureFormatProfile = s->textureFormatProfile;
                    s->pendingFpsTargetMode = s->fpsTargetMode;
                    s->pendingScreenFilterMode = s->screenFilterMode;
                }
            } else if (dataIndex == 4) {
                // Brilho
                if (pressed & SCE_CTRL_LEFT) s->brightness -= 10;
                else if (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_CROSS)) s->brightness += 10;
                if (s->brightness < 10) s->brightness = 10;
                if (s->brightness > 100) s->brightness = 100;
                saveSettings(s);
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
        } else if (logicalCategory == 4 && s->selected == 1) {
            s->confirmChapterSelect = true;
        } else if (logicalCategory == 4 && s->selected == 2) {
            loadTrophyState(s);
            s->trophiesMenuOpen = true;
            s->trophiesSelected = 0;
            s->trophiesScroll = 0;
        } else if (logicalCategory == 4 && s->selected == 3) {
            s->internalTrophyNotifications = !s->internalTrophyNotifications;
            s->internalTrophyNotificationsConfigured = true;
            saveSettings(s);
        } else if (logicalCategory == 4 && s->selected == 4) {
            s->showRestartPrompt = true;
            s->promptSelection = 1;
            s->pendingResetDefaults = true;
            playSettingSound(s, 1);
        }
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        playSettingSound(s, 2);
        s->open = false;
        s->inputCooldown = 2;
        requestRestart = s->restartOnClose;
    }
    return requestRestart;
}

static bool settingsFontHasGlyph(const Font* font, uint16_t character) {
    if (font == nullptr || !font->present) return false;
    for (uint32_t i = 0; i < font->glyphCount; ++i)
        if (font->glyphs[i].character == character) return true;
    return false;
}

static int findSettingsFont(Renderer* r, bool portuguese) {
    int bestFont = -1;
    int bestScore = -1;
    static const uint16_t portugueseGlyphs[] = {
        0x00C1, 0x00C7, 0x00C9, 0x00CD, 0x00D3, 0x00DA,
        0x00C3, 0x00D5, 0x00E1, 0x00E3, 0x00E7, 0x00E9,
        0x00EA, 0x00ED, 0x00F3, 0x00FA
    };
    for (uint32_t i = 0; i < r->dataWin->font.count; ++i) {
        Font* font = &r->dataWin->font.fonts[i];
        if (!font->present) continue;
        int score = font->name != nullptr && strcmp(font->name, "fnt_main") == 0 ? 100 : 0;
        int portugueseCoverage = 0;
        for (uint32_t g = 0; g < sizeof(portugueseGlyphs) / sizeof(portugueseGlyphs[0]); ++g)
            if (settingsFontHasGlyph(font, portugueseGlyphs[g])) portugueseCoverage++;
        score += portugueseCoverage * (portuguese ? 1000 : 5);
        if (score > bestScore) {
            bestScore = score;
            bestFont = (int)i;
        }
    }
    return bestFont;
}

static char settingsLatinBase(uint16_t cp, uint8_t* accent) {
    *accent = 0;
    switch (cp) {
        // Acute (kind 1)
        case 0x00C1: *accent = 1; return 'A'; case 0x00E1: *accent = 1; return 'a';
        case 0x00C9: *accent = 1; return 'E'; case 0x00E9: *accent = 1; return 'e';
        case 0x00CD: *accent = 1; return 'I'; case 0x00ED: *accent = 1; return 'i';
        case 0x00D3: *accent = 1; return 'O'; case 0x00F3: *accent = 1; return 'o';
        case 0x00DA: *accent = 1; return 'U'; case 0x00FA: *accent = 1; return 'u';
        case 0x00DD: *accent = 1; return 'Y'; case 0x00FD: *accent = 1; return 'y';

        // Tilde (kind 2)
        case 0x00C3: *accent = 2; return 'A'; case 0x00E3: *accent = 2; return 'a';
        case 0x00D5: *accent = 2; return 'O'; case 0x00F5: *accent = 2; return 'o';
        case 0x00D1: *accent = 2; return 'N'; case 0x00F1: *accent = 2; return 'n';

        // Cedilla (kind 3)
        case 0x00C7: *accent = 3; return 'C'; case 0x00E7: *accent = 3; return 'c';
        case 0x015E: *accent = 3; return 'S'; case 0x015F: *accent = 3; return 's';

        // Circumflex (kind 4)
        case 0x00C2: *accent = 4; return 'A'; case 0x00E2: *accent = 4; return 'a';
        case 0x00CA: *accent = 4; return 'E'; case 0x00EA: *accent = 4; return 'e';
        case 0x00CE: *accent = 4; return 'I'; case 0x00EE: *accent = 4; return 'i';
        case 0x00D4: *accent = 4; return 'O'; case 0x00F4: *accent = 4; return 'o';
        case 0x00DB: *accent = 4; return 'U'; case 0x00FB: *accent = 4; return 'u';

        // Diaeresis / Trema (kind 5)
        case 0x00C4: *accent = 5; return 'A'; case 0x00E4: *accent = 5; return 'a';
        case 0x00CB: *accent = 5; return 'E'; case 0x00EB: *accent = 5; return 'e';
        case 0x00CF: *accent = 5; return 'I'; case 0x00EF: *accent = 5; return 'i';
        case 0x00D6: *accent = 5; return 'O'; case 0x00F6: *accent = 5; return 'o';
        case 0x00DC: *accent = 5; return 'U'; case 0x00FC: *accent = 5; return 'u';

        // Grave (kind 6)
        case 0x00C0: *accent = 6; return 'A'; case 0x00E0: *accent = 6; return 'a';
        case 0x00C8: *accent = 6; return 'E'; case 0x00E8: *accent = 6; return 'e';
        case 0x00CC: *accent = 6; return 'I'; case 0x00EC: *accent = 6; return 'i';
        case 0x00D2: *accent = 6; return 'O'; case 0x00F2: *accent = 6; return 'o';
        case 0x00D9: *accent = 6; return 'U'; case 0x00F9: *accent = 6; return 'u';

        // German Eszett
        case 0x00DF: *accent = 0; return 's';

        default: return 0;
    }
}

#include "settings_latin_font.h"

static void drawSettingsText(Renderer* r, const char* text, float anchorX, float y,
                             float scale, uint32_t color, int align) {
    if (!r || !text || r->drawFont < 0 ||
        (uint32_t)r->drawFont >= r->dataWin->font.count) return;
    Font* font = &r->dataWin->font.fonts[r->drawFont];
    char plain[512];
    uint16_t replacementCp[512] = {0};
    int32_t srcLen = (int32_t)strlen(text), src = 0, dst = 0;
    while (src < srcLen && dst < (int32_t)sizeof(plain) - 1) {
        int32_t before = src;
        uint16_t cp = TextUtils_decodeUtf8(text, srcLen, &src);
        uint8_t ignored;
        char base = settingsLatinBase(cp, &ignored);
        if (base && !settingsFontHasGlyph(font, cp)) {
            replacementCp[dst] = cp;
            plain[dst++] = base; // Layout retains the game font's advance.
        } else {
            int32_t bytes = src - before;
            if (dst + bytes >= (int32_t)sizeof(plain)) break;
            memcpy(plain + dst, text + before, (size_t)bytes);
            dst += bytes;
        }
    }
    plain[dst] = 0;
    float sx = scale * (font->scaleX != 0 ? font->scaleX : 1);
    float width = TextUtils_measureLineWidth(font, plain, dst) * sx;
    float left = align == 1 ? anchorX - width * .5f : align == 2 ? anchorX - width : anchorX;
    int oldHalign = r->drawHalign;
    r->drawHalign = 0;
    FontGlyph* capital = TextUtils_findGlyph(font, 'A');
    // sourceHeight is the entire atlas cell, including transparent ascender
    // padding. Scaling the ink to that height makes accents much too large.
    // Match the capital's width and align the visible body to its baseline.
    float unit = capital && capital->sourceWidth ?
                 capital->sourceWidth / SETTINGS_LATIN_CAPITAL_WIDTH : 1;
    // Match the baked accented glyphs to the slightly smaller native bitmap
    // ink while preserving the existing baseline calculation.
    unit *= 0.94f;
    float sy = scale * (font->scaleY != 0 ? font->scaleY : 1);
    float pixelX = sx * unit, pixelY = sy * unit;
    float glyphTop = y + ((capital ? capital->sourceHeight : SETTINGS_LATIN_CAPITAL_HEIGHT) -
                         SETTINGS_LATIN_CAPITAL_HEIGHT * unit - font->ascenderOffset) * sy;
    int begin = 0;
    for (int i = 0; i <= dst; ++i) {
        if (i != dst && !replacementCp[i]) continue;
        float x = left + TextUtils_measureLineWidth(font, plain, begin) * sx;
        char saved = plain[i];
        plain[i] = 0;
        if (i > begin)
            r->vtable->drawTextColor(r, plain + begin, x, y, scale, scale, 0,
                                    color, color, color, color, 1, -1);
        plain[i] = saved;
        if (i < dst) {
            x = left + TextUtils_measureLineWidth(font, plain, i) * sx;
            for (unsigned g = 0; g < sizeof(settingsLatinGlyphs) / sizeof(settingsLatinGlyphs[0]); ++g) {
                const SettingsLatinGlyph* glyph = &settingsLatinGlyphs[g];
                if (glyph->cp != replacementCp[i]) continue;
                // Draw the complete baked glyph, including its real accent,
                // with the same baseline. No base letter or accent overlay.
                for (int row = 0; row < glyph->h; ++row) {
                    for (int col = 0; col < glyph->w;) {
                        if (!(glyph->rows[row] & (1u << col))) { ++col; continue; }
                        int start = col++;
                        while (col < glyph->w && (glyph->rows[row] & (1u << col))) ++col;
                        r->vtable->drawRectangle(r,
                            x + (glyph->x + start) * pixelX, glyphTop + (glyph->y + row) * pixelY,
                            x + (glyph->x + col) * pixelX, glyphTop + (glyph->y + row + 1) * pixelY,
                            color, 1, false);
                    }
                }
                break;
            }
        }
        begin = i + 1;
    }
    r->drawHalign = oldHalign;
}

static void drawLabel(Renderer* r, const char* text, float x, float y, uint32_t color, float scale) {
    drawSettingsText(r, text, x, y, scale, color, 0);
}

static void drawControl(Renderer* r, const char* name, float centerX, float centerY, float targetSize, float alpha);

static void drawCenteredText(Renderer* r, const char* text, float centerX, float y,
                             float scale, uint32_t color) {
    drawSettingsText(r, text, centerX, y, scale, color, 1);
}

static void drawRightText(Renderer* r, const char* text, float rightX, float y,
                          float scale, uint32_t color) {
    drawSettingsText(r, text, rightX, y, scale, color, 2);
}

static void drawDialogBorder(Renderer* r, float left, float top, float right, float bottom) {
    // The game's textbox atlases can be evicted while a large Chapter 3/5
    // scene is active. The Settings frame must not depend on those atlases:
    // draw its edges with renderer primitives and use only the bundled corner.
    // The bundled corner is visually heavier than the old 14 px placement.
    // Draw it at the same scale as DELTARUNE's dialogue corners so it joins
    // the Settings frame without looking thinner than the straight edges.
    const float cornerSize = 29.0f;
    const float thickness = 5.0f;
    // The visible elbow is around the first third of the 16x16 source sprite,
    // not at the centre of its transparent box. Anchor that elbow on the
    // panel intersection and extend the straight edges underneath the corner.
    const float cornerAnchor = 7.0f;
    r->vtable->drawRectangle(r, left + thickness, top,
                             right - thickness, top + thickness,
                             0xFFFFFF, 1.0f, false);
    r->vtable->drawRectangle(r, left + thickness, bottom - thickness,
                             right - thickness, bottom,
                             0xFFFFFF, 1.0f, false);
    r->vtable->drawRectangle(r, left, top + thickness,
                             left + thickness, bottom - thickness,
                             0xFFFFFF, 1.0f, false);
    r->vtable->drawRectangle(r, right - thickness, top + thickness,
                             right, bottom - thickness,
                             0xFFFFFF, 1.0f, false);
    // DELTARUNE's textbox frame has a two-pixel blue-grey inner bevel. Keep
    // the outer three pixels white and tint only the edge facing the panel.
    const float bevel = 2.0f;
    // Renderer colours follow GameMaker's $BBGGRR packing. The requested
    // visible RGB #99AACC is therefore encoded as 0xCCAA99 here.
    const uint32_t bevelColor = 0xCCAA99;
    r->vtable->drawRectangle(r, left + thickness, top + thickness - bevel,
                             right - thickness, top + thickness,
                             bevelColor, 1.0f, false);
    r->vtable->drawRectangle(r, left + thickness, bottom - thickness,
                             right - thickness, bottom - thickness + bevel,
                             bevelColor, 1.0f, false);
    r->vtable->drawRectangle(r, left + thickness - bevel, top + thickness,
                             left + thickness, bottom - thickness,
                             bevelColor, 1.0f, false);
    r->vtable->drawRectangle(r, right - thickness, top + thickness,
                             right - thickness + bevel, bottom - thickness,
                             bevelColor, 1.0f, false);
    drawUIIconExt(r, "spr_textbox_topleft", left + cornerAnchor,
                  top + cornerAnchor, cornerSize, cornerSize, 1.0f, false, false);
    drawUIIconExt(r, "spr_textbox_topleft", right - cornerAnchor,
                  top + cornerAnchor, cornerSize, cornerSize, 1.0f, true, false);
    drawUIIconExt(r, "spr_textbox_topleft", left + cornerAnchor,
                  bottom - cornerAnchor, cornerSize, cornerSize, 1.0f, false, true);
    drawUIIconExt(r, "spr_textbox_topleft", right - cornerAnchor,
                  bottom - cornerAnchor, cornerSize, cornerSize, 1.0f, true, true);
}


void VitaSettings_draw(VitaSettings* s, Renderer* r) {
    if (!s->open) return;
    activeSettingsRenderer = r;
    if (s->borderWarningFrames > 0) s->borderWarningFrames--;
    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r, strcmp(s->activeLanguage, "English") != 0);

    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    queuedUIIconCount = 0;
    collectingUIIcons = true;
    const float panelLeft = 108.0f, panelTop = 48.0f, panelRight = 852.0f, panelBottom = 486.0f;
    // Keep the opaque fill inside the dialog frame. Drawing it up to the
    // outer coordinates leaked through the transparent pixels of the border.
    const float panelInset = 6.0f;
    r->vtable->drawRectangle(r, panelLeft + panelInset, panelTop + panelInset,
                             panelRight - panelInset, panelBottom - panelInset,
                             0x000000, 0.96f, false);
    drawDialogBorder(r, panelLeft, panelTop, panelRight, panelBottom);
    drawCenteredText(r, settingsText(s, "GAME SETTINGS", "CONFIGURAÇÕES", "AJUSTES DEL JUEGO"), 480, 79, 1.6f, 0xFFFFFF);

    if (s->trophiesMenuOpen) {
        int unlockedCount = 0;
        for (int i = 0; i < 30; ++i) if (s->trophiesUnlocked[i]) unlockedCount++;
        char progress[48];
        snprintf(progress, sizeof(progress), "%02d / 30", unlockedCount);
        int selectedTrophyId = s->trophyDisplayOrder[s->trophiesSelected];
        drawCenteredText(r, s->trophyDescriptions[selectedTrophyId], 480, 111, 1.08f, 0xFFFFFF);
        r->vtable->drawRectangle(r, 154, 142, 806, 421, 0x000000, 1.0f, false);
        drawDialogBorder(r, 154, 142, 806, 421);
        drawCenteredText(r, settingsText(s, "TROPHIES", "TROFÉUS", "TROFEOS"), 480, 157, 1.35f, 0xFFFFFF);
        drawLabel(r, progress, 177, 157, 0xFFFFFF, 1.10f);
        for (int row = 0; row < 3; ++row) {
            int displayIndex = s->trophiesScroll + row;
            if (displayIndex >= 30) break;
            int index = s->trophyDisplayOrder[displayIndex];
            float y = 203.0f + row * 72.0f;
            bool selected = displayIndex == s->trophiesSelected;
            bool unlocked = s->trophiesUnlocked[index];
            uint32_t color = selected ? 0x00FFFF : (unlocked ? 0xFFFFFF : 0x606060);
            char tierIcon[32];
            snprintf(tierIcon, sizeof(tierIcon), "trophy_darkheart_%d", trophyTier[index]);
            drawBundledControl(tierIcon, 267.0f, y + 13.0f, 48.0f, unlocked ? 1.0f : 0.30f);
            if (selected && r->drawFont >= 0 && (uint32_t)r->drawFont < r->dataWin->font.count) {
                Font* f = &r->dataWin->font.fonts[r->drawFont];
                float fontScaleX = f->scaleX != 0.0f ? f->scaleX : 1.0f;
                float titleWidth = TextUtils_measureLineWidth(f, s->trophyTitles[index], (int)strlen(s->trophyTitles[index])) * 1.48f * fontScaleX;
                float titleLeft = 518.0f - titleWidth * 0.5f;
                drawBundledControl("hearth1", titleLeft - 26.0f, y + 15.5f, 22.0f, 1.0f);
            }
            drawCenteredText(r, s->trophyTitles[index], 518, y + 3.0f, 1.48f, color);
        }
        const float dottedTop = 190.0f, dottedBottom = 365.0f;
        for (float dot = dottedTop; dot <= dottedBottom; dot += 11.0f)
            r->vtable->drawRectangle(r, 777.0f, dot, 781.0f, dot + 5.0f, 0xFFFFFF, 1.0f, false);
        const float markerY = dottedTop + (dottedBottom - dottedTop) * ((float)s->trophiesScroll / 27.0f);
        r->vtable->drawRectangle(r, 773.0f, markerY - 2.0f, 785.0f, markerY + 7.0f,
                                 0x00FFFF, 1.0f, false);
        drawCenteredText(r, "^", 779, 174, 1.1f, 0xFFFFFF);
        drawCenteredText(r, "v", 779, 377, 1.1f, 0xFFFFFF);
        drawBundledControl("button_ps4_triangle_0", 294.0f, 396.0f, 27.0f, 1.0f);
        drawLabel(r, settingsText(s, "SORT", "ORDENAR", "ORDENAR"), 317.0f, 385.0f, 0xFFFFFF, 1.12f);
        drawBundledControl("button_ps4_circle_0", 527.0f, 396.0f, 27.0f, 1.0f);
        drawLabel(r, settingsText(s, "RETURN", "VOLTAR", "VOLVER"), 550.0f, 385.0f, 0xFFFFFF, 1.12f);
        drawQueuedUIIconPass(r);
        r->vtable->endGUI(r);
        g_vitaPortOverlayFullScreen = 0;
        r->drawFont = oldFont;
        return;
    }

    if (s->controlEditMode) {
                                        drawCenteredText(r, settingsText(s, "EDIT TOUCH CONTROLS", "EDITAR CONTROLES", "EDITAR CONTROLES TÁCTILES"), 480, 120, 1.35f, 0x00FFFF);
        drawCenteredText(r, s->uiLabels[1][5] /* Placeholder, we should fix this properly if needed */, 480, 158, 1.15f, 0xFFFFFF);
        for (int i = 0; i < 4; ++i) {
            float size = (i == 0 ? 170.0f : 82.0f) * ((float)s->touchControlScale[i] / 100.0f);
            const char* sprite = i == 0 ? "spr_joybase" : (i == 1 ? "spr_control_zkey" : (i == 2 ? "spr_control_xkey" : "spr_control_ckey"));
            drawControl(r, sprite, (float)s->touchControlX[i], (float)s->touchControlY[i], size, i == s->selectedTouchControl ? 0.95f : 0.5f);
            if (i == s->selectedTouchControl)
                r->vtable->drawRectangle(r, s->touchControlX[i] - size * 0.55f, s->touchControlY[i] - size * 0.55f,
                                         s->touchControlX[i] + size * 0.55f, s->touchControlY[i] + size * 0.55f,
                                         0x00FFFF, 1.0f, true);
        }
        drawBundledControl("button_psv_touchA", 245.0f, 439.0f, 24.0f, 1.0f);
        drawLabel(r, settingsText(s, "TOUCH: MOVE", "TOQUE: MOVER", "TOCAR: MOVER"), 264.0f, 428.0f, 0xA0A0A0, 1.05f);
        drawBundledControl("button_psv_joyL", 430.0f, 439.0f, 24.0f, 1.0f);
        drawLabel(r, settingsText(s, "MOVE", "MOVER", "MOVER"), 449.0f, 428.0f, 0xA0A0A0, 1.05f);
        drawBundledControl("button_psv_joyR", 570.0f, 439.0f, 24.0f, 1.0f);
        drawLabel(r, settingsText(s, "SIZE", "TAMANHO", "TAMAÑO"), 589.0f, 428.0f, 0xA0A0A0, 1.05f);
        drawQueuedUIIconPass(r);
        drawControlEditorFooter(r, s);
        r->vtable->endGUI(r);
        g_vitaPortOverlayFullScreen = 0;
        r->drawFont = oldFont;
        return;
    }

    const char* valueOn = settingsText(s, "On", "Ligado", "Activado");
    const char* valueOff = settingsText(s, "Off", "Desligado", "Desactivado");
    const char* touch = s->touchEnabled ? valueOn : valueOff;
    const char* mod = (s->pendingModIndex >= 0 && s->pendingModIndex < s->modCount) ? s->modNames[s->pendingModIndex] : "English";
    const char* modDisplay = languageDisplayName(mod);
    const char* screen = s->consoleBorderMode == 1 ?
        settingsText(s, "Dynamic", "Dinâmicas", "Dinámicos") :
        (s->consoleBorderMode == 2 ?
            settingsText(s, "Simple", "Simples", "Simples") : valueOff);
    char masterVolume[32], sfxVolume[32], musicVolume[32], brightness[32];
    snprintf(masterVolume, sizeof(masterVolume), "%d%%", s->masterVolume * 10);
    snprintf(sfxVolume, sizeof(sfxVolume), "%d%%", s->sfxVolume * 10);
    snprintf(musicVolume, sizeof(musicVolume), "%d%%", s->musicVolume * 10);
    snprintf(brightness, sizeof(brightness), "%d%%", s->brightness);
    if (s->graphicsMenuOpen) {
        const char* pvr = s->pendingTextureFormatProfile == 0 ?
            settingsText(s, "Optimized", "Otimizado", "Optimizado") :
            (s->pendingTextureFormatProfile == 1 ?
                settingsText(s, "None", "Nenhuma", "Ninguna") :
                settingsText(s, "Aggressive", "Agressiva", "Agresiva"));
        const char* sync = s->pendingFpsTargetMode == 0 ? "30 FPS" :
                          (s->pendingFpsTargetMode == 1 ? "40 FPS" :
                          (s->pendingFpsTargetMode == 2 ? "60 FPS" :
                           settingsText(s, "Unlock", "Desbloqueado", "Desbloqueado")));
        const char* shaderMode = s->pendingShadersEnabled ? valueOn : valueOff;
        const char* backend = s->pendingRendererMode == VITA_RENDERER_EXPERIMENTAL ?
            settingsText(s, "Experimental GL", "Experimental GL", "Experimental GL") :
            (s->pendingRendererMode == VITA_RENDERER_MODERN ?
            settingsText(s, "Modern GL (experimental)", "Modern GL (experimental)", "Modern GL (experimental)") :
            (s->pendingRendererMode == VITA_RENDERER_LEGACY ?
                settingsText(s, "Legacy GL (safe)", "Legacy GL (seguro)", "Legacy GL (seguro)") :
                settingsText(s, "Automatic", "Automático", "Automático")));
        const char* screenFilter = s->pendingScreenFilterMode == 1 ? "Scanlines" :
            (s->pendingScreenFilterMode == 2 ? "Sharp Bilinear" :
            (s->pendingScreenFilterMode == 3 ? "Dithering Blending" :
            (s->pendingScreenFilterMode == 4 ? "VHS" :
             settingsText(s, "None", "Nenhum", "Ninguno"))));
        const char* values[5] = {pvr, sync, screenFilter, shaderMode, backend};
        drawCenteredText(r, settingsText(s, "GRAPHICS SETTINGS", "CONFIGURAÇÕES GRÁFICAS", "AJUSTES GRÁFICOS"), 480, 112, 1.4f, 0x00FFFF);
        for (int i = 0; i < 5; ++i) {
            float y = 154.0f + i * 43.0f;
            uint32_t color = i == s->graphicsMenuSelected ? 0x00FFFF : 0xFFFFFF;
            if (i == s->graphicsMenuSelected) drawBundledControl("hearth1", 208.0f, y + 12.0f, 22.0f, 1.0f);
            const char* subLabel = i == 0 ? settingsText(s, "Texture Compression", "Compressão de textura", "Compresión de textura") :
                                   i == 1 ? settingsText(s, "FPS Limit", "Limite de FPS", "Límite de FPS") :
                                   i == 2 ? settingsText(s, "Screen Filter", "Filtro de tela", "Filtro de pantalla") :
                                   i == 3 ? settingsText(s, "Shaders", "Shaders", "Shaders") :
                                            settingsText(s, "Renderer", "Renderizador", "Renderizador");
            if (strcmp(s->activeLanguage, "Italian") == 0) {
                subLabel = i == 0 ? "Compressione Texture" : i == 1 ? "Limite FPS" : i == 2 ? "Filtro Schermo" : i == 3 ? "Shader" : "Renderizzatore";
            } else if (strcmp(s->activeLanguage, "Turkish") == 0) {
                subLabel = i == 0 ? "Doku Sıkıştırma" : i == 1 ? "FPS Sınırı" : i == 2 ? "Ekran Filtresi" : i == 3 ? "Shaderlar" : "Oluşturucu";
            } else if (strcmp(s->activeLanguage, "German") == 0) {
                subLabel = i == 0 ? "Texturkompression" : i == 1 ? "FPS-Limit" : i == 2 ? "Bildfilter" : i == 3 ? "Shader" : "Renderer";
            } else if (strcmp(s->activeLanguage, "Russian") == 0 || strcmp(s->activeLanguage, "Russkiy") == 0) {
                subLabel = i == 0 ? "Szhatie tekstur" : i == 1 ? "Limit FPS" : i == 2 ? "Fil'tr ekrana" : i == 3 ? "Sheydery" : "Renderer";
            }
            drawLabel(r, subLabel, 230, y, color, 1.55f);
            drawRightText(r, values[i], 750, y, 1.45f, color);
        }
        const char* desc = "";
        if (s->graphicsMenuSelected == 0) {
            desc = s->pendingTextureFormatProfile == 0 ?
                settingsText(s, "Best balance between quality and memory usage.", "Melhor equilíbrio entre qualidade e uso de memória.", "Mejor equilibrio entre calidad y uso de memoria.") :
                (s->pendingTextureFormatProfile == 1 ?
                    settingsText(s, "Keeps textures uncompressed at their original dimensions.", "Mantém as texturas sem compressão e nas dimensões originais.", "Mantiene las texturas sin comprimir y en sus dimensiones originales.") :
                    settingsText(s, "Lower memory usage, with possible compression artifacts.", "Menor uso de memória, com possíveis artefatos de compressão.", "Menor uso de memoria, con posibles artefactos de compresión."));
        } else if (s->graphicsMenuSelected == 1) {
            desc = s->pendingFpsTargetMode == 3 ?
                settingsText(s, "Unlocked disables VSync; capped modes enable it automatically.", "Desbloqueado desativa o VSync; limites ativam automaticamente.", "Desbloqueado desactiva VSync; los límites lo activan automáticamente.") :
                settingsText(s, "The selected cap enables VSync automatically.", "O limite selecionado ativa o VSync automaticamente.", "El límite seleccionado activa VSync automáticamente.");
        } else if (s->graphicsMenuSelected == 2) {
            if (s->pendingScreenFilterMode == 1) desc = settingsText(s, "Adds lightweight horizontal scanlines.", "Adiciona linhas de varredura horizontais leves.", "Añade líneas de escaneo horizontales ligeras.");
            else if (s->pendingScreenFilterMode == 2) desc = settingsText(s, "Single-pass hardware smoothing without an extra surface.", "Suavização de hardware em um passe, sem surface extra.", "Suavizado por hardware en un pase, sin surface extra.");
            else if (s->pendingScreenFilterMode == 3) desc = settingsText(s, "Blends color transitions using a subtle dither pattern.", "Suaviza transições de cor com um padrão sutil de dithering.", "Suaviza transiciones de color con un patrón sutil de dithering.");
            else if (s->pendingScreenFilterMode == 4) desc = settingsText(s, "Lightweight scanlines, VHS tracking and analog noise in one pass.", "Scanlines, tracking VHS e ruído analógico leves em um único passe.", "Scanlines, tracking VHS y ruido analógico ligeros en una sola pasada.");
            else desc = settingsText(s, "Displays the original unfiltered image.", "Exibe a imagem original sem filtro.", "Muestra la imagen original sin filtro.");
        } else if (s->graphicsMenuSelected == 3) {
            desc = s->pendingShadersEnabled ?
                settingsText(s, "Uses the Vita shader compatibility layer for supported effects.", "Usa a camada de compatibilidade de shaders do Vita nos efeitos suportados.", "Usa la capa de compatibilidad de shaders de Vita en los efectos compatibles.") :
                settingsText(s, "Uses the fixed pipeline and skips GameMaker shader effects.", "Usa o pipeline fixo e ignora efeitos de shader do GameMaker.", "Usa el pipeline fijo e ignora efectos de shader de GameMaker.");
        } else {
            desc = settingsText(s, "Legacy GL is recommended on PS Vita.", "Legacy GL é recomendado no PS Vita.", "Legacy GL es recomendado en PS Vita.");
        }
        if (!s->confirmShaderChange)
            drawCenteredUpperText(r, desc, 480, 390, 0.98f, 0xB8B8B8);
        if (s->confirmShaderChange) {
            r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
            drawDialogBorder(r, 176, 174, 784, 370);
            drawCenteredText(r,
                settingsText(s, "APPLY SHADER CHANGE?", "APLICAR ALTERAÇÃO DE SHADERS?", "¿APLICAR CAMBIO DE SHADERS?"),
                480, 208, 1.42f, 0xFFFFFF);
            drawCenteredUpperText(r,
                settingsText(s, "THE CURRENT SCENE WILL RELOAD.", "A CENA ATUAL SERÁ RECARREGADA.", "LA ESCENA ACTUAL SE RECARGARÁ."),
                480, 260, 1.05f, 0xFFFF00);
        } else if (s->showRestartPrompt) {
            r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
            drawDialogBorder(r, 176, 174, 784, 370);
            drawCenteredText(r, settingsText(s, "RESTART TO APPLY?", "REINICIAR PARA APLICAR?", "¿REINICIAR PARA APLICAR?"), 480, 208, 1.50f, 0xFFFFFF);
            drawCenteredUpperText(r, restartWarningText(s), 480, 246, 0.95f, 0xFFFF00);
            drawCenteredUpperText(r, restartSaveWarningText(s), 480, 274, 0.95f, 0xFFFF00);
        }
        drawQueuedUIIconPass(r);
        if (s->confirmShaderChange) drawConfirmControlFooter(r, s);
        else if (s->showRestartPrompt) drawRestartControlFooter(r, s);
        else drawApplyCancelFooter(r, s);
        r->vtable->endGUI(r);
        g_vitaPortOverlayFullScreen = 0;
        r->drawFont = oldFont;
        return;
    }
                    
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
        int iniCategory = getIniCategoryIndex(logicalIndex);
        const char* tabLabel = s->uiCategories[iniCategory];
        if (tabLabel == NULL || tabLabel[0] == '\0') {
            const char* categoriesPt[SETTINGS_CATEGORIES] = {"DEV", "CONTROLES", "VÍDEO", "SOM", "SISTEMA"};
            const char* categoriesEn[SETTINGS_CATEGORIES] = {"DEV", "CONTROLS", "VIDEO", "SOUND", "SYSTEM"};
            const char* categoriesEs[SETTINGS_CATEGORIES] = {"DEV", "CONTROLES", "VÍDEO", "SONIDO", "SISTEMA"};
            const char* categoriesIt[SETTINGS_CATEGORIES] = {"DEV", "CONTROLLI", "VIDEO", "SUONO", "SISTEMA"};
            const char* categoriesTr[SETTINGS_CATEGORIES] = {"DEV", "KONTROLLER", "VİDEO", "SES", "SISTEM"};
            const char* categoriesDe[SETTINGS_CATEGORIES] = {"DEV", "STEUERUNG", "VIDEO", "TON", "SYSTEM"};
            const char* categoriesRu[SETTINGS_CATEGORIES] = {"DEV", "UPRAVLENIE", "VIDEO", "ZVUK", "SISTEMA"};
            
            if (strcmp(s->activeLanguage, "Portuguese-BR") == 0) tabLabel = categoriesPt[logicalIndex];
            else if (strcmp(s->activeLanguage, "Spanish") == 0) tabLabel = categoriesEs[logicalIndex];
            else if (strcmp(s->activeLanguage, "Italian") == 0) tabLabel = categoriesIt[logicalIndex];
            else if (strcmp(s->activeLanguage, "Turkish") == 0) tabLabel = categoriesTr[logicalIndex];
            else if (strcmp(s->activeLanguage, "German") == 0) tabLabel = categoriesDe[logicalIndex];
            else if (strcmp(s->activeLanguage, "Russian") == 0 || strcmp(s->activeLanguage, "Russkiy") == 0) tabLabel = categoriesRu[logicalIndex];
            else tabLabel = categoriesEn[logicalIndex];
        }
        drawCenteredText(r, tabLabel, centerX, 132, 1.15f,
                         logicalIndex == logicalSelectedCategory ? 0x00FFFF : 0x808080);
    }
    char adjustment[64];
    snprintf(adjustment, sizeof(adjustment), "%d,%d  %d%%", s->displayOffsetX, s->displayOffsetY, s->displayZoom);
    const char* labelsPt[SETTINGS_CATEGORIES][6] = {
        {"Navegador de salas", "Debug DEV", "Máscaras de colisão", "Novo log de diagnóstico", "Enviar log atual", ""},
        {"Pular falas", "Controles touch", "Editar controles", ""}, 
        {"Bordas de console", "Bordas melhores", "Ajustar tela", "Gráficos", "Brilho", ""},
        {"Volume mestre", "Efeitos sonoros", "Música", "Desabilitar áudio"}, 
        {"Idioma", "Voltar aos capítulos", "Troféus", "Notificações de troféu", "", ""}
    };
    const char* labelsEn[SETTINGS_CATEGORIES][6] = {
        {"Room navigator", "Debug DEV", "Collision masks", "New diagnostic log", "Send current log", ""},
        {"Skip dialogs", "Touch controls", "Edit controls", ""}, 
        {"Console borders", "Better borders", "Adjust screen", "Graphics settings", "Brightness", ""},
        {"Master volume", "Sound effects", "Music", "Disable audio"}, 
        {"Language", "Chapter Select", "Trophies", "Trophy notifications", "", ""}
    };
    const char* labelsEs[SETTINGS_CATEGORIES][6] = {
        {"Navegador de salas", "Depuración DEV", "Máscaras de colisión", "Nuevo registro de diagnóstico", "Enviar registro actual", ""},
        {"Omitir diálogos", "Controles táctiles", "Editar controles", ""},
        {"Bordes de consola", "Bordes mejorados", "Ajustar pantalla", "Ajustes gráficos", "Brillo", ""},
        {"Volumen general", "Efectos de sonido", "Música", "Desactivar audio"},
        {"Idioma", "Selección de capítulos", "Trofeos", "Notificaciones de trofeos", "", ""}
    };
    const char* labelsIt[SETTINGS_CATEGORIES][6] = {
        {"Navigatore stanze", "Debug DEV", "Maschere di collisione", "Nuovo registro diagnostico", "Invia log corrente", ""},
        {"Salta dialoghi", "Controlli touch", "Modifica controlli", ""}, 
        {"Bordi console", "Bordi HD", "Regola schermo", "Impostazioni grafiche", "Luminosità", ""},
        {"Volume principale", "Effetti sonori", "Musica", "Disattiva audio"}, 
        {"Lingua", "Selezione capitolo", "Trofei", "Notifiche trofei", "", ""}
    };
    const char* labelsTr[SETTINGS_CATEGORIES][6] = {
        {"Oda gezgini", "Hata ayıklama DEV", "Çarpışma maskeleri", "Yeni tanılama günlüğü", "Guncel logu gonder", ""},
        {"Diyalogları geç", "Dokunmatik kontroller", "Kontrolleri düzenle", ""}, 
        {"Konsol kenarlıkları", "HD Kenarlıklar", "Ekranı ayarla", "Grafik ayarları", "Parlaklık", ""},
        {"Ana ses", "Ses efektleri", "Müzik", "Sesi kapat"}, 
        {"Dil", "Bölüm Seçimi", "Başarımlar", "Başarım bildirimleri", "", ""}
    };
    const char* labelsDe[SETTINGS_CATEGORIES][6] = {
        {"Room-Navigator", "DEV-Debug", "Kollisionsmasken", "Neues Diagnoseprotokoll", "Aktuelles Log senden", ""},
        {"Dialoge überspringen", "Touch-Steuerung", "Steuerung bearbeiten", ""},
        {"Konsolenrahmen", "Bessere Rahmen", "Bild anpassen", "Grafikeinstellungen", "Helligkeit", ""},
        {"Gesamtlautstärke", "Soundeffekte", "Musik", "Audio deaktivieren"},
        {"Sprache", "Kapitelauswahl", "Trophäen", "Trophäenmeldungen", "", ""}
    };
    const char* labelsRu[SETTINGS_CATEGORIES][6] = {
        {"Navigator komnat", "Otladka DEV", "Maski stolknoveniy", "Novyy zhurnal diagnostiki", "Otpravit tekushchiy log", ""},
        {"Propusk dialogov", "Sensornoe upravlenie", "Izmenit' upravlenie", ""},
        {"Ramki konsoli", "Uluchshennye ramki", "Nastroit' ekran", "Nastroyki grafiki", "Yarkost'", ""},
        {"Obshchaya gromkost'", "Zvukovye effekty", "Muzyka", "Otkluchit' zvuk"},
        {"Yazyk", "Vybor glavy", "Trofei", "Uvedomleniya trofeev", "", ""}
    };
    const char* values[SETTINGS_CATEGORIES][6] = {
        {s->devRoomNavEnabled ? valueOn : valueOff, s->debugDevEnabled ? valueOn : valueOff, s->debugCollisionMasks ? valueOn : valueOff, ">", ">", ""},
        {s->shortcutSkipDialogs ? valueOn : valueOff, touch, s->controlEditMode ? settingsText(s, "Open", "Aberto", "Abierto") : ">", ""}, 
        {screen, s->betterBordersEnabled ? valueOn : valueOff, adjustment, ">", brightness, ""},
        {masterVolume, sfxVolume, musicVolume, s->audioDisabled ? valueOn : valueOff}, 
        {modDisplay, "", ">", s->internalTrophyNotifications ? valueOn : valueOff, "", ""}
    };
    int visibleItems = settingsItemCount(s->category);
    for (int i = 0; i < visibleItems; ++i) {
        float y = visibleItems == 6 ? 172.0f + i * 34.0f : (visibleItems == 5 ? 184.0f + i * 38.0f : (visibleItems == 4 ? 200.0f + i * 42.0f : (visibleItems == 3 ? 206.0f + i * 50.0f : 214.0f + i * 64.0f)));
        uint32_t textColor = i == s->selected ? 0x00FFFF : 0xFFFFFF;
        if (i == s->selected) drawBundledControl("hearth1", 228.0f, y + 12.0f, 22.0f, 1.0f);
        int dataIndex = logicalSelectedCategory == 2 ? getVideoDataIndex(s, i) : i;
        const char* label = (i == 4 && logicalSelectedCategory == 4)
            ? settingsText(s, "Restore defaults", "Restaurar padrão", "Restaurar valores")
            : (strcmp(s->activeLanguage, "Portuguese-BR") == 0 ? labelsPt[logicalSelectedCategory][dataIndex] :
               (strcmp(s->activeLanguage, "Spanish") == 0 ? labelsEs[logicalSelectedCategory][dataIndex] :
               (strcmp(s->activeLanguage, "Italian") == 0 ? labelsIt[logicalSelectedCategory][dataIndex] :
               (strcmp(s->activeLanguage, "Turkish") == 0 ? labelsTr[logicalSelectedCategory][dataIndex] :
               (strcmp(s->activeLanguage, "German") == 0 ? labelsDe[logicalSelectedCategory][dataIndex] :
               ((strcmp(s->activeLanguage, "Russian") == 0 || strcmp(s->activeLanguage, "Russkiy") == 0) ? labelsRu[logicalSelectedCategory][dataIndex] :
               labelsEn[logicalSelectedCategory][dataIndex]))))));
        drawLabel(r, label, 252, y, textColor, 1.60f);
        if ((logicalSelectedCategory == 3 && i < 3) ||
            (logicalSelectedCategory == 2 && dataIndex == 4)) {
            int sliderValue = logicalSelectedCategory == 2 ? s->brightness / 10 :
                              (i == 0 ? s->masterVolume : (i == 1 ? s->sfxVolume : s->musicVolume));
            float sliderLeft = 570.0f, sliderRight = 698.0f, sliderY = y + 10.0f;
            r->vtable->drawRectangle(r, sliderLeft, sliderY, sliderRight, sliderY + 8.0f,
                                     0x505050, 1.0f, false);
            r->vtable->drawRectangle(r, sliderLeft, sliderY,
                                     sliderLeft + (sliderRight - sliderLeft) * ((float)sliderValue / 10.0f),
                                     sliderY + 8.0f, i == s->selected ? 0x00FFFF : 0xFFFFFF, 1.0f, false);
            drawRightText(r, values[logicalSelectedCategory][dataIndex], 742, y, 1.25f,
                          i == s->selected ? 0x00FFFF : 0xFFFFFF);
        } else {
            drawRightText(r, values[logicalSelectedCategory][dataIndex], 742, y, 1.6f,
                          i == s->selected ? 0x00FFFF : 0xFFFFFF);
            if (logicalSelectedCategory == 4 && dataIndex == 0) {
                drawLanguageIcon(mod, 769.0f, y + 14.0f,
                                 i == s->selected ? 1.0f : 0.92f);
            }
        }
    }
    const char* selectedHelp = "";
    int selectedDataIndex = logicalSelectedCategory == 2 ? getVideoDataIndex(s, s->selected) : s->selected;
    if (logicalSelectedCategory == 0) {
        if (selectedDataIndex == 0) selectedHelp = settingsText(s, "Loads a specific Room for testing.", "Carrega uma Room específica para testes.", "Carga una Room específica para pruebas.");
        else if (selectedDataIndex == 1) selectedHelp = settingsText(s, "Shows performance diagnostics and writes a Dev Log.", "Exibe diagnósticos e grava um Dev Log.", "Muestra diagnósticos y guarda un Dev Log.");
        else if (selectedDataIndex == 2) selectedHelp = settingsText(s, "Shows collision masks for debugging.", "Exibe máscaras de colisão para depuração.", "Muestra máscaras de colisión para depuración.");
        else if (selectedDataIndex == 3) selectedHelp = settingsText(s, "Archives the current probe and starts a new log immediately.", "Arquiva o probe atual e inicia um novo log imediatamente.", "Archiva el probe actual e inicia un registro nuevo inmediatamente.");
        else selectedHelp = g_probeUploadStatus;
    } else if (logicalSelectedCategory == 1) {
        if (selectedDataIndex == 0) selectedHelp = settingsText(s, "Allows Triangle to advance dialogue quickly.", "Permite usar Triângulo para avançar falas rapidamente.", "Permite usar Triángulo para avanzar diálogos rápidamente.");
        else if (selectedDataIndex == 1) selectedHelp = settingsText(s, "Enables the optional touch controls.", "Ativa os controles opcionais por toque.", "Activa los controles táctiles opcionales.");
        else selectedHelp = settingsText(s, "Changes touch button positions and sizes.", "Altera posições e tamanhos dos botões touch.", "Cambia posiciones y tamaños de los botones táctiles.");
    } else if (logicalSelectedCategory == 2) {
        if (selectedDataIndex == 0) selectedHelp = settingsText(s, "Selects dynamic, simple or disabled borders.", "Seleciona bordas dinâmicas, simples ou desligadas.", "Selecciona bordes dinámicos, simples o desactivados.");
        else if (selectedDataIndex == 1) selectedHelp = settingsText(s, "Uses the higher-quality border set.", "Usa o conjunto de bordas de maior qualidade.", "Usa el conjunto de bordes de mayor calidad.");
        else if (selectedDataIndex == 2) selectedHelp = settingsText(s, "Adjusts game position and scale.", "Ajusta posição e escala do jogo.", "Ajusta posición y escala del juego.");
        else if (selectedDataIndex == 3) selectedHelp = settingsText(s, "Opens texture, FPS, filter, shader and renderer options.", "Abre opções de textura, FPS, filtro, shaders e renderizador.", "Abre opciones de textura, FPS, filtro, shaders y renderizador.");
        else selectedHelp = settingsText(s, "Adjusts screen brightness.", "Ajusta o brilho da tela.", "Ajusta el brillo de la pantalla.");
    } else if (logicalSelectedCategory == 3) {
        if (selectedDataIndex == 0) selectedHelp = settingsText(s, "Controls the complete game volume.", "Controla o volume total do jogo.", "Controla el volumen total del juego.");
        else if (selectedDataIndex == 1) selectedHelp = settingsText(s, "Controls sound-effect volume.", "Controla o volume dos efeitos sonoros.", "Controla el volumen de los efectos de sonido.");
        else if (selectedDataIndex == 2) selectedHelp = settingsText(s, "Controls music volume.", "Controla o volume das músicas.", "Controla el volumen de la música.");
        else selectedHelp = settingsText(s, "Disables all audio to reduce processing cost.", "Desativa todo o áudio para reduzir processamento.", "Desactiva todo el audio para reducir procesamiento.");
    } else if (logicalSelectedCategory == 4) {
        if (selectedDataIndex == 0) selectedHelp = settingsText(s, "Changes the game language data.", "Altera os dados de idioma do jogo.", "Cambia los datos de idioma del juego.");
        else if (selectedDataIndex == 1) selectedHelp = settingsText(s, "Returns to Chapter Select.", "Volta para a seleção de capítulos.", "Vuelve a la selección de capítulos.");
        else if (selectedDataIndex == 2) selectedHelp = settingsText(s, "Shows unlocked and available trophies.", "Exibe troféus disponíveis e desbloqueados.", "Muestra trofeos disponibles y desbloqueados.");
        else if (selectedDataIndex == 3) selectedHelp = settingsText(s,
            "Shows the port trophy popup. With native trophies active it moves to the left.",
            "Exibe a notificação de troféu do port. Com troféus nativos ativos, ela vai para a esquerda.",
            "Muestra la notificación de trofeo del port. Con trofeos nativos, aparece a la izquierda.");
        else selectedHelp = settingsText(s, "Restores the recommended defaults.", "Restaura as configurações recomendadas.", "Restaura los ajustes recomendados.");
    }
    if (!s->confirmChapterSelect && !s->confirmRotateLog && !s->showRestartPrompt && s->borderWarningFrames <= 0)
        drawCenteredUpperText(r, selectedHelp, 480, 397, 1.02f, 0xB8B8B8);
    if (s->borderWarningFrames > 0) {
        r->vtable->drawRectangle(r, 176, 354, 784, 414, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 354, 784, 414);
        drawCenteredText(r,
            settingsText(s, "BORDER FILES WERE NOT FOUND.", "ARQUIVOS DE BORDA NÃO ENCONTRADOS.", "NO SE ENCONTRARON LOS ARCHIVOS DE BORDES."),
            480, 372, 1.05f, 0xFFFF00);
    }

    if (s->confirmChapterSelect) {
        r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 174, 784, 370);
        drawCenteredText(r, settingsText(s, "RETURN TO CHAPTER SELECT?", "VOLTAR AOS CAPÍTULOS?", "¿VOLVER A LA SELECCIÓN DE CAPÍTULOS?"),
                         480, 208, 1.55f, 0xFFFFFF);
        drawCenteredText(r, settingsText(s, "UNSAVED PROGRESS WILL BE LOST.", "O PROGRESSO NÃO SALVO SERÁ PERDIDO.", "SE PERDERÁ EL PROGRESO NO GUARDADO."),
                         480, 260, 1.25f, 0xFFFF00);
    }

    if (s->confirmRotateLog) {
        r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 174, 784, 370);
        drawCenteredText(r, settingsText(s, "START NEW LOG FILE?", "INICIAR NOVO ARQUIVO DE LOG?", "¿INICIAR NUEVO REGISTRO?"),
                         480, 208, 1.55f, 0xFFFFFF);
        drawCenteredText(r, settingsText(s, "CURRENT LOG WILL BE ARCHIVED.", "O LOG ATUAL SERÁ ARQUIVADO.", "EL REGISTRO ACTUAL SERÁ ARCHIVADO."),
                         480, 260, 1.25f, 0xFFFF00);
    }
    
    if (s->showRestartPrompt) {
        r->vtable->drawRectangle(r, 176, 174, 784, 370, 0x000000, 0.98f, false);
        drawDialogBorder(r, 176, 174, 784, 370);
        drawCenteredText(r, settingsText(s, "RESTART TO APPLY?", "REINICIAR PARA APLICAR?", "¿REINICIAR PARA APLICAR?"),
                         480, 208, 1.50f, 0xFFFFFF);
        drawCenteredUpperText(r, restartWarningText(s), 480, 246, 0.95f, 0xFFFF00);
        drawCenteredUpperText(r, restartSaveWarningText(s), 480, 274, 0.95f, 0xFFFF00);
        
    }
    drawQueuedUIIconPass(r);
    if (s->confirmChapterSelect || s->confirmRotateLog)
        drawConfirmControlFooter(r, s);
    else if (s->showRestartPrompt)
        drawRestartControlFooter(r, s);
    else
        drawSettingsControlFooter(r, s);
    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
    r->drawFont = oldFont;
}

void VitaSettings_updateTrophies(VitaSettings* s) {
    if (++s->trophyPollFrames < 30) {
        if (s->trophyNotificationFrames > 0) s->trophyNotificationFrames--;
        return;
    }
    s->trophyPollFrames = 0;
    bool current[30];
    bool enabled = true;
    if (!readTrophyUnlocks(current, &enabled)) return;
    if (!s->trophyStateInitialized) {
        memcpy(s->trophiesUnlocked, current, sizeof(current));
        rebuildTrophyDisplayOrder(s);
        s->trophyStateInitialized = true;
        return;
    }
    for (int i = 0; i < 30; ++i) {
        if (current[i] && !s->trophiesUnlocked[i]) {
            s->trophyNotificationId = i;
            s->trophyNotificationFrames = 300;
        }
    }
    memcpy(s->trophiesUnlocked, current, sizeof(current));
    if (s->trophiesUnlockedFirst) rebuildTrophyDisplayOrder(s);
    s->trophiesEnabled = enabled;
    if (s->trophyNotificationFrames > 0) s->trophyNotificationFrames--;
}

uint32_t VitaSettings_syncGameTrophies(VitaSettings* s, const bool unlocked[30]) {
    if (s == NULL || unlocked == NULL) return 0;
    bool merged[30];
    bool enabled = true;
    readTrophyUnlocks(merged, &enabled);
    uint32_t changed = 0;
    for (int i = 0; i < 30; ++i) {
        if (!unlocked[i] || merged[i]) continue;
        merged[i] = true;
        s->trophiesUnlocked[i] = true;
        s->trophyNotificationId = i;
        s->trophyNotificationFrames = 300;
        changed |= 1U << i;
    }
    // Trophy 0 is the platinum entry. DELTARUNE's original event manager only
    // queues it on a non-console debug path, so consolidate it here after all
    // 29 gameplay trophies are present. This keeps trophies.ini, the in-game
    // list and the native Vita state consistent.
    bool allGameplayTrophies = true;
    for (int i = 1; i < 30; ++i) {
        if (!merged[i]) {
            allGameplayTrophies = false;
            break;
        }
    }
    if (allGameplayTrophies && !merged[0]) {
        merged[0] = true;
        s->trophiesUnlocked[0] = true;
        s->trophyNotificationId = 0;
        s->trophyNotificationFrames = 300;
        changed |= 1U;
    }
    if (changed) writeTrophyUnlocks(merged, enabled);
    uint32_t unlockedMask = 0;
    for (int i = 0; i < 30; ++i)
        if (merged[i]) unlockedMask |= 1U << i;
    return unlockedMask;
}

void VitaSettings_setNativeTrophiesActive(VitaSettings* s, bool active) {
    if (s == NULL) return;
    s->nativeTrophiesActive = active;
    // No saved preference means "automatic": keep the internal popup when the
    // port is using local trophies, but avoid a duplicate popup when the native
    // NP trophy path (normally NoTrpDrm) is available.
    if (!s->internalTrophyNotificationsConfigured)
        s->internalTrophyNotifications = !active;
}

void VitaSettings_drawTrophyNotification(VitaSettings* s, Renderer* r) {
    if (s->trophyNotificationFrames <= 0 || s->trophyNotificationId < 0 ||
        s->trophyNotificationId >= 30 || !s->trophiesEnabled ||
        !s->internalTrophyNotifications) return;
    int id = s->trophyNotificationId;
    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r, strcmp(s->activeLanguage, "English") != 0);
    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);

    // Native Vita/NoTrpDrm notifications normally occupy the upper-right. If
    // the user explicitly keeps both notification systems enabled, put the
    // port popup on the opposite side so the two can be read independently.
    const float panelLeft = s->nativeTrophiesActive ? 26.0f : 548.0f;
    const float panelRight = panelLeft + 386.0f;
    const float iconX = panelLeft + 46.0f;
    const float textX = panelLeft + 87.0f;
    r->vtable->drawRectangle(r, panelLeft, 24, panelRight, 118, 0x000000, 0.96f, false);
    drawDialogBorder(r, panelLeft, 24, panelRight, 118);
    char icon[32];
    snprintf(icon, sizeof(icon), "trophy_darkheart_%d", trophyTier[id]);
    drawBundledControl(icon, iconX, 71.0f, 54.0f, 1.0f);
    drawLabel(r, settingsText(s, "TROPHY UNLOCKED!", "TROFÉU DESBLOQUEADO!", "¡TROFEO DESBLOQUEADO!"),
              textX, 44.0f, 0x00FFFF, 1.02f);
    drawLabel(r, s->trophyTitles[id], textX, 76.0f, 0xFFFFFF, 1.12f);
    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
    r->drawFont = oldFont;
}

void VitaSettings_drawBrightness(VitaSettings* s, Renderer* r) {
    if (s == NULL || r == NULL || s->brightness >= 100) return;
    float darkness = 1.0f - (float)s->brightness / 100.0f;
    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    r->vtable->drawRectangle(r, 0.0f, 0.0f, 960.0f, 544.0f,
                             0x000000, darkness, false);
    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
}

void VitaSettings_drawDevOverlay(VitaSettings* s, Renderer* r,
                                 const char* room, float fps, uint64_t stepUs,
                                 uint64_t audioUs, uint64_t renderUs,
                                 uint64_t gpuBytes, uint32_t evictions,
                                 uint32_t deferred, uint32_t ramHits,
                                 const char* devTargetRoom, int32_t devTargetIndex) {
    if (!s->devMode || (!s->debugDevEnabled && !s->devRoomNavEnabled) || s->open) return;
    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r, strcmp(s->activeLanguage, "English") != 0);
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);

    if (s->debugDevEnabled) {
        // A 20% larger diagnostic face remains readable at the Vita's native
        // resolution. Grow the panel with it so lines never overlap.
        r->vtable->drawRectangle(r, 8, 8, 944, 166, 0x000000, 0.86f, false);
        r->vtable->drawRectangle(r, 8, 8, 944, 166, 0x606060, 0.70f, true);
        char line[192];
        snprintf(line, sizeof(line), "DEV  %s", room != NULL ? room : "<null>");
        r->vtable->drawTextColor(r, line, 20, 17, 1.66f, 1.66f, 0,
                                 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1, -1);
        snprintf(line, sizeof(line), "FPS %.1f   STEP %lluus   AUDIO %lluus   RENDER %lluus",
                 fps, (unsigned long long)stepUs, (unsigned long long)audioUs,
                 (unsigned long long)renderUs);
        r->vtable->drawTextColor(r, line, 20, 55, 1.36f, 1.36f, 0,
                                 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1, -1);
        snprintf(line, sizeof(line), "GPU %.1f MiB   EVICT %u   DEFER %u   RAM HIT %u",
                 (double)gpuBytes / 1048576.0, evictions, deferred, ramHits);
        r->vtable->drawTextColor(r, line, 20, 91, 1.36f, 1.36f, 0,
                                 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 1, -1);
        snprintf(line, sizeof(line), "TARGET [%d] %s",
                 devTargetIndex, devTargetRoom != NULL ? devTargetRoom : "<none>");
        r->vtable->drawTextColor(r, line, 20, 127, 1.32f, 1.32f, 0,
                                 0xFFFF00, 0xFFFF00, 0xFFFF00, 0xFFFF00, 1, -1);
    }

    if (s->devRoomNavEnabled) {
        // Seven nearby rooms provide context. The wider panel accommodates the
        // requested 20% typography increase without clipping long room names.
        const float left = 214.0f, top = s->debugDevEnabled ? 176.0f : 42.0f;
        const float right = 952.0f, bottom = 536.0f;
        r->vtable->drawRectangle(r, left, top, right, bottom, 0x000000, 0.95f, false);
        r->vtable->drawRectangle(r, left, top, right, bottom, 0xFFFFFF, 0.78f, true);
        char navLine[192];
        snprintf(navLine, sizeof(navLine), "%s",
                 settingsText(s, "ROOM NAVIGATOR", "NAVEGADOR DE ROOMS", "NAVEGADOR DE ROOMS"));
        r->vtable->drawTextColor(r, navLine, left + 20, top + 12, 1.64f, 1.64f, 0,
                                 0xFFFF00, 0xFFFF00, 0xFFFF00, 0xFFFF00, 1, -1);
        snprintf(navLine, sizeof(navLine), "%s: %s",
                 settingsText(s, "CURRENT", "ATUAL", "ACTUAL"),
                 room != NULL ? room : settingsText(s, "<none>", "<nenhuma>", "<ninguna>"));
        r->vtable->drawTextColor(r, navLine, left + 20, top + 47, 1.27f, 1.27f, 0,
                                 0xA0A0A0, 0xA0A0A0, 0xA0A0A0, 0xA0A0A0, 1, -1);

        if (r->dataWin->room.count > 0) {
            for (int offset = -3; offset <= 3; ++offset) {
                int32_t index = devTargetIndex + offset;
                while (index < 0) index += (int32_t)r->dataWin->room.count;
                while ((uint32_t)index >= r->dataWin->room.count)
                    index -= (int32_t)r->dataWin->room.count;
                const char* name = r->dataWin->room.rooms[index].name;
                snprintf(navLine, sizeof(navLine), "%s %03d  %s",
                         offset == 0 ? ">" : " ", index, name != NULL ? name : "<unnamed>");
                uint32_t color = offset == 0 ? 0x00FFFF : 0xB0B0B0;
                float scale = offset == 0 ? 1.52f : 1.27f;
                r->vtable->drawTextColor(r, navLine, left + 20,
                                         top + 82 + (offset + 3) * 32,
                                         scale, scale, 0, color, color, color, color, 1, -1);
            }
        }

        // Three self-contained command cards are easier to scan than the old
        // staggered icons, separators and labels.
        const float footerTop = bottom - 68.0f;
        const float cardTop = footerTop + 4.0f, cardBottom = bottom - 8.0f;
        const float card1L = left + 12.0f, card1R = left + 230.0f;
        const float card2L = left + 238.0f, card2R = left + 470.0f;
        const float card3L = left + 478.0f, card3R = right - 12.0f;
        r->vtable->drawRectangle(r, card1L, cardTop, card1R, cardBottom, 0x181818, 0.98f, false);
        r->vtable->drawRectangle(r, card2L, cardTop, card2R, cardBottom, 0x181818, 0.98f, false);
        r->vtable->drawRectangle(r, card3L, cardTop, card3R, cardBottom, 0x181818, 0.98f, false);
        r->vtable->drawRectangle(r, card1L, cardTop, card1R, cardBottom, 0x505050, 0.9f, true);
        r->vtable->drawRectangle(r, card2L, cardTop, card2R, cardBottom, 0x505050, 0.9f, true);
        r->vtable->drawRectangle(r, card3L, cardTop, card3R, cardBottom, 0x00A0A0, 0.9f, true);

        const float iconY = footerTop + 30.0f;
        drawBundledControl("button_ps4_dpad_up", card1L + 24, iconY, 30.0f, 1.0f);
        drawBundledControl("button_ps4_dpad_down", card1L + 58, iconY, 30.0f, 1.0f);
        r->vtable->drawTextColor(r, "MOVE 1", card1L + 88, footerTop + 16, 1.10f, 1.10f, 0,
                                 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1, -1);

        drawBundledControl("button_ps4_dpad_left", card2L + 24, iconY, 30.0f, 1.0f);
        drawBundledControl("button_ps4_dpad_right", card2L + 58, iconY, 30.0f, 1.0f);
        r->vtable->drawTextColor(r, "MOVE 10", card2L + 88, footerTop + 16, 1.10f, 1.10f, 0,
                                 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1, -1);

        drawBundledControl("button_psv_L", card3L + 22, iconY, 31.0f, 1.0f);
        r->vtable->drawTextColor(r, "+", card3L + 47, footerTop + 15, 1.00f, 1.00f, 0,
                                 0xA0A0A0, 0xA0A0A0, 0xA0A0A0, 0xA0A0A0, 1, -1);
        drawBundledControl("button_ps4_cross_0", card3L + 72, iconY, 31.0f, 1.0f);
        r->vtable->drawTextColor(r, settingsText(s, "LOAD", "ABRIR", "ABRIR"),
                                 card3L + 101, footerTop + 16, 1.10f, 1.10f, 0,
                                 0x00FFFF, 0x00FFFF, 0x00FFFF, 0x00FFFF, 1, -1);
    }
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

typedef struct {
    const char* name;
    float centerX, centerY, w, h, alpha;
    bool flipX, flipY;
} QueuedUIIcon;

static QueuedUIIcon queuedUIIcons[64];

static void touchControlLog(const char* phase, const char* name) {
    printf("TOUCH=%s control=%s\n", phase, name);
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
    {"spr_joybase", "app0:assets/ui/spr_joybase_frame0.png", 0, 0, 0},
    {"spr_joystick", "app0:assets/ui/spr_joystick_frame0.png", 0, 0, 0},
    {"spr_control_zkey", "app0:assets/ui/spr_control_zkey_frame0.png", 0, 0, 0},
    {"spr_control_xkey", "app0:assets/ui/spr_control_xkey_frame0.png", 0, 0, 0},
    {"spr_control_ckey", "app0:assets/ui/spr_control_ckey_frame0.png", 0, 0, 0},
    {"button_ps4_circle_0", "app0:assets/ui/button_ps4_circle_0.png", 0, 0, 0},
    {"button_ps4_cross_0", "app0:assets/ui/button_ps4_cross_0.png", 0, 0, 0},
    {"button_ps4_triangle_0", "app0:assets/ui/button_ps4_triangle_0.png", 0, 0, 0},
    {"button_psv_L", "app0:assets/ui/button_psv_L.png", 0, 0, 0},
    {"button_psv_R", "app0:assets/ui/button_psv_R.png", 0, 0, 0},
    {"button_psv_select", "app0:assets/ui/button_psv_select.png", 0, 0, 0},
    {"button_psv_joyL", "app0:assets/ui/button_psv_joyL.png", 0, 0, 0},
    {"button_psv_joyR", "app0:assets/ui/button_psv_joyR.png", 0, 0, 0},
    {"button_psv_touchA", "app0:assets/ui/button_psv_touchA.png", 0, 0, 0},
    {"hearth1", "app0:assets/ui/hearth1.png", 0, 0, 0},
    {"trophy_heart", "app0:assets/ui/spr_heart_0.png", 0, 0, 0},
    {"trophy_darkheart_0", "app0:assets/ui/spr_darkheart_0.png", 0, 0, 0},
    {"trophy_darkheart_1", "app0:assets/ui/spr_darkheart_1.png", 0, 0, 0},
    {"trophy_darkheart_2", "app0:assets/ui/spr_darkheart_2.png", 0, 0, 0},
    {"trophy_darkheart_3", "app0:assets/ui/spr_darkheart_3.png", 0, 0, 0},
    {"trophy_darkheart_4", "app0:assets/ui/spr_darkheart_4.png", 0, 0, 0},
};

typedef struct {
    char language[MOD_NAME_MAX];
    char path[192];
    BundledControl control;
    bool initialized;
    bool missing;
} LanguageIcon;

static LanguageIcon languageIcons[MAX_MODS];

static BundledControl uiIcons[] = {
    {"O", "app0:assets/ui/button_ps4_circle_0.png", 0, 0, 0},
    {"X", "app0:assets/ui/button_ps4_cross_0.png", 0, 0, 0},
    {"TRIANGLE", "app0:assets/ui/button_ps4_triangle_0.png", 0, 0, 0},
    {"SQUARE", "app0:assets/ui/button_ps4_square_0.png", 0, 0, 0},
    {"L1", "app0:assets/ui/button_psv_L.png", 0, 0, 0},
    {"R1", "app0:assets/ui/button_psv_R.png", 0, 0, 0},
    {"OPTIONS", "app0:assets/ui/button_psv_select.png", 0, 0, 0},
    {"JOYL", "app0:assets/ui/button_psv_joyL.png", 0, 0, 0},
    {"JOYR", "app0:assets/ui/button_psv_joyR.png", 0, 0, 0},
    {"TOUCH", "app0:assets/ui/button_psv_touchA.png", 0, 0, 0},
    {"UP", "app0:assets/ui/button_ps4_dpad_up.png", 0, 0, 0},
    {"DOWN", "app0:assets/ui/button_ps4_dpad_down.png", 0, 0, 0},
    {"LEFT", "app0:assets/ui/button_ps4_dpad_left.png", 0, 0, 0},
    {"RIGHT", "app0:assets/ui/button_ps4_dpad_right.png", 0, 0, 0},
    {"spr_textbox_topleft", "app0:assets/ui/spr_textbox_topleft_0.png", 0, 0, 0},
};

static SceUID openBundledControlFile(const BundledControl* control) {
    SceUID fd = sceIoOpen(control->path, SCE_O_RDONLY, 0);
    if (fd >= 0) return fd;
    const char* name = strrchr(control->path, '/');
    name = name != nullptr ? name + 1 : control->path;
    char fallback[192];
    snprintf(fallback, sizeof(fallback),
             "ux0:data/deltarune/deltarunevita/ui/%s", name);
    return sceIoOpen(fallback, SCE_O_RDONLY, 0);
}

static bool drawUIIconExt(Renderer* r, const char* name, float centerX, float centerY, float w, float h, float alpha, bool flipX, bool flipY) {
    if (collectingUIIcons && queuedUIIconCount < 64) {
        queuedUIIcons[queuedUIIconCount++] = (QueuedUIIcon){name, centerX, centerY, w, h, alpha, flipX, flipY};
        return true;
    }
    BundledControl* control = nullptr;
    for (unsigned i = 0; i < sizeof(uiIcons) / sizeof(uiIcons[0]); ++i) {
        if (strcmp(uiIcons[i].name, name) == 0) { control = &uiIcons[i]; break; }
    }
    if (control == nullptr) return false;
    // GUI rectangles and text are batched. Flush them before issuing this
    // immediate VitaGL draw, otherwise the later batch paints over the icon.
    if (r != nullptr && r->vtable->flush != nullptr) r->vtable->flush(r);
    if (control->texture == 0) {
        int channels = 0;
        SceUID fd = openBundledControlFile(control);
        if (fd < 0) {
            touchControlLog("ui_icon_open_fail", control->path);
            return false;
        }
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
        if (pixels == nullptr) {
            touchControlLog("ui_icon_decode_fail", control->path);
            return false;
        }
        glGenTextures(1, &control->texture);
        glBindTexture(GL_TEXTURE_2D, control->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        size_t pixelCount = (size_t)control->width * (size_t)control->height;
        unsigned short* packed = (unsigned short*)pixels;
        for (size_t p = 0; p < pixelCount; ++p) {
            const unsigned char* src = pixels + p * 4U;
            unsigned short a = src[3] == 0 ? 0 : (unsigned short)((src[3] + 15U) >> 4);
            if (a > 15U) a = 15U;
            packed[p] = (unsigned short)(((unsigned short)(src[0] >> 4) << 12) |
                                         ((unsigned short)(src[1] >> 4) << 8) |
                                         ((unsigned short)(src[2] >> 4) << 4) | a);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, control->width, control->height,
                     0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, packed);
        stbi_image_free(pixels);
    }
    float halfW = w * 0.5f;
    float halfH = h * 0.5f;
    float u0 = flipX ? 1.0f : 0.0f;
    float u1 = flipX ? 0.0f : 1.0f;
    float v0 = flipY ? 1.0f : 0.0f;
    float v1 = flipY ? 0.0f : 1.0f;
    if (g_vitaModernGlActive) {
        GLRenderer_drawExternalTextureFlipped((GLRenderer*)r, control->texture,
                                       centerX - halfW, centerY - halfH,
                                       w, h, alpha, flipX, flipY);
        return true;
    }
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, control->texture);
    const TouchControlVertex vertices[4] = {
        {u0, v0, 1.0f, 1.0f, 1.0f, alpha, centerX - halfW, centerY - halfH},
        {u1, v0, 1.0f, 1.0f, 1.0f, alpha, centerX + halfW, centerY - halfH},
        {u1, v1, 1.0f, 1.0f, 1.0f, alpha, centerX + halfW, centerY + halfH},
        {u0, v1, 1.0f, 1.0f, 1.0f, alpha, centerX - halfW, centerY + halfH},
    };
    drawTouchControlQuad(vertices);
    return true;
}

static bool drawUIIcon(Renderer* r, const char* name, float centerX, float centerY, float targetSize, float alpha) {
    return drawUIIconExt(r, name, centerX, centerY, targetSize, targetSize,
                         alpha, false, false);
}

static void drawQueuedUIIconPass(Renderer* r) {
    if (r->vtable->flush != nullptr) r->vtable->flush(r);
    collectingUIIcons = false;
    for (uint32_t i = 0; i < queuedUIIconCount; ++i) {
        QueuedUIIcon* icon = &queuedUIIcons[i];
        drawUIIconExt(r, icon->name, icon->centerX, icon->centerY,
                      icon->w, icon->h, icon->alpha, icon->flipX, icon->flipY);
    }
    queuedUIIconCount = 0;
}

#define ICON_TARGET_SIZE 20.0f
#define ICON_SPACING 4.0f

static float getSegmentWidth(MAYBE_UNUSED Renderer* r, Font* font, const char* str, int len, float scale) {
    if (len <= 0) return 0.0f;
    if (font == nullptr) {
        return len * 11.0f * scale; // Fallback character width
    }
    return TextUtils_measureLineWidth(font, str, len) * scale;
}

static void drawSegmentText(Renderer* r, const char* str, int len, float x, float y, float scale, uint32_t color) {
    if (len <= 0) return;
    char* buf = (char*)malloc(len + 1);
    memcpy(buf, str, len);
    buf[len] = '\0';
    r->vtable->drawTextColor(r, buf, x, y, scale, scale, 0.0f, color, color, color, color, 1.0f, -1.0f);
    free(buf);
}

static void drawTextAndIconsExt(Renderer* r, const char* fmt, float x, float y, float scale, uint32_t color, bool center) {
    int32_t fontIndex = r->drawFont;
    Font* font = nullptr;
    if (fontIndex >= 0 && r->dataWin->font.count > (uint32_t)fontIndex) {
        Font* f = &r->dataWin->font.fonts[fontIndex];
        if (f->present) font = f;
    }

    float totalWidth = 0.0f;
    const char* p = fmt;
    while (*p) {
        if (*p == '{') {
            const char* tagStart = p + 1;
            const char* tagEnd = strchr(tagStart, '}');
            if (tagEnd) {
                totalWidth += ICON_TARGET_SIZE + ICON_SPACING;
                p = tagEnd + 1;
                continue;
            }
        }
        const char* textStart = p;
        while (*p && *p != '{') p++;
        int textLen = p - textStart;
        totalWidth += getSegmentWidth(r, font, textStart, textLen, scale);
    }

    float currentX = center ? (x - totalWidth * 0.5f) : x;
    p = fmt;
    while (*p) {
        if (*p == '{') {
            const char* tagStart = p + 1;
            const char* tagEnd = strchr(tagStart, '}');
            if (tagEnd) {
                char tag[32];
                int tagLen = tagEnd - tagStart;
                if (tagLen < 32) {
                    memcpy(tag, tagStart, tagLen);
                    tag[tagLen] = '\0';
                    // emSize differs substantially between chapter fonts and
                    // translated data.win files. Align to the actual 20 px
                    // control icon instead of deriving its center from emSize.
                    float iconY = y + ICON_TARGET_SIZE * 0.5f;
                    drawUIIcon(r, tag, currentX + ICON_TARGET_SIZE * 0.5f, iconY, ICON_TARGET_SIZE, 1.0f);
                    currentX += ICON_TARGET_SIZE + ICON_SPACING;
                }
                p = tagEnd + 1;
                continue;
            }
        }
        const char* textStart = p;
        while (*p && *p != '{') p++;
        int textLen = p - textStart;
        float segW = getSegmentWidth(r, font, textStart, textLen, scale);
        drawSegmentText(r, textStart, textLen, currentX, y, scale, color);
        currentX += segW;
    }
}

static bool drawBundledControlEntry(BundledControl* control, float centerX, float centerY,
                                    float targetSize, float alpha) {
    if (control == nullptr) return false;
    if (control->texture == 0) {
        if (g_vitaModernGlActive && activeSettingsRenderer != NULL &&
            activeSettingsRenderer->vtable->flush != NULL)
            activeSettingsRenderer->vtable->flush(activeSettingsRenderer);
        touchControlLog("asset_load_begin", control->name);
        int channels = 0;
        SceUID fd = openBundledControlFile(control);
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
        size_t pixelCount = (size_t)control->width * (size_t)control->height;
        unsigned short* packed = (unsigned short*)pixels;
        for (size_t p = 0; p < pixelCount; ++p) {
            const unsigned char* src = pixels + p * 4U;
            unsigned short a = src[3] == 0 ? 0 : (unsigned short)((src[3] + 15U) >> 4);
            if (a > 15U) a = 15U;
            packed[p] = (unsigned short)(((unsigned short)(src[0] >> 4) << 12) |
                                         ((unsigned short)(src[1] >> 4) << 8) |
                                         ((unsigned short)(src[2] >> 4) << 4) | a);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, control->width, control->height,
                     0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, packed);
        stbi_image_free(pixels);
        touchControlLog("asset_upload_complete", control->name);
    }
    float largest = control->width > control->height ? (float)control->width : (float)control->height;
    float scale = targetSize / largest;
    float halfW = control->width * scale * 0.5f;
    float halfH = control->height * scale * 0.5f;
    if (g_vitaModernGlActive && activeSettingsRenderer != NULL) {
        GLRenderer_drawExternalTexture((GLRenderer*)activeSettingsRenderer,
                                       control->texture,
                                       centerX - halfW, centerY - halfH,
                                       halfW * 2.0f, halfH * 2.0f, alpha);
        return true;
    }
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

static bool drawBundledControl(const char* name, float centerX, float centerY, float targetSize, float alpha) {
    BundledControl* control = nullptr;
    for (unsigned i = 0; i < sizeof(bundledControls) / sizeof(bundledControls[0]); ++i) {
        if (strcmp(bundledControls[i].name, name) == 0) { control = &bundledControls[i]; break; }
    }
    return drawBundledControlEntry(control, centerX, centerY, targetSize, alpha);
}

static bool drawLanguageIcon(const char* language, float centerX, float centerY, float alpha) {
    if (language == nullptr || language[0] == '\0')
        return false;
    const char* iconName = language;
    if (strcmp(language, "English") == 0) iconName = "English";
    else if (strcmp(language, "Japanese") == 0) iconName = "Japonese";
    else if (strcmp(language, "Russkiy") == 0) iconName = "Russian";
    LanguageIcon* slot = nullptr;
    LanguageIcon* empty = nullptr;
    for (int i = 0; i < MAX_MODS; ++i) {
        if (!languageIcons[i].initialized) {
            if (empty == nullptr) empty = &languageIcons[i];
            continue;
        }
        if (strcmp(languageIcons[i].language, language) == 0) {
            slot = &languageIcons[i];
            break;
        }
    }
    if (slot == nullptr) {
        if (empty == nullptr) return false;
        slot = empty;
        snprintf(slot->language, sizeof(slot->language), "%s", language);
        snprintf(slot->path, sizeof(slot->path), "app0:assets/ui/Lang/%s.png", iconName);
        slot->control.name = slot->language;
        slot->control.path = slot->path;
        slot->initialized = true;
    }
    if (slot->missing) return false;
    if (slot->control.texture == 0) {
        SceUID fd = sceIoOpen(slot->path, SCE_O_RDONLY, 0);
        if (fd < 0) {
            slot->missing = true;
            return false;
        }
        sceIoClose(fd);
    }
    // The sources are 34x28. targetSize=34 preserves their native aspect and
    // avoids filtering or an unnecessary larger UI atlas.
    return drawBundledControlEntry(&slot->control, centerX, centerY, 34.0f, alpha);
}

static void drawControl(Renderer* r, const char* name, float centerX, float centerY, float targetSize, float alpha) {
    if (!drawGameControl(r, name, centerX, centerY, targetSize, alpha)) {
        drawBundledControl(name, centerX, centerY, targetSize, alpha);
    }
}

// The external Sony icons are intentionally drawn through the same bundled
// texture path as the touch overlay. Immediate UI-icon rendering depends on
// chapter GL state, while this path is already proven across every data.win.
static void drawSettingsControlFooter(Renderer* r, VitaSettings* s) {
    const float textY = 426.0f;
    const float iconY = 437.0f;
    drawLabel(r, s->uiFooters[0], 254.0f, textY, 0x808080, 1.1f);
    drawLabel(r, s->uiFooters[1], 493.0f, textY, 0x808080, 1.1f);
    drawLabel(r, s->uiFooters[2], 655.0f, textY, 0x808080, 1.1f);
    drawBundledControl("button_psv_L", 191.0f, iconY, 27.0f, 1.0f);
    drawBundledControl("button_psv_R", 223.0f, iconY, 27.0f, 1.0f);
    drawBundledControl("button_ps4_cross_0", 465.0f, iconY, 25.0f, 1.0f);
    drawBundledControl("button_ps4_circle_0", 627.0f, iconY, 25.0f, 1.0f);
}

static void drawConfirmControlFooter(Renderer* r, VitaSettings* s) {
    const float textY = 316.0f, iconY = 327.0f;
    drawLabel(r, s->uiFooters[3], 366.0f, textY, 0xA0A0A0, 1.2f);
    drawLabel(r, s->uiFooters[4], 575.0f, textY, 0xA0A0A0, 1.2f);
    drawBundledControl("button_ps4_cross_0", 338.0f, iconY, 25.0f, 1.0f);
    drawBundledControl("button_ps4_circle_0", 547.0f, iconY, 25.0f, 1.0f);
}

static void drawRestartControlFooter(Renderer* r, VitaSettings* s) {
    const float textY = 330.0f, iconY = 341.0f;
    drawLabel(r, s->uiFooters[6], 366.0f, textY, 0xA0A0A0, 1.15f);
    drawLabel(r, s->uiFooters[4], 575.0f, textY, 0xA0A0A0, 1.15f);
    drawBundledControl("button_ps4_cross_0", 338.0f, iconY, 25.0f, 1.0f);
    drawBundledControl("button_ps4_circle_0", 547.0f, iconY, 25.0f, 1.0f);
}

static void drawApplyCancelFooter(Renderer* r, VitaSettings* s) {
    const float textY = 426.0f, iconY = 437.0f;
    drawLabel(r, s->uiFooters[6], 366.0f, textY, 0xA0A0A0, 1.2f);
    drawLabel(r, s->uiFooters[4], 575.0f, textY, 0xA0A0A0, 1.2f);
    drawBundledControl("button_ps4_cross_0", 338.0f, iconY, 25.0f, 1.0f);
    drawBundledControl("button_ps4_circle_0", 547.0f, iconY, 25.0f, 1.0f);
}

static void drawControlEditorFooter(Renderer* r, VitaSettings* s) {
    const float textY = 458.0f, iconY = 469.0f;
    drawLabel(r, "ITEM", 306.0f, textY, 0xA0A0A0, 1.15f);
    drawLabel(r, s->uiFooters[7], 442.0f, textY, 0xA0A0A0, 1.15f);
    drawLabel(r, "RESET", 608.0f, textY, 0xA0A0A0, 1.15f);
    drawBundledControl("button_psv_L", 241.0f, iconY, 27.0f, 1.0f);
    drawBundledControl("button_psv_R", 273.0f, iconY, 27.0f, 1.0f);
    drawBundledControl("button_ps4_cross_0", 414.0f, iconY, 25.0f, 1.0f);
    drawBundledControl("button_ps4_triangle_0", 580.0f, iconY, 25.0f, 1.0f);
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
    if (g_vitaDisplayStretchToFill) {
        width = 960;
        height = 544;
    } else if ((gameW * 544) / gameH < 960) {
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

    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r, strcmp(s->activeLanguage, "English") != 0);
    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    r->vtable->drawRectangle(r, (float)x, (float)y,
                             (float)(x + width - 1), (float)(y + height - 1),
                             0xFFFFFF, 1.0f, true);

    // Compact HUD: D-Pad fine adjustment is intentionally not repeated in the
    // legend. Left analog remains the fast move control; Triangle exposes the
    // aspect/fill toggle that was previously hidden.
    const float panelLeft = 126.0f, panelRight = 834.0f;
    r->vtable->drawRectangle(r, panelLeft, 14, panelRight, 126, 0x000000, 0.90f, false);
    r->vtable->drawRectangle(r, panelLeft, 14, panelRight, 126, 0x808080, 0.72f, true);
    drawCenteredText(r, settingsText(s, "ADJUST SCREEN", "AJUSTAR TELA", "AJUSTAR PANTALLA"),
                     480, 24, 1.08f, 0x00FFFF);

    drawBundledControl("button_psv_joyL", 155.0f, 70.0f, 34.0f, 1.0f);
    drawLabel(r, settingsText(s, "Move", "Mover", "Mover"), 179.0f, 57.0f, 0xFFFFFF, 0.88f);
    drawBundledControl("button_psv_joyR", 275.0f, 70.0f, 34.0f, 1.0f);
    drawLabel(r, "Zoom", 299.0f, 57.0f, 0xFFFFFF, 0.88f);
    drawBundledControl("button_ps4_triangle_0", 384.0f, 70.0f, 32.0f, 1.0f);
    drawLabel(r, settingsText(s, "Aspect", "Proporção", "Proporción"), 408.0f, 57.0f, 0xFFFFFF, 0.88f);
    drawBundledControl("button_ps4_cross_0", 538.0f, 70.0f, 32.0f, 1.0f);
    drawLabel(r, settingsText(s, "Save", "Salvar", "Guardar"), 562.0f, 57.0f, 0xFFFFFF, 0.88f);
    drawBundledControl("button_ps4_circle_0", 666.0f, 70.0f, 32.0f, 1.0f);
    drawLabel(r, "Reset", 690.0f, 57.0f, 0xFFFFFF, 0.88f);

    char values[96];
    snprintf(values, sizeof(values), "X %+d   Y %+d   ZOOM %d%%   %s",
             s->displayOffsetX, s->displayOffsetY, s->displayZoom,
             s->displayStretchToFill ? settingsText(s, "FILL", "PREENCHER", "LLENAR")
                                     : settingsText(s, "ASPECT", "PROPORÇÃO", "PROPORCIÓN"));
    drawCenteredText(r, values, 480, 98, 0.88f, 0xC8C8C8);

    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
    r->drawFont = oldFont;
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

void VitaSettings_setActiveChapter(int chapter) {
    g_activeChapter = chapter;
}

