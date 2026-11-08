#ifndef DELTARUNE_VITA_ROOM_PREFETCH_H
#define DELTARUNE_VITA_ROOM_PREFETCH_H

#include "data_win.h"
#include "vita_background_work.h"

#include <stdbool.h>
#include <stdint.h>

void VitaRoomPrefetch_init(VitaBackgroundWork* work, DataWin* dataWin);
bool VitaRoomPrefetch_prepare(int32_t roomIndex);
/* Schedule one predictive room, with bounded worker RAM and main-thread
 * adoption. Used by GXM while the player is still in the previous room. */
bool VitaRoomPrefetch_prime(int32_t roomIndex);
void VitaRoomPrefetch_pump(void);
void VitaRoomPrefetch_cancel(void);
void VitaRoomPrefetch_shutdown(void);

#endif
