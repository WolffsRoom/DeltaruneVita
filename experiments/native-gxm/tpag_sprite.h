#ifndef DELTARUNE_GXM_TPAG_SPRITE_H
#define DELTARUNE_GXM_TPAG_SPRITE_H
typedef struct NVGcontext NVGcontext;
/* Mirrors the position/dimension fields of GameMaker TexturePageItem. */
typedef struct {
    int sourceX, sourceY, sourceWidth, sourceHeight;
    int targetX, targetY, targetWidth, targetHeight;
    int boundingWidth, boundingHeight;
} NativeGXMTpag;
/* Renderer-independent public interface; GXM2 uses NanoVG GXP as adapter. */
int native_gxm_draw_tpag(NVGcontext *vg, int image, int pageWidth, int pageHeight,
                         const NativeGXMTpag *tpag,
                         float x, float y, float originX, float originY,
                         float xscale, float yscale, float angleDeg, float alpha);
#endif
