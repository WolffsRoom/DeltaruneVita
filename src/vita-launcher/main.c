/* Always launch VitaGL without a renderer selection screen.
 * Native GXM remains packaged for explicit dev use; an old GXM boot marker
 * must not reopen the selector after crashes. No UI is drawn on success.
 */
#include "vita_backend_selection.h"
#include <stdint.h>
#include "debugScreen.h"
#include <psp2/appmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>

/* v0.83: VitaGL compiles its shaders with ur0:data/libshacccg.suprx. Without
 * it, vitagl.bin used to stop right after vglInit: its native message dialog
 * did not appear on some consoles, so the game showed black for 1-3 s and
 * closed with no config.ini and no log (boot_stages.txt ended at
 * vitagl_init_complete). Check it here, where the debug screen always works. */
static int launcher_shader_compiler_present(void){
    SceIoStat st;
    if(sceIoGetstat("ur0:data/libshacccg.suprx",&st)<0)return 0;
    return !SCE_S_ISDIR(st.st_mode) && st.st_size>=1024;
}

int main(void) {
    if(!launcher_shader_compiler_present()){
        SceUID fd=sceIoOpen("ux0:data/deltarune/deltarunevita/boot_stages.txt",
                            SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
        if(fd>=0){
            static const char line[]="launcher libshacccg_missing ur0:data/libshacccg.suprx\n";
            sceIoWrite(fd,line,sizeof(line)-1);
            sceIoClose(fd);
        }
        psvDebugScreenInit();
        psvDebugScreenPrintf(
            "DELTARUNE VITA\n\n"
            "libshacccg.suprx nao encontrado / not found:\n"
            "  ur0:data/libshacccg.suprx\n\n"
            "PT: Instale com o ShaRKF00D (VitaDB) e reinicie o console.\n"
            "EN: Install it with ShaRKF00D (VitaDB) and restart the console.\n\n"
            "Pressione PS para sair. / Press PS to exit.\n");
        for(;;)sceKernelDelayThread(16000);
    }
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
