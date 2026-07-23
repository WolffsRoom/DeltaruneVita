#include "utils/diagnostic.h"
#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include <stdatomic.h>

static char current_phase[96] = "not_started";
static atomic_uint current_frame = ATOMIC_VAR_INIT(0);
static atomic_uint heartbeat = ATOMIC_VAR_INIT(0);

void diagnostic_phase(const char *phase) {
    sceClibSnprintf(current_phase, sizeof(current_phase), "%s", phase);
    log_marker(phase);
}

void diagnostic_frame(uint32_t frame, const char *phase) {
    atomic_store_explicit(&current_frame, frame, memory_order_relaxed);
    diagnostic_phase(phase);
}

static int watchdog_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    for (;;) {
        sceKernelDelayThread(2000000);

        char line[192];
        unsigned int beat = atomic_fetch_add_explicit(
            &heartbeat, 1, memory_order_relaxed) + 1;
        unsigned int frame = atomic_load_explicit(
            &current_frame, memory_order_relaxed);
        sceClibSnprintf(line, sizeof(line),
                       "WATCHDOG beat=%u frame=%u last=%s",
                       beat, frame, current_phase);
        log_marker(line);
    }
}

void diagnostic_start(void) {
    SceUID thread = sceKernelCreateThread(
        "deltarune_watchdog", watchdog_thread, 0x40, 0x10000,
        0, 0, NULL);
    if (thread < 0) {
        log_marker("WATCHDOG=create_failed");
        return;
    }

    int result = sceKernelStartThread(thread, 0, NULL);
    if (result < 0) {
        log_marker("WATCHDOG=start_failed");
        return;
    }
    log_marker("WATCHDOG=started interval_ms=2000");
}
