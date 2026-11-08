/* Always launch VitaGL without a renderer selection screen.
 * Native GXM remains packaged for explicit dev use; an old GXM boot marker
 * must not reopen the selector after crashes. No UI is drawn on success.
 */
#include "vita_backend_selection.h"
#include <stdint.h>
#include "debugScreen.h"
#include <psp2/appmgr.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

int main(void) {
    const VitaBackend requested=VitaBackendSelection_load(VITA_BACKEND_PREFERENCE_PATH);
    SceIoStat stGL,stGxm;
    const bool glAvailable=sceIoGetstat(VITA_BACKEND_VITAGL_EXECUTABLE,&stGL)>=0;
    const bool gxmAvailable=sceIoGetstat(VITA_BACKEND_GXM_EXECUTABLE,&stGxm)>=0;
    const VitaBackend backend=VitaBackendSelection_resolve(requested,glAvailable,gxmAvailable);
    const char* executable=backend!=VITA_BACKEND_UNAVAILABLE ?
        VitaBackendSelection_executable(backend) : VITA_BACKEND_VITAGL_EXECUTABLE;
    SceIoStat st;
    /* Old renderer_backend.ini may be set to GXM after a previous crash.
     * Repair that preference before launching to avoid repeated boot loops. */
    if(backend!=VITA_BACKEND_UNAVAILABLE)
        VitaBackendSelection_save(VITA_BACKEND_PREFERENCE_PATH,backend);
    if(backend!=VITA_BACKEND_UNAVAILABLE && sceIoGetstat(executable,&st)>=0){
        int rc=sceAppMgrLoadExec(executable,NULL,NULL);
        psvDebugScreenInit();
        psvDebugScreenPrintf("DELTARUNE VITA\n\nFalha ao iniciar %s: 0x%08X\n",
                             VitaBackendSelection_name(backend),
                             (unsigned)rc);
    }else{
        psvDebugScreenInit();
        psvDebugScreenPrintf( "DELTARUNE VITA\n\nExecutavel de renderizador ausente. Reinstale a VPK.\n");
    }
    for(;;)sceKernelDelayThread(16000);
}
