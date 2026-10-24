#include "launcher_ui.h"
#include "vita_backend_selection.h"

#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdbool.h>
#include <stdio.h>

#define VITA_LAUNCHER_COUNTDOWN_US 3000000ULL

typedef enum LaunchResult {
    LAUNCH_RESULT_READY = 0,
    LAUNCH_RESULT_GXM_FALLBACK = 1,
    LAUNCH_RESULT_MISSING_VITAGL = 2
} LaunchResult;

static bool fileExists(const char* path) {
    SceIoStat stat;
    return path != NULL && sceIoGetstat(path, &stat) >= 0;
}

static LaunchResult resolveBackend(VitaBackend requested,
                                   VitaBackend* resolved) {
    bool vitaGlAvailable = fileExists(VITA_BACKEND_VITAGL_EXECUTABLE);
    bool gxmAvailable = fileExists(VITA_BACKEND_GXM_EXECUTABLE);
    *resolved = VitaBackendSelection_resolve(requested, vitaGlAvailable,
                                             gxmAvailable);
    if (*resolved == VITA_BACKEND_UNAVAILABLE)
        return LAUNCH_RESULT_MISSING_VITAGL;
    if (requested == VITA_BACKEND_GXM && *resolved == VITA_BACKEND_VITAGL)
        return LAUNCH_RESULT_GXM_FALLBACK;
    return LAUNCH_RESULT_READY;
}

static int launchBackend(VitaBackend backend) {
    return sceAppMgrLoadExec(VitaBackendSelection_executable(backend),
                             NULL, NULL);
}

int main(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    LauncherUi_init();

    VitaBackend selected = VitaBackendSelection_load(
        VITA_BACKEND_PREFERENCE_PATH);
    bool previousGxmFailure = fileExists(VITA_BACKEND_GXM_BOOT_MARKER);
    if (previousGxmFailure && selected == VITA_BACKEND_GXM)
        selected = VITA_BACKEND_VITAGL;

    SceCtrlData pad = {0};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    bool countdownHeld = (pad.buttons & SCE_CTRL_LTRIGGER) != 0 &&
                         (pad.buttons & SCE_CTRL_RTRIGGER) != 0;
    uint32_t previousButtons = pad.buttons;
    uint64_t countdownStart = sceKernelGetProcessTimeWide();
    char status[128] = {0};

    for (;;) {
        uint64_t now = sceKernelGetProcessTimeWide();
        uint64_t elapsed = now - countdownStart;
        uint32_t remaining = elapsed >= VITA_LAUNCHER_COUNTDOWN_US ? 0U :
            (uint32_t)((VITA_LAUNCHER_COUNTDOWN_US - elapsed + 999999ULL) /
                       1000000ULL);
        LauncherUi_draw(selected, remaining, countdownHeld,
                        previousGxmFailure, status);

        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~previousButtons;
        previousButtons = pad.buttons;
        bool confirm = false;
        if ((pressed & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT |
                        SCE_CTRL_RIGHT)) != 0) {
            selected = selected == VITA_BACKEND_GXM ?
                       VITA_BACKEND_VITAGL : VITA_BACKEND_GXM;
            countdownHeld = true;
            status[0] = '\0';
        }
        if ((pressed & SCE_CTRL_CIRCLE) != 0) {
            selected = VITA_BACKEND_VITAGL;
            confirm = true;
        }
        if ((pressed & SCE_CTRL_CROSS) != 0) confirm = true;
        if (!countdownHeld && elapsed >= VITA_LAUNCHER_COUNTDOWN_US)
            confirm = true;

        if (confirm) {
            VitaBackend resolved;
            LaunchResult result = resolveBackend(selected, &resolved);
            if (result == LAUNCH_RESULT_MISSING_VITAGL) {
                snprintf(status, sizeof(status),
                         "Falha ao iniciar: vitagl.bin ausente. Reinstale o VPK.");
                countdownHeld = true;
            } else {
                if (result == LAUNCH_RESULT_GXM_FALLBACK)
                    snprintf(status, sizeof(status),
                             "gxm.bin ausente; iniciando VitaGL.");
                selected = resolved;
                VitaBackendSelection_save(VITA_BACKEND_PREFERENCE_PATH,
                                          selected);
                int error = launchBackend(selected);
                snprintf(status, sizeof(status),
                         "Falha ao iniciar %s: 0x%08X",
                         VitaBackendSelection_name(selected),
                         (unsigned int)error);
                countdownHeld = true;
            }
        }
        sceKernelDelayThread(16667);
    }
}
