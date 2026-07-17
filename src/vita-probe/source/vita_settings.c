#include "vita_settings.h"

#include <psp2/io/fcntl.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#include "runner.h"
#include "audio/openal/al_audio_system.h"

#define SETTINGS_PATH "ux0:data/deltarune/config.ini"
#define SETTINGS_CATEGORIES 4

static int settingsItemCount(int category) {
    if (category == 0) return 1;
    if (category == 3) return 4;
    return 2;
}

int g_vitaDisplayOffsetX = 0;
int g_vitaDisplayOffsetY = 0;
int g_vitaDisplayZoom = 100;
int g_vitaPortOverlayFullScreen = 0;
int g_vitaTouchEnabled = 1;
int g_vitaConsoleBordersEnabled = 0;
static bool g_launcherMode = false;

static void resetDefaults(VitaSettings* s) {
    s->touchEnabled = true;
    s->ptbrEnabled = false;
    s->widescreenEnabled = false;
    s->musicVolume = 10;
    s->sfxVolume = 10;
    s->vsyncEnabled = true;
    s->displayOffsetX = 0;
    s->displayOffsetY = 0;
    s->displayZoom = 100;
}

static void applyDisplaySettings(const VitaSettings* s) {
    g_vitaDisplayOffsetX = g_launcherMode ? 0 : s->displayOffsetX;
    g_vitaDisplayOffsetY = g_launcherMode ? 0 : s->displayOffsetY;
    g_vitaDisplayZoom = g_launcherMode ? 100 : s->displayZoom;
    g_vitaTouchEnabled = s->touchEnabled ? 1 : 0;
    g_vitaConsoleBordersEnabled = (!g_launcherMode && s->widescreenEnabled) ? 1 : 0;
}

static void saveSettings(const VitaSettings* s) {
    char text[256];
    int length = snprintf(text, sizeof(text), "touch=%d\nmod=%s\nwidescreen=%d\nmusic_volume=%d\nsfx_volume=%d\nvsync=%d\nscreen_profile=3\noffset_x=%d\noffset_y=%d\nzoom=%d\n",
                          s->touchEnabled ? 1 : 0, s->ptbrEnabled ? "PTBR" : "Original",
                          s->widescreenEnabled ? 1 : 0, s->musicVolume, s->sfxVolume, s->vsyncEnabled ? 1 : 0,
                          s->displayOffsetX, s->displayOffsetY, s->displayZoom);
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, length);
        sceIoClose(fd);
    }
}

void VitaSettings_load(VitaSettings* s) {
    memset(s, 0, sizeof(*s));
    resetDefaults(s);
    bool migrateScreenProfile = false;
    char text[256] = {0};
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        int read = sceIoRead(fd, text, sizeof(text) - 1);
        sceIoClose(fd);
        if (read > 0) text[read] = '\0';
        s->touchEnabled = strstr(text, "touch=0") == NULL;
        s->ptbrEnabled = strstr(text, "mod=PTBR") != NULL;
        s->widescreenEnabled = strstr(text, "widescreen=1") != NULL;
        s->vsyncEnabled = strstr(text, "vsync=0") == NULL;
        char* musicVolume = strstr(text, "music_volume=");
        char* sfxVolume = strstr(text, "sfx_volume=");
        if (musicVolume != NULL) sscanf(musicVolume + 13, "%d", &s->musicVolume);
        if (sfxVolume != NULL) sscanf(sfxVolume + 11, "%d", &s->sfxVolume);
        char* offsetX = strstr(text, "offset_x=");
        char* offsetY = strstr(text, "offset_y=");
        char* zoom = strstr(text, "zoom=");
        if (offsetX != NULL) sscanf(offsetX + 9, "%d", &s->displayOffsetX);
        if (offsetY != NULL) sscanf(offsetY + 9, "%d", &s->displayOffsetY);
        if (zoom != NULL) sscanf(zoom + 5, "%d", &s->displayZoom);
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
    if (s->displayZoom < 50) s->displayZoom = 50;
    if (s->displayZoom > 160) s->displayZoom = 160;
    applyDisplaySettings(s);
    if (migrateScreenProfile) saveSettings(s);
}

void VitaSettings_applyAudio(VitaSettings* s, AudioSystem* audio) {
    audio->vtable->setMasterGain(audio, 1.0f);
    AlAudioSystem_setCategoryGains((AlAudioSystem*)audio, (float)s->musicVolume / 10.0f, (float)s->sfxVolume / 10.0f);
}

bool VitaSettings_handleInput(VitaSettings* s, const SceCtrlData* pad, AudioSystem* audio) {
    if (s->inputCooldown > 0) s->inputCooldown--;
    uint32_t pressed = pad->buttons & ~s->previousButtons;
    s->previousButtons = pad->buttons;
    bool requestRestart = false;

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
        if (s->open && s->restartOnClose) requestRestart = true;
        s->open = !s->open;
    }
    if (!s->open) return requestRestart;

    if (pressed & SCE_CTRL_LTRIGGER) {
        s->category = (s->category + SETTINGS_CATEGORIES - 1) % SETTINGS_CATEGORIES;
        s->selected = 0;
    }
    if (pressed & SCE_CTRL_RTRIGGER) {
        s->category = (s->category + 1) % SETTINGS_CATEGORIES;
        s->selected = 0;
    }
    int itemCount = settingsItemCount(s->category);
    if (pressed & SCE_CTRL_UP) s->selected = (s->selected + itemCount - 1) % itemCount;
    if (pressed & SCE_CTRL_DOWN) s->selected = (s->selected + 1) % itemCount;

    bool activate = (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) != 0;
    if (activate) {
        if (s->category == 0 && s->selected == 0) s->touchEnabled = !s->touchEnabled;
        else if (s->category == 1 && s->selected == 0) {
            s->widescreenEnabled = !s->widescreenEnabled;
            s->displayOffsetX = 0;
            s->displayOffsetY = 0;
            s->displayZoom = s->widescreenEnabled ? 90 : 100;
            applyDisplaySettings(s);
        } else if (s->category == 1 && s->selected == 1) {
            s->adjustMode = true;
            s->open = false;
        } else if (s->category == 2) {
            int* value = s->selected == 0 ? &s->sfxVolume : &s->musicVolume;
            if (pressed & SCE_CTRL_LEFT) (*value)--;
            else (*value)++;
            if (*value < 0) *value = 10;
            if (*value > 10) *value = 0;
            VitaSettings_applyAudio(s, audio);
        } else if (s->category == 3 && s->selected == 0) {
            s->vsyncEnabled = !s->vsyncEnabled;
        } else if (s->category == 3 && s->selected == 1) {
            s->ptbrEnabled = !s->ptbrEnabled;
            s->restartOnClose = false;
            s->open = false;
            requestRestart = true;
        } else if (s->category == 3 && s->selected == 2) {
            s->confirmChapterSelect = true;
        } else {
            resetDefaults(s);
            applyDisplaySettings(s);
            VitaSettings_applyAudio(s, audio);
        }
        saveSettings(s);
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        s->open = false;
        s->inputCooldown = 2;
        requestRestart = s->restartOnClose;
    }
    return requestRestart;
}

static void drawLabel(Renderer* r, const char* text, float x, float y, uint32_t color) {
    r->vtable->drawTextColor(r, text, x, y, 1.6f, 1.6f, 0.0f,
                             color, color, color, color, 1.0f, -1.0f);
}

static int findSettingsFont(Renderer* r) {
    for (uint32_t i = 0; i < r->dataWin->font.count; ++i) {
        Font* font = &r->dataWin->font.fonts[i];
        if (font->present && font->name != nullptr && strcmp(font->name, "fnt_main") == 0) return (int)i;
    }
    return r->dataWin->font.count > 0 ? 0 : -1;
}

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

    float cw = (float) corner->width;
    float ch = (float) corner->height;
    float horizontalScale = (right - left - cw * 2.0f) / (float) edgeTop->width;
    float verticalScale = (bottom - top - ch * 2.0f) / (float) edgeLeft->height;
    r->vtable->drawSprite(r, corner->tpagIndices[0], left, top, 0, 0, 1, 1, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, corner->tpagIndices[0], right, top, 0, 0, -1, 1, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, corner->tpagIndices[0], left, bottom, 0, 0, 1, -1, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, corner->tpagIndices[0], right, bottom, 0, 0, -1, -1, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeTop->tpagIndices[0], left + cw, top, 0, 0, horizontalScale, 1, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeTop->tpagIndices[0], left + cw, bottom, 0, 0, horizontalScale, -1, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeLeft->tpagIndices[0], left, top + ch, 0, 0, 1, verticalScale, 0, 0xFFFFFF, 1.0f);
    r->vtable->drawSprite(r, edgeLeft->tpagIndices[0], right, top + ch, 0, 0, -1, verticalScale, 0, 0xFFFFFF, 1.0f);
}

void VitaSettings_draw(VitaSettings* s, Renderer* r) {
    if (!s->open) return;
    int oldFont = r->drawFont;
    r->drawFont = findSettingsFont(r);

    g_vitaPortOverlayFullScreen = 1;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    const float panelLeft = 108.0f, panelTop = 48.0f, panelRight = 852.0f, panelBottom = 486.0f;
    r->vtable->drawRectangle(r, panelLeft, panelTop, panelRight, panelBottom, 0x000000, 0.96f, false);
    drawDialogBorder(r, panelLeft, panelTop, panelRight, panelBottom);
    drawCenteredText(r, s->ptbrEnabled ? "CONFIGURACOES" : "GAME SETTINGS", 480, 79, 1.6f, 0xFFFFFF);

    const char* touch = s->touchEnabled ? (s->ptbrEnabled ? "Ligado" : "On") : (s->ptbrEnabled ? "Desligado" : "Off");
    const char* mod = s->ptbrEnabled ? "PT-BR" : "Original";
    const char* screen = s->widescreenEnabled ? (s->ptbrEnabled ? "Ligadas" : "On") : (s->ptbrEnabled ? "Desligadas" : "Off");
    char sfxVolume[32], musicVolume[32];
    snprintf(sfxVolume, sizeof(sfxVolume), "%d%%", s->sfxVolume * 10);
    snprintf(musicVolume, sizeof(musicVolume), "%d%%", s->musicVolume * 10);
    const char* categoriesPt[SETTINGS_CATEGORIES] = {"CONTROLES", "TELA", "AUDIO", "SISTEMA"};
    const char* categoriesEn[SETTINGS_CATEGORIES] = {"CONTROLS", "SCREEN", "AUDIO", "SYSTEM"};
    const char** categories = s->ptbrEnabled ? categoriesPt : categoriesEn;
    for (int i = 0; i < SETTINGS_CATEGORIES; ++i) {
        float centerX = 210.0f + i * 180.0f;
        if (i == s->category) {
            r->vtable->drawRectangle(r, centerX - 70, 126, centerX + 70, 156, 0x181818, 1.0f, false);
            r->vtable->drawRectangle(r, centerX - 70, 126, centerX + 70, 156, 0x00FFFF, 0.72f, true);
        }
        drawCenteredText(r, categories[i], centerX, 132, 1.15f,
                         i == s->category ? 0x00FFFF : 0x808080);
    }
    char adjustment[64];
    snprintf(adjustment, sizeof(adjustment), "%d,%d  %d%%", s->displayOffsetX, s->displayOffsetY, s->displayZoom);
    const char* vsync = s->vsyncEnabled ? (s->ptbrEnabled ? "Ligado" : "On") : (s->ptbrEnabled ? "Desligado" : "Off");
    const char* labelsPt[SETTINGS_CATEGORIES][3] = {
        {"Controles touch", "", ""}, {"Bordas de console", "Ajustar tela", ""},
        {"Efeitos sonoros", "Musica", ""}, {"VSync / 30 FPS", "Mod / Idioma", "Voltar aos capitulos"}
    };
    const char* labelsEn[SETTINGS_CATEGORIES][3] = {
        {"Touch controls", "", ""}, {"Console borders", "Adjust screen", ""},
        {"Sound effects", "Music", ""}, {"VSync / 30 FPS", "Mod / Language", "Chapter Select"}
    };
    const char* values[SETTINGS_CATEGORIES][4] = {
        {touch, "", "", ""}, {screen, adjustment, "", ""}, {sfxVolume, musicVolume, "", ""}, {vsync, mod, "", ""}
    };
    int visibleItems = settingsItemCount(s->category);
    for (int i = 0; i < visibleItems; ++i) {
        float y = visibleItems == 4 ? 184.0f + i * 58.0f : (visibleItems == 3 ? 196.0f + i * 72.0f : 214.0f + i * 88.0f);
        if (i == s->selected) {
            r->vtable->drawRectangle(r, 194, y - 8, 766, y + 34, 0x141414, 1.0f, false);
            r->vtable->drawRectangle(r, 194, y - 8, 766, y + 34, 0x404040, 1.0f, true);
        }
        drawLabel(r, i == s->selected ? ">" : " ", 214, y, i == s->selected ? 0x00FFFF : 0xFFFFFF);
        const char* label = (i == 3 && s->category == 3)
            ? (s->ptbrEnabled ? "Restaurar padrao" : "Restore defaults")
            : (s->ptbrEnabled ? labelsPt[s->category][i] : labelsEn[s->category][i]);
        drawLabel(r, label, 252, y, 0xFFFFFF);
        drawRightText(r, values[s->category][i], 742, y, 1.6f, i == s->selected ? 0x00FFFF : 0xFFFFFF);
    }
    drawCenteredText(r,
        s->ptbrEnabled ? "L/R: CATEGORIA    X: ALTERAR    O: FECHAR" : "L/R: CATEGORY    X: CHANGE    O: CLOSE",
        480, 426, 1.1f, 0x808080);

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
    r->vtable->endGUI(r);
    g_vitaPortOverlayFullScreen = 0;
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
    drawControl(r, "spr_joybase", 155, 420, 205, 0.48f);
    drawControl(r, "spr_joystick", 155 + s->visualStickX * 38.0f, 420 + s->visualStickY * 38.0f,
                125 + (s->visualStickX != 0.0f || s->visualStickY != 0.0f ? 8 : 0), 0.68f);
    drawControl(r, "spr_control_zkey", 850, 385 + (s->visualConfirm ? 4 : 0), s->visualConfirm ? 108 : 92, s->visualConfirm ? 0.92f : 0.58f);
    drawControl(r, "spr_control_xkey", 755, 455 + (s->visualCancel ? 4 : 0), s->visualCancel ? 108 : 92, s->visualCancel ? 0.92f : 0.58f);
    drawControl(r, "spr_control_ckey", 755, 340 + (s->visualMenu ? 4 : 0), s->visualMenu ? 108 : 92, s->visualMenu ? 0.92f : 0.58f);
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
    drawLabel(r, s->ptbrEnabled ? "ESQ: MOVER  DIR: ZOOM  X: SALVAR  O: RESET" : "LEFT: MOVE  RIGHT: ZOOM  X: SAVE  O: RESET", 125, 18, 0xFFFFFF);
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
