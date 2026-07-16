#include "vita_settings.h"

#include <psp2/io/fcntl.h>
#include <stdio.h>
#include <string.h>

#include "runner.h"

#define SETTINGS_PATH "ux0:data/deltarune/config.ini"
#define SETTINGS_ITEMS 6

int g_vitaDisplayOffsetX = 0;
int g_vitaDisplayOffsetY = 0;
int g_vitaDisplayZoom = 100;

static void applyDisplaySettings(const VitaSettings* s) {
    g_vitaDisplayOffsetX = s->displayOffsetX;
    g_vitaDisplayOffsetY = s->displayOffsetY;
    g_vitaDisplayZoom = s->displayZoom;
}

static void saveSettings(const VitaSettings* s) {
    char text[128];
    int length = snprintf(text, sizeof(text), "touch=%d\nmod=%s\nwidescreen=%d\nvolume=%d\noffset_x=%d\noffset_y=%d\nzoom=%d\n",
                          s->touchEnabled ? 1 : 0, s->ptbrEnabled ? "PTBR" : "Original",
                          s->widescreenEnabled ? 1 : 0, s->volume,
                          s->displayOffsetX, s->displayOffsetY, s->displayZoom);
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, length);
        sceIoClose(fd);
    }
}

void VitaSettings_load(VitaSettings* s) {
    memset(s, 0, sizeof(*s));
    s->touchEnabled = true;
    s->widescreenEnabled = true;
    s->volume = 10;
    s->displayZoom = 100;
    char text[256] = {0};
    SceUID fd = sceIoOpen(SETTINGS_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        int read = sceIoRead(fd, text, sizeof(text) - 1);
        sceIoClose(fd);
        if (read > 0) text[read] = '\0';
        s->touchEnabled = strstr(text, "touch=0") == NULL;
        s->ptbrEnabled = strstr(text, "mod=PTBR") != NULL;
        s->widescreenEnabled = strstr(text, "widescreen=0") == NULL;
        char* volume = strstr(text, "volume=");
        if (volume != NULL) {
            int value = 10;
            if (sscanf(volume + 7, "%d", &value) == 1) s->volume = value;
        }
        char* offsetX = strstr(text, "offset_x=");
        char* offsetY = strstr(text, "offset_y=");
        char* zoom = strstr(text, "zoom=");
        if (offsetX != NULL) sscanf(offsetX + 9, "%d", &s->displayOffsetX);
        if (offsetY != NULL) sscanf(offsetY + 9, "%d", &s->displayOffsetY);
        if (zoom != NULL) sscanf(zoom + 5, "%d", &s->displayZoom);
    }
    if (s->volume < 0) s->volume = 0;
    if (s->volume > 10) s->volume = 10;
    if (s->displayZoom < 50) s->displayZoom = 50;
    if (s->displayZoom > 160) s->displayZoom = 160;
    applyDisplaySettings(s);
}

void VitaSettings_applyAudio(VitaSettings* s, AudioSystem* audio) {
    audio->vtable->setMasterGain(audio, (float)s->volume / 10.0f);
}

bool VitaSettings_handleInput(VitaSettings* s, const SceCtrlData* pad, AudioSystem* audio) {
    uint32_t pressed = pad->buttons & ~s->previousButtons;
    s->previousButtons = pad->buttons;
    bool requestRestart = false;

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

    if (pressed & SCE_CTRL_UP) s->selected = (s->selected + SETTINGS_ITEMS - 1) % SETTINGS_ITEMS;
    if (pressed & SCE_CTRL_DOWN) s->selected = (s->selected + 1) % SETTINGS_ITEMS;

    bool activate = (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) != 0;
    if (activate) {
        if (s->selected == 0) s->touchEnabled = !s->touchEnabled;
        else if (s->selected == 1) {
            s->ptbrEnabled = !s->ptbrEnabled;
            s->restartOnClose = true;
        } else if (s->selected == 2) {
            s->widescreenEnabled = !s->widescreenEnabled;
        } else if (s->selected == 3) {
            s->adjustMode = true;
            s->open = false;
        } else if (s->selected == 4) {
            if (pressed & SCE_CTRL_LEFT) s->volume--;
            else s->volume++;
            if (s->volume < 0) s->volume = 10;
            if (s->volume > 10) s->volume = 0;
            VitaSettings_applyAudio(s, audio);
        } else {
            s->open = false;
            requestRestart = s->restartOnClose;
        }
        saveSettings(s);
    }
    if (pressed & SCE_CTRL_CIRCLE) {
        s->open = false;
        requestRestart = s->restartOnClose;
    }
    return requestRestart;
}

static void drawLabel(Renderer* r, const char* text, float x, float y, uint32_t color) {
    r->vtable->drawTextColor(r, text, x, y, 1.6f, 1.6f, 0.0f,
                             color, color, color, color, 1.0f, -1.0f);
}

void VitaSettings_draw(VitaSettings* s, Renderer* r) {
    if (!s->open) return;
    int oldFont = r->drawFont;
    if (r->drawFont < 0 || (uint32_t)r->drawFont >= r->dataWin->font.count) r->drawFont = 0;

    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    r->vtable->drawRectangle(r, 145, 55, 815, 489, 0x000000, 0.94f, false);
    r->vtable->drawRectangle(r, 145, 55, 815, 489, 0xFFFFFF, 1.0f, true);
    r->vtable->drawRectangle(r, 154, 64, 806, 480, 0xFFFFFF, 1.0f, true);
    drawLabel(r, s->ptbrEnabled ? "CONFIGURACOES DO JOGO" : "GAME SETTINGS", s->ptbrEnabled ? 245 : 315, 78, 0xFFFFFF);

    const char* touch = s->touchEnabled ? (s->ptbrEnabled ? "Ligado" : "On") : (s->ptbrEnabled ? "Desligado" : "Off");
    const char* mod = s->ptbrEnabled ? "PT-BR" : "Original";
    const char* screen = s->widescreenEnabled ? (s->ptbrEnabled ? "Laterais" : "Side borders") : (s->ptbrEnabled ? "Original" : "Original");
    char volume[32];
    snprintf(volume, sizeof(volume), "%d%%", s->volume * 10);
    const char* labelsPt[SETTINGS_ITEMS] = {"Touch", "Mod / Idioma", "Tela / Bordas", "Ajustar tela", "Volume geral", "Voltar"};
    const char* labelsEn[SETTINGS_ITEMS] = {"Touch", "Mod / Language", "Screen / Borders", "Adjust Screen", "Master volume", "Back"};
    const char** labels = s->ptbrEnabled ? labelsPt : labelsEn;
    char adjustment[64];
    snprintf(adjustment, sizeof(adjustment), "%d,%d  %d%%", s->displayOffsetX, s->displayOffsetY, s->displayZoom);
    const char* values[SETTINGS_ITEMS] = {touch, mod, screen, adjustment, volume, ""};
    for (int i = 0; i < SETTINGS_ITEMS; ++i) {
        float y = 120.0f + i * 55.0f;
        drawLabel(r, i == s->selected ? "*" : " ", 180, y, 0xFFFFFF);
        drawLabel(r, labels[i], 225, y, 0xFFFFFF);
        drawLabel(r, values[i], 590, y, i == s->selected ? 0x00FFFF : 0xFFFFFF);
    }
    drawLabel(r, s->ptbrEnabled ? "X: alterar   O: voltar" : "X: change   O: back", 280, 450, 0x808080);
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

void VitaSettings_drawTouchControls(VitaSettings* s, Renderer* r) {
    if (!s->touchEnabled || s->open || s->adjustMode) return;
    int oldFont = r->drawFont;
    if (r->drawFont < 0 || (uint32_t)r->drawFont >= r->dataWin->font.count) r->drawFont = 0;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    drawGameControl(r, "spr_joybase", 155, 420, 205, 0.48f);
    drawGameControl(r, "spr_joystick", 155, 420, 125, 0.62f);
    drawGameControl(r, "spr_control_zkey", 850, 385, 92, 0.58f);
    drawGameControl(r, "spr_control_xkey", 755, 455, 92, 0.58f);
    drawGameControl(r, "spr_control_ckey", 755, 340, 92, 0.58f);
    r->vtable->endGUI(r);
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
    width = width * s->displayZoom / 100;
    height = height * s->displayZoom / 100;
    int x = (960 - width) / 2 + s->displayOffsetX;
    int y = (544 - height) / 2 + s->displayOffsetY;
    r->vtable->beginGUI(r, 960, 544, 0, 0, 960, 544, RENDER_TARGET_HOST_FRAMEBUFFER);
    r->vtable->drawRectangle(r, (float)x, (float)y, (float)(x + width - 1), (float)(y + height - 1), 0xFFFFFF, 1.0f, true);
    drawLabel(r, s->ptbrEnabled ? "ESQ: MOVER  DIR: ZOOM  X: SALVAR  O: RESET" : "LEFT: MOVE  RIGHT: ZOOM  X: SAVE  O: RESET", 125, 18, 0xFFFFFF);
    r->vtable->endGUI(r);
}
