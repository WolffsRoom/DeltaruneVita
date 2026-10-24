#ifndef _BS_GL_RENDERER_H_
#define _BS_GL_RENDERER_H_

#include "common.h"
#include "renderer.h"
#include "runner.h"
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#ifdef __vita__
#include <vitaGL.h>
#define VITA_MODERN_RETIRED_TEXTURE_SLOTS 16
#else
#include <glad/glad.h>
#endif
#endif

typedef enum {
    BATCHTYPE_QUAD,
    BATCHTYPE_TRIANGLE
} BatchType;

// ===[ GLRenderer Struct ]===
typedef struct {
    char* name; // owned
    int32_t location;
    GLenum type;
    uint32_t arraySize;
    uint32_t samplerSlot;
} GLShaderUniform;

typedef struct {
    GLuint shaderId;
    bool compiled;
    bool compileAttempted;
    uint32_t uniformCount;
    GLShaderUniform* uniforms;
} GMLShader;

typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} Vertex;

// Exposed in the header so platform-specific code (main.c) can access FBO fields for screenshots.
typedef struct {
    Renderer base; // Must be first field for struct embedding

    GMLShader* defaultShaderProgram;
    GMLShader* gmlShaders;
    uint32_t gmlShaderCount;

    bool alphaTestEnable;
    float alphaTestRef;
    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;
    bool fogEnable;
    uint32_t fogColor; // BGR

    GLuint vao, vbo, ebo;
    Vertex* vertexData; // MAX_QUADS * VERTICES_PER_QUAD vertices
    int32_t primitiveType;
    int32_t primitiveVertexCount;
    GLuint primitiveTextureId;
    bool primitiveHasTexture;

    BatchType batchType;

    GLuint* glTextures;       // one GL texture per TXTR page
    int32_t* textureWidths;   // needed for UV normalization
    int32_t* textureHeights;
    bool* textureLookupPages; // original pages containing numerical LUT data
    bool* textureFontPages; // original regular/sprite-font atlases, priority on demand
    bool* textureLoaded;      // lazy loading: true once PNG decoded and uploaded
    uint32_t* textureLastUsedFrame;
    bool* texturePinned;
    uint32_t textureFrame;
    uint32_t textureCacheBlockedFrame;
    bool textureCollectionPending;
    uint64_t residentTextureBytes;
    uint64_t* textureGpuBytes;
    bool textureMaintenanceRequested;
    bool textureMaintenanceActive;
    uint32_t textureMaintenanceProtectedPage;
    uint64_t textureMaintenanceBytes;
    uint32_t textureCount;

#ifdef __vita__
    uint16_t* cpuTextureCachePixels[4];
    uint32_t cpuTextureCachePage[4];
    uint32_t cpuTextureCacheStamp[4];
    int32_t cpuTextureCacheWidth[4];
    int32_t cpuTextureCacheHeight[4];
    uint32_t cpuTextureCacheClock;
    GLuint retiredTextures[VITA_MODERN_RETIRED_TEXTURE_SLOTS];
    uint32_t retiredTextureFrames[VITA_MODERN_RETIRED_TEXTURE_SLOTS];
    uint64_t retiredTextureBytes[VITA_MODERN_RETIRED_TEXTURE_SLOTS];
    uint32_t vitaTextureEvictions;
    uint32_t vitaTextureDeferred;
    uint32_t vitaTextureRamHits;
#endif
    int32_t batchCount;
    uint32_t frameSubmittedPrimitives;
    uint32_t frameFlushCount;
    uint64_t frameBufferUploadUs;
    uint64_t frameDrawSubmitUs;
    uint32_t frameTargetSwitches;
    GLuint currentTextureId;
    GLuint stageTextures[MAX_TEXTURE_STAGES];



    GLuint whiteTexture; // 1x1 white pixel for drawing primitives (rectangles, lines, etc.)

    int32_t windowW; // stored from beginFrame for endFrame blit
    int32_t windowH;
    int32_t gameW; // game width (matches the application_surface size)
    int32_t gameH; // game height (matches the application_surface size)

    GLuint hostFramebuffer; // present target for the composited frame, where 0 == the window

    // Original counts from data.win (dynamic slots start at these indices)
    uint32_t originalTexturePageCount;
    uint32_t originalTpagCount;
    uint32_t originalSpriteCount;
    GLuint* surfaces;
    GLuint* surfaceTexture;
    int32_t* surfaceWidth;
    int32_t* surfaceHeight;
    uint32_t surfaceCount;

    // Blending mode + factors
    int32_t currentBlendMode;
    int32_t currentSFactor;
    int32_t currentDFactor;
    int32_t currentSFactorAlpha;
    int32_t currentDFactorAlpha;

    bool isGL3; // TRUE if running on OpenGL (ES) 3.x+
    bool isGLES;  // TRUE if running on OpenGL ES (GLES)
} GLRenderer;

bool GLRenderer_ensureTextureLoaded(GLRenderer* gl, uint32_t pageId);
uint32_t GLRenderer_prepareTextureCache(DataWin* dw, void (*progressCallback)(uint32_t, uint32_t, void*), void* user);
void GLRenderer_trimTextureCacheForRoomChange(GLRenderer* gl, uint64_t targetBytes);
void GLRenderer_drawExternalTexture(GLRenderer* gl, GLuint textureId,
                                    float x, float y, float width, float height,
                                    float alpha);
void GLRenderer_drawExternalTextureFlipped(GLRenderer* gl, GLuint textureId,
                                    float x, float y, float width, float height,
                                    float alpha, bool flipX, bool flipY);
Renderer* GLRenderer_create(void);
#ifdef _WIN32
bool GLRenderer_testExternalPng(GLRenderer* gl, const char* path);
#endif

// Vita-only compatibility probe used by the legacy frontend. It compiles the
// embedded GameMaker GLSL ES shaders without switching the active renderer.
// This lets us validate VitaGL shader compatibility before enabling modern-gl.
bool GLRenderer_probeVitaShaders(DataWin* dataWin);
// Tests the GL capabilities and the default programmable pipeline without
// committing the runner to this backend. Used by Vita for a safe legacy
// fallback before Runner_create initializes renderer-owned resources.
bool GLRenderer_preflight(DataWin* dataWin, char* reason, size_t reasonSize);
void GLRenderer_setDiagnosticCallback(void (*callback)(const char*));

#endif /* _BS_GL_RENDERER_H_ */
