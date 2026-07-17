#include "vita_borders.h"

#include <psp2/io/fcntl.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#define BORDER_ROOT "ux0:data/deltarune/deltarunevita/borders/"
#define BORDER_LOG "ux0:data/deltarune/deltarunevita/butterscotch-probe.log"

extern int g_vitaConsoleBordersEnabled;

typedef struct {
    GLfloat u, v;
    GLfloat r, g, b, a;
    GLfloat x, y;
} BorderVertex;

static const char* borderPath = NULL;
static GLuint borderTexture = 0;
static int borderWidth = 0;
static int borderHeight = 0;
static int borderLoadAttempted = 0;
static int borderChapter = 0;
static char borderRoom[96] = {0};

static void borderLog(const char* phase) {
    char line[256];
    int length = snprintf(line, sizeof(line), "CONSOLE_BORDER=%s path=%s size=%dx%d\n",
                          phase, borderPath != NULL ? borderPath : "none", borderWidth, borderHeight);
    SceUID fd = sceIoOpen(BORDER_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, (SceSize)length);
        sceIoClose(fd);
    }
}

static const char* chapterBorder(int chapter) {
    switch (chapter) {
        case 1: return BORDER_ROOT "border_dw_blue_stars_0.png";
        case 2: return BORDER_ROOT "border_dw_cyber_0.png";
        case 3: return BORDER_ROOT "border_dw_teevie_0.png";
        case 4: return BORDER_ROOT "border_dw_church_a_0.png";
        case 5: return BORDER_ROOT "border_dw_garden_0.png";
        default: return NULL;
    }
}

static int loadBorderTexture(void) {
    borderLoadAttempted = 1;
    if (borderPath == NULL) return 0;
    SceUID fd = sceIoOpen(borderPath, SCE_O_RDONLY, 0);
    if (fd < 0) { borderLog("open_failed"); return 0; }
    SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (fileSize <= 0) { sceIoClose(fd); borderLog("empty_file"); return 0; }
    unsigned char* fileData = (unsigned char*)malloc((size_t)fileSize);
    if (fileData == NULL) { sceIoClose(fd); borderLog("file_alloc_failed"); return 0; }
    int bytesRead = sceIoRead(fd, fileData, (unsigned int)fileSize);
    sceIoClose(fd);
    if (bytesRead != fileSize) { free(fileData); borderLog("read_failed"); return 0; }

    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(fileData, bytesRead, &borderWidth, &borderHeight, &channels, 4);
    free(fileData);
    if (pixels == NULL) { borderLog("decode_failed"); return 0; }

    glGenTextures(1, &borderTexture);
    glBindTexture(GL_TEXTURE_2D, borderTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    while (glGetError() != GL_NO_ERROR) {}
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, borderWidth, borderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &borderTexture);
        borderTexture = 0;
        borderLog("upload_failed");
        return 0;
    }
    borderLog("loaded");
    return 1;
}

void VitaBorders_init(int chapter) {
    borderChapter = chapter;
    borderPath = chapterBorder(chapter);
    borderLoadAttempted = 0;
    borderLog(borderPath != NULL ? "selected" : "launcher_disabled");
}

static const char* roomBorder(const char* room) {
    if (room == NULL) return chapterBorder(borderChapter);
    if (strstr(room, "titan") != NULL) return BORDER_ROOT "border_dw_titan_eyes_0.png";
    if (strstr(room, "cliff") != NULL) return BORDER_ROOT "border_dw_garden_cliff_0.png";
    if (strstr(room, "garden") != NULL) return BORDER_ROOT "border_dw_garden_0.png";
    if (strstr(room, "church") != NULL) return BORDER_ROOT "border_dw_church_a_0.png";
    if (strstr(room, "mansion") != NULL) return BORDER_ROOT "border_dw_mansion_0.png";
    if (strstr(room, "green") != NULL) return BORDER_ROOT "border_dw_green_room_0.png";
    if (strstr(room, "teevie") != NULL || strstr(room, "tv_") != NULL) return BORDER_ROOT "border_dw_teevie_0.png";
    if (strstr(room, "cyber") != NULL) return BORDER_ROOT "border_dw_cyber_0.png";
    if (strstr(room, "city") != NULL) return BORDER_ROOT "border_dw_city_0.png";
    if (strstr(room, "castle") != NULL || strstr(room, "town") != NULL) {
        return borderChapter >= 2 ? BORDER_ROOT "border_dw_castletown_0.png" : BORDER_ROOT "border_lw_town_0.png";
    }
    if (strstr(room, "home") != NULL || strstr(room, "house") != NULL || strstr(room, "school") != NULL || strstr(room, "class") != NULL)
        return BORDER_ROOT "border_lw_town_morning_0.png";
    return chapterBorder(borderChapter);
}

void VitaBorders_updateRoom(const char* roomName) {
    const char* next = roomBorder(roomName);
    if (roomName != NULL && strcmp(borderRoom, roomName) == 0 && next == borderPath) return;
    snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
    if (next == borderPath) return;
    if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
    borderTexture = 0;
    borderWidth = borderHeight = 0;
    borderLoadAttempted = 0;
    borderPath = next;
    borderLog("room_selected");
}

void VitaBorders_draw(int windowW, int windowH) {
    if (!g_vitaConsoleBordersEnabled || borderPath == NULL) return;
    if (borderTexture == 0 && !borderLoadAttempted && !loadBorderTexture()) return;
    if (borderTexture == 0) return;

    const BorderVertex vertices[4] = {
        {0, 0, 1, 1, 1, 1, -1,  1},
        {1, 0, 1, 1, 1, 1,  1,  1},
        {1, 1, 1, 1, 1, 1,  1, -1},
        {0, 1, 1, 1, 1, 1, -1, -1},
    };
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW, windowH);
    glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, borderTexture);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BorderVertex), &vertices[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(BorderVertex), &vertices[0].r);
    glVertexPointer(2, GL_FLOAT, sizeof(BorderVertex), &vertices[0].x);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);
}

void VitaBorders_shutdown(void) {
    if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
    borderTexture = 0;
}
