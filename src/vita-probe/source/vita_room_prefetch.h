#ifndef DELTARUNE_VITA_ROOM_PREFETCH_H
#define DELTARUNE_VITA_ROOM_PREFETCH_H

#include "data_win.h"
#include "vita_background_work.h"

#include <stdbool.h>
#include <stdint.h>

void VitaRoomPrefetch_init(VitaBackgroundWork* work, DataWin* dataWin);
bool VitaRoomPrefetch_prepare(int32_t roomIndex);
void VitaRoomPrefetch_cancel(void);
void VitaRoomPrefetch_shutdown(void);

#endif
