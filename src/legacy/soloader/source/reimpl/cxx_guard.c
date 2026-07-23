#include "reimpl/cxx_guard.h"

#include <psp2/kernel/threadmgr.h>

/* Android ARM and Vita newlib use incompatible C++ guard object states. */
#define GUARD_INITIALIZED 0x00000001u
#define GUARD_PENDING     0x00000100u

int cxa_guard_acquire_android(volatile uint32_t *guard) {
    for (;;) {
        uint32_t value = __atomic_load_n(guard, __ATOMIC_ACQUIRE);
        if (value & GUARD_INITIALIZED)
            return 0;

        if (!(value & GUARD_PENDING)) {
            uint32_t desired = value | GUARD_PENDING;
            if (__atomic_compare_exchange_n(guard, &value, desired, 0,
                                            __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                return 1;
        }

        sceKernelDelayThread(1000);
    }
}

void cxa_guard_release_android(volatile uint32_t *guard) {
    __atomic_store_n(guard, GUARD_INITIALIZED, __ATOMIC_RELEASE);
}

void cxa_guard_abort_android(volatile uint32_t *guard) {
    __atomic_store_n(guard, 0, __ATOMIC_RELEASE);
}
