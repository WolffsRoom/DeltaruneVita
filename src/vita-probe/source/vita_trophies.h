#ifndef DELTARUNE_VITA_TROPHIES_H
#define DELTARUNE_VITA_TROPHIES_H

#include <stdbool.h>
#include <stdint.h>

// Native Vita trophies are optional. Local trophies continue to work when
// NoTrpDrm or the DELT00001_01 trophy pack is unavailable.
bool VitaTrophies_init(void);
const char* VitaTrophies_lastStage(void);
int VitaTrophies_lastResult(void);
unsigned int VitaTrophies_syncMask(uint32_t unlockedMask);
void VitaTrophies_unlock(int id);
void VitaTrophies_shutdown(void);

#endif
