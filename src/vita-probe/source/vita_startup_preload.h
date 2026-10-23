#ifndef DELTARUNE_VITA_STARTUP_PRELOAD_H
#define DELTARUNE_VITA_STARTUP_PRELOAD_H

#include "data_win.h"
#include "vita_background_work.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct VitaStartupPreloadStats {
    uint64_t beginUs;
    uint64_t parseBeginUs;
    uint64_t parseEndUs;
    uint64_t joinBeginUs;
    uint64_t joinEndUs;
    bool usedUnlockedCore;
    bool submitted;
    bool adopted;
} VitaStartupPreloadStats;

typedef struct VitaStartupPreload {
    VitaBackgroundWork* work;
    uint32_t ticket;
    uint32_t generation;
    char path[256];
    VitaStartupPreloadStats stats;
} VitaStartupPreload;

bool VitaStartupPreload_begin(VitaStartupPreload* preload,
                              VitaBackgroundWork* work,
                              const char* path,
                              DataWinParserOptions options,
                              uint32_t generation);
DataWin* VitaStartupPreload_join(VitaStartupPreload* preload,
                                 const char* expectedPath);
void VitaStartupPreload_cancel(VitaStartupPreload* preload);

#endif
