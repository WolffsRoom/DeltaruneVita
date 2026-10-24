#ifndef DELTARUNE_REAL_TPAG_PROBE_H
#define DELTARUNE_REAL_TPAG_PROBE_H
#include "tpag_sprite.h"

typedef struct {
    int images[3];
    int pageW[3], pageH[3];
    NativeGXMTpag entries[3];
    int loaded;
} RealTpagProbe;

/* Uses user-installed Chapter 0 data.win + RGBA4444 texture cache.
 * No game art is bundled in the executable. Negative = missing/invalid files.
 */
int real_tpag_probe_create(NVGcontext *vg, RealTpagProbe *probe);
void real_tpag_probe_draw(NVGcontext *vg, const RealTpagProbe *probe, float t);
void real_tpag_probe_destroy(NVGcontext *vg, RealTpagProbe *probe);
#endif
