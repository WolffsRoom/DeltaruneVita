#ifndef DELTARUNE_VITA_SAVE_SYNC_H
#define DELTARUNE_VITA_SAVE_SYNC_H

/* v0.84: optional PC <-> Vita save sync over the local network (no FTP).
 * The Vita announces itself by UDP broadcast and serves its save folder to
 * the PC app over one TCP connection (protocol: docs/save-sync-protocol.md).
 * Only DELTARUNE save files are listed or accepted. */

#include <stdbool.h>

#define SAVE_SYNC_DISCOVERY_PORT 26540
#define SAVE_SYNC_TCP_PORT 26541
#define SAVE_SYNC_PROTOCOL 1

/* Starts (enabled=true) or stops the background server for saveDir
 * (with a trailing slash, e.g. "ux0:data/deltarune_saves/"). */
void SaveSync_setEnabled(bool enabled, const char *saveDir);
bool SaveSync_isEnabled(void);
/* Short status for the settings menu, e.g. "waiting for PC". */
const char *SaveSync_status(void);

/* Exposed for the desktop test harness. */
bool SaveSync_isSaveName(const char *name);
unsigned SaveSync_crc32(const unsigned char *data, unsigned long size);

#endif
