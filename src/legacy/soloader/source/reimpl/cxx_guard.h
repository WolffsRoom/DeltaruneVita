#ifndef SOLOADER_CXX_GUARD_H
#define SOLOADER_CXX_GUARD_H

#include <stdint.h>

int cxa_guard_acquire_android(volatile uint32_t *guard);
void cxa_guard_release_android(volatile uint32_t *guard);
void cxa_guard_abort_android(volatile uint32_t *guard);

#endif
