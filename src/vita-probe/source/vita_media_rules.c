#include "vita_media_rules.h"

#include <string.h>

int VitaMediaRules_isChapter5Intro(const char* sourcePath) {
    return sourcePath != NULL && strstr(sourcePath, "ch5_intro_") != NULL;
}

int VitaMediaRules_allowHardAudioResync(int alreadyApplied) {
    return alreadyApplied == 0;
}

uint64_t VitaMediaRules_firstLoadHoldUs(void) {
    return 3000000ULL;
}

uint64_t VitaMediaRules_firstLoadFadeUs(void) {
    return 500000ULL;
}

float VitaMediaRules_firstLoadBrightness(uint64_t elapsedAfterPlaybackUs) {
    const uint64_t holdUs = VitaMediaRules_firstLoadHoldUs();
    const uint64_t fadeUs = VitaMediaRules_firstLoadFadeUs();
    if (elapsedAfterPlaybackUs <= holdUs) return 1.0f;
    if (elapsedAfterPlaybackUs >= holdUs + fadeUs) return 0.0f;
    return 1.0f - (float)(elapsedAfterPlaybackUs - holdUs) / (float)fadeUs;
}
