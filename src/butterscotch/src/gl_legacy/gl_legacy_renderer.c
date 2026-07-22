#include "gl_legacy_renderer.h"
#include "image_decoder.h"

#ifdef PLATFORM_VITA
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdio.h>
#define VITA_TX_CACHE_ROOT "ux0:data/deltarune/deltarunevita/texture-cache"

typedef struct {
    uint32_t magic;
    uint32_t sourceSize;
    uint32_t sourceOffset;
    uint32_t width;
    uint32_t height;
} VitaTextureCacheHeader;

#define VITA_TX_CACHE_MAGIC 0x31435456U /* VTC1 */
#define VITA_TX_CACHE_COMPLETE_MAGIC 0x32435456U /* VTC2 */

static void vitaTextureLog(uint32_t pageId, int w, int h, GLint maxTextureSize, const char* phase);

static int vitaTextureChapter(const DataWin* dw) {
    const char* path = dw != nullptr ? dw->lazyLoadFilePath : nullptr;
    if (path == nullptr) return 0;
    const char* marker = strstr(path, "chapter");
    if (marker == nullptr || marker[7] < '0' || marker[7] > '5') return 0;
    return marker[7] - '0';
}

static const char* vitaTextureCacheVariant(const DataWin* dw) {
    const char* path = dw != nullptr ? dw->lazyLoadFilePath : nullptr;
    return path != nullptr && strstr(path, "mods/PTBR") != nullptr ? "-ptbr" : "";
}

static void vitaTextureCachePath(const DataWin* dw, uint32_t pageId, char* output, size_t outputSize) {
    snprintf(output, outputSize, VITA_TX_CACHE_ROOT "/chapter%d%s/page_%03u.r444",
             vitaTextureChapter(dw), vitaTextureCacheVariant(dw), pageId);
}

static void vitaTextureCacheDir(const DataWin* dw, char* output, size_t outputSize) {
    snprintf(output, outputSize, VITA_TX_CACHE_ROOT "/chapter%d%s",
             vitaTextureChapter(dw), vitaTextureCacheVariant(dw));
}

static bool vitaTextureCacheIsComplete(const DataWin* dw) {
    char path[256];
    char dir[192];
    vitaTextureCacheDir(dw, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/complete.vtc", dir);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    uint32_t marker[2] = {0, 0};
    int got = sceIoRead(fd, marker, sizeof(marker));
    sceIoClose(fd);
    return got == sizeof(marker) && marker[0] == VITA_TX_CACHE_COMPLETE_MAGIC &&
           marker[1] == dw->txtr.count;
}

static void vitaMarkTextureCacheComplete(const DataWin* dw) {
    char path[256];
    char dir[192];
    vitaTextureCacheDir(dw, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/complete.vtc", dir);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    uint32_t marker[2] = {VITA_TX_CACHE_COMPLETE_MAGIC, dw->txtr.count};
    sceIoWrite(fd, marker, sizeof(marker));
    sceIoClose(fd);
}

static uint16_t* vitaLoadPreparedTexture(const DataWin* dw, const Texture* txtr,
                                         uint32_t pageId, int* width, int* height) {
    char path[256];
    vitaTextureCachePath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return nullptr;
    VitaTextureCacheHeader header;
    int got = sceIoRead(fd, &header, sizeof(header));
    if (got != sizeof(header) || header.magic != VITA_TX_CACHE_MAGIC ||
        header.sourceSize != txtr->blobSize || header.sourceOffset != txtr->blobOffset ||
        header.width == 0 || header.height == 0 || header.width > 4096 || header.height > 4096) {
        sceIoClose(fd);
        return nullptr;
    }
    size_t bytes = (size_t)header.width * (size_t)header.height * sizeof(uint16_t);
    uint16_t* packed = (uint16_t*)malloc(bytes);
    if (packed == nullptr || sceIoRead(fd, packed, (unsigned int)bytes) != (int)bytes) {
        free(packed);
        sceIoClose(fd);
        return nullptr;
    }
    sceIoClose(fd);
    *width = (int)header.width;
    *height = (int)header.height;
    return packed;
}

static void vitaSavePreparedTexture(const DataWin* dw, const Texture* txtr, uint32_t pageId,
                                    int width, int height, const uint16_t* packed) {
    sceIoMkdir(VITA_TX_CACHE_ROOT, 0777);
    char chapterDir[192];
    vitaTextureCacheDir(dw, chapterDir, sizeof(chapterDir));
    sceIoMkdir(chapterDir, 0777);
    char path[256];
    vitaTextureCachePath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    VitaTextureCacheHeader header = {
        VITA_TX_CACHE_MAGIC, txtr->blobSize, txtr->blobOffset, (uint32_t)width, (uint32_t)height
    };
    size_t bytes = (size_t)width * (size_t)height * sizeof(uint16_t);
    sceIoWrite(fd, &header, sizeof(header));
    sceIoWrite(fd, packed, (unsigned int)bytes);
    sceIoClose(fd);
}

uint32_t GLLegacyRenderer_prepareTextureCache(DataWin* dw,
                                              VitaTexturePrepareProgress progress,
                                              void* user) {
    if (dw == nullptr || dw->txtr.count == 0) return 0;
    if (vitaTextureCacheIsComplete(dw)) return dw->txtr.count;
    uint32_t prepared = 0;
    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    for (uint32_t pageId = 0; pageId < dw->txtr.count; ++pageId) {
        Texture* txtr = &dw->txtr.textures[pageId];
        int w = 0, h = 0;
        uint16_t* cached = vitaLoadPreparedTexture(dw, txtr, pageId, &w, &h);
        if (cached != nullptr) {
            free(cached);
            prepared++;
            vitaTextureLog(pageId, w, h, 4096, "preload_cache_hit");
        } else {
            vitaTextureLog(pageId, 0, 0, 4096, "preload_decode_begin");
            DataWin_loadTxtrIfNeeded(dw, pageId);
            uint8_t* pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t)txtr->blobSize,
                                                        gm2022_5, &w, &h);
            if (pixels != nullptr && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                uint64_t pixelCount = (uint64_t)w * (uint64_t)h;
                uint16_t* packed = (uint16_t*)pixels;
                for (uint64_t i = 0; i < pixelCount; ++i) {
                    const uint8_t* src = &pixels[i * 4ULL];
                    uint16_t alpha4 = src[3] == 0 ? 0 : (uint16_t)((src[3] + 15U) >> 4);
                    if (alpha4 > 15U) alpha4 = 15U;
                    packed[i] = (uint16_t)(((uint16_t)(src[0] >> 4) << 12) |
                                           ((uint16_t)(src[1] >> 4) << 8) |
                                           ((uint16_t)(src[2] >> 4) << 4) | alpha4);
                }
                vitaSavePreparedTexture(dw, txtr, pageId, w, h, packed);
                prepared++;
                vitaTextureLog(pageId, w, h, 4096, "preload_cache_written");
            } else {
                vitaTextureLog(pageId, w, h, 4096, "preload_decode_skipped");
            }
            free(pixels);
            if (!txtr->mapped) {
                free(txtr->blobData);
                txtr->blobData = nullptr;
            }
        }
        if (progress != nullptr) progress(pageId + 1, dw->txtr.count, user);
    }
    if (prepared == dw->txtr.count) vitaMarkTextureCacheComplete(dw);
    return prepared;
}

static void vitaTextureLog(uint32_t pageId, int w, int h, GLint maxTextureSize, const char* phase) {
    char line[160];
    int length = snprintf(line, sizeof(line), "TXTR page=%u size=%dx%d max=%d phase=%s\n",
                          pageId, w, h, (int) maxTextureSize, phase);
    SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, (SceSize) length);
        sceIoClose(fd);
    }
}

static void vitaRenderLog(const char* text) {
    SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, (SceSize) strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

typedef struct Vita2DVertex {
    GLfloat u, v;
    GLfloat r, g, b, a;
    GLfloat x, y;
} Vita2DVertex;

// VitaGL's immediate-mode compatibility layer generates a large fixed-function
// shader at the first glEnd(). Use explicit client arrays on Vita instead. This
// is the primitive used by the Vita renderer migration; desktop/PS3 keep the
// original immediate-mode path below.
static void vitaDrawQuad(const Vita2DVertex vertices[4]) {
    static bool firstDraw = true;
    if (firstDraw) vitaRenderLog("VITA_ARRAY=first_draw_begin");
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].r);
    glVertexPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].x);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    if (firstDraw) {
        vitaRenderLog("VITA_ARRAY=first_draw_complete");
        firstDraw = false;
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

#define VITA_IMMEDIATE_MAX_VERTICES 262144
static Vita2DVertex vitaImmediateVertices[VITA_IMMEDIATE_MAX_VERTICES];
static GLsizei vitaImmediateCount;
static GLenum vitaImmediateMode;
static GLfloat vitaImmediateU, vitaImmediateV;
static GLfloat vitaImmediateR = 1.0f, vitaImmediateG = 1.0f;
static GLfloat vitaImmediateB = 1.0f, vitaImmediateA = 1.0f;

static void vitaBegin(GLenum mode) {
    vitaImmediateMode = mode;
    vitaImmediateCount = 0;
}

static void vitaColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    vitaImmediateR = r; vitaImmediateG = g; vitaImmediateB = b; vitaImmediateA = a;
}

static void vitaColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    vitaColor4f((GLfloat) r / 255.0f, (GLfloat) g / 255.0f,
                (GLfloat) b / 255.0f, (GLfloat) a / 255.0f);
}

static void vitaTexCoord2f(GLfloat u, GLfloat v) {
    vitaImmediateU = u; vitaImmediateV = v;
}

static void vitaVertex2f(GLfloat x, GLfloat y) {
    if (vitaImmediateCount >= VITA_IMMEDIATE_MAX_VERTICES) return;
    vitaImmediateVertices[vitaImmediateCount++] = (Vita2DVertex) {
        vitaImmediateU, vitaImmediateV,
        vitaImmediateR, vitaImmediateG, vitaImmediateB, vitaImmediateA,
        x, y
    };
}

static void vitaDrawVertexRange(GLenum mode, const Vita2DVertex* vertices, GLsizei count) {
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].r);
    glVertexPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].x);
    glDrawArrays(mode, 0, count);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void vitaEnd(void) {
    static bool firstConvertedBatch = true;
    if (firstConvertedBatch) vitaRenderLog("VITA_IMMEDIATE_BRIDGE=first_batch_begin");
    
    if (vitaImmediateMode == GL_QUADS) {
        static GLushort quadIndices[65536 * 6 / 4];
        static bool indicesInited = false;
        if (!indicesInited) {
            for (int i = 0, v = 0; i < (65536 * 6 / 4); i += 6, v += 4) {
                quadIndices[i+0] = v + 0;
                quadIndices[i+1] = v + 1;
                quadIndices[i+2] = v + 2;
                quadIndices[i+3] = v + 0;
                quadIndices[i+4] = v + 2;
                quadIndices[i+5] = v + 3;
            }
            indicesInited = true;
        }

        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        
        // Draw in chunks of 65536 vertices (16-bit index limit)
        for (GLsizei offset = 0; offset + 3 < vitaImmediateCount; offset += 65536) {
            GLsizei count = vitaImmediateCount - offset;
            if (count > 65536) count = 65536;
            GLsizei quads = count / 4;
            
            glTexCoordPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vitaImmediateVertices[offset].u);
            glColorPointer(4, GL_FLOAT, sizeof(Vita2DVertex), &vitaImmediateVertices[offset].r);
            glVertexPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vitaImmediateVertices[offset].x);
            
            glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_SHORT, quadIndices);
        }
        
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    } else if (vitaImmediateMode == GL_TRIANGLES) {
        vitaDrawVertexRange(GL_TRIANGLES, vitaImmediateVertices, vitaImmediateCount);
    }
    
    if (firstConvertedBatch) {
        vitaRenderLog("VITA_IMMEDIATE_BRIDGE=first_batch_complete");
        firstConvertedBatch = false;
    }
    vitaImmediateCount = 0;
}

// Convert every remaining immediate-mode call in this translation unit. This
// covers text, tiled sprites, primitives and surface blits while the dedicated
// Vita renderer is progressively split out of the legacy backend.
#define glBegin(mode) vitaBegin(mode)
#define glEnd() vitaEnd()
#define glColor4f(r, g, b, a) vitaColor4f((r), (g), (b), (a))
#define glColor4ub(r, g, b, a) vitaColor4ub((r), (g), (b), (a))
#define glTexCoord2f(u, v) vitaTexCoord2f((u), (v))
#define glVertex2f(x, y) vitaVertex2f((x), (y))
#endif
#include "matrix_math.h"
#include "text_utils.h"


#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#include "ps3_textures.h"
extern GLuint gPalettedProgram;
extern GLint  gPalettedUPaletteVLoc;
// Activate the paletted shader for a sprite draw. The caller has already bound the index texture (via glBindTexture on TEXUNIT0).
// Sets unit 1 to the CLUT atlas and pushes uPaletteV for the TPAG's row.
#define PS3_PALETTED_BEGIN(tpagIndex) do {                                                  \
    float _v = PS3Textures_getTpagPaletteV(tpagIndex);                                      \
    if (0.0f > _v) break;                                                                   \
    glActiveTexture(GL_TEXTURE1);                                                           \
    glBindTexture(GL_TEXTURE_2D, PS3Textures_getClutTexture());                             \
    glEnable(GL_TEXTURE_2D);                                                                \
    glActiveTexture(GL_TEXTURE0);                                                           \
    glUseProgram(gPalettedProgram);                                                        \
    if (gPalettedUPaletteVLoc >= 0) glUniform1f(gPalettedUPaletteVLoc, _v);               \
} while (0)
#define PS3_PALETTED_END() do {                                                             \
    glUseProgram(0);                                                                        \
    glActiveTexture(GL_TEXTURE1);                                                           \
    glDisable(GL_TEXTURE_2D);                                                               \
    glActiveTexture(GL_TEXTURE0);                                                           \
} while (0)
#elif defined(PLATFORM_VITA)
#include <vitaGL.h>
#define PS3_PALETTED_BEGIN(tpagIndex) ((void)0)
#define PS3_PALETTED_END()            ((void)0)
#else
#include <glad/glad.h>
#define PS3_PALETTED_BEGIN(tpagIndex) ((void)0)
#define PS3_PALETTED_END()            ((void)0)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "math_compat.h"

// Next power-of-two, used for FBO texture dimensions on older GPUs (Intel 82865G etc.)
// that cannot attach NPOT textures to framebuffer objects.
static inline int32_t nextPow2(int32_t v) {
    int32_t r = 1;
    while (r < v) r <<= 1;
    return r;
}

// Checks whether an OpenGL extension is available. Uses the modern
// (glGetStringi + GL_NUM_EXTENSIONS) path when glGetStringi is non-null
// (GL 3.0+), otherwise falls back to the legacy glGetString(GL_EXTENSIONS)
// approach so the code works with any GL loader (glad, PS3, etc.).
#if !defined(PLATFORM_PS3) && !defined(PLATFORM_VITA)
static bool hasGLExtension(const char* name) {
    if (glGetStringi) {
        GLint numExts = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
        for (GLint i = 0; i < numExts; i++) {
            const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (ext && strcmp(ext, name) == 0)
                return true;
        }
        return false;
    }
    const char* extStr = (const char*)glGetString(GL_EXTENSIONS);
    if (!extStr) return false;
    size_t len = strlen(name);
    for (const char* p = extStr; (p = strstr(p, name)) != NULL; p++) {
        if ((p == extStr || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0'))
            return true;
    }
    return false;
}
#endif

#ifdef PLATFORM_VITA
// VitaGL supports NPOT textures and the framebuffer functionality used by this renderer.
static bool hasGLExtension(const char* name) {
    (void)name;
    return true;
}
#endif

#include "stb_image.h"
#include "stb_ds.h"
#include "utils.h"
#include "image_decoder.h"
#include "gl_common.h"

// ===[ Runtime OpenGL extension checks ]===

static bool hasFBO() {
#if defined(PLATFORM_PS3) || defined(PLATFORM_VITA)
    return true;
#else
    return (glGenFramebuffers || (glGenFramebuffersEXT && glBlitFramebufferEXT));
#endif
}

#include "gl_wrappers.h"

// ===[ Helpers ]===

static void glApplyViewport(GLLegacyRenderer* gl, int32_t x, int32_t y, int32_t w, int32_t h) {
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);

    gl->base.CPortX = x;
    gl->base.CPortY = y;
    gl->base.CPortW = w;
    gl->base.CPortH = h;
}

// camera_apply: swap the active world->clip projection on the current target without touching its viewport.
static void glApplyProjection(Renderer* renderer, const Matrix4f* viewMatrix, const Matrix4f* projectionMatrix) {

    Matrix4f world = renderer->gmlMatrices[MATRIX_WORLD];
    Matrix4f view = *viewMatrix;
    Matrix4f projection = *projectionMatrix;

    Matrix4f worldView;
    Matrix4f_multiply(&worldView, &view, &world);

    Matrix4f worldViewProjection;
    Matrix4f_multiply(&worldViewProjection, &projection, &worldView);
  
    renderer->gmlMatrices[MATRIX_VIEW] = view;   
    renderer->gmlMatrices[MATRIX_PROJECTION] = projection;
    renderer->gmlMatrices[MATRIX_WORLD_VIEW] = worldView;   
    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = worldViewProjection;

    Matrix4f_flipClipY(&projection);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(worldView.m);
}

// ===[ Vtable Implementations ]===

static void glInit(Renderer* renderer, DataWin* dataWin) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    renderer->dataWin = dataWin;

    Matrix4f world;
    Matrix4f_identity(&world);
    renderer->gmlMatrices[MATRIX_WORLD] = world;

    if (!hasFBO()) {
        fprintf(stderr, "GL: The legacy-gl renderer requires FBO support!\n");
        abort();
    }

    // GL 2.0+ has NPOT textures as core; older GL (1.x) may or may not have
    // GL_ARB_texture_non_power_of_two. Only round up to power-of-two on GPUs
    // that actually need it (Intel 82865G etc.).
    {
#ifdef PLATFORM_PS3
        gl->needsPOT = false;
#else
        GLVer ver = GLCommon_getGLVersion();
        gl->needsPOT = (ver.major < 2) && !hasGLExtension("GL_ARB_texture_non_power_of_two");
#endif
    }

    // Prepare texture slots for lazy loading (PNG decode deferred to first use)
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

#ifdef PLATFORM_PS3
    // TXTR is empty on PS3; page count comes from TEXTURES.BIN.
    gl->textureCount = PS3Textures_getPageCount();
#else
    gl->textureCount = dataWin->txtr.count;
#endif
    gl->glTextures = (GLuint *)safeMalloc(gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeMalloc(gl->textureCount * sizeof(bool));
    gl->textureLastUsedFrame = (uint32_t *)safeCalloc(gl->textureCount, sizeof(uint32_t));
    gl->texturePinned = (bool *)safeCalloc(gl->textureCount, sizeof(bool));
    gl->textureFrame = 1;
    gl->residentTextureBytes = 0;

    glGenTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->textureCount > i; i++) {
        gl->textureWidths[i] = 0;
        gl->textureHeights[i] = 0;
        gl->textureLoaded[i] = false;
    }

    // Create 1x1 white pixel texture for primitive drawing (rectangles, lines, etc.)
    glGenTextures(1, &gl->whiteTexture);
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Save original counts so we know which slots are from data.win vs dynamic
    gl->originalTexturePageCount = gl->textureCount;
    gl->originalTpagCount = dataWin->tpag.count;
    gl->originalSpriteCount = dataWin->sprt.count;
#ifdef PLATFORM_VITA
    // Keep the primary DELTARUNE font/UI atlas resident. Chapter 5 uses a full
    // 2048x2048 page for it, so size-based UI protection did not recognize it.
    bool pinnedMainFont = false;
    for (uint32_t i = 0; i < dataWin->font.count; ++i) {
        Font* font = &dataWin->font.fonts[i];
        if (!font->present || font->tpagIndex < 0 ||
            (uint32_t)font->tpagIndex >= dataWin->tpag.count) continue;
        if (font->name != nullptr && strstr(font->name, "fnt_main") != nullptr) {
            int page = dataWin->tpag.items[font->tpagIndex].texturePageId;
            if (page >= 0 && (uint32_t)page < gl->textureCount) {
                gl->texturePinned[page] = true;
                pinnedMainFont = true;
            }
        }
    }
    if (!pinnedMainFont && dataWin->font.count > 0) {
        Font* font = &dataWin->font.fonts[0];
        if (font->tpagIndex >= 0 && (uint32_t)font->tpagIndex < dataWin->tpag.count) {
            int page = dataWin->tpag.items[font->tpagIndex].texturePageId;
            if (page >= 0 && (uint32_t)page < gl->textureCount) gl->texturePinned[page] = true;
        }
    }
#endif

    // application_surface is allocated lazily by glLegacyEnsureApplicationSurface as a normal entry in the surface table.
    gl->surfaces = nullptr;
    gl->surfaceTexture = nullptr;
    gl->surfaceWidth = nullptr;
    gl->surfaceHeight = nullptr;
    gl->surfaceCount = 0;

    fprintf(stderr, "GL: Renderer initialized (%u texture pages)\n", gl->textureCount);
}

static void glDestroy(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glDeleteTextures(1, &gl->whiteTexture);

    glDeleteTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->surfaceCount > i; i++) {
        if (gl->surfaceTexture[i] != 0) glDeleteTextures(1, &gl->surfaceTexture[i]);
        if (gl->surfaces[i] != 0) glDeleteFramebuffers(1, &gl->surfaces[i]);
    }
    free(gl->surfaces);
    free(gl->surfaceTexture);
    free(gl->surfaceWidth);
    free(gl->surfaceHeight);

    free(gl->glTextures);
    free(gl->textureWidths);
    free(gl->textureHeights);
    free(gl->textureLoaded);
    free(gl->textureLastUsedFrame);
    free(gl->texturePinned);
#ifdef PLATFORM_VITA
    for (int i = 0; i < 4; ++i) free(gl->cpuTextureCachePixels[i]);
#endif
    free(gl);
}

static void glBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    gl->textureFrame++;
    if (gl->textureFrame == 0) gl->textureFrame = 1;

    gl->windowW = windowW;
    gl->windowH = windowH;
    gl->gameW = gameW;
    gl->gameH = gameH;

    // Bind the application_surface (sized/created by Runner_beginFrame's ensureApplicationSurface call right before this).
    int32_t appId = gl->base.runner->applicationSurfaceId;
    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[appId]);
    glViewport(0, 0, gameW, gameH);
    gl->base.CPortX = 0;
    gl->base.CPortY = 0;
    gl->base.CPortW = gameW;
    gl->base.CPortH = gameH;
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void glBeginView(Renderer* renderer, MAYBE_UNUSED int32_t viewX, MAYBE_UNUSED int32_t viewY, MAYBE_UNUSED int32_t viewW, MAYBE_UNUSED int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, MAYBE_UNUSED float viewAngle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glBindTexture(GL_TEXTURE_2D, 0);

    // Set viewport and scissor to the port rectangle within the FBO
    // FBO uses game resolution, port coordinates are in game space
    // OpenGL viewport Y is bottom-up, game Y is top-down
    glApplyViewport(gl, portX, portY, portW, portH);

    int32_t viewCurrent = 0;
    if (renderer->runner->viewsEnabled) {
    viewCurrent = renderer->runner->viewCurrent;
    }
    RuntimeView* view = &renderer->runner->views[viewCurrent];
    gl->base.cameraCurrent = view->cameraId;
    GMLCamera* camera = Runner_getCameraById(renderer->runner, gl->base.cameraCurrent);
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);

    glActiveTexture(GL_TEXTURE0);

}

static void glEndView(MAYBE_UNUSED Renderer* renderer) {
    glDisable(GL_SCISSOR_TEST);
}

static void glBeginGUI(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glBindTexture(GL_TEXTURE_2D, 0);

    if (targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
#ifdef PLATFORM_VITA
        extern int g_vitaPortOverlayFullScreen;
        if (!g_vitaPortOverlayFullScreen) {
            int32_t sx, sy, ex, ey;
            GLCommon_computeLetterbox(gl->gameW, gl->gameH, gl->windowW, gl->windowH, &sx, &sy, &ex, &ey);
            glViewport(sx, gl->windowH - ey, ex - sx, ey - sy);
            glEnable(GL_SCISSOR_TEST);
            glScissor(sx, gl->windowH - ey, ex - sx, ey - sy);
        } else {
            glViewport(0, 0, portW, portH);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, portW, portH);
        }
#else
        glViewport(0, 0, portW, portH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, portW, portH);
#endif
    } else {
        require(targetSurfaceId >= 0 && (uint32_t) targetSurfaceId < gl->surfaceCount);
        require(gl->surfaces[targetSurfaceId] != 0);
        glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[targetSurfaceId]);
        glApplyViewport(gl, portX, portY, portW, portH);
    }

    //I dunno hopefully this is at least somewhat correct...
    gl->base.cameraCurrent = GUI_CAMERA;
    GMLCamera* camera = &renderer->runner->guiCamera;
    camera->allocated = true;
    camera->viewX = 0.0;
    camera->viewY = 0.0;
    camera->viewWidth = guiW;
    camera->viewHeight = guiH;
    camera->borderX = 0;
    camera->borderY = 0;
    camera->speedX = 0;
    camera->speedY = 0;
    camera->objectId = -1;
    camera->viewAngle = 0;

    Matrix4f projectionMatrix;
    Matrix4f_Orthographic(&projectionMatrix, (float) guiW, (float) guiH, 32000.0, 0.0);

    Matrix4f viewMatrix;
    float x = (float) guiW * 0.5f;
    float y = (float) guiH * 0.5f;
    Matrix4f_identity(&viewMatrix);
    Matrix4f_LookAt(&viewMatrix, x, y, -16000.0, x, y, 16000.0, 0.0, 1.0, 0.0);
    camera->viewMatrix = viewMatrix;
    camera->projectionMatrix = projectionMatrix;
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);

    glActiveTexture(GL_TEXTURE0);
}

static void glSetGuiProjection(MAYBE_UNUSED Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, bool renderingToUserSurface) {
    Matrix4f projection;
    Matrix4f_guiProjection(&projection, (float) guiW, (float) guiH, (float) portW, (float) portH);
    // GL surfaces are stored bottom-up and draw_surface samples them with vertical flip.

    renderer->cameraCurrent = GUI_CAMERA;
    GMLCamera* camera = &renderer->runner->guiCamera;
    camera->allocated = true;
    camera->viewX = 0.0;
    camera->viewY = 0.0;
    camera->viewWidth = guiW;
    camera->viewHeight = guiH;
    camera->borderX = 0;
    camera->borderY = 0;
    camera->speedX = 0;
    camera->speedY = 0;
    camera->objectId = -1;
    camera->viewAngle = 0;

    //yeah no I have no idea how to do the GUI
    Matrix4f projectionMatrix;
    Matrix4f_Orthographic(&projectionMatrix, (float) guiW, (float) guiH, 32000.0, 0.0);
    // Flip the projection when we are rendering to a user surface so it comes back upright.
    if (renderingToUserSurface) Matrix4f_flipClipY(&projectionMatrix);
    Matrix4f viewMatrix;
    float x = (float) guiW * 0.5f;
    float y = (float) guiH * 0.5f;
    Matrix4f_identity(&viewMatrix);
    Matrix4f_LookAt(&viewMatrix, x, y, -16000.0, x, y, 16000.0, 0.0, 1.0, 0.0);
    camera->viewMatrix = viewMatrix;
    camera->projectionMatrix = projectionMatrix;
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);
}

static void glEndGUI(MAYBE_UNUSED Renderer* renderer) {
    glDisable(GL_SCISSOR_TEST);
}

static void glEndFrameInit(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (renderer->runner->usingAppSurface && !renderer->runner->appSurfaceAutoDraw) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    int32_t appId = gl->base.runner->applicationSurfaceId;
    GLCommon_beginLetterboxBlit(gl->surfaces[appId], 0);
}

static void glEndFrameEnd(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (renderer->runner->usingAppSurface && !renderer->runner->appSurfaceAutoDraw) {
        return;
    }
    int32_t appId = gl->base.runner->applicationSurfaceId;
    GLCommon_beginLetterboxBlit(gl->surfaces[appId], 0);
    GLCommon_endLetterboxBlit(gl->surfaceWidth[appId], gl->surfaceHeight[appId], gl->gameW, gl->gameH, gl->windowW, gl->windowH, 0);
}

static void glRendererFlush(MAYBE_UNUSED Renderer* renderer) {}

static void glClearScreen(MAYBE_UNUSED Renderer* renderer, uint32_t color, float alpha) {
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // GML draw_clear ignores the active scissor and clears the whole target. Disable scissor for the clear and restore it after.

    glClearColor(r, g, b, alpha);
    glClear(GL_COLOR_BUFFER_BIT);

}

// Lazily decodes and uploads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to decode.
#ifdef PLATFORM_VITA
// Chapter 2's castle scene actively alternates five 2048x2048 pages (80 MiB)
// plus smaller pages. Keep that working set, but leave roughly 16 MiB of the
// 112 MiB graphics pool for framebuffers, VitaGL bookkeeping and console
// borders. A 104 MiB texture limit left under 8 MiB free and Chapter 5 crashed
// while replacing a 2048x2048 atlas at about 101 MiB resident.
static uint64_t vitaTextureCacheLimit(void) {
    extern int g_vitaActiveChapter;
    // Chapter 2 needs one extra 2048x2048 RGBA4444 page resident to avoid
    // continuously swapping its battle/cutscene atlases. Chapter 5 keeps the
    // safer ceiling because its larger scenery working set previously
    // exhausted the Vita graphics pool at 104 MiB.
    return (g_vitaActiveChapter == 5 ? 112ULL : 128ULL) * 1024ULL * 1024ULL;
}

static uint16_t* vitaCpuTextureCacheGet(GLLegacyRenderer* gl, uint32_t pageId, int* w, int* h) {
    for (int i = 0; i < 4; ++i) {
        if (gl->cpuTextureCachePixels[i] != nullptr && gl->cpuTextureCachePage[i] == pageId) {
            gl->vitaTextureRamHits++;
            gl->cpuTextureCacheStamp[i] = ++gl->cpuTextureCacheClock;
            *w = gl->cpuTextureCacheWidth[i];
            *h = gl->cpuTextureCacheHeight[i];
            return gl->cpuTextureCachePixels[i];
        }
    }
    return nullptr;
}

static uint16_t* vitaCpuTextureCacheStore(GLLegacyRenderer* gl, uint32_t pageId,
                                          uint16_t* pixels, int w, int h) {
    int slot = -1;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < 4; ++i) {
        if (gl->cpuTextureCachePixels[i] == nullptr) { slot = i; break; }
        if (gl->cpuTextureCacheStamp[i] < oldest) {
            oldest = gl->cpuTextureCacheStamp[i];
            slot = i;
        }
    }
    if (slot < 0) return pixels;
    free(gl->cpuTextureCachePixels[slot]);
    gl->cpuTextureCachePixels[slot] = pixels;
    gl->cpuTextureCachePage[slot] = pageId;
    gl->cpuTextureCacheWidth[slot] = w;
    gl->cpuTextureCacheHeight[slot] = h;
    gl->cpuTextureCacheStamp[slot] = ++gl->cpuTextureCacheClock;
    return pixels;
}

static bool vitaCpuTextureCacheOwns(const GLLegacyRenderer* gl, const uint16_t* pixels) {
    if (pixels == nullptr) return false;
    for (int i = 0; i < 4; ++i)
        if (gl->cpuTextureCachePixels[i] == pixels) return true;
    return false;
}

static void vitaReleasePreparedPixels(GLLegacyRenderer* gl, uint16_t* pixels) {
    if (!vitaCpuTextureCacheOwns(gl, pixels)) free(pixels);
}

static bool vitaTextureNeedsFullColor(GLLegacyRenderer* gl, uint32_t pageId) {
    // Non-zero alpha rounding keeps fonts and spr_darkconfigbt visible in
    // RGBA4444, avoiding a 16 MiB full-color atlas in Chapter 2.
    (void)gl;
    (void)pageId;
    return false;
}

static bool vitaTexturePageHasFont(GLLegacyRenderer* gl, uint32_t pageId) {
    DataWin* dw = gl->base.dataWin;
    for (uint32_t i = 0; i < dw->font.count; ++i) {
        Font* font = &dw->font.fonts[i];
        if (!font->present || font->tpagIndex < 0 || (uint32_t)font->tpagIndex >= dw->tpag.count) continue;
        if ((uint32_t)dw->tpag.items[font->tpagIndex].texturePageId == pageId) return true;
    }
    return false;
}

static void vitaGpuAtlasSize(GLLegacyRenderer* gl, uint32_t pageId, int w, int h, int* gpuW, int* gpuH) {
    // A 2048x2048 atlas contains far more source pixels than the Vita can show
    // on its 960x544 display. Keep logical dimensions unchanged for UV math,
    // but upload a half-size nearest-neighbour copy to CDRAM.
    extern int g_vitaGraphicsQuality;
    extern int g_vitaActiveChapter;
    bool largeAtlas = w == 2048 && h == 2048;
    // Chapter 5 Town references more full-size scenery atlases in one frame
    // than the Vita graphics pool can hold. Even in Original mode, keep UI and
    // font pages intact but use the Medium scenery size for this chapter. This
    // avoids permanent deferred-upload loops and GPU-memory crashes.
    bool chapter5SafetyScale = g_vitaActiveChapter == 5 && largeAtlas &&
                               !gl->texturePinned[pageId] &&
                               !vitaTexturePageHasFont(gl, pageId);
    bool preserveOriginal = !largeAtlas || gl->texturePinned[pageId] ||
                            vitaTexturePageHasFont(gl, pageId) ||
                            (g_vitaGraphicsQuality == 0 && !chapter5SafetyScale);
    int target = g_vitaGraphicsQuality == 2 ? 1024 : 1280;
    *gpuW = preserveOriginal ? w : target;
    *gpuH = preserveOriginal ? h : target;
}

static uint64_t vitaGpuTextureBytes(GLLegacyRenderer* gl, uint32_t pageId, int w, int h) {
    int gpuW, gpuH;
    vitaGpuAtlasSize(gl, pageId, w, h, &gpuW, &gpuH);
    return (uint64_t)gpuW * (uint64_t)gpuH *
           (vitaTextureNeedsFullColor(gl, pageId) ? 4ULL : 2ULL);
}

static bool vitaEvictTexturePage(GLLegacyRenderer* gl, uint32_t protectedPage) {
    uint32_t victim = UINT32_MAX;
    uint32_t oldestFrame = UINT32_MAX;
    for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
        if (i == protectedPage || gl->texturePinned[i] || !gl->textureLoaded[i] || gl->textureWidths[i] <= 0 || gl->textureHeights[i] <= 0) continue;
        // Small atlases contain fonts, dialogue frames and menu sprites. They
        // occupy at most 512 KiB in RGBA4444 but Chapter 5's 2048x2048 scenery
        // previously evicted page 7 repeatedly, leaving dialogue/menu elements
        // incomplete after a room transition. Keep UI-sized pages resident and
        // reclaim one of the large scenery atlases instead.
        if (gl->textureWidths[i] <= 512 && gl->textureHeights[i] <= 512) continue;
        if (gl->textureLastUsedFrame[i] == gl->textureFrame) continue;
        if (gl->textureLastUsedFrame[i] < oldestFrame) {
            oldestFrame = gl->textureLastUsedFrame[i];
            victim = i;
        }
    }
    if (victim == UINT32_MAX) return false;

    uint64_t bytes = vitaGpuTextureBytes(gl, victim, gl->textureWidths[victim], gl->textureHeights[victim]);
    gl->vitaTextureEvictions++;
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &gl->glTextures[victim]);
    glGenTextures(1, &gl->glTextures[victim]);
    gl->textureLoaded[victim] = false;
    gl->textureWidths[victim] = 0;
    gl->textureHeights[victim] = 0;
    gl->textureLastUsedFrame[victim] = 0;
    gl->residentTextureBytes = gl->residentTextureBytes > bytes ? gl->residentTextureBytes - bytes : 0;
    return true;
}
#endif

bool GLLegacyRenderer_ensureTextureLoaded(GLLegacyRenderer* gl, uint32_t pageId) {
    if (gl->textureLoaded[pageId]) {
        if (pageId < gl->originalTexturePageCount) gl->textureLastUsedFrame[pageId] = gl->textureFrame;
        return (gl->textureWidths[pageId] != 0);
    }
#ifdef PLATFORM_VITA
    // Once the protected working set fills the cache, do not decode every
    // remaining missing atlas only to discover the same condition again.
    if (gl->textureCacheBlockedFrame == gl->textureFrame) return false;
#endif

    gl->textureLoaded[pageId] = true;

    int w, h;
#ifdef PLATFORM_PS3
    // We'll load the textures on demand.
    uint8_t* pixels;
    if (!PS3Textures_loadPage(pageId, &w, &h, &pixels)) {
        fprintf(stderr, "GL: PS3 page %u has no pixels\n", pageId);
        return false;
    }
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    // Nearest is mandatory for index textures, bilinear would interpolate palette indices into nonsense colors.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    free(pixels);
#else
    DataWin* dw = gl->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];
    uint8_t* pixels = nullptr;
#ifdef PLATFORM_VITA
    uint16_t* preparedPixels = vitaCpuTextureCacheGet(gl, pageId, &w, &h);
    if (preparedPixels == nullptr) {
        preparedPixels = vitaLoadPreparedTexture(dw, txtr, pageId, &w, &h);
        if (preparedPixels != nullptr) {
            vitaCpuTextureCacheStore(gl, pageId, preparedPixels, w, h);
        }
    }
#endif
#ifdef PLATFORM_VITA
    if (preparedPixels == nullptr)
#endif
    {
        DataWin_loadTxtrIfNeeded(dw, pageId);
        bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
        pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
        if (pixels == nullptr) {
            fprintf(stderr, "GL: Failed to decode TXTR page %u\n", pageId);
            return false;
        }
        if (!txtr->mapped) {
            free(txtr->blobData);
            txtr->blobData = nullptr;
        }
    }

    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

#ifdef PLATFORM_VITA
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (w <= 0 || h <= 0 || w > maxTextureSize || h > maxTextureSize) {
        vitaTextureLog(pageId, w, h, maxTextureSize, "rejected_invalid_size");
        free(pixels);
        vitaReleasePreparedPixels(gl, preparedPixels);
        gl->textureWidths[pageId] = 0;
        gl->textureHeights[pageId] = 0;
        return false;
    }
    // RGBA4444 cuts atlas residency and transfer size in half. DELTARUNE's
    // pixel-art presentation tolerates the 4-bit channels well, while keeping
    // the Chapter 2/5 working sets resident instead of decoding the same PNGs
    // every few frames.
    uint64_t pixelCount = (uint64_t)w * (uint64_t)h;
    bool fullColor = vitaTextureNeedsFullColor(gl, pageId);
    int gpuW, gpuH;
    vitaGpuAtlasSize(gl, pageId, w, h, &gpuW, &gpuH);
    uint64_t uploadBytes = (uint64_t)gpuW * (uint64_t)gpuH * (fullColor ? 4ULL : 2ULL);
    uint64_t cacheLimit = vitaTextureCacheLimit();
    while (gl->residentTextureBytes + uploadBytes > cacheLimit) {
        if (!vitaEvictTexturePage(gl, pageId)) break;
    }
    if (gl->residentTextureBytes + uploadBytes > cacheLimit) {
        // Every possible victim was already used by this frame. The old path
        // uploaded anyway, temporarily exceeding the 112 MiB graphics pool;
        // Chapter 5 could enqueue many such pages in one room change and crash.
        // Defer this atlas until the next frame, when an older page can be
        // reclaimed safely. This also prevents a 20-30 second unbroken upload
        // burst while leaving currently referenced textures valid.
        gl->vitaTextureDeferred++;
        free(pixels);
        vitaReleasePreparedPixels(gl, preparedPixels);
        gl->textureCacheBlockedFrame = gl->textureFrame;
        gl->textureLoaded[pageId] = false;
        gl->textureWidths[pageId] = 0;
        gl->textureHeights[pageId] = 0;
        return false;
    }
    uint16_t* packedPixels = preparedPixels;
    uint16_t* gpuPackedPixels = nullptr;
    if (!fullColor) {
        // Pack into the first half of the decoded RGBA allocation. Reading four
        // bytes and writing two bytes advances the destination more slowly than
        // the source, so this is safe in-place and avoids an extra 8 MiB staging
        // allocation for every 2048x2048 atlas. The old 24 MiB peak per upload
        // fragmented memory during Chapter 5's large room changes.
        if (packedPixels == nullptr) {
            packedPixels = (uint16_t*)pixels;
            for (uint64_t i = 0; i < pixelCount; ++i) {
                const uint8_t* src = &pixels[i * 4ULL];
                uint16_t alpha4 = src[3] == 0 ? 0 : (uint16_t)((src[3] + 15U) >> 4);
                if (alpha4 > 15U) alpha4 = 15U;
                packedPixels[i] = (uint16_t)(((uint16_t)(src[0] >> 4) << 12) |
                                             ((uint16_t)(src[1] >> 4) << 8) |
                                             ((uint16_t)(src[2] >> 4) << 4) |
                                             alpha4);
            }
            vitaSavePreparedTexture(dw, txtr, pageId, w, h, packedPixels);
            vitaTextureLog(pageId, w, h, maxTextureSize, "prepared_cache_written");
        }
        if (gpuW != w || gpuH != h) {
            gpuPackedPixels = (uint16_t*)safeMalloc((size_t)uploadBytes);
            for (int y = 0; y < gpuH; ++y) {
                int srcY = (int)((int64_t)y * h / gpuH);
                const uint16_t* srcRow = packedPixels + (size_t)srcY * (size_t)w;
                uint16_t* dstRow = gpuPackedPixels + (size_t)y * (size_t)gpuW;
                for (int x = 0; x < gpuW; ++x) {
                    int srcX = (int)((int64_t)x * w / gpuW);
                    dstRow[x] = srcRow[srcX];
                }
            }
        } else {
            gpuPackedPixels = packedPixels;
        }
    } else {
        vitaReleasePreparedPixels(gl, preparedPixels);
        preparedPixels = nullptr;
    }
#endif
    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
#ifdef PLATFORM_VITA
    while (glGetError() != GL_NO_ERROR) {}
#endif
#ifdef PLATFORM_VITA
    if (fullColor) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);
        pixels = nullptr;
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gpuW, gpuH, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, gpuPackedPixels);
        if (gpuPackedPixels != packedPixels) free(gpuPackedPixels);
        if (pixels != nullptr) free(pixels);
        else vitaReleasePreparedPixels(gl, packedPixels);
        pixels = nullptr;
        packedPixels = nullptr;
        gpuPackedPixels = nullptr;
    }
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#endif

#ifdef PLATFORM_VITA
    GLenum uploadError = glGetError();
    if (uploadError != GL_NO_ERROR) {
        vitaTextureLog(pageId, w, h, maxTextureSize, "upload_failed_gpu_memory");
        free(pixels);
        glDeleteTextures(1, &gl->glTextures[pageId]);
        glGenTextures(1, &gl->glTextures[pageId]);
        gl->textureLoaded[pageId] = false;
        gl->textureWidths[pageId] = 0;
        gl->textureHeights[pageId] = 0;
        return false;
    }
    gl->residentTextureBytes += uploadBytes;
    gl->textureLastUsedFrame[pageId] = gl->textureFrame;
#endif

    free(pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
#endif
    fprintf(stderr, "GL: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
    return true;
}

static void glDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);

    // Compute normalized UVs from TPAG source rect
    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Use targetWidth/Height (draw size in bounding rect), not sourceWidth/Height (texture sample size).
    // They differ when the texture was auto-downscaled by GMS to fit a texture page.
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->targetWidth;
    float localY1 = localY0 + (float) tpag->targetHeight;

    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

#ifdef PLATFORM_VITA
    const Vita2DVertex vertices[4] = {
        {u0, v0, r, g, b, alpha, x0, y0},
        {u1, v0, r, g, b, alpha, x1, y1},
        {u1, v1, r, g, b, alpha, x2, y2},
        {u0, v1, r, g, b, alpha, x3, y3},
    };
    vitaDrawQuad(vertices);
#else
    glBegin(GL_QUADS);
        // Vertex 0: top-left
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v0);
        glVertex2f(x0, y0);

        // Vertex 1: top-right
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v0);
        glVertex2f(x1, y1);

        // Vertex 2: bottom-right
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v1);
        glVertex2f(x2, y2);

        // Vertex 3: bottom-left
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v1);
        glVertex2f(x3, y3);
    glEnd();
#endif
    PS3_PALETTED_END();
}

static void glDrawSpriteTiled(Renderer* renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) tpag->boundingWidth * axScale;
    float tileH = (float) tpag->boundingHeight * ayScale;
    if (0 >= tileW || 0 >= tileH) return;

    float startX, endX, startY, endY;
    if (tileX) {
        startX = fmodf(x - originX * axScale, tileW);
        if (startX > 0) startX -= tileW;
        endX = roomW;
    } else {
        startX = x - originX * axScale;
        endX = startX + tileW;
    }
    if (tileY) {
        startY = fmodf(y - originY * ayScale, tileH);
        if (startY > 0) startY -= tileH;
        endY = roomH;
    } else {
        startY = y - originY * ayScale;
        endY = startY + tileH;
    }

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Use targetWidth/Height (draw size in bounding rect), not sourceWidth/Height (texture sample size).
    // They differ when the texture was auto-downscaled by GMS to fit a texture page.
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->targetWidth;
    float localY1 = localY0 + (float) tpag->targetHeight;
    float sx0 = xscale * localX0;
    float sy0 = yscale * localY0;
    float sx1 = xscale * localX1;
    float sy1 = yscale * localY1;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Emit the entire tile grid in a single glBegin -> glEnd
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);
    glBegin(GL_QUADS);
    glColor4f(r, g, b, alpha);
    for (float dy = startY; endY > dy; dy += tileH) {
        float cy = dy + originY * ayScale;
        float vy0 = cy + sy0;
        float vy1 = cy + sy1;
        for (float dx = startX; endX > dx; dx += tileW) {
            float cx = dx + originX * axScale;
            float vx0 = cx + sx0;
            float vx1 = cx + sx1;

            glTexCoord2f(u0, v0); glVertex2f(vx0, vy0);
            glTexCoord2f(u1, v0); glVertex2f(vx1, vy0);
            glTexCoord2f(u1, v1); glVertex2f(vx1, vy1);
            glTexCoord2f(u0, v1); glVertex2f(vx0, vy1);
        }
    }
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawSpritePos(Renderer* renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u0, v0);
        glVertex2f(x1, y1);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u1, v0);
        glVertex2f(x2, y2);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u1, v1);
        glVertex2f(x3, y3);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u0, v1);
        glVertex2f(x4, y4);
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    glBindTexture(GL_TEXTURE_2D, texId);

    // Compute UVs for the sub-region within the atlas
    float u0 = (float) (tpag->sourceX + srcOffX) / (float) texW;
    float v0 = (float) (tpag->sourceY + srcOffY) / (float) texH;
    float u1 = (float) (tpag->sourceX + srcOffX + srcW) / (float) texW;
    float v1 = (float) (tpag->sourceY + srcOffY + srcH) / (float) texH;

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Quad corners (no origin offset - draw_sprite_part ignores sprite origin)
    float cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3;
    if (angleDeg == 0.0f) {
        cx0 = x;                         cy0 = y;
        cx1 = x + (float) srcW * xscale; cy1 = y;
        cx2 = x + (float) srcW * xscale; cy2 = y + (float) srcH * yscale;
        cx3 = x;                         cy3 = y + (float) srcH * yscale;
    } else {
        float angleRad = -angleDeg * ((float) M_PI / 180.0f);
        float cosA = cosf(angleRad);
        float sinA = sinf(angleRad);
        float qx0 = x,                         qy0 = y;
        float qx1 = x + (float) srcW * xscale, qy1 = y;
        float qx2 = x + (float) srcW * xscale, qy2 = y + (float) srcH * yscale;
        float qx3 = x,                         qy3 = y + (float) srcH * yscale;
        float dx, dy;
        dx = qx0 - pivotX; dy = qy0 - pivotY; cx0 = cosA * dx - sinA * dy + pivotX; cy0 = sinA * dx + cosA * dy + pivotY;
        dx = qx1 - pivotX; dy = qy1 - pivotY; cx1 = cosA * dx - sinA * dy + pivotX; cy1 = sinA * dx + cosA * dy + pivotY;
        dx = qx2 - pivotX; dy = qy2 - pivotY; cx2 = cosA * dx - sinA * dy + pivotX; cy2 = sinA * dx + cosA * dy + pivotY;
        dx = qx3 - pivotX; dy = qy3 - pivotY; cx3 = cosA * dx - sinA * dy + pivotX; cy3 = sinA * dx + cosA * dy + pivotY;
    }

    PS3_PALETTED_BEGIN(tpagIndex);
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v0); glVertex2f(cx0, cy0);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v0); glVertex2f(cx1, cy1);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v1); glVertex2f(cx2, cy2);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v1); glVertex2f(cx3, cy3);
    glEnd();
    PS3_PALETTED_END();
}

// Emits a single colored quad into the batch using the white pixel texture
static void emitColoredQuad(GLLegacyRenderer* gl, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

#ifdef PLATFORM_VITA
    const Vita2DVertex vertices[4] = {
        {0.5f, 0.5f, r, g, b, a, x0, y0},
        {0.5f, 0.5f, r, g, b, a, x1, y0},
        {0.5f, 0.5f, r, g, b, a, x1, y1},
        {0.5f, 0.5f, r, g, b, a, x0, y1},
    };
    vitaDrawQuad(vertices);
#else
    glBegin(GL_QUADS);
        // All UVs point to (0.5, 0.5) center of the 1x1 white texture
        // Vertex 0: top-left
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x0, y0);

        // Vertex 1: top-right
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1, y0);

        // Vertex 2: bottom-right
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1, y1);

        // Vertex 3: bottom-left
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x0, y1);
    glEnd();
#endif
}

static void glDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        emitColoredQuad(gl, x1, y1, x2 + 1, y1 + 1, r, g, b, alpha); // top
        emitColoredQuad(gl, x1, y2, x2 + 1, y2 + 1, r, g, b, alpha); // bottom
        emitColoredQuad(gl, x1, y1 + 1, x1 + 1, y2, r, g, b, alpha); // left
        emitColoredQuad(gl, x2, y1 + 1, x2 + 1, y2, r, g, b, alpha); // right
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects
        emitColoredQuad(gl, x1, y1, x2 + 1, y2 + 1, r, g, b, alpha);
    }
}

static void glDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha);
static void glDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha, bool outline) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    float r3 = (float) BGR_R(color3) / 255.0f;
    float g3 = (float) BGR_G(color3) / 255.0f;
    float b3 = (float) BGR_B(color3) / 255.0f;

    float r4 = (float) BGR_R(color4) / 255.0f;
    float g4 = (float) BGR_G(color4) / 255.0f;
    float b4 = (float) BGR_B(color4) / 255.0f;

    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        glDrawLineColor(renderer, x1, y1, x2, y1, 1.0, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y1, x2, y2, 1.0, color2, color3, alpha);
        glDrawLineColor(renderer, x2, y2, x1, y2, 1.0, color3, color4, alpha);
        glDrawLineColor(renderer, x1, y2, x1, y1, 1.0, color4, color1, alpha);
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects

        // All UVs point to (0.5, 0.5) center of the 1x1 white texture
        glBegin(GL_QUADS);
            // Vertex 0: top-left
            glColor4f(r1, g1, b1, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1, y1); 

            // Vertex 1: top-right
            glColor4f(r2, g2, b2, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2+1, y1);

            // Vertex 2: bottom-right
            glColor4f(r3, g3, b3, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2+1, y2+1);

            // Vertex 3: bottom-left
            glColor4f(r4, g4, b4, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1, y2+1); 

        glEnd();
    }
}

// ===[ Line Drawing ]===

static void glDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    // Vertex 0: start + perpendicular
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 + px, y1 + py);

        // Vertex 1: start - perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 - px, y1 - py);

        // Vertex 2: end - perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 - px, y2 - py);

        // Vertex 3: end + perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 + px, y2 + py);
    glEnd();
}

static void glDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    // Emit quad with per-vertex colors (color1 at start, color2 at end)
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    glBegin(GL_QUADS);
        // Vertex 0: start + perpendicular (color1)
        glColor4f(r1, g1, b1, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 + px, y1 + py); 

        // Vertex 1: start - perpendicular (color1)
        glColor4f(r1, g1, b1, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 - px, y1 - py); 

        // Vertex 2: end - perpendicular (color2)
        glColor4f(r2, g2, b2, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 - px, y2 - py); 

        // Vertex 3: end + perpendicular (color2)
        glColor4f(r2, g2, b2, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 + px, y2 + py); 
    glEnd();
}

static void glDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline)
{
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if(outline)
    {
        glDrawLineColor(renderer, x1, y1, x2, y2, 1, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y2, x3, y3, 1, color2, color3, alpha);
        glDrawLineColor(renderer, x3, y3, x1, y1, 1, color3, color1, alpha);
    } else {
        glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

        glBegin(GL_TRIANGLES);
            glColor4f((float) BGR_R(color1) / 255.0f, (float) BGR_G(color1) / 255.0f, (float) BGR_B(color1) / 255.0f, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1 , y1);

            glColor4f((float) BGR_R(color2) / 255.0f, (float) BGR_G(color2) / 255.0f, (float) BGR_B(color2) / 255.0f, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2, y2);

            glColor4f((float) BGR_R(color3) / 255.0f, (float) BGR_G(color3) / 255.0f, (float) BGR_B(color3) / 255.0f, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x3, y3);
        glEnd();
    }
}

// ===[ Text Drawing ]===

// Resolved font state shared between glDrawText and glDrawTextColor
typedef struct {
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (nullptr for sprite fonts)
    int32_t fontTpagIndex;     // TPAG index for regular fonts (-1 for sprite fonts)
    GLuint texId;
    int32_t texW, texH;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (nullptr for regular fonts)
} GlFontState;

// Resolves font texture state
// Returns false if the font can't be drawn
static bool glResolveFontState(GLLegacyRenderer* gl, DataWin* dw, Font* font, GlFontState* state) {
    state->font = font;
    state->fontTpag = nullptr;
    state->fontTpagIndex = -1;
    state->texId = 0;
    state->texW = 0;
    state->texH = 0;
    state->spriteFontSprite = nullptr;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = font->tpagIndex;
        if (0 > fontTpagIndex) return false;

        state->fontTpagIndex = fontTpagIndex;
        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= gl->textureCount) return false;
        if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return false;

        state->texId = gl->glTextures[pageId];
        state->texW = gl->textureWidths[pageId];
        state->texH = gl->textureHeights[pageId];
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    return true;
}

// Resolves UV coordinates, texture ID, and local position for a single glyph
// Returns false if the glyph can't be drawn
static bool glResolveGlyph(GLLegacyRenderer* gl, DataWin* dw, GlFontState* state, FontGlyph* glyph, float cursorX, float cursorY, GLuint* outTexId, int32_t* outTpagIdx, float* outU0, float* outV0, float* outU1, float* outV1, float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIdx = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= gl->textureCount) return false;
        if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pid)) return false;

        *outTexId = gl->glTextures[pid];
        *outTpagIdx = tpagIdx;
        int32_t tw = gl->textureWidths[pid];
        int32_t th = gl->textureHeights[pid];

        *outU0 = (float) glyphTpag->sourceX / (float) tw;
        *outV0 = (float) glyphTpag->sourceY / (float) th;
        *outU1 = (float) (glyphTpag->sourceX + glyphTpag->sourceWidth) / (float) tw;
        *outV1 = (float) (glyphTpag->sourceY + glyphTpag->sourceHeight) / (float) th;

        // Sprite-font glyphs sit at the cell offset. GM 2023.2+ subtracts the sprite origin, pre-2023.2 it cancels.
        // (See GameMaker-HTML5's commit a7c5b909209d5a28602fedfe2031965386a99921)
        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) (int32_t) glyphTpag->targetY - (float) font->spriteOriginYAdjust;
    } else {
        *outTexId = state->texId;
        *outTpagIdx = state->fontTpagIndex;
        *outU0 = (float) (state->fontTpag->sourceX + glyph->sourceX) / (float) state->texW;
        *outV0 = (float) (state->fontTpag->sourceY + glyph->sourceY) / (float) state->texH;
        *outU1 = (float) (state->fontTpag->sourceX + glyph->sourceX + glyph->sourceWidth) / (float) state->texW;
        *outV1 = (float) (state->fontTpag->sourceY + glyph->sourceY + glyph->sourceHeight) / (float) state->texH;

        *outLocalX0 = cursorX + glyph->offset;
        *outLocalY0 = cursorY;
    }
    return true;
}

static void glDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    uint32_t color = renderer->drawColor;
    float alpha = renderer->drawAlpha;
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    int32_t textLen = (int32_t) strlen(text);

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    // Per-line vertical stride. HTML5 runner's default `linesep` is `max_glyph_height * scaleY`.
    // We apply scaleY via the transform matrix below, so keep the stride in pre-scale (local) coords.
    // Caller-supplied separation is in world pre-scale pixels; divide by font->scaleY so the transform restores it.
    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from the per-line y offset
    // (see yyFont.GR_Text_Draw), shifting glyphs up so the baseline aligns with the drawn y.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;
                    int32_t glyphTpagIdx;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &glyphTpagIdx, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        glBindTexture(GL_TEXTURE_2D, glyphTexId);
                        PS3_PALETTED_BEGIN(glyphTpagIdx);

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        glBegin(GL_QUADS);
                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u0, v0);
                            glVertex2f(px0, py0);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u1, v0);
                            glVertex2f(px1, py1);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u1, v1);
                            glVertex2f(px2, py2);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u0, v1);
                            glVertex2f(px3, py3);
                        glEnd();
                        PS3_PALETTED_END();

                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    cursorX += TextUtils_getKerningOffset(glyph, nextCh);
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

static void glDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha, float lineSeparation) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    int32_t textLen = (int32_t) strlen(text);
    if(textLen == 0) return;

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from per-line y offset.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;
        // Pixel-position cursor for the gradient
        float gradientX = 0.0f;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                float advance = (float) glyph->shift;
                float leftFrac  = (lineWidth > 0.0f) ? (gradientX           / lineWidth) : 0.0f;
                float rightFrac = (lineWidth > 0.0f) ? ((gradientX + advance) / lineWidth) : 1.0f;
                int32_t c1 = Color_lerp(_c1, _c2, leftFrac);
                int32_t c2 = Color_lerp(_c1, _c2, rightFrac);
                int32_t c3 = Color_lerp(_c4, _c3, rightFrac);
                int32_t c4 = Color_lerp(_c4, _c3, leftFrac);

                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;
                    int32_t glyphTpagIdx;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &glyphTpagIdx, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        glBindTexture(GL_TEXTURE_2D, glyphTexId);
                        PS3_PALETTED_BEGIN(glyphTpagIdx);

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        glBegin(GL_QUADS);
                            glColor4ub(BGR_R(c1), BGR_G(c1), BGR_B(c1), alpha * 255);
                            glTexCoord2f(u0, v0);
                            glVertex2f(px0, py0);

                            glColor4ub(BGR_R(c2), BGR_G(c2), BGR_B(c2), alpha * 255);
                            glTexCoord2f(u1, v0);
                            glVertex2f(px1, py1);

                            glColor4ub(BGR_R(c3), BGR_G(c3), BGR_B(c3), alpha * 255);
                            glTexCoord2f(u1, v1);
                            glVertex2f(px2, py2);

                            glColor4ub(BGR_R(c4), BGR_G(c4), BGR_B(c4), alpha * 255);
                            glTexCoord2f(u0, v1);
                            glVertex2f(px3, py3);
                        glEnd();
                        PS3_PALETTED_END();

                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                gradientX   += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    float kern = TextUtils_getKerningOffset(glyph, nextCh);
                    cursorX += kern;
                    gradientX   += kern;
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

// ===[ Dynamic Sprite Creation/Deletion ]===

// Finds a free dynamic texture page slot (glTextures[i] == 0), or appends a new one.
static uint32_t findOrAllocTexturePageSlot(GLLegacyRenderer* gl) {
    // Scan dynamic range for a reusable slot
    for (uint32_t i = gl->originalTexturePageCount; gl->textureCount > i; i++) {
        if (gl->glTextures[i] == 0) return i;
    }
    // No free slot found, grow the arrays
    uint32_t newPageId = gl->textureCount;
    gl->textureCount++;
    gl->glTextures = (GLuint *)safeRealloc(gl->glTextures, gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeRealloc(gl->textureWidths, gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeRealloc(gl->textureHeights, gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeRealloc(gl->textureLoaded, gl->textureCount * sizeof(bool));
    gl->glTextures[newPageId] = 0;
    gl->textureWidths[newPageId] = 0;
    gl->textureHeights[newPageId] = 0;
    gl->textureLoaded[newPageId] = false;
    return newPageId;
}

// Finds a free dynamic TPAG slot (texturePageId == -1), or appends a new one.
static uint32_t findOrAllocTpagSlot(DataWin* dw, uint32_t originalTpagCount) {
    for (uint32_t i = originalTpagCount; dw->tpag.count > i; i++) {
        if (dw->tpag.items[i].texturePageId == -1) return i;
    }
    uint32_t newIndex = dw->tpag.count;
    dw->tpag.count++;
    dw->tpag.items = (TexturePageItem *)safeRealloc(dw->tpag.items, dw->tpag.count * sizeof(TexturePageItem));
    memset(&dw->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dw->tpag.items[newIndex].texturePageId = -1;
    return newIndex;
}

static int32_t glCreateSpriteFromSurface(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    // TODO: implement these
    (void)smooth;
    (void)removeback;
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 >= w || 0 >= h) return -1;
    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return -1;
    if (gl->surfaces[surfaceID] == 0) return -1;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, gl->surfaces[surfaceID]);

    uint8_t* pixels = (uint8_t *)safeMalloc((size_t) w * (size_t) h * 4);
    if (pixels == nullptr)
        return -1;

    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Create a new GL texture from the captured pixels
    GLuint newTexId;
    glGenTextures(1, &newTexId);
    glBindTexture(GL_TEXTURE_2D, newTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    free(pixels);

    // Find or allocate slots for texture page, TPAG, and sprite
    uint32_t pageId = findOrAllocTexturePageSlot(gl);
    gl->glTextures[pageId] = newTexId;
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;
    gl->textureLoaded[pageId] = true;

    uint32_t tpagIndex = findOrAllocTpagSlot(dw, gl->originalTpagCount);
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t) w;
    tpag->sourceHeight = (uint16_t) h;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t) w;
    tpag->targetHeight = (uint16_t) h;
    tpag->boundingWidth = (uint16_t) w;
    tpag->boundingHeight = (uint16_t) h;
    tpag->texturePageId = (int16_t) pageId;

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, gl->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    // name was set by DataWin_allocSpriteSlot ("__newsprite<N>"); don't overwrite it here
    sprite->width = (uint32_t) w;
    sprite->height = (uint32_t) h;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = (int32_t *)safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t) tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = nullptr;

    fprintf(stderr, "GL: Created dynamic sprite %u (%dx%d) from surface at (%d,%d)\n", spriteIndex, w, h, x, y);
    return (int32_t) spriteIndex;
}

static void glDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > spriteIndex || dw->sprt.count <= (uint32_t) spriteIndex) return;

    // Refuse to delete original data.win sprites
    if (gl->originalSpriteCount > (uint32_t) spriteIndex) {
        fprintf(stderr, "GL: Cannot delete data.win sprite %d\n", spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return; // already deleted

    // Clean up GL texture and TPAG entries owned by this sprite.
    // Slots with index >= originalTpagCount are dynamically allocated and ours to free.
    repeat(sprite->textureCount, i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t) tpagIdx >= gl->originalTpagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && gl->textureCount > (uint32_t) pageId) {
                glDeleteTextures(1, &gl->glTextures[pageId]);
                gl->glTextures[pageId] = 0;
            }
            // Mark TPAG slot as free for reuse
            tpag->texturePageId = -1;
        }
    }

    // Clear the sprite entry so it won't be drawn and can be reused. Preserve `name` across the memset: the slot is still in sprt.count and must keep a valid string for asset_get_index / name lookups.
    free(sprite->tpagIndices);
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;

    fprintf(stderr, "GL: Deleted sprite %d\n", spriteIndex);
}

static BlendFactors glGpuGetBlendFactors(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*)renderer;
    return (BlendFactors){
        gl->currentSFactor, 
        gl->currentDFactor, 
        gl->currentSFactorAlpha, 
        gl->currentDFactorAlpha
    };
}

static int32_t glGpuGetBlendMode(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    return gl->currentBlendMode;
}

static void glGpuSetBlendMode(Renderer* renderer, int32_t mode) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    
    gl->currentBlendMode = mode;
    gl->currentSFactor = GLCommon_blendModeToSFactor(mode);
    gl->currentDFactor = GLCommon_blendModeToDFactor(mode);
    gl->currentSFactorAlpha = gl->currentSFactor; 
    gl->currentDFactorAlpha = gl->currentDFactor;
    glBlendEquation(GLCommon_blendModeToEquation(mode));
    glBlendFunc(gl->currentSFactor, gl->currentDFactor);
}

static void glGpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    
    gl->currentBlendMode = bm_complex;
    gl->currentSFactor = sfactor;
    gl->currentDFactor = dfactor;
    gl->currentSFactorAlpha = sfactor_alpha;
    gl->currentDFactorAlpha = dfactor_alpha;
    
    glBlendFuncSeparate(
        GLCommon_blendFactorToGL(sfactor), 
        GLCommon_blendFactorToGL(dfactor), 
        GLCommon_blendFactorToGL(sfactor_alpha), 
        GLCommon_blendFactorToGL(dfactor_alpha)
    );
}

static void glGpuSetBlendEnable(Renderer* renderer, bool enable) {
    (void)renderer;
    enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
}

static bool glGpuGetBlendEnable(MAYBE_UNUSED Renderer* renderer) {
    
    return glIsEnabled(GL_BLEND);
}

static void glGpuSetAlphaTestEnable(MAYBE_UNUSED Renderer* renderer, bool enable) {
    enable ? glEnable(GL_ALPHA_TEST) : glDisable(GL_ALPHA_TEST);
}

static void glGpuSetAlphaTestRef(MAYBE_UNUSED Renderer* renderer, uint8_t ref) {
    glAlphaFunc(GL_GREATER, ref/255.0f);
}

static void glGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    gl->colorWriteR = red;
    gl->colorWriteG = green;
    gl->colorWriteB = blue;
    gl->colorWriteA = alpha;
    glColorMask(red, green, blue, alpha);
}

static void glGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    *red = gl->colorWriteR;
    *green = gl->colorWriteG;
    *blue = gl->colorWriteB;
    *alpha = gl->colorWriteA;
}

// ===[ Surfaces ]===

static int32_t glLegacyCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    // Save the current FBO binding so creating a surface doesn't change the active render target.
    GLint prevBinding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBinding);

    uint32_t surfaceIndex = GLCommon_findOrAllocateSurfaceSlot(&gl->surfaces, &gl->surfaceTexture, &gl->surfaceWidth, &gl->surfaceHeight, &gl->surfaceCount);

    int32_t texW = gl->needsPOT ? nextPow2(width)  : width;
    int32_t texH = gl->needsPOT ? nextPow2(height) : height;

    glGenFramebuffers(1, &gl->surfaces[surfaceIndex]);
    glGenTextures(1, &gl->surfaceTexture[surfaceIndex]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((texW & (texW - 1)) == 0) && ((texH & (texH - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceIndex]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex], 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "GL: Surface FBO incomplete (status=0x%X)\n", status);
    }

    gl->surfaceWidth[surfaceIndex] = width;
    gl->surfaceHeight[surfaceIndex] = height;

    fprintf(stderr, "GL: Created surface %u with size (%dx%d)\n", surfaceIndex, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevBinding);
    return (int32_t) surfaceIndex;
}

static int32_t glLegacyEnsureApplicationSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    int32_t id = renderer->runner->applicationSurfaceId;

    bool needsCreate = (id < 0) || ((uint32_t) id >= gl->surfaceCount) || (gl->surfaces[id] == 0);
    if (needsCreate) {
        id = glLegacyCreateSurface(renderer, width, height);
        // Publish immediately so anything that re-queries the runner during this frame sees the new ID.
        renderer->runner->applicationSurfaceId = id;
        return id;
    }

    if (gl->surfaceWidth[id] != width || gl->surfaceHeight[id] != height) {
        renderer->vtable->surfaceResize(renderer, id, width, height);
    }
    return id;
}

static bool glLegacySurfaceExists(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    return gl->surfaces[surfaceId] != 0;
}

static float glLegacyGetSurfaceWidth(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceWidth[surfaceId];
}

static float glLegacyGetSurfaceHeight(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceHeight[surfaceId];
}

static void glLegacySurfaceResize(Renderer* renderer, int32_t surfaceId, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return;
    if (gl->surfaces[surfaceId] == 0) return;
    if (gl->surfaceWidth[surfaceId] == width && gl->surfaceHeight[surfaceId] == height) return;

    GLint prevBinding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBinding);

    if (gl->surfaceTexture[surfaceId] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceId]);

    int32_t texW = gl->needsPOT ? nextPow2(width)  : width;
    int32_t texH = gl->needsPOT ? nextPow2(height) : height;

    glGenTextures(1, &gl->surfaceTexture[surfaceId]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((texW & (texW - 1)) == 0) && ((texH & (texH - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceId], 0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevBinding);

    gl->surfaceWidth[surfaceId] = width;
    gl->surfaceHeight[surfaceId] = height;
    fprintf(stderr, "GL: Resized Surface %u to (%dx%d)\n", surfaceId, width, height);
}

static void glLegacySurfaceFree(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return;
    // Freeing the application_surface is a no-op from GML; the runner manages its lifecycle via application_surface_enable.
    if (surfaceId == renderer->runner->applicationSurfaceId) return;
    if (gl->surfaceTexture[surfaceId] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceId]);
    if (gl->surfaces[surfaceId] != 0) glDeleteFramebuffers(1, &gl->surfaces[surfaceId]);
    gl->surfaces[surfaceId] = 0;
    gl->surfaceTexture[surfaceId] = 0;
    gl->surfaceWidth[surfaceId] = 0;
    gl->surfaceHeight[surfaceId] = 0;
    fprintf(stderr, "GL: Freed Surface %d\n", surfaceId);
}

static bool glLegacySetRenderTarget(Renderer* renderer, int32_t surfaceId, bool implicitApplicationSurface) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    int32_t viewCurrent = 0;
    if (renderer->runner->viewsEnabled) {
    viewCurrent = renderer->runner->viewCurrent;
    }
    RuntimeView* view = &renderer->runner->views[viewCurrent];
    gl->base.cameraCurrent = view->cameraId;
    GMLCamera* camera = Runner_getCameraById(renderer->runner, gl->base.cameraCurrent);

    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);

    if (surfaceId == renderer->runner->applicationSurfaceId && implicitApplicationSurface) {
        glViewport(gl->base.CPortX, gl->base.CPortY, gl->base.CPortW, gl->base.CPortH);
        glEnable(GL_SCISSOR_TEST);
        glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);
        return true;
    }

    if (surfaceId == view->surfaceId) {
    //the surface belongs to the view we are rending, we use the view's camera.
    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);
    return true;
    } else {
    //camera will use full surface.
    gl->base.cameraCurrent = SURFACE_CAMERA;
    GMLCamera* camera =  &renderer->runner->surfaceCamera;

    camera->allocated = true;
    camera->viewX = 0.0;
    camera->viewY = 0.0;
    camera->viewWidth = gl->surfaceWidth[surfaceId];
    camera->viewHeight = gl->surfaceHeight[surfaceId];
    camera->borderX = 0;
    camera->borderY = 0;
    camera->speedX = 0;
    camera->speedY = 0;
    camera->objectId = -1;
    camera->viewAngle = 0;
    Runner_updateCameraViewSimple(camera);

    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);
    glApplyProjection(renderer, &camera->viewMatrix,&camera->projectionMatrix);
    return true;
    }


    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);

    return true;
}

// Resolves a surfaceID to a GL texture and its actual texture size
// (POT dimensions if needsPOT, logical dimensions otherwise).
static bool resolveSurfaceTexture(GLLegacyRenderer* gl, int32_t surfaceId, GLuint* outTexId, int32_t* outTexW, int32_t* outTexH) {
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;
    *outTexId = gl->surfaceTexture[surfaceId];
    if (gl->needsPOT) {
        *outTexW = nextPow2(gl->surfaceWidth[surfaceId]);
        *outTexH = nextPow2(gl->surfaceHeight[surfaceId]);
    } else {
        *outTexW = gl->surfaceWidth[surfaceId];
        *outTexH = gl->surfaceHeight[surfaceId];
    }
    return true;
}

static void glLegacyDrawSurfaceTiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float roomW, MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    // No-op
}

static void glLegacyDrawSurface(Renderer* renderer, int32_t surfaceId, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSurfaceTexture(gl, surfaceId, &texId, &texW, &texH)) return;

    // Use the logical surface size for the default "draw everything" case,
    // not the POT texture dimensions (texW/texH may be rounded up).
    if (0 > srcWidth) {
        srcLeft = 0;
        srcTop = 0;
        srcWidth = gl->surfaceWidth[surfaceId];
        srcHeight = gl->surfaceHeight[surfaceId];
    }

    // top-down GML coords -> flipped V for our bottom-up texture
    float u0 = (float) srcLeft / (float) texW;
    float u1 = (float) (srcLeft + srcWidth) / (float) texW;
#ifndef PLATFORM_PS3
    float v0 = (float) srcTop / (float) texH;
    float v1 = (float) (srcTop + srcHeight) / (float) texH;
#else
    float v1 = (float) srcTop / (float) texH;
    float v0 = (float) (srcTop + srcHeight) / (float) texH;
#endif

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, 0.0f,             0.0f,             &x0, &y0);
    Matrix4f_transformPoint(&transform, (float) srcWidth, 0.0f,             &x1, &y1);
    Matrix4f_transformPoint(&transform, (float) srcWidth, (float) srcHeight, &x2, &y2);
    Matrix4f_transformPoint(&transform, 0.0f,             (float) srcHeight, &x3, &y3);

    glBindTexture(GL_TEXTURE_2D, texId);
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha); glTexCoord2f(u0, v0); glVertex2f(x0, y0);
        glColor4f(r, g, b, alpha); glTexCoord2f(u1, v0); glVertex2f(x1, y1);
        glColor4f(r, g, b, alpha); glTexCoord2f(u1, v1); glVertex2f(x2, y2);
        glColor4f(r, g, b, alpha); glTexCoord2f(u0, v1); glVertex2f(x3, y3);
    glEnd();
}

static void glLegacySurfaceCopy(Renderer* renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLCommon_surfaceBlit(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, destSurfaceID, destX, destY, srcSurfaceID, srcX, srcY, srcW, srcH, part);
}

static bool glLegacySurfaceGetPixels(Renderer* renderer, int32_t surfaceId, uint8_t* outRGBA) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    return GLCommon_surfaceGetPixels(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, surfaceId, outRGBA);
}


// ===[ Vtable ]===

// Decode a texture handle produced by glSpriteGetTexture back into its tpag and page dimensions.
// Returns false for the 0 ("no texture") handle or an unresolvable one.
static bool glLegacyResolveTextureHandle(GLLegacyRenderer* gl, uint32_t texHandle, TexturePageItem** outTpag, int32_t* outW, int32_t* outH) {
    if (texHandle == 0) return false;
    if (texHandle & GL_SURFACE_TEXTURE_FLAG) {
        uint32_t sid = texHandle & ~GL_SURFACE_TEXTURE_FLAG;
        if (sid >= gl->surfaceCount || gl->surfaceTexture[sid] == 0) return false;
        if (outTpag) *outTpag = nullptr;
        *outW = gl->surfaceWidth[sid];
        *outH = gl->surfaceHeight[sid];
        return true;
    }
    DataWin* dw = gl->base.dataWin;
    int32_t tpagIndex = (int32_t) texHandle - 1;
    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return false;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return false;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return false;
    *outTpag = tpag;
    *outW = gl->textureWidths[pageId];
    *outH = gl->textureHeights[pageId];
    return true;
}

static uint32_t glSpriteGetTexture(Renderer* renderer, int32_t tpagIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;
    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return 0;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return 0;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return 0;
    return (uint32_t) (tpagIndex + 1);
}

static uint32_t glSurfaceGetTexture(Renderer* renderer, int32_t surfaceID) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (surfaceID < 0 || (uint32_t) surfaceID >= gl->surfaceCount) return 0;
    if (gl->surfaceTexture[surfaceID] == 0) return 0;
    return GL_SURFACE_TEXTURE_FLAG | (uint32_t) surfaceID;
}

static float glTextureGetTexelWidth(Renderer* renderer, uint32_t texHandle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    TexturePageItem* tpag;
    int32_t w = 0, h = 0;
    if (!glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h) || 0 >= w) return 1.0f;
    return 1.0f / (float) w;
}

static float glTextureGetTexelHeight(Renderer* renderer, uint32_t texHandle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    TexturePageItem* tpag;
    int32_t w = 0, h = 0;
    if (!glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h) || 0 >= h) return 1.0f;
    return 1.0f / (float) h;
}

static bool glTextureGetUVs(Renderer* renderer, uint32_t texHandle, float* outUVs) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    TexturePageItem* tpag;
    int32_t w = 0, h = 0;
    if (!glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h) || 0 >= w || 0 >= h) return false;
    // Surface handles cover the whole texture (no tpag sub-region).
    if (tpag == nullptr) {
        outUVs[0] = 0.0f; outUVs[1] = 0.0f; outUVs[2] = 1.0f; outUVs[3] = 1.0f;
        return true;
    }
    float divW = 1.0f / (float) w;
    float divH = 1.0f / (float) h;
    outUVs[0] = (float) tpag->sourceX * divW;                       // left
    outUVs[1] = (float) tpag->sourceY * divH;                       // top
    outUVs[2] = outUVs[0] + (float) tpag->sourceWidth * divW;       // right
    outUVs[3] = outUVs[1] + (float) tpag->sourceHeight * divH;      // bottom
    return true;
}

static void glTextureSetStage(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texHandle) {
}

static void glGpuSetShader(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex) {}
static void glGpuResetShader(MAYBE_UNUSED Renderer* renderer) {}
static int32_t glShaderGetUniform(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) { return -1; }
static int32_t glShaderGetSamplerIndex(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) { return -1; }
static void glShaderSetUniformF(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED float value1, MAYBE_UNUSED float value2, MAYBE_UNUSED float value3, MAYBE_UNUSED float value4) {}
static void glShaderSetUniformFArray(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED float* values, MAYBE_UNUSED uint32_t count) {}
static void glShaderSetUniformI(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t value1, MAYBE_UNUSED int32_t value2, MAYBE_UNUSED int32_t value3, MAYBE_UNUSED int32_t value4) {}
static bool glShaderIsCompiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shader) { return false; }
static bool glShadersSupported(void) { return false; }

static RendererVtable glVtable;

// ===[ Public API ]===

Renderer* GLLegacyRenderer_create(void) {
    GLLegacyRenderer* gl = (GLLegacyRenderer *)safeCalloc(1, sizeof(GLLegacyRenderer));
    gl->base.vtable = &glVtable;
    glVtable.init = glInit;
    glVtable.destroy = glDestroy;
    glVtable.beginFrame = glBeginFrame;
    glVtable.endFrameInit = glEndFrameInit;
    glVtable.endFrameEnd = glEndFrameEnd;
    glVtable.beginView = glBeginView;
    glVtable.endView = glEndView;
    glVtable.applyProjection = glApplyProjection;
    glVtable.beginGUI = glBeginGUI;
    glVtable.setGuiProjection = glSetGuiProjection;
    glVtable.endGUI = glEndGUI;
    glVtable.drawSprite = glDrawSprite;
    glVtable.drawSpritePos = glDrawSpritePos;
    glVtable.drawSpritePart = glDrawSpritePart;
    glVtable.drawRectangle = glDrawRectangle;
    glVtable.drawRectangleColor = glDrawRectangleColor;
    glVtable.drawLine = glDrawLine;
    glVtable.drawLineColor = glDrawLineColor;
    glVtable.drawTriangle = glDrawTriangle;
    glVtable.drawText = glDrawText;
    glVtable.drawTextColor = glDrawTextColor;
    glVtable.flush = glRendererFlush;
    glVtable.clearScreen = glClearScreen;
    glVtable.createSpriteFromSurface = glCreateSpriteFromSurface;
    glVtable.deleteSprite = glDeleteSprite;
    glVtable.gpuGetBlendFactors = glGpuGetBlendFactors;
    glVtable.gpuGetBlendMode = glGpuGetBlendMode;
    glVtable.gpuSetBlendMode = glGpuSetBlendMode;
    glVtable.gpuSetBlendModeExt = glGpuSetBlendModeExt;
    glVtable.gpuSetBlendEnable = glGpuSetBlendEnable;
    glVtable.gpuSetAlphaTestEnable = glGpuSetAlphaTestEnable;
    glVtable.gpuSetAlphaTestRef = glGpuSetAlphaTestRef;
    glVtable.gpuSetColorWriteEnable = glGpuSetColorWriteEnable;
    glVtable.gpuGetColorWriteEnable = glGpuGetColorWriteEnable;
    glVtable.gpuGetBlendEnable = glGpuGetBlendEnable;
    glVtable.drawTile = nullptr;
    glVtable.drawSpriteTiled = glDrawSpriteTiled;
    glVtable.createSurface = glLegacyCreateSurface;
    glVtable.surfaceExists = glLegacySurfaceExists;
    glVtable.setRenderTarget = glLegacySetRenderTarget;
    glVtable.ensureApplicationSurface = glLegacyEnsureApplicationSurface;
    glVtable.getSurfaceWidth = glLegacyGetSurfaceWidth;
    glVtable.getSurfaceHeight = glLegacyGetSurfaceHeight;
    glVtable.drawSurface = glLegacyDrawSurface;
    glVtable.drawSurfaceTiled = glLegacyDrawSurfaceTiled;
    glVtable.surfaceResize = glLegacySurfaceResize;
    glVtable.surfaceFree = glLegacySurfaceFree;
    glVtable.surfaceCopy = glLegacySurfaceCopy;
    glVtable.surfaceGetPixels = glLegacySurfaceGetPixels;
    glVtable.spriteGetTexture = glSpriteGetTexture;
    glVtable.surfaceGetTexture = glSurfaceGetTexture;
    glVtable.textureGetTexelWidth = glTextureGetTexelWidth;
    glVtable.textureGetTexelHeight = glTextureGetTexelHeight;
    glVtable.textureGetUVs = glTextureGetUVs;
    glVtable.textureSetStage = glTextureSetStage;
    glVtable.gpuSetShader = glGpuSetShader;
    glVtable.gpuResetShader = glGpuResetShader;
    glVtable.shaderGetUniform = glShaderGetUniform;
    glVtable.shaderGetSamplerIndex = glShaderGetSamplerIndex;
    glVtable.shaderSetUniformF = glShaderSetUniformF;
    glVtable.shaderSetUniformFArray = glShaderSetUniformFArray;
    glVtable.shaderSetUniformI = glShaderSetUniformI;
    glVtable.shaderIsCompiled = glShaderIsCompiled;
    glVtable.shadersSupported = glShadersSupported;
    gl->base.drawColor = 0xFFFFFF; // white (BGR)
    gl->base.drawAlpha = 1.0f;
    gl->base.drawFont = -1;
    gl->base.drawHalign = 0;
    gl->base.drawValign = 0;
    gl->base.circlePrecision = 24;
    gl->colorWriteR = true;
    gl->colorWriteG = true;
    gl->colorWriteB = true;
    gl->colorWriteA = true;

    return (Renderer*) gl;
}




