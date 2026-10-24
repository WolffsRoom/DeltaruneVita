#ifndef _BS_DEBUG_OVERLAY_H_
#define _BS_DEBUG_OVERLAY_H_

#include "common.h"
#include "runner.h"

// Draws collision overlays for every active + visible instance in the current room.
// For instances backed by a sprite that has a precise mask (sepMasks == 1), every set
// mask pixel is filled with a translucent tint and the AABB outline is green.
// Otherwise, only the AABB outline is drawn in red.
//
// The drawing happens through the Renderer vtable, so this works on any platform that
// implements drawRectangle. Must be called inside a beginView/endView pair so the
// world-space coordinates project correctly.
extern int32_t g_debugSelectedInstanceIndex;

// Draws collision overlays for active + visible instances.
// Selected instance (g_debugSelectedInstanceIndex) is highlighted in Magenta/Yellow with object/sprite info.
void DebugOverlay_drawCollisionMasks(Runner* runner);
void DebugOverlay_selectNextInstance(Runner* runner, int direction);
void DebugOverlay_selectInstanceAtPoint(Runner* runner, float worldX, float worldY);
void DebugOverlay_saveSelectedObjectLog(Runner* runner, int chapter);

#endif /* _BS_DEBUG_OVERLAY_H_ */
