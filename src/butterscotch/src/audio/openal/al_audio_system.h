#ifndef _BS_AL_AUDIO_SYSTEM_H_
#define _BS_AL_AUDIO_SYSTEM_H_

#include "common.h"
#include "audio_system.h"
#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#define MAX_SOUND_INSTANCES 128
#define SOUND_INSTANCE_ID_BASE 100000
#define MAX_AUDIO_STREAMS 32
#define MAX_SFX_BUFFER_CACHE 256
// This is the index space that the native runner uses
#define AUDIO_STREAM_INDEX_BASE 300000

// Keep roughly six seconds of music queued, but decode smaller pieces. The
// 4096-sample refill produced recurring 4-5 ms spikes while walking through
// Chapter 5 town.  The Chapter 2 logs also contain 2-6 second room changes;
// a 64-block queue could drain while the main thread was constructing the
// destination, making an unchanged song stop and repeat its last section.
// 128 small blocks preserve the transition-safe duration without increasing
// the per-frame decode burst.
#define AL_STREAM_BUFFER_COUNT 192
#define AL_STREAM_BUFFER_SAMPLES 2048

struct stb_vorbis;

typedef struct {
    bool active;
    int32_t soundIndex; // SOND resource that spawned this
    int32_t instanceId; // unique ID returned to GML
    ALuint alSource; // OpenAL source object
    ALuint alBuffer; // OpenAL buffer object (only valid when streaming == false)
    float targetGain;
    float currentGain;
    float fadeTimeRemaining;
    float fadeTotalTime;
    float startGain;
    int32_t priority;
    bool music;
    bool sharedBuffer;
    // Track host-side holds independently so ended one-shot effects are not
    // restarted when a room transition or settings overlay releases audio.
    bool pausedByDisabled;
    bool pausedByTransition;
    // A room-end script requested that this music stop while the destination
    // room was still under construction. Resolve it after the next room has
    // had a chance to request its own music.
    bool pendingTransitionStop;
    // Preserve the audible state of an outgoing BGM while Room End executes.
    // Deltarune commonly fades/stops the global stream before Room Start can
    // decide whether the destination uses the exact same track.
    float transitionSavedGain;
    bool transitionSnapshotValid;
    bool releaseAfterFade;

    // Streaming state (only valid when streaming == true)
    bool streaming;
    bool loop;
    bool streamEnded; // decoder produced no more samples; waiting for queue to drain
    struct stb_vorbis* vorbis;
    uint8_t* streamCompressedData;
    size_t streamCompressedSize;
    ALuint streamBuffers[AL_STREAM_BUFFER_COUNT];
    int streamPrimedCount;
    int streamBufferTarget;
    int16_t* decodeScratch; // sized for AL_STREAM_BUFFER_SAMPLES * streamChannels shorts
    int streamChannels;
    int streamSampleRate;
    ALenum streamFormat;
    float streamLengthSeconds;
    uint32_t streamLengthSamples;
    uint64_t playedSamples; // cumulative per-channel samples that have left the queue
} SoundInstance;

typedef struct {
    bool active;
    bool pendingDestroy;
    char* filePath; // resolved file path (owned, freed on destroy)
} AudioStreamEntry;

typedef struct {
    bool active;
    int32_t soundIndex;
    ALuint buffer;
} SfxBufferCacheEntry;

typedef struct {
    AudioSystem base;
    ALCdevice* alDevice;
    ALCcontext* alContext;
    SoundInstance instances[MAX_SOUND_INSTANCES];
    int32_t nextInstanceCounter;
    FileSystem* fileSystem;
    AudioStreamEntry streams[MAX_AUDIO_STREAMS];
    SfxBufferCacheEntry sfxBufferCache[MAX_SFX_BUFFER_CACHE];
    float musicGain;
    float sfxGain;
    bool disabled;
    // Queue new sounds during a room transition without submitting them to
    // the Vita playback thread until the destination has presented a frame.
    bool transitionHold;
#ifdef PLATFORM_VITA
    bool videoAudioHandoff;
    int videoResumeCount;
    struct {
        int32_t soundIndex;
        int32_t priority;
        bool loop;
        float gain;
        float positionSeconds;
    } videoResumeMusic[4];
#endif
} AlAudioSystem;

AlAudioSystem* AlAudioSystem_create(void);
void AlAudioSystem_setCategoryGains(AlAudioSystem* audio, float musicGain, float sfxGain);
void AlAudioSystem_setDisabled(AlAudioSystem* audio, bool disabled);
void AlAudioSystem_setTransitionHold(AlAudioSystem* audio, bool held);
#ifdef PLATFORM_VITA
bool AlAudioSystem_beginVideoHandoff(AlAudioSystem* audio);
bool AlAudioSystem_endVideoHandoff(AlAudioSystem* audio);
#endif
uint32_t AlAudioSystem_preloadChapterSfx(AlAudioSystem* audio, bool preloadBuffers);
uint32_t AlAudioSystem_preloadBattleIntroSfx(AlAudioSystem* audio);

#endif /* _BS_AL_AUDIO_SYSTEM_H_ */
