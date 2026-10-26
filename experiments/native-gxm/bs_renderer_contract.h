#ifndef BS_RENDERER_CONTRACT_H
#define BS_RENDERER_CONTRACT_H
#include "renderer.h"
/* Bootstrap-only renderer: init/destroy are real; every draw operation fails explicitly. */
Renderer* BSContractRenderer_create(void);
typedef struct NVGcontext NVGcontext;
void BSContractRenderer_cacheMaintenance(Renderer* r);
/* DEV Room Navigator only: called AFTER GXM submit and sceGxmFinish.
 * Releases old room TXTR atlas pages before unrelated room GML loads. */
void BSContractRenderer_devRoomJumpPrepare(Renderer* r);
/* Scene is already submitted/presented: no NanoVG/GXM frame is active. */
void BSContractRenderer_framePresented(Renderer* r);

void BSContractRenderer_bind(Renderer* r, NVGcontext* vg);
void BSContractRenderer_frameStart(Renderer* r);
void BSContractRenderer_metrics(Renderer* r, char* out, unsigned capacity);
void BSContractRenderer_getDevMetrics(Renderer*,uint64_t*,uint32_t*);
void BSContractRenderer_collectRetired(Renderer*);
/* After a room change, before its first Step: room-specific page preparation. */
void BSContractRenderer_roomEntered(Renderer* r);
/* Draw calls recorded in the last completed frame. */
unsigned long BSContractRenderer_lastFrameDraws(Renderer* r);
/* Pages of this data.win missing from the shared texture-cache (0 when
 * complete.vtc marks it complete), and the generator writing them in the
 * VitaGL format. progress(done,total,user) is called after every page. */
unsigned BSContractRenderer_textureCacheMissing(Renderer* r);
unsigned BSContractRenderer_generateTextureCache(Renderer* r,
    void (*progress)(unsigned done, unsigned total, void* user), void* user);
#endif
