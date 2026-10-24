#include "vita_thread_policy.h"

/* User core 3 / CPU4. CapUnlocker removes SceThreadmgr's game-app affinity
 * rejection for this mask. Keep the literal local because older VitaSDK
 * headers do not expose SCE_KERNEL_CPU_MASK_USER_3. */
#define VITA_CPU_MASK_UNLOCKED_CORE 0x00080000

SceUID VitaThread_createBackground(const char* name,
                                   SceKernelThreadEntry entry,
                                   int priority,
                                   int stackSize,
                                   unsigned int attributes,
                                   bool* usedUnlockedCore) {
    if (usedUnlockedCore != NULL) *usedUnlockedCore = false;

    SceUID thread = sceKernelCreateThread(name, entry, priority, stackSize,
                                          attributes,
                                          VITA_CPU_MASK_UNLOCKED_CORE, NULL);
    if (thread >= 0) {
        if (usedUnlockedCore != NULL) *usedUnlockedCore = true;
        return thread;
    }

    /* CapUnlocker is deliberately optional: an unmodified Vita uses the
     * scheduler-selected application cores exactly as previous builds did. */
    SceUID fallback = sceKernelCreateThread(name, entry, priority, stackSize,
                                            attributes, 0, NULL);
    return fallback;
}
