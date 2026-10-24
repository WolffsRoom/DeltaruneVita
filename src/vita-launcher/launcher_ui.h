#ifndef DELTARUNE_VITA_LAUNCHER_UI_H
#define DELTARUNE_VITA_LAUNCHER_UI_H

#include "vita_backend_selection.h"

#include <stdbool.h>
#include <stdint.h>

void LauncherUi_init(void);
void LauncherUi_draw(VitaBackend selected, uint32_t countdownSeconds,
                     bool countdownHeld, bool previousGxmFailure,
                     const char* status);

#endif
