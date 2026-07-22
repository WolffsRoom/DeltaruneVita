#include "vita_borders.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/processmgr.h>
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
static GLuint previousBorderTexture = 0;
static int previousBorderWidth = 0;
static int previousBorderHeight = 0;
static uint64_t borderTransitionStart = 0;
#define BORDER_TRANSITION_US 350000ULL
static int borderWidth = 0;
static int borderHeight = 0;
static int borderLoadAttempted = 0;
static int borderChapter = 0;
static char borderRoom[96] = {0};
static int chapterMenuSeen = 0;
static int gameplayBordersActive = 0;

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
        case 5: return BORDER_ROOT "border_lw_town_0.png";
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

    // The source pack uses 1920x1080 images, but the Vita output is 960x544.
    // Downsample before the GPU upload to reduce each border from about 8 MiB
    // to about 2 MiB of graphics memory.
    if (borderWidth > 960 || borderHeight > 544) {
        const int targetWidth = 960;
        const int targetHeight = 544;
        unsigned char* resized = (unsigned char*)malloc((size_t)targetWidth * targetHeight * 4U);
        if (resized == NULL) {
            stbi_image_free(pixels);
            borderLog("resize_alloc_failed");
            return 0;
        }
        for (int y = 0; y < targetHeight; ++y) {
            int sourceY = (int)((long long)y * borderHeight / targetHeight);
            for (int x = 0; x < targetWidth; ++x) {
                int sourceX = (int)((long long)x * borderWidth / targetWidth);
                const unsigned char* source = pixels + ((size_t)sourceY * borderWidth + sourceX) * 4U;
                unsigned char* target = resized + ((size_t)y * targetWidth + x) * 4U;
                target[0] = source[0];
                target[1] = source[1];
                target[2] = source[2];
                target[3] = source[3];
            }
        }
        stbi_image_free(pixels);
        pixels = resized;
        borderWidth = targetWidth;
        borderHeight = targetHeight;
    }

    glGenTextures(1, &borderTexture);
    glBindTexture(GL_TEXTURE_2D, borderTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Borders are background art and do not need 32-bit color. Pack in place
    // to halve GPU residency, which is especially important while two textures
    // coexist during a crossfade.
    size_t pixelCount = (size_t)borderWidth * (size_t)borderHeight;
    unsigned short* packed = (unsigned short*)pixels;
    for (size_t i = 0; i < pixelCount; ++i) {
        const unsigned char* src = pixels + i * 4U;
        unsigned short a = src[3] == 0 ? 0 : (unsigned short)((src[3] + 15U) >> 4);
        if (a > 15U) a = 15U;
        packed[i] = (unsigned short)(((unsigned short)(src[0] >> 4) << 12) |
                                     ((unsigned short)(src[1] >> 4) << 8) |
                                     ((unsigned short)(src[2] >> 4) << 4) | a);
    }
    while (glGetError() != GL_NO_ERROR) {}
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, borderWidth, borderHeight, 0, GL_RGBA,
                 GL_UNSIGNED_SHORT_4_4_4_4, packed);
    free(pixels);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &borderTexture);
        borderTexture = 0;
        borderLog("upload_failed");
        return 0;
    }
    // PNG decoding/uploading can itself take longer than the transition. Start
    // the visible crossfade only after the replacement texture is actually ready.
    if (previousBorderTexture != 0) borderTransitionStart = sceKernelGetProcessTimeWide();
    borderLog("loaded");
    return 1;
}

void VitaBorders_init(int chapter) {
    borderChapter = chapter;
    // Never show chapter art over VitaGL, initialization, opening logos or the
    // chapter title/menu. Gameplay explicitly unlocks it after PLACE_MENU.
    borderPath = NULL;
    borderLoadAttempted = 0;
    chapterMenuSeen = 0;
    gameplayBordersActive = 0;
    borderLog(chapter > 0 ? "waiting_for_gameplay" : "launcher_disabled");
}

static char customBorderPath[256] = {0};

static int roomInList(const char* room, const char* const* rooms, size_t count) {
    if (room == NULL) return 0;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(room, rooms[i]) == 0) return 1;
    return 0;
}

#define ROOM_LIST_COUNT(list) (sizeof(list) / sizeof((list)[0]))

/* Room groups recovered from NXRUNE's console-border controller.  Keep these
 * explicit: substring guesses select the wrong art in Chapter 5's Garden,
 * cliff and Card Castle transitions. */
static const char* const ch5LightWorldRooms[] = {
    "room_town_krisyard", "room_town_northwest", "room_town_north", "room_beach",
    "room_town_mid", "room_town_apartments", "room_town_south", "room_town_school",
    "room_town_church", "room_graveyard", "room_town_shelter", "room_town_noellehouse",
    "room_lw_church_main", "room_torielclass", "room_schoollobby", "room_alphysclass",
    "room_schooldoor", "room_school_unusedroom"
};
static const char* const ch5CastleRightRooms[] = {
    "room_dw_fcastle_foyer", "room_dw_fcastle_foxhunt", "room_dw_fcastle_gloves_tower",
    "room_dw_fcastle_green_orange_battle", "room_dw_fcastle_obscured_bullets",
    "room_dw_fcastle_orange_puppet_introduction", "room_dw_fcastle_right_endingscene",
    "room_dw_fcastle_right_puzzle", "room_dw_fcastle_right_wing_floweryscene",
    "room_dw_fcastle_second_diner", "room_dw_fcastle_sidepuzzle",
    "room_dw_fcastle_terracotta_bonus", "room_dw_fcastle_terracotta_encounter",
    "room_dw_fcastle_trainroom", "room_dw_fcastle_terracotta_puzzle",
    "room_dw_fcastle_foxhunt_terakota", "room_dw_fcastle_foxhunt_socks",
    "room_dw_fcastle_foxhunt_chaos", "room_dw_fcastle_foxhunt_secret",
    "room_dw_fcastle_fusumadodge"
};
static const char* const ch5CastleLeftRooms[] = {
    "room_dw_fcastle_cafe", "room_dw_fcastle_blueroom", "room_dw_fcastle_bounce_1",
    "room_dw_fcastle_bounce_3", "room_dw_fcastle_dangerous_platforming",
    "room_dw_fcastle_left_twodoors", "room_dw_fcastle_left_wing_floweryscene",
    "room_dw_fcastle_onsen", "room_dw_fcastle_sandtrap",
    "room_dw_fcastle_shinobeetle_encounter", "room_dw_fcastle_yellow_miniboss",
    "room_dw_fcastle_yellowjail", "room_dw_fcastle_zenlooker"
};
static const char* const ch5CastleGoldRooms[] = {
    "room_dw_fcastle_top_pinkdoor", "room_dw_fcastle_green_checkpoint",
    "room_dw_fcastle_flowery", "room_dw_post_fountain_close",
    "room_dw_fcastle_top_staircase_1", "room_dw_fcastle_top_staircase_2",
    "room_dw_fcastle_final_save"
};
static const char* const ch5CastleTopRooms[] = {
    "room_dw_fcastle_top_entrance", "room_dw_fcastle_top_fountain", "room_dw_flowery_tree",
    "room_dw_fcastle_ultradash", "room_dw_fcastle_yellowblue",
    "room_dw_fcastle_seth_encounter", "room_dw_fcastle_top_ascent",
    "room_dw_fcastle_top_challenge", "room_dw_fcastle_top_descent",
    "room_dw_fcastle_orange_gauntlet"
};
static const char* const ch5GardenCliffRooms[] = {
    "room_dw_garden_aqua", "room_dw_garden_finalplatforming", "room_dw_garden_aquahole",
    "room_dw_garden_aquadarkness", "room_dw_garden_aquashrine", "room_dw_garden_starwalkerdash",
    "room_dw_garden_wateringcan_aqua", "room_dw_garden_aquadash",
    "room_dw_garden_aquaplatforming", "room_dw_garden_finalplatforming_right",
    "room_dw_garden_aquatransition", "room_dw_garden_aquahole_left",
    "room_dw_fcastle_heldmushrooms", "room_dw_fcastle_right_penultimate",
    "room_dw_fcastle_left_penultimate", "room_dw_fcastle_flowerydash",
    "room_dw_fcastle_shinobeetle_3d"
};

static const char* getCustomBorder(const char* room) {
    if (!room) return NULL;
    SceUID fd = sceIoOpen(BORDER_ROOT "borders_config.txt", SCE_O_RDONLY, 0);
    if (fd < 0) return NULL;
    
    // Manual mappings are appended over time by R + D-Pad.  The old 1 KiB
    // read silently ignored entries near the end of a real configuration and
    // the automatic chapter border replaced the selected border next frame.
    char buffer[4096];
    int bytes = sceIoRead(fd, buffer, sizeof(buffer) - 1);
    sceIoClose(fd);
    if (bytes <= 0) return NULL;
    buffer[bytes] = '\0';
    
    char search[128];
    snprintf(search, sizeof(search), "%s=", room);
    char* line = buffer;
    while ((line = strstr(line, search)) != NULL) {
        if (line == buffer || line[-1] == '\n' || line[-1] == '\r') break;
        line++;
    }
    if (!line) {
        // try fallback chapter mapping e.g. chapter_1=...
        snprintf(search, sizeof(search), "chapter_%d=", borderChapter);
        line = buffer;
        while ((line = strstr(line, search)) != NULL) {
            if (line == buffer || line[-1] == '\n' || line[-1] == '\r') break;
            line++;
        }
    }
    
    if (line) {
        line += strlen(search);
        char* end = strpbrk(line, "\r\n");
        if (end) *end = '\0';
        snprintf(customBorderPath, sizeof(customBorderPath), BORDER_ROOT "%s", line);
        return customBorderPath;
    }
    return NULL;
}

static const char* roomBorder(const char* room) {
    if (room == NULL) return NULL;
    // Borders belong behind gameplay only. Native save/configuration screens
    // and the chapter selector already provide their own complete backdrop.
    if (strstr(room, "MENU") != NULL || strstr(room, "menu") != NULL ||
        strstr(room, "CHAPTER_SELECT") != NULL || strstr(room, "chapter_select") != NULL ||
        strstr(room, "SELECT") != NULL || strstr(room, "select") != NULL) {
        if (strstr(room, "PLACE_MENU") != NULL) {
            chapterMenuSeen = 1;
            gameplayBordersActive = 0;
        }
        return NULL;
    }
    
    const char* custom = getCustomBorder(room);
    if (custom != NULL) return custom;

    if (!gameplayBordersActive) {
        if (!chapterMenuSeen) return NULL;
        // The first regular room reached after the chapter menu marks the
        // actual game session. Openings before that point stay borderless.
        gameplayBordersActive = 1;
        borderLog("gameplay_enabled");
    }
    // Room mappings are intentionally user-controlled. NXRUNE remains an
    // offline reference, while borders_config.txt (including entries written
    // by the in-game border cycler) is authoritative on Vita. Unmapped rooms
    // receive only the conservative chapter default.
    return chapterBorder(borderChapter);
#if 0
    if (borderChapter == 5) {
        if (roomInList(room, ch5LightWorldRooms, ROOM_LIST_COUNT(ch5LightWorldRooms)) ||
            strstr(room, "krisroom") != NULL || strstr(room, "torhouse") != NULL)
            return BORDER_ROOT "border_lw_town_0.png";
        if (strcmp(room, "room_dw_fcastle_cafe") == 0)
            return BORDER_ROOT "border_dw_castle_cafe_0.png";
        if (roomInList(room, ch5GardenCliffRooms, ROOM_LIST_COUNT(ch5GardenCliffRooms)))
            return BORDER_ROOT "border_dw_garden_cliff_0.png";
        if (roomInList(room, ch5CastleRightRooms, ROOM_LIST_COUNT(ch5CastleRightRooms)))
            return BORDER_ROOT "border_dw_castle_right_0.png";
        if (roomInList(room, ch5CastleLeftRooms, ROOM_LIST_COUNT(ch5CastleLeftRooms)))
            return BORDER_ROOT "border_dw_castle_left_0.png";
        if (roomInList(room, ch5CastleGoldRooms, ROOM_LIST_COUNT(ch5CastleGoldRooms)))
            return BORDER_ROOT "border_dw_castle_right_gold_0.png";
        if (roomInList(room, ch5CastleTopRooms, ROOM_LIST_COUNT(ch5CastleTopRooms)))
            return BORDER_ROOT "border_dw_castle_top_0.png";
        if (strcmp(room, "room_dw_pink_encounter") == 0)
            return BORDER_ROOT "border_dw_pink_0.png";
    }
    // Chapter 5 opens in Hometown. Garden is selected only by rooms whose
    // names explicitly identify that Dark World area.
    if (borderChapter == 5 &&
        (strstr(room, "intro") != NULL || strstr(room, "kris") != NULL ||
         strstr(room, "torhouse") != NULL || strstr(room, "hallway") != NULL ||
         strstr(room, "town") != NULL || strstr(room, "city") != NULL ||
         strstr(room, "home") != NULL || strstr(room, "house") != NULL))
        return BORDER_ROOT "border_lw_town_0.png";
    if (strstr(room, "titan") != NULL) return BORDER_ROOT "border_dw_titan_eyes_0.png";
    if (strstr(room, "cliff") != NULL) return BORDER_ROOT "border_dw_garden_cliff_0.png";
    if (strstr(room, "garden") != NULL) return BORDER_ROOT "border_dw_garden_0.png";
    if (borderChapter == 4 && strstr(room, "churchc_") != NULL)
        return BORDER_ROOT "border_dw_church_c_0.png";
    if (borderChapter == 4 && strstr(room, "churchb_") != NULL)
        return BORDER_ROOT "border_dw_church_b_0.png";
    if (strstr(room, "church") != NULL) return BORDER_ROOT "border_dw_church_a_0.png";
    if (strstr(room, "mansion") != NULL) return BORDER_ROOT "border_dw_mansion_0.png";
    if (borderChapter == 3 && strstr(room, "ranking_z") != NULL)
        return BORDER_ROOT "border_dw_green_sloppy_z_0.png";
    if (borderChapter == 3 && strstr(room, "changing_room") != NULL)
        return BORDER_ROOT "border_dw_green_sloppy_0.png";
    if (borderChapter == 3 && (strstr(room, "puzzlecloset") != NULL))
        return BORDER_ROOT "border_dw_tv_blue_0.png";
    if (borderChapter == 3 && (strstr(room, "board_") != NULL))
        return BORDER_ROOT "border_dw_tv_meta_0.png";
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
#endif
}

void VitaBorders_updateRoom(const char* roomName) {
    const char* next = roomBorder(roomName);
    if (roomName != NULL && strcmp(borderRoom, roomName) == 0 && next == borderPath) return;
    snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
    
    SceUID fd_room = sceIoOpen(BORDER_ROOT "current_room.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd_room >= 0) {
        sceIoWrite(fd_room, borderRoom, strlen(borderRoom));
        sceIoClose(fd_room);
    }

    if (next == borderPath) return;
    if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
    previousBorderTexture = borderTexture;
    previousBorderWidth = borderWidth;
    previousBorderHeight = borderHeight;
    borderTexture = 0;
    borderWidth = borderHeight = 0;
    borderLoadAttempted = 0;
    borderPath = next;
    borderTransitionStart = sceKernelGetProcessTimeWide();
    borderLog("room_selected");
}

static void updateBordersConfig(const char* room, const char* borderFilename) {
    if (!room || !borderFilename) return;
    
    SceUID fd = sceIoOpen(BORDER_ROOT "borders_config.txt", SCE_O_RDONLY, 0);
    char buffer[4096] = {0};
    int bytes = 0;
    if (fd >= 0) {
        bytes = sceIoRead(fd, buffer, sizeof(buffer) - 1);
        sceIoClose(fd);
    }
    if (bytes < 0) bytes = 0;
    buffer[bytes] = '\0';
    
    char search[128];
    snprintf(search, sizeof(search), "%s=", room);
    
    char newBuffer[4096] = {0};
    char* line = strstr(buffer, search);
    if (line) {
        int prefixLen = line - buffer;
        strncpy(newBuffer, buffer, prefixLen);
        char* end = strpbrk(line, "\r\n");
        if (!end) end = buffer + bytes;
        snprintf(newBuffer + prefixLen, sizeof(newBuffer) - prefixLen, "%s=%s", room, borderFilename);
        strncat(newBuffer, end, sizeof(newBuffer) - strlen(newBuffer) - 1);
    } else {
        snprintf(newBuffer, sizeof(newBuffer), "%s\n%s=%s\n", buffer, room, borderFilename);
    }
    
    fd = sceIoOpen(BORDER_ROOT "borders_config.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, newBuffer, strlen(newBuffer));
        sceIoClose(fd);
    }
}

void VitaBorders_cycleCurrent(int direction) {
    if (strlen(borderRoom) == 0) return;
    
    SceUID dir = sceIoDopen(BORDER_ROOT);
    if (dir < 0) return;
    
    char files[64][128];
    int count = 0;
    SceIoDirent dir_stat;
    while (sceIoDread(dir, &dir_stat) > 0 && count < 64) {
        if (strstr(dir_stat.d_name, ".png") != NULL) {
            strncpy(files[count], dir_stat.d_name, 127);
            files[count][127] = '\0';
            count++;
        }
    }
    sceIoDclose(dir);
    
    if (count == 0) return;
    
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(files[i], files[j]) > 0) {
                char temp[128];
                strcpy(temp, files[i]);
                strcpy(files[i], files[j]);
                strcpy(files[j], temp);
            }
        }
    }
    
    int currentIndex = -1;
    if (borderPath != NULL) {
        const char* currentFilename = strrchr(borderPath, '/');
        if (currentFilename) currentFilename++;
        else currentFilename = borderPath;
        
        for (int i = 0; i < count; i++) {
            if (strcmp(files[i], currentFilename) == 0) {
                currentIndex = i;
                break;
            }
        }
    }
    
    if (currentIndex == -1) currentIndex = 0;
    else currentIndex = (currentIndex + direction + count) % count;
    
    snprintf(customBorderPath, sizeof(customBorderPath), BORDER_ROOT "%s", files[currentIndex]);
    
    if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
    previousBorderTexture = borderTexture;
    previousBorderWidth = borderWidth;
    previousBorderHeight = borderHeight;
    borderTexture = 0;
    borderWidth = borderHeight = 0;
    borderLoadAttempted = 0;
    borderPath = customBorderPath;
    borderTransitionStart = sceKernelGetProcessTimeWide();
    borderLog("room_selected_cycle");
    
    updateBordersConfig(borderRoom, files[currentIndex]);
}

static void drawBorderTexture(GLuint texture, int windowW, int windowH, float alpha, int opaque) {
    if (texture == 0 || alpha <= 0.0f) return;
    const BorderVertex vertices[4] = {
        {0, 0, 1, 1, 1, alpha, -1,  1},
        {1, 0, 1, 1, 1, alpha,  1,  1},
        {1, 1, 1, 1, 1, alpha,  1, -1},
        {0, 1, 1, 1, 1, alpha, -1, -1},
    };
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW, windowH);
    glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    if (opaque) glDisable(GL_BLEND);
    else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BorderVertex), &vertices[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(BorderVertex), &vertices[0].r);
    glVertexPointer(2, GL_FLOAT, sizeof(BorderVertex), &vertices[0].x);
    glUseProgram(0);
#ifdef __vita__
    glBindVertexArray(0);
#endif
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);
}

void VitaBorders_draw(int windowW, int windowH) {
    if (!g_vitaConsoleBordersEnabled) return;
    if (borderPath != NULL && borderTexture == 0 && !borderLoadAttempted) loadBorderTexture();

    float progress = 1.0f;
    if (borderTransitionStart != 0) {
        uint64_t elapsed = sceKernelGetProcessTimeWide() - borderTransitionStart;
        progress = elapsed >= BORDER_TRANSITION_US ? 1.0f : (float)elapsed / (float)BORDER_TRANSITION_US;
    }
    if (previousBorderTexture != 0) {
        // True crossfade when a replacement exists; fade to black when entering
        // a menu where borders are deliberately disabled.
        drawBorderTexture(previousBorderTexture, windowW, windowH,
                          borderTexture != 0 ? 1.0f : 1.0f - progress,
                          borderTexture != 0);
    }
    if (borderTexture != 0) drawBorderTexture(borderTexture, windowW, windowH, progress, previousBorderTexture == 0 && progress >= 1.0f);

    if (progress >= 1.0f && previousBorderTexture != 0) {
        glDeleteTextures(1, &previousBorderTexture);
        previousBorderTexture = 0;
        previousBorderWidth = previousBorderHeight = 0;
        borderTransitionStart = 0;
    }
}

int VitaBorders_filesAvailable(void) {
    const char* required = chapterBorder(borderChapter);
    if (required == NULL) return 0;
    SceIoStat stat;
    return sceIoGetstat(required, &stat) >= 0 && stat.st_size > 0;
}

void VitaBorders_shutdown(void) {
    if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
    if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
    borderTexture = 0;
    previousBorderTexture = 0;
}
