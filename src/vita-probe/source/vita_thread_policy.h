#ifndef DELTARUNE_VITA_THREAD_POLICY_H
#define DELTARUNE_VITA_THREAD_POLICY_H

#include <stdbool.h>
#include <psp2/kernel/threadmgr.h>

/*
 * Creates a CPU-heavy background worker on the Vita's normally restricted
 * fourth application core when CapUnlocker is installed.  Stock systems
 * reject that affinity mask, so the helper transparently retries with the
 * normal scheduler and keeps CapUnlocker optional.
 */
SceUID VitaThread_createBackground(const char* name,
                                   SceKernelThreadEntry entry,
                                   int priority,
                                   int stackSize,
                                   unsigned int attributes,
                                   bool* usedUnlockedCore);

#endif
