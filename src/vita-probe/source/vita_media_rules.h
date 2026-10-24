#ifndef DELTARUNE_VITA_MEDIA_RULES_H
#define DELTARUNE_VITA_MEDIA_RULES_H

#include <stdint.h>

int VitaMediaRules_isChapter5Intro(const char* sourcePath);
int VitaMediaRules_allowHardAudioResync(int alreadyApplied);
uint64_t VitaMediaRules_firstLoadHoldUs(void);
uint64_t VitaMediaRules_firstLoadFadeUs(void);
float VitaMediaRules_firstLoadBrightness(uint64_t elapsedAfterPlaybackUs);

#endif
