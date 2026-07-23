#ifndef DELTARUNE_DIAGNOSTIC_H
#define DELTARUNE_DIAGNOSTIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void diagnostic_start(void);
void diagnostic_phase(const char *phase);
void diagnostic_frame(uint32_t frame, const char *phase);

#ifdef __cplusplus
}
#endif

#endif
