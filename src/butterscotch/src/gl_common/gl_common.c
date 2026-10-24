#include "gl_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runner.h"
#include "utils.h"
#include "renderer.h" // for bm_* constants

#include "gl_wrappers.h"

// ===[ Letterbox blit ]===

#ifdef PLATFORM_VITA
extern int g_vitaDisplayOffsetX;
extern int g_vitaDisplayOffsetY;
extern int g_vitaDisplayZoom;
extern int g_vitaDisplayStretchToFill;
extern void VitaBorders_draw(int windowW, int windowH);
#endif

void GLCommon_computeLetterbox(int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, int32_t* outStartX, int32_t* outStartY, int32_t* outEndX, int32_t* outEndY) {
    int32_t effW, effH;
#ifdef PLATFORM_VITA
    if (g_vitaDisplayStretchToFill) {
        effW = windowW;
        effH = windowH;
    } else
#endif
    if ((gameW * windowH) / gameH < windowW) {
        effW = (gameW * windowH) / gameH;
        effH = windowH;
    } else {
        effW = windowW;
        effH = (gameH * windowW) / gameW;
    }
    if (abs(windowH - effH) <= 2) effH = windowH;
    if (abs(windowW - effW) <= 2) effW = windowW;
#ifdef PLATFORM_VITA
    effW = effW * g_vitaDisplayZoom / 100;
    effH = effH * g_vitaDisplayZoom / 100;
#endif
    int32_t startX = (windowW - effW) / 2;
    int32_t startY = (windowH - effH) / 2;
#ifdef PLATFORM_VITA
    startX += g_vitaDisplayOffsetX;
    startY += g_vitaDisplayOffsetY;
#endif
    *outStartX = startX;
    *outStartY = startY;
    *outEndX = startX + effW;
    *outEndY = startY + effH;
}

void GLCommon_beginLetterboxBlit(GLuint fbo, GLuint hostFbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hostFbo);
}

void GLCommon_endLetterboxBlit(int32_t fboWidth, int32_t fboHeight, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH, GLuint hostFbo) {
    int32_t sx, sy, ex, ey;
#ifdef PLATFORM_VITA
    GLint sourceReadFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &sourceReadFbo);
#endif
    glClearColor(0.0, 0.0, 0.0, 1.0); //please remove if it breaks something like borders, it was just my quick-fix for the color to not be randomly changed
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hostFbo);
#ifdef PLATFORM_VITA
    // FPS Unlocked (vsync off) can present a partially-composited host buffer,
    // and the opening full-screen black clear is exactly what such an unsynced
    // scanout latches onto -> the reported "black bars". When a full-screen
    // console border will cover the framebuffer this frame we skip the black
    // clear: the uncleared base is then the PREVIOUS complete frame, so tearing
    // shows game/border imagery instead of black. When no border covers the
    // screen (disabled / suppressed / not yet loaded) we still clear to black so
    // the pillarbox margins remain correct.
    extern int VitaBorders_coversScreen(void);
    if (!VitaBorders_coversScreen())
        glClear(GL_COLOR_BUFFER_BIT);
    VitaBorders_draw(windowW, windowH);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)sourceReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hostFbo);
#else
    glClear(GL_COLOR_BUFFER_BIT);
#endif
    GLCommon_computeLetterbox(gameW, gameH, windowW, windowH, &sx, &sy, &ex, &ey);
    glBlitFramebuffer(0, 0, fboWidth, fboHeight, sx, ey, ex, sy, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, hostFbo);
}

// ===[ Surface arrays ]===

uint32_t GLCommon_findOrAllocateSurfaceSlot(GLuint** surfaces, GLuint** surfaceTexture, int32_t** surfaceWidth, int32_t** surfaceHeight, uint32_t* count) {
    repeat(*count, i) {
        if ((*surfaces)[i] == 0)
            return i;
    }

    uint32_t newIndex = *count;
    (*count)++;
    *surfaces = (GLuint *)safeRealloc(*surfaces, *count * sizeof(GLuint));
    *surfaceTexture = (GLuint *)safeRealloc(*surfaceTexture, *count * sizeof(GLuint));
    *surfaceWidth = (int32_t *)safeRealloc(*surfaceWidth,   *count * sizeof(int32_t));
    *surfaceHeight = (int32_t *)safeRealloc(*surfaceHeight,  *count * sizeof(int32_t));
    (*surfaces)[newIndex]       = 0;
    (*surfaceTexture)[newIndex] = 0;
    (*surfaceWidth)[newIndex]   = 0;
    (*surfaceHeight)[newIndex]  = 0;
    return newIndex;
}

// Resolves a surface ID to its FBO handle and dimensions. Returns false for out-of-range or freed surfaces.
static bool resolveSurfaceFBO(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t id, GLuint* outFbo, int32_t* outW, int32_t* outH) {
    if (0 > id || (uint32_t) id >= count) return false;
    if (surfaces[id] == 0) return false;
    *outFbo = surfaces[id];
    *outW = surfaceWidth[id];
    *outH = surfaceHeight[id];
    return true;
}

void GLCommon_surfaceBlit(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t dstId, int32_t dstX, int32_t dstY, int32_t srcId, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLuint srcFbo, dstFbo;
    int32_t srcFboW, srcFboH;
    MAYBE_UNUSED int32_t dstFboW, dstFboH;

    if (!resolveSurfaceFBO(surfaces, surfaceWidth, surfaceHeight, count, srcId, &srcFbo, &srcFboW, &srcFboH))
        return;

    if (!resolveSurfaceFBO(surfaces, surfaceWidth, surfaceHeight, count, dstId, &dstFbo, &dstFboW, &dstFboH))
        return;

    int originalFramebufferBinding;

    // Yes, in OpenGL you need to use _BINDING to query things
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &originalFramebufferBinding);

    GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    if (scissorWasEnabled) glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);

    // VitaGL's FBO blitter maps destination positions from the top edge and
    // samples source V as (height - y) / height. Our surface row zero is GML
    // row zero. Convert BOTH rectangles; desktop OpenGL already uses our row
    // coordinates. Off-centre monitor crops exposed the missing conversion.
    if (!part) {
        srcX = 0;
        srcY = 0;
        srcW = srcFboW;
        srcH = srcFboH;
    }
#ifdef PLATFORM_VITA
    glBlitFramebuffer(srcX, srcFboH - srcY, srcX + srcW, srcFboH - srcY - srcH,
                      dstX, dstFboH - dstY, dstX + srcW, dstFboH - dstY - srcH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
#else
    glBlitFramebuffer(srcX, srcY, srcX + srcW, srcY + srcH,
                      dstX, dstY, dstX + srcW, dstY + srcH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, originalFramebufferBinding);
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
}

static bool surfaceReadNeedsFinish = false;

void GLCommon_setSurfaceReadFinish(bool enabled) {
    surfaceReadNeedsFinish = enabled;
}

bool GLCommon_surfaceGetPixels(GLuint* surfaces, int32_t* surfaceWidth, int32_t* surfaceHeight, uint32_t count, int32_t surfaceId, uint8_t* outRGBA) {
    if (0 > surfaceId || (uint32_t) surfaceId >= count)
        return false;

    if (surfaces[surfaceId] == 0)
        return false;

    int32_t w = surfaceWidth[surfaceId];
    int32_t h = surfaceHeight[surfaceId];
    if (0 >= w || 0 >= h)
        return false;

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint prevPackAlign = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlign);

    glBindFramebuffer(GL_FRAMEBUFFER, surfaces[surfaceId]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    uint8_t* tmp = (uint8_t *)safeMalloc((size_t) w * (size_t) h * 4);
    // The darkbulb puzzle hashes a surface while it can still be the active
    // render target. It needs an explicit barrier on Vita. Applying glFinish
    // to every surface consumer caused periodic 80-90 ms GPU stalls in Chapter
    // 3, so ordinary reads rely on glReadPixels' synchronous semantics.
    if (surfaceReadNeedsFinish) glFinish();
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    surfaceReadNeedsFinish = false;

    // OpenGL reads bottom-up; native expects y=0 at the top
    int32_t rowBytes = w * 4;
    for (int32_t py = 0; h > py; py++) {
        memcpy(outRGBA + (size_t) py * (size_t) rowBytes, tmp + (size_t) (h - 1 - py) * (size_t) rowBytes, (size_t) rowBytes);
    }
    free(tmp);

    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlign);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevFbo);
    return true;
}

#ifndef PLATFORM_PS3

// ===[ GL version queries ]===

GLVer GLCommon_getGLVersion(void) {
    GLVer v = {0, 0, false};
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (!ver) return v;
    if (strstr(ver, "OpenGL ES")) v.isGLES = true;
    const char* p = ver;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (*p) {
        v.major = *p - '0';
        ++p;
        if (*p == '.') ++p;
        v.minor = *p - '0';
    }
    return v;
}

#endif

// ===[ Blend mode translation ]===

int GLCommon_blendFactorFromGL(GLenum factor) {
    switch (factor) {
        case GL_ZERO: return bm_zero;
        case GL_SRC_COLOR: return bm_src_color;
        case GL_ONE_MINUS_SRC_COLOR: return bm_inv_src_color;
        case GL_SRC_ALPHA: return bm_src_alpha;
        case GL_ONE_MINUS_SRC_ALPHA: return bm_inv_src_alpha;
        case GL_DST_ALPHA: return bm_dest_alpha;
        case GL_ONE_MINUS_DST_ALPHA: return bm_inv_dest_alpha;
        case GL_DST_COLOR: return bm_dest_color;
        case GL_ONE_MINUS_DST_COLOR: return bm_inv_dest_color;
        case GL_SRC_ALPHA_SATURATE: return bm_src_alpha_sat;
        default: return bm_one;
    }
}

GLenum GLCommon_blendFactorToGL(int factor) {
    switch (factor) {
        case bm_zero:           return GL_ZERO;
        default:
        case bm_one:            return GL_ONE;
        case bm_src_color:      return GL_SRC_COLOR;
        case bm_inv_src_color:  return GL_ONE_MINUS_SRC_COLOR;
        case bm_src_alpha:      return GL_SRC_ALPHA;
        case bm_inv_src_alpha:  return GL_ONE_MINUS_SRC_ALPHA;
        case bm_dest_alpha:     return GL_DST_ALPHA;
        case bm_inv_dest_alpha: return GL_ONE_MINUS_DST_ALPHA;
        case bm_dest_color:     return GL_DST_COLOR;
        case bm_inv_dest_color: return GL_ONE_MINUS_DST_COLOR;
        case bm_src_alpha_sat:  return GL_SRC_ALPHA_SATURATE;
    }
}

GLenum GLCommon_blendModeToEquation(int mode) {
    switch (mode) {
        default:
        case bm_normal:           return GL_FUNC_ADD;
        case bm_add:              return GL_FUNC_ADD;
        case bm_subtract:         return GL_FUNC_ADD;
        case bm_reverse_subtract: return GL_FUNC_REVERSE_SUBTRACT;
        case bm_min:              return GL_MIN;
        case bm_max:              return GL_FUNC_ADD;
    }
}

GLenum GLCommon_blendModeToSFactor(int mode) {
    switch (mode) {
        default:
        case bm_normal:           return GL_SRC_ALPHA;
        case bm_add:              return GL_SRC_ALPHA;
        case bm_subtract:         return GL_ZERO;
        case bm_reverse_subtract: return GL_SRC_ALPHA;
        case bm_min:              return GL_ONE;
        case bm_max:              return GL_SRC_ALPHA;
    }
}

GLenum GLCommon_blendModeToDFactor(int mode) {
    switch (mode) {
        default:
        case bm_normal:           return GL_ONE_MINUS_SRC_ALPHA;
        case bm_add:              return GL_ONE;
        case bm_subtract:         return GL_ONE_MINUS_SRC_COLOR;
        case bm_reverse_subtract: return GL_ONE;
        case bm_min:              return GL_ONE;
        case bm_max:              return GL_ONE_MINUS_SRC_COLOR;
    }
}
