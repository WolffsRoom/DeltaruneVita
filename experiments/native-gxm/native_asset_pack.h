#ifndef GXM_ASSET_PACK_H
#define GXM_ASSET_PACK_H
#include <stddef.h>
/* Reads packed prologue resources into caller-owned malloc memory.
 * Returns 1 for packed hit; 0 for not found/invalid pack. */
int GxmAssetPack_read(const char*path,unsigned char**bytes,size_t*length);
void GxmAssetPack_close(void);
#endif
