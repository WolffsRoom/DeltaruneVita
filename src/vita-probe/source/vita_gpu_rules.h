#ifndef VITA_GPU_RULES_H
#define VITA_GPU_RULES_H

#include <stdint.h>

int VitaGpuRules_retiredTextureMature(uint32_t currentFrame, uint32_t retiredFrame);
int VitaGpuRules_textureRecentlyUsed(uint32_t currentFrame, uint32_t lastUsedFrame);
int VitaGpuRules_deferTextureEvictionToFrameBoundary(int chapter, const char* roomName);
int VitaGpuRules_ch5CliffSimpleMode(const char* roomName);
int VitaGpuRules_modernSynchronousEviction(int chapter, const char* roomName);
int VitaGpuRules_ch3Round2ResidentPage(uint32_t pageId);
int VitaGpuRules_ch3LegacySubmissionMode(int chapter, const char* roomName);
int VitaGpuRules_modernBc3RoomSafe(int chapter, const char* roomName);
int VitaGpuRules_modernShaderSafe(int chapter, const char* roomName,
                                  const char* shaderName);
uint64_t VitaGpuRules_modernTextureCacheLimitBytes(int chapter, const char* roomName,
                                                   int floweryBattleRoom);
int VitaGpuRules_floweryStructuralSurface(int width, int height);
int VitaGpuRules_floweryTowerLayerVisible(int surfaceId, int sourceWidth, int sourceHeight,
                                          int targetWidth, int targetHeight);
int VitaGpuRules_floweryTowerUpdateFrame(uint32_t frame);

#endif
