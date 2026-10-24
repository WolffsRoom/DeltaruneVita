#include "launcher_ui.h"

#include "debugScreen.h"

void LauncherUi_init(void) {
    psvDebugScreenInit();
}

void LauncherUi_draw(VitaBackend selected, uint32_t countdownSeconds,
                     bool countdownHeld, bool previousGxmFailure,
                     const char* status) {
    psvDebugScreenPrintf("\e[2J\e[1;1H");
    psvDebugScreenPrintf("\n\n        DELTARUNE VITA - RENDERIZADOR\n");
    psvDebugScreenPrintf("        ==============================\n\n");
    psvDebugScreenPrintf("   %s VitaGL - Estavel\n\n",
                         selected == VITA_BACKEND_VITAGL ? ">" : " ");
    psvDebugScreenPrintf("   %s Native-GXM - Experimental\n\n",
                         selected == VITA_BACKEND_GXM ? ">" : " ");
    if (previousGxmFailure)
        psvDebugScreenPrintf("   O Native-GXM nao concluiu a abertura anterior.\n"
                             "   VitaGL e recomendado para recuperar o jogo.\n\n");
    if (status != 0 && status[0] != '\0')
        psvDebugScreenPrintf("   %s\n\n", status);
    if (countdownHeld)
        psvDebugScreenPrintf("   Seletor mantido aberto por L+R.\n");
    else
        psvDebugScreenPrintf("   Inicio automatico em %u segundo(s).\n",
                             countdownSeconds);
    psvDebugScreenPrintf("\n   Direcional: alterar   X: iniciar   O: VitaGL\n");
}
