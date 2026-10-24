#ifndef DELTARUNE_VITA_BORDERS_H
#define DELTARUNE_VITA_BORDERS_H

#include "vita_border_rules.h"

void VitaBorders_init(int chapter);
void VitaBorders_updateGameState(const VitaBorderRuleState* state);
// Mirrors the official GameMaker obj_border_controller while keeping border
// artwork external to data.win. Sprite names are resolved to borders/<name>_0.png.
void VitaBorders_updateControllerState(int active, const char* currentSpriteName,
                                       const char* pendingSpriteName, int crossfade,
                                       float crossfadeAlpha, float overlayAlpha,
                                       unsigned int overlayColor,
                                       const char* layerSpriteName, float layerAlpha,
                                       float redLayerAlpha, float customEffectAlpha,
                                       unsigned int customEffectColor);
void VitaBorders_updateRoom(const char* roomName);
int VitaBorders_needsMemoryTrim(void);
void VitaBorders_setSuppressed(int suppressed);
void VitaBorders_setMemoryPressure(int pressured);
void VitaBorders_prepareRoomChange(const char* nextRoomName);
// Dark-world transition animations replace the whole game viewport before the
// room change is requested. Fade the host surround from the first visible
// transition frame, so the previous room's border cannot remain around it.
void VitaBorders_noteTransitionObject(const char* objectName);
void VitaBorders_prepareLauncherReturn(void);
// Chapter 3 changes borders inside a room as the TV show advances. These
// signals are latched until the next room instead of deriving the border only
// from the room name.
void VitaBorders_noteVisibleAsset(const char* assetName);
void VitaBorders_noteChapter3BoardSwitch(void);
void VitaBorders_noteSound(const char* soundName);
void VitaBorders_cycleCurrent(int direction);
void VitaBorders_draw(int windowW, int windowH);
int VitaBorders_coversScreen(void);
int VitaBorders_filesAvailable(void);
void VitaBorders_shutdown(void);

#endif


