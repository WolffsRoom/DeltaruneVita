#ifndef DELTARUNE_VITA_DEBUG_SYNC_H
#define DELTARUNE_VITA_DEBUG_SYNC_H

/* v0.84-3: "Send reports locally" (DEBUG tab, config.ini debug_report_sync=1).
 * A low-priority server that lets the DELTARUNE Debug PC app, on the same
 * network, download new devlogs and screenshots and follow the active probe
 * log live. Protocol: docs/debug-sync-protocol.md. Separate ports from the
 * save sync, so both can run at once. */
#include <stdbool.h>

#define DEBUG_SYNC_DISCOVERY_PORT 26550
#define DEBUG_SYNC_TCP_PORT 26551
#define DEBUG_SYNC_PROTOCOL 1

#define DEBUG_SYNC_DATA_ROOT "ux0:data/deltarune/deltarunevita/"
#define DEBUG_SYNC_DEVLOG_DIR DEBUG_SYNC_DATA_ROOT "devlogs/"
#define DEBUG_SYNC_SHOT_DIR "ux0:picture/SCREENSHOT/Deltarune/"

void DebugSync_setEnabled(bool enabled);
bool DebugSync_isEnabled(void);
/* Reads debug_report_sync=1 from ux0:data/deltarune/config.ini (used by the
 * Native GXM executable, which has its own settings code). */
void DebugSync_startFromConfig(void);
const char *DebugSync_status(void);

#endif
