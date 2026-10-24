#include "vita_borders.h"
#include "vita_border_rules.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#define BORDER_ROOT "ux0:data/deltarune/deltarunevita/borders/"
#define BETTER_BORDER_ROOT "ux0:data/deltarune/deltarunevita/mods/Borders/"
#define BORDER_LOG "ux0:data/deltarune/deltarunevita/butterscotch-probe.log"
// A full-resolution 960x544 BC3 border occupies about 510 KiB. It preserves
// substantially more detail than the former half-resolution RGBA4444 fallback
// while remaining small enough to keep resident between neighbouring rooms.
#define BORDER_BC3_UPLOAD_ENABLED 1

extern int g_vitaConsoleBordersEnabled;
extern int g_vitaConsoleBorderMode;
extern int g_vitaBetterBordersEnabled;
extern int g_vitaProbeLoggingEnabled;
extern bool g_vitaModernGlActive;

typedef struct {
    GLfloat u, v;
    GLfloat r, g, b, a;
    GLfloat x, y;
} BorderVertex;

// VitaGL may defer consumption of client arrays until command submission.
// Keep border vertices alive for the entire process instead of pointing it at
// a temporary stack array that disappears when drawBorderTexture returns.
static BorderVertex borderVertices[4];
static GLuint borderModernProgram = 0;
static GLint borderModernTexture = -1;

static GLuint compileBorderShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static int ensureModernBorderProgram(void) {
    if (borderModernProgram != 0) return 1;
    static const char* vertexSource =
        "#version 100\n"
        "attribute vec2 aPosition; attribute vec2 aTexCoord; attribute vec4 aColor;\n"
        "varying vec2 vTexCoord; varying vec4 vColor;\n"
        "void main(){ gl_Position=vec4(aPosition,0.0,1.0); vTexCoord=aTexCoord; vColor=aColor; }\n";
    static const char* fragmentSource =
        "#version 100\n"
        "precision mediump float; uniform sampler2D uTexture;\n"
        "varying vec2 vTexCoord; varying vec4 vColor;\n"
        "void main(){ gl_FragColor=texture2D(uTexture,vTexCoord)*vColor; }\n";
    GLuint vs = compileBorderShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = compileBorderShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    borderModernProgram = glCreateProgram();
    glAttachShader(borderModernProgram, vs);
    glAttachShader(borderModernProgram, fs);
    glBindAttribLocation(borderModernProgram, 0, "aPosition");
    glBindAttribLocation(borderModernProgram, 1, "aTexCoord");
    glBindAttribLocation(borderModernProgram, 2, "aColor");
    glLinkProgram(borderModernProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(borderModernProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(borderModernProgram);
        borderModernProgram = 0;
        return 0;
    }
    borderModernTexture = glGetUniformLocation(borderModernProgram, "uTexture");
    return 1;
}

static char borderPath[512] = {0};
static GLuint borderTexture = 0;
static GLuint previousBorderTexture = 0;
static int previousBorderWidth = 0;
static int previousBorderHeight = 0;
static uint64_t borderTransitionStart = 0;
#define BORDER_TRANSITION_US 700000ULL
static int borderWidth = 0;
static int borderHeight = 0;
static int borderLoadAttempted = 0;
static int borderChapter = 0;
static char borderRoom[96] = {0};
static int chapterMenuSeen = 0;
static int gameplayBordersActive = 0;
static int bordersWereEnabled = 0;
static int borderDrawSuppressed = 0;
static int borderMenuSuppressed = 0;
static int borderSceneSuppressed = 0;
static int borderBlackHold = 0;
static int borderBlackHoldCompleteLogged = 0;
static int borderMemoryPressure = 0;
static int borderMemoryPressureLogged = 0;
static char bordersConfig[8192] = {0};
static char automaticBorderNames[64][128];
static char automaticBorderKeys[64][112];
static int automaticBorderCount = 0;
static VitaBorderRuleState borderRuleState = {0};
static char ruleBorderPath[512] = {0};
static VitaBorderSource borderSelectedSource = VITA_BORDER_SOURCE_FALLBACK;
static VitaBorderDecision borderRuleDecision = { VITA_BORDER_ASSET_NONE, 0, 0.025f, 0x000000U };
static float borderOverlayAlpha = 0.0f;
static float borderOverlayTarget = 0.0f;
static float borderOverlaySpeed = 0.025f;
static unsigned int borderOverlayColor = 0x000000U;
static GLuint borderOverlayTexture = 0;
static int borderNativeControllerActive = 0;
static int borderNativeCrossfade = 0;
static float borderNativeCrossfadeAlpha = 0.0f;
static float borderNativeOverlayAlpha = 0.0f;
static unsigned int borderNativeOverlayColor = 0x000000U;
static char borderNativeCurrentPath[512] = {0};
static char borderNativePendingPath[512] = {0};
static char borderNativeLayerPath[512] = {0};
static float borderNativeLayerAlpha = 0.0f;
static float borderNativeRedLayerAlpha = 0.0f;
static float borderNativeCustomEffectAlpha = 0.0f;
static unsigned int borderNativeCustomEffectColor = 0x000000U;
static GLuint borderNativeLayerTexture = 0;
static GLuint borderNativeRedLayerTexture = 0;
static char borderNativeLayerLoadedPath[512] = {0};

typedef enum Chapter3BorderScene {
    CH3_BORDER_SCENE_ROOM_DEFAULT = 0,
    CH3_BORDER_SCENE_GAMESHOW_BLACK,
    CH3_BORDER_SCENE_GAMESHOW_BLUE,
    CH3_BORDER_SCENE_TENNA_RED,
    CH3_BORDER_SCENE_TV_BLACK,
    CH3_BORDER_SCENE_KNIGHT_BLACK
} Chapter3BorderScene;

static Chapter3BorderScene chapter3BorderScene = CH3_BORDER_SCENE_ROOM_DEFAULT;

static void borderLog(const char* phase);

static int isBorderBlackFadeRoom(const char* roomName) {
    if (roomName == NULL) return 0;
    return strcmp(roomName, "room_dw_castle_area_1") == 0 ||
           (borderChapter == 3 && strcmp(roomName, "room_ed") == 0) ||
           (borderChapter == 4 && strcmp(roomName, "room_cc_fountain") == 0) ||
           strncmp(roomName, "room_gameover", 13) == 0 ||
           strcmp(roomName, "PLACE_FAILURE") == 0;
}

static void startBorderBlackFade(void) {
    const int starting = !borderBlackHold;
    borderBlackHold = 1;
    if (starting) borderBlackHoldCompleteLogged = 0;
    // Keep the current surround drawable while the black cover comes in.
    // Use a full second at 30 FPS so room/title transitions do not expose a
    // one-frame border cut while the application surface is being replaced.
    borderMenuSuppressed = 0;
    borderSceneSuppressed = 0;
    borderOverlayTarget = 1.0f;
    borderOverlaySpeed = 1.0f / 30.0f;
    borderOverlayColor = 0x000000U;
    if (starting) borderLog("black_fade_begin");
}

static void startImmediateBlackHold(void) {
    const int starting = !borderBlackHold;
    borderBlackHold = 1;
    borderBlackHoldCompleteLogged = 0;
    borderMenuSuppressed = 0;
    borderSceneSuppressed = 0;
    // This room is already the black approach to Castle Town. Starting a
    // 30-frame fade here leaves the school surround visible through most of
    // the transformation and carries its texture into the room allocation.
    borderOverlayAlpha = 1.0f;
    borderOverlayTarget = 1.0f;
    borderOverlaySpeed = 1.0f;
    borderOverlayColor = 0x000000U;
    if (starting) borderLog("black_hold_immediate");
}

static uint32_t borderReadLe32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t borderReadLe64(const unsigned char* p) {
    return (uint64_t)borderReadLe32(p) |
           ((uint64_t)borderReadLe32(p + 4) << 32);
}

static int loadBorderBc3(void) {
    size_t pathLength = strlen(borderPath);
    if (pathLength < 4 || strcmp(borderPath + pathLength - 4, ".png") != 0)
        return 0;
    char pvrPath[512];
    snprintf(pvrPath, sizeof(pvrPath), "%s", borderPath);
    memcpy(pvrPath + pathLength - 4, ".pvr", 5);
    FILE* file = fopen(pvrPath, "rb");
    if (file == NULL) return 0;

    unsigned char header[52];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        borderReadLe32(header) != 0x03525650U ||
        borderReadLe64(header + 8) != 11U ||
        borderReadLe32(header + 16) != 1U ||
        borderReadLe32(header + 32) != 1U ||
        borderReadLe32(header + 36) != 1U ||
        borderReadLe32(header + 40) != 1U ||
        borderReadLe32(header + 44) != 1U) {
        fclose(file);
        borderLog("bc3_invalid_fallback_png");
        return 0;
    }
    uint32_t height = borderReadLe32(header + 24);
    uint32_t width = borderReadLe32(header + 28);
    uint32_t metadataSize = borderReadLe32(header + 48);
    uint64_t expected = (uint64_t)((width + 3U) / 4U) *
                        (uint64_t)((height + 3U) / 4U) * 16ULL;
    if (width == 0 || height == 0 || width > 960 || height > 544 ||
        metadataSize > 1024U * 1024U || expected == 0 || expected > UINT32_MAX ||
        fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        borderLog("bc3_layout_fallback_png");
        return 0;
    }
    long dataOffset = 52L + (long)metadataSize;
    long end = ftell(file);
    if (end - dataOffset != (long)expected ||
        fseek(file, dataOffset, SEEK_SET) != 0) {
        fclose(file);
        borderLog("bc3_size_fallback_png");
        return 0;
    }
    unsigned char* data = (unsigned char*)malloc((size_t)expected);
    if (data == NULL || fread(data, 1, (size_t)expected, file) != expected) {
        free(data);
        fclose(file);
        borderLog("bc3_read_fallback_png");
        return 0;
    }
    fclose(file);

    glGenTextures(1, &borderTexture);
    glBindTexture(GL_TEXTURE_2D, borderTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    while (glGetError() != GL_NO_ERROR) {}
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                           (GLsizei)width, (GLsizei)height, 0,
                           (GLsizei)expected, data);
    free(data);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &borderTexture);
        borderTexture = 0;
        borderLog("bc3_upload_fallback_png");
        return 0;
    }
    borderWidth = (int)width;
    borderHeight = (int)height;
    if (previousBorderTexture != 0)
        borderTransitionStart = sceKernelGetProcessTimeWide();
    borderLog("loaded_bc3");
    return 1;
}


static int loadAuxBorderBc3(const char* pngPath, GLuint* outTexture) {
    if (pngPath == NULL || outTexture == NULL || pngPath[0] == '\0') return 0;
    size_t pathLength = strlen(pngPath);
    if (pathLength < 4 || strcmp(pngPath + pathLength - 4, ".png") != 0) return 0;
    char pvrPath[512];
    snprintf(pvrPath, sizeof(pvrPath), "%s", pngPath);
    memcpy(pvrPath + pathLength - 4, ".pvr", 5);
    FILE* file = fopen(pvrPath, "rb");
    if (file == NULL) return 0;
    unsigned char header[52];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        borderReadLe32(header) != 0x03525650U || borderReadLe64(header + 8) != 11U ||
        borderReadLe32(header + 16) != 1U || borderReadLe32(header + 32) != 1U ||
        borderReadLe32(header + 36) != 1U || borderReadLe32(header + 40) != 1U ||
        borderReadLe32(header + 44) != 1U) { fclose(file); return 0; }
    uint32_t height = borderReadLe32(header + 24), width = borderReadLe32(header + 28);
    uint32_t metadataSize = borderReadLe32(header + 48);
    uint64_t expected = (uint64_t)((width + 3U) / 4U) * ((height + 3U) / 4U) * 16ULL;
    if (width == 0 || height == 0 || width > 960 || height > 544 || expected > UINT32_MAX ||
        fseek(file, 52L + (long)metadataSize, SEEK_SET) != 0) { fclose(file); return 0; }
    unsigned char* data = (unsigned char*)malloc((size_t)expected);
    if (data == NULL || fread(data, 1, (size_t)expected, file) != expected) {
        free(data); fclose(file); return 0;
    }
    fclose(file);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    while (glGetError() != GL_NO_ERROR) {}
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                           (GLsizei)width, (GLsizei)height, 0, (GLsizei)expected, data);
    free(data);
    if (glGetError() != GL_NO_ERROR) { if (texture) glDeleteTextures(1, &texture); return 0; }
    *outTexture = texture;
    return 1;
}

static void updateNativeLayerTextures(void) {
    if (borderSelectedSource != VITA_BORDER_SOURCE_CONTROLLER) {
        if (borderNativeLayerTexture != 0) glDeleteTextures(1, &borderNativeLayerTexture);
        if (borderNativeRedLayerTexture != 0) glDeleteTextures(1, &borderNativeRedLayerTexture);
        borderNativeLayerTexture = 0;
        borderNativeRedLayerTexture = 0;
        borderNativeLayerLoadedPath[0] = '\0';
        return;
    }
    if (borderNativeLayerPath[0] == '\0' || borderNativeLayerAlpha <= 0.0f) {
        if (borderNativeLayerTexture != 0) glDeleteTextures(1, &borderNativeLayerTexture);
        borderNativeLayerTexture = 0;
        borderNativeLayerLoadedPath[0] = '\0';
    } else if (strcmp(borderNativeLayerLoadedPath, borderNativeLayerPath) != 0) {
        if (borderNativeLayerTexture != 0) glDeleteTextures(1, &borderNativeLayerTexture);
        borderNativeLayerTexture = 0;
        borderNativeLayerLoadedPath[0] = '\0';
        if (loadAuxBorderBc3(borderNativeLayerPath, &borderNativeLayerTexture))
            snprintf(borderNativeLayerLoadedPath, sizeof(borderNativeLayerLoadedPath), "%s", borderNativeLayerPath);
    }
    if (borderNativeRedLayerAlpha > 0.0f && borderNativeRedLayerTexture == 0)
        loadAuxBorderBc3(BORDER_ROOT "border_dw_titan_eyes_red_0.png", &borderNativeRedLayerTexture);
    if (borderNativeRedLayerAlpha <= 0.0f && borderNativeRedLayerTexture != 0) {
        glDeleteTextures(1, &borderNativeRedLayerTexture);
        borderNativeRedLayerTexture = 0;
    }
}

static void loadAutomaticBorderCatalog(void) {
    automaticBorderCount = 0;
    SceUID dir = sceIoDopen(BORDER_ROOT);
    if (dir < 0) return;
    SceIoDirent entry;
    while (sceIoDread(dir, &entry) > 0 && automaticBorderCount < 64) {
        const char* name = entry.d_name;
        size_t length = strlen(name);
        static const char prefix[] = "border_";
        static const char suffix[] = "_0.png";
        if (length <= sizeof(prefix) - 1 + sizeof(suffix) - 1 ||
            strncmp(name, prefix, sizeof(prefix) - 1) != 0 ||
            strcmp(name + length - (sizeof(suffix) - 1), suffix) != 0)
            continue;
        size_t keyLength = length - (sizeof(prefix) - 1) - (sizeof(suffix) - 1);
        if (keyLength == 0 || keyLength >= sizeof(automaticBorderKeys[0])) continue;
        snprintf(automaticBorderNames[automaticBorderCount],
                 sizeof(automaticBorderNames[automaticBorderCount]), "%s", name);
        memcpy(automaticBorderKeys[automaticBorderCount], name + sizeof(prefix) - 1, keyLength);
        automaticBorderKeys[automaticBorderCount][keyLength] = '\0';
        automaticBorderCount++;
    }
    sceIoDclose(dir);
}

static void loadBordersConfig(void) {
    bordersConfig[0] = '\0';
    SceUID fd = sceIoOpen(BORDER_ROOT "borders_config.txt", SCE_O_RDONLY, 0);
    if (fd < 0) return;
    int bytes = sceIoRead(fd, bordersConfig, sizeof(bordersConfig) - 1);
    sceIoClose(fd);
    if (bytes > 0) bordersConfig[bytes] = '\0';
}

static const char* borderSourceName(VitaBorderSource source) {
    switch (source) {
        case VITA_BORDER_SOURCE_CUSTOM: return "custom";
        case VITA_BORDER_SOURCE_BETTER: return "better";
        case VITA_BORDER_SOURCE_CONTROLLER: return "controller";
        case VITA_BORDER_SOURCE_FALLBACK:
        default: return "fallback";
    }
}

static void borderLog(const char* phase) {
    char line[1024];
    int length = snprintf(line, sizeof(line),
                          "CONSOLE_BORDER=%s chapter=%d room=%s source=%s controller=%d "
                          "path=%s current=%s pending=%s crossfade=%d alpha=%.3f "
                          "overlay=%.3f color=%06X layer=%s layer_alpha=%.3f red_alpha=%.3f "
                          "custom_alpha=%.3f custom_color=%06X size=%dx%d\n",
                          phase, borderChapter,
                          borderRoom[0] != '\0' ? borderRoom : "<none>",
                          borderSourceName(borderSelectedSource), borderNativeControllerActive,
                          borderPath[0] != '\0' ? borderPath : "none",
                          borderNativeCurrentPath[0] != '\0' ? borderNativeCurrentPath : "none",
                          borderNativePendingPath[0] != '\0' ? borderNativePendingPath : "none",
                          borderNativeCrossfade, (double)borderNativeCrossfadeAlpha,
                          (double)borderNativeOverlayAlpha, borderNativeOverlayColor,
                          borderNativeLayerPath[0] != '\0' ? borderNativeLayerPath : "none",
                          (double)borderNativeLayerAlpha, (double)borderNativeRedLayerAlpha,
                          (double)borderNativeCustomEffectAlpha, borderNativeCustomEffectColor,
                          borderWidth, borderHeight);
    if (!g_vitaProbeLoggingEnabled) return;
    if (length < 0) return;
    if ((size_t)length >= sizeof(line)) length = (int)sizeof(line) - 1;
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
    if (borderPath[0] == '\0') return 0;
    borderLog("png_load_begin");
    // BC3 halves the steady-state footprint, but VitaGL may abort instead of
    // returning an allocation error when CDRAM is fragmented near its limit.
    // RGBA4444 is larger, yet has proven safe across room transitions.
    if (BORDER_BC3_UPLOAD_ENABLED && loadBorderBc3()) return 1;
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
    borderLog("png_decode_complete");

    // Borders are deliberately kept at half the Vita framebuffer resolution.
    // They are background decoration and scale cleanly with linear filtering;
    // this lowers steady GPU residency from about 1 MiB to 255 KiB and keeps a
    // two-image crossfade close to 510 KiB.
    if (borderWidth > 480 || borderHeight > 272) {
        const int targetWidth = 480;
        const int targetHeight = 272;
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
    borderLog("png_resize_complete");

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
    borderLog("rgba4444_upload_begin");
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
    borderLog("loaded_rgba4444_halfres");
    return 1;
}

void VitaBorders_init(int chapter) {
    borderChapter = chapter;
    memset(&borderRuleState, 0, sizeof(borderRuleState));
    borderRuleState.chapter = chapter;
    borderNativeControllerActive = 0;
    borderNativeCrossfade = 0;
    borderNativeCrossfadeAlpha = 0.0f;
    borderNativeOverlayAlpha = 0.0f;
    borderNativeCurrentPath[0] = '\0';
    borderNativePendingPath[0] = '\0';
    borderNativeLayerPath[0] = '\0';
    borderNativeLayerAlpha = 0.0f;
    borderNativeRedLayerAlpha = 0.0f;
    borderNativeCustomEffectAlpha = 0.0f;
    // Never show chapter art over VitaGL, initialization, opening logos or the
    // chapter title/menu. Gameplay explicitly unlocks it after PLACE_MENU.
    borderPath[0] = '\0';
    borderLoadAttempted = 0;
    borderMenuSuppressed = 0;
    borderSceneSuppressed = 0;
    chapterMenuSeen = 0;
    gameplayBordersActive = 0;
    bordersWereEnabled = g_vitaConsoleBordersEnabled;
    loadBordersConfig();
    loadAutomaticBorderCatalog();
    borderLog(chapter > 0 ? "waiting_for_gameplay" : "launcher_disabled");
}

static void applyBorderRuleDecision(const VitaBorderDecision* decision) {
    if (decision == NULL) return;
    borderOverlayTarget = decision->overlayTarget ? 1.0f : 0.0f;
    borderOverlaySpeed = decision->overlaySpeed > 0.0f ? decision->overlaySpeed : 0.025f;
    borderOverlayColor = decision->overlayColor;
}
void VitaBorders_updateGameState(const VitaBorderRuleState* state) {
    if (state == NULL) return;
    borderRuleState = *state;
    borderRuleState.chapter = borderChapter;
}

static void borderSpriteExternalPath(const char* spriteName, char* out, size_t outSize) {
    if (out == NULL || outSize == 0) return;
    out[0] = '\0';
    if (spriteName == NULL || strncmp(spriteName, "border_", 7) != 0) return;
    size_t len = strlen(spriteName);
    if (len >= 2 && strcmp(spriteName + len - 2, "_0") == 0)
        snprintf(out, outSize, BORDER_ROOT "%.127s.png", spriteName);
    else
        snprintf(out, outSize, BORDER_ROOT "%.127s_0.png", spriteName);
}

void VitaBorders_updateControllerState(int active, const char* currentSpriteName,
                                       const char* pendingSpriteName, int crossfade,
                                       float crossfadeAlpha, float overlayAlpha,
                                       unsigned int overlayColor,
                                       const char* layerSpriteName, float layerAlpha,
                                       float redLayerAlpha, float customEffectAlpha,
                                       unsigned int customEffectColor) {
    borderNativeControllerActive = active ? 1 : 0;
    if (!borderNativeControllerActive) {
        borderNativeCrossfade = 0;
        borderNativeCrossfadeAlpha = 0.0f;
        borderNativeCurrentPath[0] = '\0';
        borderNativePendingPath[0] = '\0';
        borderNativeLayerPath[0] = '\0';
        borderNativeLayerAlpha = 0.0f;
        borderNativeRedLayerAlpha = 0.0f;
        borderNativeCustomEffectAlpha = 0.0f;
        return;
    }
    borderSpriteExternalPath(currentSpriteName, borderNativeCurrentPath, sizeof(borderNativeCurrentPath));
    borderSpriteExternalPath(pendingSpriteName, borderNativePendingPath, sizeof(borderNativePendingPath));
    borderNativeCrossfade = crossfade && borderNativePendingPath[0] != '\0' &&
                            strcmp(borderNativePendingPath, borderNativeCurrentPath) != 0;
    borderNativeCrossfadeAlpha = crossfadeAlpha;
    if (borderNativeCrossfadeAlpha < 0.0f) borderNativeCrossfadeAlpha = 0.0f;
    if (borderNativeCrossfadeAlpha > 1.0f) borderNativeCrossfadeAlpha = 1.0f;
    borderNativeOverlayAlpha = overlayAlpha;
    if (borderNativeOverlayAlpha < 0.0f) borderNativeOverlayAlpha = 0.0f;
    if (borderNativeOverlayAlpha > 1.0f) borderNativeOverlayAlpha = 1.0f;
    borderNativeOverlayColor = overlayColor & 0xFFFFFFU;
    borderSpriteExternalPath(layerSpriteName, borderNativeLayerPath, sizeof(borderNativeLayerPath));
    borderNativeLayerAlpha = layerAlpha < 0.0f ? 0.0f : (layerAlpha > 1.0f ? 1.0f : layerAlpha);
    borderNativeRedLayerAlpha = redLayerAlpha < 0.0f ? 0.0f : (redLayerAlpha > 1.0f ? 1.0f : redLayerAlpha);
    borderNativeCustomEffectAlpha = customEffectAlpha < 0.0f ? 0.0f :
                                    (customEffectAlpha > 1.0f ? 1.0f : customEffectAlpha);
    borderNativeCustomEffectColor = customEffectColor & 0xFFFFFFU;
}

static char customBorderPath[512] = {0};
static char automaticBorderPath[512] = {0};



static const char* getCustomBorder(const char* room) {
    if (!room) return NULL;
    if (bordersConfig[0] == '\0') return NULL;
    
    char search[128];
    snprintf(search, sizeof(search), "%s=", room);
    char* line = bordersConfig;
    while ((line = strstr(line, search)) != NULL) {
        if (line == bordersConfig || line[-1] == '\n' || line[-1] == '\r') break;
        line++;
    }
    if (!line) {
        // try fallback chapter mapping e.g. chapter_1=...
        snprintf(search, sizeof(search), "chapter_%d=", borderChapter);
        line = bordersConfig;
        while ((line = strstr(line, search)) != NULL) {
            if (line == bordersConfig || line[-1] == '\n' || line[-1] == '\r') break;
            line++;
        }
    }
    
    if (line) {
        line += strlen(search);
        size_t length = strcspn(line, "\r\n");
        // An explicit NONE is authoritative. Returning a regular NULL would
        // select the chapter fallback and draw another decorative border.
        if ((length == 4 && strncmp(line, "NONE", 4) == 0) ||
            (length == 4 && strncmp(line, "none", 4) == 0)) {
            customBorderPath[0] = '\0';
            return customBorderPath;
        }
        snprintf(customBorderPath, sizeof(customBorderPath), BORDER_ROOT "%.*s",
                 (int)length, line);
        return customBorderPath;
    }
    return NULL;
}

static const char* getAutomaticBorder(const char* room) {
    if (room == NULL) return NULL;
    int best = -1;
    size_t bestLength = 0;
    for (int i = 0; i < automaticBorderCount; ++i) {
        size_t keyLength = strlen(automaticBorderKeys[i]);
        if (keyLength > bestLength && strstr(room, automaticBorderKeys[i]) != NULL) {
            best = i;
            bestLength = keyLength;
        }
    }
    if (best < 0) return NULL;
    snprintf(automaticBorderPath, sizeof(automaticBorderPath), BORDER_ROOT "%s",
             automaticBorderNames[best]);
    return automaticBorderPath;
}

static const char* roomBorder(const char* room) {
    borderSelectedSource = VITA_BORDER_SOURCE_FALLBACK;
    if (room == NULL) return NULL;
    // Failure/Game Over fade the current surround to black. Their selection is
    // intentionally empty so no automatic rule can replace the held black.
    if (isBorderBlackFadeRoom(room) || strcmp(room, "PLACE_FAILURE") == 0 ||
        strncmp(room, "room_gameover", 13) == 0)
        return NULL;
    // Borders belong behind gameplay only. Native save/configuration screens
    // and the chapter selector already provide their own complete backdrop.
    if (VitaBorderRules_isMenuRoom(room)) {
        if (strstr(room, "PLACE_MENU") != NULL) {
            chapterMenuSeen = 1;
            gameplayBordersActive = 0;
        }
        return NULL;
    }
    
    if (!gameplayBordersActive) {
        if (!chapterMenuSeen) return NULL;
        // The first regular room reached after the chapter menu marks the
        // actual game session. Openings before that point stay borderless.
        gameplayBordersActive = 1;
        borderLog("gameplay_enabled");
    }
    // Simple mode deliberately keeps one inexpensive, neutral texture across
    // every gameplay room.  The normal transition code therefore retains the
    // same GL texture between adjacent rooms instead of recreating it.
    if (g_vitaConsoleBorderMode == 2)
        return BORDER_ROOT "border_line_simple.png";
    // This scene starts with a black surround and reveals Garden at the
    // sakura transition. A broad custom dw_garden mapping can otherwise
    // select the cliff-bottom image immediately and bypass that sequence.
    if (borderChapter == 5 &&
        strcmp(room, "room_dw_garden_floradinnencounter") == 0)
        return BORDER_ROOT "border_dw_garden_0.png";
    const char* custom = getCustomBorder(room);
    const char* betterPath = NULL;
    int chapter1HasUppercase = 0;
    if (borderChapter == 1) {
        for (const char* p = room; *p != '\0'; ++p) {
            if (*p >= 'A' && *p <= 'Z') { chapter1HasUppercase = 1; break; }
        }
        if (g_vitaBetterBordersEnabled && !chapter1HasUppercase) {
            if (strncmp(room, "room_cc", 7) == 0)
                betterPath = BETTER_BORDER_ROOT "border1.png";
            else if (strncmp(room, "room_field", 10) == 0)
                betterPath = BETTER_BORDER_ROOT "border2.png";
            else if (strncmp(room, "room_forest", 11) == 0)
                betterPath = BETTER_BORDER_ROOT "border3.png";
        }
    }

    const char* nativePath = NULL;
    if (borderChapter >= 2 && borderChapter <= 5 && borderNativeControllerActive) {
        const char* selectedNative =
            borderNativeCrossfade && borderNativePendingPath[0] != '\0' ?
            borderNativePendingPath : borderNativeCurrentPath;
        if (selectedNative[0] != '\0') nativePath = selectedNative;
    }

    VitaBorderSource source = VitaBorderRules_chooseSource(
        custom != NULL, betterPath != NULL, nativePath != NULL);
    borderSelectedSource = source;
    if (source == VITA_BORDER_SOURCE_CUSTOM) return custom;
    if (source == VITA_BORDER_SOURCE_BETTER) return betterPath;
    if (source == VITA_BORDER_SOURCE_CONTROLLER) return nativePath;

    // Chapter 1 remains on the established Vita mapping. The source selector
    // above only restores the required explicit-config > Better Borders order;
    // it does not migrate Chapter 1 to the native controller.
    if (borderChapter == 1) {
        if (!chapter1HasUppercase &&
            (strncmp(room, "room_town", 9) == 0 ||
             strncmp(room, "room_beach", 10) == 0))
            return BORDER_ROOT "border_lw_town_0.png";
        if (!chapter1HasUppercase &&
            (strncmp(room, "room_cc", 7) == 0 ||
             strncmp(room, "room_forest", 11) == 0 ||
             strncmp(room, "room_field", 10) == 0 ||
             strncmp(room, "room_castle", 11) == 0 ||
             strncmp(room, "room_shop", 9) == 0 ||
             strncmp(room, "room_legend", 11) == 0))
            return BORDER_ROOT "border_dw_castletown_0.png";
        return BORDER_ROOT "border_line_1080_0.png";
    }
    // Chapters 2-5 use the deterministic NXRUNE-derived resolver. Explicit
    // borders_config.txt entries above remain authoritative.
    if (borderChapter >= 2 && borderChapter <= 5) {
        VitaBorderRuleState state = borderRuleState;
        state.chapter = borderChapter;
        if (borderChapter == 3) {
            switch (chapter3BorderScene) {
                case CH3_BORDER_SCENE_GAMESHOW_BLUE:
                    state.sceneFlags |= VITA_BORDER_SCENE_CH3_BLUE_STARS;
                    break;
                case CH3_BORDER_SCENE_TENNA_RED:
                    state.sceneFlags |= VITA_BORDER_SCENE_CH3_RED_SMILES;
                    break;
                case CH3_BORDER_SCENE_TV_BLACK:
                    state.sceneFlags |= VITA_BORDER_SCENE_CH3_TV_BLACK;
                    break;
                case CH3_BORDER_SCENE_KNIGHT_BLACK:
                    state.sceneFlags |= VITA_BORDER_SCENE_CH3_KNIGHT_NEUTRAL;
                    break;
                default:
                    break;
            }
        }
        VitaBorderDecision decision = VitaBorderRules_resolve(room, &state);
        borderRuleDecision = decision;
        const char* filename = VitaBorderRules_filename(decision.asset);
        if (filename != NULL) {
            snprintf(ruleBorderPath, sizeof(ruleBorderPath), BORDER_ROOT "%s", filename);
            return ruleBorderPath;
        }
    }
    // Chapter 2 uses a closed prefix map, matching the deterministic Chapter
    // 1 policy. A manual room entry from borders_config.txt remains
    // authoritative because it is resolved above. Prefixes intentionally
    // include the trailing room family name: for example
    // room_dw_mansion_dininghall resolves to border_dw_mansion_0.png.
    if (borderChapter == 2) {
        // Shops live outside the normal area-name prefixes, so map them to the
        // border of the region they represent before applying family rules.
        if (strcmp(room, "room_shop_ch2_swatch") == 0)
            return BORDER_ROOT "border_dw_mansion_0.png";
        if (strcmp(room, "room_shop_ch2_music") == 0)
            return BORDER_ROOT "border_dw_cyber_0.png";
        if (strcmp(room, "room_shop_ch2_spamton") == 0)
            return BORDER_ROOT "border_dw_city_0.png";

        if (strncmp(room, "room_town", 9) == 0 ||
            strncmp(room, "room_graveyard", 14) == 0)
            return BORDER_ROOT "border_lw_town_0.png";

        if (strncmp(room, "room_castle", 11) == 0 ||
            strncmp(room, "room_dw_castle", 14) == 0 ||
            strncmp(room, "room_dw_ralsei", 14) == 0)
            return BORDER_ROOT "border_dw_castletown_0.png";

        if (strncmp(room, "room_dw_cyber", 13) == 0)
            return BORDER_ROOT "border_dw_cyber_0.png";

        if (strncmp(room, "room_dw_city", 12) == 0)
            return BORDER_ROOT "border_dw_city_0.png";

        if (strncmp(room, "room_transformation_sequence", 28) == 0 ||
            strncmp(room, "room_dw_mansion", 15) == 0)
            return BORDER_ROOT "border_dw_mansion_0.png";

        return BORDER_ROOT "border_line_1080_0.png";
    }
    // Chapter 3 uses a closed prefix map just like Chapters 1 and 2. It must
    // precede the automatic filename catalogue: that heuristic previously
    // made TV, gameshow and board rooms inherit border_dw_teevie_0 merely
    // because of a substring in the room name.
    if (borderChapter == 3) {
        // In-room cutscene signals have priority over the room family. They
        // remain latched until the next room so a one-frame sprite or sound
        // cannot make the decorative surround flicker.
        if (chapter3BorderScene == CH3_BORDER_SCENE_TENNA_RED)
            return BORDER_ROOT "border_dw_red_smiles_0.png";
        if (chapter3BorderScene == CH3_BORDER_SCENE_TV_BLACK)
            return BORDER_ROOT "border_dw_tv_black_0.png";
        if (chapter3BorderScene == CH3_BORDER_SCENE_KNIGHT_BLACK)
            return BORDER_ROOT "border_line_1080_0.png";
        if (strncmp(room, "room_ch3_gameshowroom", 21) == 0) {
            if (chapter3BorderScene == CH3_BORDER_SCENE_GAMESHOW_BLUE)
                return BORDER_ROOT "border_dw_blue_stars_0.png";
            return BORDER_ROOT "border_line_1080_0.png";
        }

        // Board/adventure rooms are minigames. A room transition inside the
        // board sequence deliberately resolves to the same file and therefore
        // keeps the already resident border texture.
        if (strncmp(room, "room_board", 10) == 0 ||
            strncmp(room, "room_adventureboard", 19) == 0)
            return BORDER_ROOT "border_dw_tv_meta_0.png";

        if (strncmp(room, "room_town", 9) == 0 ||
            strncmp(room, "room_graveyard", 14) == 0)
            return BORDER_ROOT "border_lw_town_0.png";
        if (strncmp(room, "room_castle", 11) == 0 ||
            strncmp(room, "room_dw_castle", 14) == 0 ||
            strncmp(room, "room_dw_ralsei", 14) == 0)
            return BORDER_ROOT "border_dw_castletown_0.png";
        if (strncmp(room, "room_dw_chef", 12) == 0)
            return BORDER_ROOT "border_dw_blue_stars_0.png";
        if (strncmp(room, "room_dw_couch", 13) == 0 ||
            strncmp(room, "room_dw_nondescript", 19) == 0)
            return BORDER_ROOT "border_line_1080_0.png";
        if (strcmp(room, "room_dw_tv_cutscene1g") == 0)
            return BORDER_ROOT "border_dw_blue_stars_0.png";
        if (strncmp(room, "room_dw_puzzlecloset", 20) == 0)
            return BORDER_ROOT "border_dw_tv_blue_0.png";
        if (strncmp(room, "room_dw_tv_curtain", 18) == 0)
            return BORDER_ROOT "border_dw_blue_stars_0.png";
        if (strncmp(room, "room_dw_teevie", 14) == 0 ||
            strncmp(room, "room_dw_b3bs", 12) == 0)
            return BORDER_ROOT "border_dw_teevie_0.png";

        // Every other ordinary Dark World family uses the green-room surround
        // unless a more specific rule above applies.
        if (strncmp(room, "room_dw_", 8) == 0)
            return BORDER_ROOT "border_dw_green_room_0.png";

        // Unmapped/system rooms are intentionally neutral rather than guessed.
        return BORDER_ROOT "border_line_1080_0.png";
    }
    // Convention fallback: border_dw_castletown_0.png applies to every room
    // whose name contains dw_castletown_. Longest keys win, so a specific
    // border such as dw_garden_cliff_bottom overrides the generic dw_garden.
    const char* automatic = getAutomaticBorder(room);
    if (automatic != NULL) return automatic;
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

static void clearBorderVisualsForMenu(void) {
    int hasTextures = borderTexture != 0 || previousBorderTexture != 0 ||
                      borderOverlayTexture != 0 || borderNativeLayerTexture != 0 ||
                      borderNativeRedLayerTexture != 0;
    if (hasTextures) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glFinish();
        if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
        if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
        if (borderOverlayTexture != 0) glDeleteTextures(1, &borderOverlayTexture);
        if (borderNativeLayerTexture != 0) glDeleteTextures(1, &borderNativeLayerTexture);
        if (borderNativeRedLayerTexture != 0) glDeleteTextures(1, &borderNativeRedLayerTexture);
        glFinish();
    }
    borderTexture = 0;
    previousBorderTexture = 0;
    borderOverlayTexture = 0;
    borderNativeLayerTexture = 0;
    borderNativeRedLayerTexture = 0;
    borderWidth = borderHeight = 0;
    previousBorderWidth = previousBorderHeight = 0;
    borderPath[0] = '\0';
    borderLoadAttempted = 0;
    borderTransitionStart = 0;
    borderOverlayAlpha = 0.0f;
    borderOverlayTarget = 0.0f;
    borderOverlaySpeed = 0.025f;
    borderOverlayColor = 0x000000U;
    borderNativeControllerActive = 0;
    borderNativeCrossfade = 0;
    borderNativeCrossfadeAlpha = 0.0f;
    borderNativeOverlayAlpha = 0.0f;
    borderNativeOverlayColor = 0x000000U;
    borderNativeCurrentPath[0] = '\0';
    borderNativePendingPath[0] = '\0';
    borderNativeLayerPath[0] = '\0';
    borderNativeLayerLoadedPath[0] = '\0';
    borderNativeLayerAlpha = 0.0f;
    borderNativeRedLayerAlpha = 0.0f;
    borderNativeCustomEffectAlpha = 0.0f;
    borderNativeCustomEffectColor = 0x000000U;
    borderSelectedSource = VITA_BORDER_SOURCE_FALLBACK;
}
void VitaBorders_updateRoom(const char* roomName) {
    int startGardenReveal = 0;
    if (borderChapter == 3 && roomName != NULL && strcmp(borderRoom, roomName) != 0) {
        chapter3BorderScene =
            strncmp(roomName, "room_ch3_gameshowroom", 21) == 0 ?
            CH3_BORDER_SCENE_GAMESHOW_BLACK : CH3_BORDER_SCENE_ROOM_DEFAULT;
    }
    if (isBorderBlackFadeRoom(roomName)) {
        if (roomName != NULL && strcmp(roomName, "room_dw_castle_area_1") == 0)
            startImmediateBlackHold();
        else
            startBorderBlackFade();
        snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
        return;
    }
    borderRuleDecision = (VitaBorderDecision){ VITA_BORDER_ASSET_NONE, 0, 0.025f, 0x000000U };
    const char* next = roomBorder(roomName);
    if (borderSelectedSource == VITA_BORDER_SOURCE_CONTROLLER) {
        borderOverlayAlpha = borderNativeOverlayAlpha;
        borderOverlayTarget = borderNativeOverlayAlpha;
        borderOverlaySpeed = 1.0f;
        borderOverlayColor = borderNativeOverlayColor;
    } else {
        applyBorderRuleDecision(&borderRuleDecision);
    }
    char nextPath[512] = {0};
    if (next != NULL) snprintf(nextPath, sizeof(nextPath), "%s", next);
    if (VitaBorderRules_isMenuRoom(roomName)) {
        if (borderBlackHold || borderTexture != 0 || previousBorderTexture != 0) {
            startBorderBlackFade();
            snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
            return;
        }
        int shouldLog = !borderMenuSuppressed || borderTexture != 0 || previousBorderTexture != 0 ||
                        borderNativeLayerTexture != 0 || borderNativeRedLayerTexture != 0 ||
                        (roomName != NULL && strcmp(borderRoom, roomName) != 0);
        borderMenuSuppressed = 1;
        snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
        SceUID fd_room = sceIoOpen(BORDER_ROOT "current_room.txt",
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (fd_room >= 0) {
            sceIoWrite(fd_room, borderRoom, strlen(borderRoom));
            sceIoClose(fd_room);
        }
        clearBorderVisualsForMenu();
        if (shouldLog) borderLog("menu_suppressed");
        return;
    }
    borderMenuSuppressed = 0;

    // Black persists across menus, death and chapter changes. Only an actual
    // border selection releases it; the replacement is loaded behind black
    // and revealed using the same smooth overlay cadence.
    if (borderBlackHold && nextPath[0] != '\0') {
        borderBlackHold = 0;
        borderBlackHoldCompleteLogged = 0;
        borderOverlayTarget = 0.0f;
        borderOverlaySpeed = 1.0f / 30.0f;
        borderOverlayColor = 0x000000U;
        borderLog("black_hold_released");
    }

    // Chapter 5's Garden opening intentionally has no decorative console
    // surround. Do not retain the previous Castletown/Garden texture behind a
    // black overlay: release it so the host margins are truly black. In the
    // Floradinn scene the Garden art is loaded only when the sakura transition
    // reaches stage 1. Explicit custom/Better borders remain authoritative.
    int blankSurround = VitaBorderRules_shouldBlankSurround(
        borderChapter, roomName, borderRuleState.sceneFlags);
    if (blankSurround) {
        int shouldLog = !borderSceneSuppressed ||
                        (roomName != NULL && strcmp(borderRoom, roomName) != 0) ||
                        borderTexture != 0 || previousBorderTexture != 0;
        borderSceneSuppressed = 1;
        snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
        if (borderTexture != 0 || previousBorderTexture != 0 ||
            borderNativeLayerTexture != 0 || borderNativeRedLayerTexture != 0)
            clearBorderVisualsForMenu();
        else {
            borderPath[0] = '\0';
            borderLoadAttempted = 0;
            borderTransitionStart = 0;
        }
        if (shouldLog) borderLog("room_blank_surround");
        return;
    }

    // Leaving the black Garden surround at the sakura reveal: start fully
    // covered and reveal the newly-loaded Garden border at the original
    // show_border(1/90) cadence.
    if (borderSceneSuppressed) {
        borderSceneSuppressed = 0;
        if (borderChapter == 5 && roomName != NULL &&
            strcmp(roomName, "room_dw_garden_floradinnencounter") == 0 &&
            (borderRuleState.sceneFlags & VITA_BORDER_SCENE_CH5_GARDEN_REVEAL)) {
            startGardenReveal = 1;
            borderOverlayAlpha = 1.0f;
            borderOverlayTarget = 0.0f;
            borderOverlaySpeed = 1.0f / 90.0f;
            borderOverlayColor = 0x000000U;
            borderLog("chapter5_garden_reveal_begin");
        }
    }
    if (roomName != NULL && strcmp(borderRoom, roomName) == 0 &&
        strcmp(nextPath, borderPath) == 0) return;

    // prepareRoomChange keeps a border alive when adjacent rooms resolve to
    // the same file.  The old update path still replaced it on the next frame
    // solely because borderRoom changed, then asked the renderer to trim its
    // atlas cache before uploading the identical image again.  In the Chapter
    // 2 castle this evicted page 11 (castle top) and page 19 (restaurant art),
    // producing intermittent wrong/missing scenery.  Update the room identity
    // without touching the retained texture when the resolved path is equal.
    if (borderTexture != 0 && strcmp(nextPath, borderPath) == 0) {
        snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
        SceUID fd_room = sceIoOpen(BORDER_ROOT "current_room.txt",
                                  SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (fd_room >= 0) {
            sceIoWrite(fd_room, borderRoom, strlen(borderRoom));
            sceIoClose(fd_room);
        }
        borderTransitionStart = 0;
        borderLog("room_updated_retained_same_border");
        return;
    }
    snprintf(borderRoom, sizeof(borderRoom), "%s", roomName != NULL ? roomName : "<null>");
    
    SceUID fd_room = sceIoOpen(BORDER_ROOT "current_room.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd_room >= 0) {
        sceIoWrite(fd_room, borderRoom, strlen(borderRoom));
        sceIoClose(fd_room);
    }

    if (previousBorderTexture != 0) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glFinish();
        glDeleteTextures(1, &previousBorderTexture);
        glFinish();
    }
    previousBorderTexture = borderTexture;
    previousBorderWidth = borderWidth;
    previousBorderHeight = borderHeight;
    borderTexture = 0;
    borderWidth = borderHeight = 0;
    borderLoadAttempted = 0;
    snprintf(borderPath, sizeof(borderPath), "%s", nextPath);
    // The sakura transition owns this reveal. Keep the Garden art fully
    // present behind the black overlay, then uncover it at show_border(1/90).
    // A second generic 350 ms texture fade made the two effects drift apart.
    borderTransitionStart = startGardenReveal ? 0 : sceKernelGetProcessTimeWide();
    borderLog("room_selected");
}

void VitaBorders_noteVisibleAsset(const char* assetName) {
    if (borderChapter != 3 || assetName == NULL || borderRoom[0] == '\0') return;

    Chapter3BorderScene next = chapter3BorderScene;
    // The Knight is the final and strongest signal. Keep the neutral black
    // border even if older gameshow/Tenna drawables remain alive behind it.
    if (strstr(assetName, "roaringknight") != NULL ||
        strstr(assetName, "obj_knight") != NULL ||
        strstr(assetName, "spr_knight") != NULL) {
        next = CH3_BORDER_SCENE_KNIGHT_BLACK;
    } else if (next != CH3_BORDER_SCENE_KNIGHT_BLACK &&
               strcmp(assetName, "spr_tenna_evil") == 0) {
        next = CH3_BORDER_SCENE_TENNA_RED;
    }

    if (next != chapter3BorderScene) {
        chapter3BorderScene = next;
        borderLog("chapter3_scene_signal");
    }
}

void VitaBorders_noteChapter3BoardSwitch(void) {
    if (borderChapter != 3 || chapter3BorderScene == CH3_BORDER_SCENE_KNIGHT_BLACK)
        return;
    if (chapter3BorderScene != CH3_BORDER_SCENE_GAMESHOW_BLUE) {
        chapter3BorderScene = CH3_BORDER_SCENE_GAMESHOW_BLUE;
        borderLog("chapter3_board_switch_blue");
    }
}

void VitaBorders_noteSound(const char* soundName) {
    if (borderChapter != 3 || soundName == NULL) return;
    if (strcmp(borderRoom, "room_dw_snow_zone") == 0 &&
        strcmp(soundName, "snd_whip_crack_only") == 0 &&
        chapter3BorderScene != CH3_BORDER_SCENE_KNIGHT_BLACK) {
        chapter3BorderScene = CH3_BORDER_SCENE_GAMESHOW_BLUE;
        borderLog("chapter3_tenna_snap_blue");
        return;
    }
    if (strcmp(soundName, "snd_tv_alarm") != 0 ||
        chapter3BorderScene == CH3_BORDER_SCENE_KNIGHT_BLACK) return;
    // The alarm follows the red Tenna presentation and switches to the black
    // TV surround. Accept it throughout the gameshow to remain robust across
    // translated/repacked data.win variants.
    chapter3BorderScene = CH3_BORDER_SCENE_TV_BLACK;
    borderLog("chapter3_tv_alarm");
}

void VitaBorders_setSuppressed(int suppressed) {
    borderDrawSuppressed = suppressed ? 1 : 0;
}

void VitaBorders_setMemoryPressure(int pressured) {
    int nextPressure = pressured ? 1 : 0;
    // A resident half-resolution RGBA4444 border costs only ~255 KiB and does
    // not allocate while it is drawn. Deleting it here made the free-memory
    // test alternate every frame between pressure/no-pressure, repeatedly
    // recreating a texture and eventually corrupting VitaGL's allocator. Keep
    // an existing border resident; pressure only postpones a missing upload.
    borderMemoryPressure = nextPressure;
    if (!borderMemoryPressure) borderMemoryPressureLogged = 0;
}

void VitaBorders_prepareRoomChange(const char* nextRoomName) {
    if (isBorderBlackFadeRoom(nextRoomName)) {
        if (nextRoomName != NULL && strcmp(nextRoomName, "room_dw_castle_area_1") == 0)
            startImmediateBlackHold();
        else
            startBorderBlackFade();
        borderLog("room_black_prepare");
        return;
    }
    const char* next = roomBorder(nextRoomName);
    char nextPath[512] = {0};
    if (next != NULL) snprintf(nextPath, sizeof(nextPath), "%s", next);
    if (VitaBorderRules_isMenuRoom(nextRoomName)) {
        if (borderTexture != 0 || previousBorderTexture != 0 || borderBlackHold) {
            startBorderBlackFade();
            borderLog("menu_prepare_black_fade");
        } else {
            borderMenuSuppressed = 1;
            borderSceneSuppressed = 0;
            clearBorderVisualsForMenu();
            borderLog("menu_prepare_suppressed");
        }
        return;
    }
    // Enter Garden intro/video/Floradinn with genuinely black host margins.
    // Floradinn is always prepared blank; updateRoom enables its Garden border
    // once obj_sakurafubuki_new reports transition_stage >= 1.
    if (VitaBorderRules_shouldBlankSurround(borderChapter, nextRoomName, 0)) {
        borderSceneSuppressed = 1;
        if (borderTexture != 0 || previousBorderTexture != 0 ||
            borderNativeLayerTexture != 0 || borderNativeRedLayerTexture != 0)
            clearBorderVisualsForMenu();
        else {
            borderPath[0] = '\0';
            borderLoadAttempted = 0;
            borderTransitionStart = 0;
        }
        borderLog("room_blank_prepare");
        return;
    }
    borderSceneSuppressed = 0;
    // Neighbouring town/castle rooms usually use the same border. Keeping its
    // ~510 KiB BC3 texture avoids delete/recreate fragmentation and removes an
    // unnecessary card read/upload from every doorway.
    if (borderTexture != 0 && strcmp(nextPath, borderPath) == 0) {
        if (previousBorderTexture != 0) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glFinish();
            glDeleteTextures(1, &previousBorderTexture);
            glFinish();
            previousBorderTexture = 0;
            previousBorderWidth = previousBorderHeight = 0;
        }
        borderTransitionStart = 0;
        borderLog("room_change_retained_same_border");
        return;
    }
    if (borderTexture == 0 && previousBorderTexture == 0) return;
    // Suppressing draw calls is not enough: both crossfade textures remain in
    // VitaGL and can overlap the incoming room's allocation peak. Retire the
    // last submitted frame and release them before Room End/room construction.
    glBindTexture(GL_TEXTURE_2D, 0);
    glFinish();
    if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
    if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
    glFinish();
    borderTexture = previousBorderTexture = 0;
    borderWidth = borderHeight = 0;
    previousBorderWidth = previousBorderHeight = 0;
    borderTransitionStart = 0;
    borderLoadAttempted = 0;
    borderLog("room_change_released");
}

void VitaBorders_noteTransitionObject(const char* objectName) {
    if (objectName != NULL && strcmp(objectName, "obj_dw_transition") == 0 &&
        !borderBlackHold && (borderTexture != 0 || previousBorderTexture != 0)) {
        startBorderBlackFade();
        borderLog("transition_black_fade");
    }
}

void VitaBorders_prepareLauncherReturn(void) {
    startBorderBlackFade();
    // The launcher switch stops the regular render loop. Present the fade
    // here so returning to the title cannot cut directly from border art to
    // black. This bounded transition runs only on the explicit return path.
    for (int frame = 0; frame < 18; ++frame) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, 960, 544);
        VitaBorders_draw(960, 544);
        vglSwapBuffers(GL_FALSE);
        sceKernelDelayThread(16667);
    }
    borderMenuSuppressed = 1;
    clearBorderVisualsForMenu();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, 960, 544);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    vglSwapBuffers(GL_FALSE);
    glFinish();
    borderLog("launcher_return_black_presented");
}

int VitaBorders_needsMemoryTrim(void) {
    return g_vitaConsoleBordersEnabled && !borderDrawSuppressed && !borderMenuSuppressed && !borderMemoryPressure &&
           borderPath[0] != '\0' && borderTexture == 0 && !borderLoadAttempted;
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
    
    char newBuffer[8192] = {0};
    char* line = strstr(buffer, search);
    if (line) {
        int prefixLen = line - buffer;
        strncpy(newBuffer, buffer, prefixLen);
        char* end = strpbrk(line, "\r\n");
        if (!end) end = buffer + bytes;
        snprintf(newBuffer + prefixLen, sizeof(newBuffer) - prefixLen, "%s=%s", room, borderFilename);
        strncat(newBuffer, end, sizeof(newBuffer) - strlen(newBuffer) - 1);
    } else {
        snprintf(newBuffer, sizeof(newBuffer), "%.4095s\n%.255s=%.255s\n", buffer, room, borderFilename);
    }
    
    fd = sceIoOpen(BORDER_ROOT "borders_config.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, newBuffer, strlen(newBuffer));
        sceIoClose(fd);
        loadBordersConfig();
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
            size_t nlen = strlen(dir_stat.d_name);
            if (nlen >= 128) nlen = 127;
            memcpy(files[count], dir_stat.d_name, nlen);
            files[count][nlen] = '\0';
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
    if (borderPath[0] != '\0') {
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
    
    snprintf(customBorderPath, sizeof(customBorderPath), BORDER_ROOT "%.127s", files[currentIndex]);
    
    if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
    previousBorderTexture = borderTexture;
    previousBorderWidth = borderWidth;
    previousBorderHeight = borderHeight;
    borderTexture = 0;
    borderWidth = borderHeight = 0;
    borderLoadAttempted = 0;
    snprintf(borderPath, sizeof(borderPath), "%s", customBorderPath);
    borderTransitionStart = sceKernelGetProcessTimeWide();
    borderLog("room_selected_cycle");
    
    updateBordersConfig(borderRoom, files[currentIndex]);
}

static void drawBorderTextureTinted(GLuint texture, int windowW, int windowH, float alpha, int opaque,
                                    float red, float green, float blue) {
    if (texture == 0 || alpha <= 0.0f) return;
    const BorderVertex updated[4] = {
        {0, 0, red, green, blue, alpha, -1,  1},
        {1, 0, red, green, blue, alpha,  1,  1},
        {1, 1, red, green, blue, alpha,  1, -1},
        {0, 1, red, green, blue, alpha, -1, -1},
    };
    memcpy(borderVertices, updated, sizeof(borderVertices));
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
#ifdef __vita__
    glBindVertexArray(0);
#endif
    if (g_vitaModernGlActive && ensureModernBorderProgram()) {
        GLint previousProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glUseProgram(borderModernProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        if (borderModernTexture >= 0) glUniform1i(borderModernTexture, 0);
        glBindBuffer(GL_ARRAY_BUFFER, 0); // pointers below are client addresses, not VBO offsets
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BorderVertex), &borderVertices[0].x);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BorderVertex), &borderVertices[0].u);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BorderVertex), &borderVertices[0].r);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(0);
        glUseProgram((GLuint)previousProgram);
        glEnable(GL_BLEND);
        return;
    }
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(BorderVertex), &borderVertices[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(BorderVertex), &borderVertices[0].r);
    glVertexPointer(2, GL_FLOAT, sizeof(BorderVertex), &borderVertices[0].x);
    glUseProgram(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_BLEND);
}

static void drawBorderTexture(GLuint texture, int windowW, int windowH, float alpha, int opaque) {
    drawBorderTextureTinted(texture, windowW, windowH, alpha, opaque, 1.0f, 1.0f, 1.0f);
}

static int ensureBorderOverlayTexture(void) {
    if (borderOverlayTexture != 0) return 1;
    static const unsigned char whitePixel[4] = {255, 255, 255, 255};
    glGenTextures(1, &borderOverlayTexture);
    if (borderOverlayTexture == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, borderOverlayTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    return glGetError() == GL_NO_ERROR;
}

static void drawBorderOverlayColor(int windowW, int windowH, float alpha, unsigned int gmlColor) {
    if (alpha <= 0.0f || !ensureBorderOverlayTexture()) return;
    // GameMaker stores colours as 0xBBGGRR.
    float red = (float)(gmlColor & 0xFFU) / 255.0f;
    float green = (float)((gmlColor >> 8) & 0xFFU) / 255.0f;
    float blue = (float)((gmlColor >> 16) & 0xFFU) / 255.0f;
    drawBorderTextureTinted(borderOverlayTexture, windowW, windowH, alpha, 0, red, green, blue);
}

static void drawBorderOverlay(int windowW, int windowH) {
    if (borderOverlayAlpha <= 0.0f) return;
    float red = (float)(borderOverlayColor & 0xFFU) / 255.0f;
    float green = (float)((borderOverlayColor >> 8) & 0xFFU) / 255.0f;
    float blue = (float)((borderOverlayColor >> 16) & 0xFFU) / 255.0f;
    drawBorderTextureTinted(borderOverlayTexture, windowW, windowH,
                            borderOverlayAlpha, 0, red, green, blue);
}
// Reports whether VitaBorders_draw will paint a full-screen (960x544) border
// image this frame that fully covers the host framebuffer, including behind the
// centered game blit. When it does, the letterbox composite can skip its
// opening full-screen black glClear: with FPS Unlocked (vsync off) an unsynced
// scanout that samples a partially-composited buffer then falls back to the
// PREVIOUS complete frame instead of a black flash, turning "black bars" into
// ordinary image-vs-image tearing. When no border covers the screen
// (disabled / suppressed / not yet loaded) the caller must still clear to black
// so the pillarbox margins stay correct.
int VitaBorders_coversScreen(void) {
    return g_vitaConsoleBordersEnabled && !borderDrawSuppressed && !borderMenuSuppressed && !borderSceneSuppressed &&
           (borderTexture != 0 || previousBorderTexture != 0 ||
            (borderBlackHold && borderOverlayAlpha >= 1.0f));
}

void VitaBorders_draw(int windowW, int windowH) {
    if (borderDrawSuppressed || borderMenuSuppressed || borderSceneSuppressed) return;
    if (!g_vitaConsoleBordersEnabled) {
        if (bordersWereEnabled && (borderTexture != 0 || previousBorderTexture != 0 || borderOverlayTexture != 0)) {
            glBindTexture(GL_TEXTURE_2D, 0);
            glFinish();
            if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
            if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
            if (borderOverlayTexture != 0) glDeleteTextures(1, &borderOverlayTexture);
            glFinish();
            borderTexture = previousBorderTexture = 0;
            borderOverlayTexture = 0;
            borderOverlayAlpha = borderOverlayTarget = 0.0f;
            borderWidth = borderHeight = 0;
            previousBorderWidth = previousBorderHeight = 0;
            borderLoadAttempted = 0;
            borderTransitionStart = 0;
            borderLog("disabled_released");
        }
        bordersWereEnabled = 0;
        return;
    }
    bordersWereEnabled = 1;
    if (borderOverlayAlpha < borderOverlayTarget) {
        borderOverlayAlpha += borderOverlaySpeed;
        if (borderOverlayAlpha > borderOverlayTarget) borderOverlayAlpha = borderOverlayTarget;
    } else if (borderOverlayAlpha > borderOverlayTarget) {
        borderOverlayAlpha -= borderOverlaySpeed;
        if (borderOverlayAlpha < borderOverlayTarget) borderOverlayAlpha = borderOverlayTarget;
    }
    if (borderBlackHold && borderOverlayAlpha >= 1.0f && !borderBlackHoldCompleteLogged) {
        borderBlackHoldCompleteLogged = 1;
        borderLog("black_hold_complete");
    }
    updateNativeLayerTextures();
    if (borderPath[0] != '\0' && borderTexture == 0 && !borderLoadAttempted) loadBorderTexture();

    float progress = 1.0f;
    float eased = 1.0f;
    const int nativeAuthoritative = borderSelectedSource == VITA_BORDER_SOURCE_CONTROLLER;
    if (nativeAuthoritative) {
        progress = borderNativeCrossfade ? borderNativeCrossfadeAlpha : 1.0f;
        eased = progress;
    } else {
        if (borderTransitionStart != 0) {
            uint64_t elapsed = sceKernelGetProcessTimeWide() - borderTransitionStart;
            progress = elapsed >= BORDER_TRANSITION_US ? 1.0f : (float)elapsed / (float)BORDER_TRANSITION_US;
        }
        eased = progress * progress * (3.0f - 2.0f * progress);
    }
    if (previousBorderTexture != 0 && borderTexture != 0) {
        drawBorderTexture(borderTexture, windowW, windowH, 1.0f, 1);
        drawBorderTexture(previousBorderTexture, windowW, windowH, 1.0f - eased, 0);
    } else if (previousBorderTexture != 0) {
        // A native pending border can be deferred under memory pressure. Keep
        // the current border fully visible until the pending texture exists.
        float previousAlpha = (nativeAuthoritative && borderNativeCrossfade) ? 1.0f : (1.0f - eased);
        drawBorderTexture(previousBorderTexture, windowW, windowH, previousAlpha, 0);
    } else if (borderTexture != 0) {
        drawBorderTexture(borderTexture, windowW, windowH, eased, eased >= 1.0f);
    }

    if (borderNativeLayerTexture != 0 && borderNativeLayerAlpha > 0.0f)
        drawBorderTexture(borderNativeLayerTexture, windowW, windowH, borderNativeLayerAlpha, 0);
    if (borderNativeRedLayerTexture != 0 && borderNativeRedLayerAlpha > 0.0f)
        drawBorderTexture(borderNativeRedLayerTexture, windowW, windowH, borderNativeRedLayerAlpha, 0);
    if (borderNativeCustomEffectAlpha > 0.0f)
        drawBorderOverlayColor(windowW, windowH, borderNativeCustomEffectAlpha, borderNativeCustomEffectColor);
    drawBorderOverlay(windowW, windowH);

    // Once black is fully opaque, the previous surround can no longer
    // contribute to the picture. Retire it now and keep only the 1x1 overlay
    // texture resident until a later room explicitly selects a new border.
    if (borderBlackHold && borderOverlayAlpha >= 1.0f &&
        (borderTexture != 0 || previousBorderTexture != 0 ||
         borderNativeLayerTexture != 0 || borderNativeRedLayerTexture != 0)) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glFinish();
        if (borderTexture != 0) glDeleteTextures(1, &borderTexture);
        if (previousBorderTexture != 0) glDeleteTextures(1, &previousBorderTexture);
        if (borderNativeLayerTexture != 0) glDeleteTextures(1, &borderNativeLayerTexture);
        if (borderNativeRedLayerTexture != 0) glDeleteTextures(1, &borderNativeRedLayerTexture);
        glFinish();
        borderTexture = previousBorderTexture = 0;
        borderNativeLayerTexture = borderNativeRedLayerTexture = 0;
        borderWidth = borderHeight = 0;
        previousBorderWidth = previousBorderHeight = 0;
        borderPath[0] = '\0';
        borderNativeLayerLoadedPath[0] = '\0';
        borderLoadAttempted = 0;
        borderTransitionStart = 0;
        borderLog("black_hold_textures_released");
    }

    if (progress >= 1.0f && previousBorderTexture != 0) {
        // The old border was submitted earlier in this same frame. Retire that
        // work before handing its storage to vitaGL's garbage collector.
        glBindTexture(GL_TEXTURE_2D, 0);
        glFinish();
        glDeleteTextures(1, &previousBorderTexture);
        glFinish();
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
    if (borderOverlayTexture != 0) glDeleteTextures(1, &borderOverlayTexture);
    if (borderNativeLayerTexture != 0) glDeleteTextures(1, &borderNativeLayerTexture);
    if (borderNativeRedLayerTexture != 0) glDeleteTextures(1, &borderNativeRedLayerTexture);
    borderTexture = 0;
    previousBorderTexture = 0;
    borderOverlayTexture = 0;
    borderNativeLayerTexture = 0;
    borderNativeRedLayerTexture = 0;
    if (borderModernProgram != 0) glDeleteProgram(borderModernProgram);
    borderModernProgram = 0;
    borderModernTexture = -1;
}







