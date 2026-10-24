/* GameMaker TPAG mapping for native sceGxm experiments.
 * NanoVG supplies embedded GXP shaders for this bring-up, not for the
 * eventual production RendererVtable backend.
 */
#include "tpag_sprite.h"
#include "nanovg.h"

int native_gxm_draw_tpag(NVGcontext *vg, int image, int pageWidth, int pageHeight,
                         const NativeGXMTpag *p,
                         float x, float y, float originX, float originY,
                         float xscale, float yscale, float angleDeg, float alpha) {
    if (!vg || !p || image <= 0 || pageWidth <= 0 || pageHeight <= 0 ||
        p->sourceWidth <= 0 || p->sourceHeight <= 0 ||
        p->targetWidth <= 0 || p->targetHeight <= 0 ||
        p->sourceX < 0 || p->sourceY < 0 ||
        p->sourceX >= pageWidth || p->sourceY >= pageHeight ||
        p->sourceWidth > pageWidth - p->sourceX ||
        p->sourceHeight > pageHeight - p->sourceY ||
        xscale == 0 || yscale == 0 || alpha <= 0) return 0;

    /* Texture source rectangle vs. trimmed logical sprite rectangle.
     * Their dimensions can differ in GameMaker-generated texture pages.
     */
    const float sx = (float)p->targetWidth / (float)p->sourceWidth;
    const float sy = (float)p->targetHeight / (float)p->sourceHeight;
    const float left = (float)p->targetX - originX;
    const float top = (float)p->targetY - originY;

    nvgSave(vg);
    nvgTranslate(vg, x, y);
    nvgRotate(vg, -angleDeg * (3.14159265358979323846f / 180.0f));
    nvgScale(vg, xscale, yscale);
    NVGpaint paint = nvgImagePattern(vg,
        left - (float)p->sourceX * sx,
        top - (float)p->sourceY * sy,
        (float)pageWidth * sx,
        (float)pageHeight * sy,
        0.0f, image, alpha);
    nvgBeginPath(vg);
    nvgRect(vg, left, top, (float)p->targetWidth, (float)p->targetHeight);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    nvgRestore(vg);
    return 1;
}
