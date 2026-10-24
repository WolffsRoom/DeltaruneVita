#ifndef DELTARUNE_NATIVE_GXM_ATLAS_PROBE_H
#define DELTARUNE_NATIVE_GXM_ATLAS_PROBE_H

typedef struct NVGcontext NVGcontext;

/* Stage 2 of the sceGxm bring-up: uploaded RGBA atlas + cropped sprite draws.
 * NanoVG GXM supplies the embedded GXP texture programs for this experiment.
 * This is NOT yet the production Butterscotch RendererVtable backend.
 */
int atlas_probe_create(NVGcontext *vg);
void atlas_probe_draw(NVGcontext *vg, int image, float timeSeconds);
void atlas_probe_destroy(NVGcontext *vg, int image);

#endif
