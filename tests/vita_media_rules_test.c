#include <assert.h>
#include <stdio.h>
#include <math.h>
#include "../src/vita-probe/source/vita_media_rules.h"

int main(void) {
    assert(VitaMediaRules_isChapter5Intro("ux0:data/deltarune/deltarunevita/chapter5/vid/ch5_intro_en.mp4") == 1);
    assert(VitaMediaRules_isChapter5Intro("vid/ch5_intro_jp.mp4") == 1);
    assert(VitaMediaRules_isChapter5Intro("vid/tennaIntroF1_compressed_28.mp4") == 0);
    assert(VitaMediaRules_isChapter5Intro("app0:first_load.mp4") == 0);
    assert(VitaMediaRules_isChapter5Intro(NULL) == 0);

    assert(VitaMediaRules_allowHardAudioResync(0) == 1);
    assert(VitaMediaRules_allowHardAudioResync(1) == 0);
    assert(VitaMediaRules_allowHardAudioResync(2) == 0);

    assert(VitaMediaRules_firstLoadHoldUs() == 3000000ULL);
    assert(VitaMediaRules_firstLoadFadeUs() == 500000ULL);
    assert(fabsf(VitaMediaRules_firstLoadBrightness(0ULL) - 1.0f) < 0.0001f);
    assert(fabsf(VitaMediaRules_firstLoadBrightness(3000000ULL) - 1.0f) < 0.0001f);
    assert(fabsf(VitaMediaRules_firstLoadBrightness(3250000ULL) - 0.5f) < 0.0001f);
    assert(fabsf(VitaMediaRules_firstLoadBrightness(3500000ULL) - 0.0f) < 0.0001f);
    assert(fabsf(VitaMediaRules_firstLoadBrightness(4000000ULL) - 0.0f) < 0.0001f);

    puts("vita_media_rules_test: PASS");
    return 0;
}
