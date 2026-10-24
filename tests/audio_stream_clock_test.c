#include <assert.h>
#include <stdio.h>
#include "../src/butterscotch/src/audio_stream_clock.h"
#include "../src/butterscotch/vendor/stb/vorbis/stb_vorbis.c"

int main(int argc, char** argv) {
    assert(argc == 2);
    int error = 0;
    stb_vorbis* v = stb_vorbis_open_filename(argv[1], &error, NULL);
    assert(v);
    uint32_t length = stb_vorbis_stream_length_in_samples(v);
    int rate = stb_vorbis_get_info(v).sample_rate;
    assert(length > 0 && rate > 0);
    int channels, referenceRate;
    short* reference = NULL;
    int referenceLength = stb_vorbis_decode_filename(argv[1], &channels, &referenceRate, &reference);
    assert(referenceLength == length && referenceRate == rate && channels <= 2);
    // The same audible note must retain its chart position on every loop,
    // including when the current OpenAL buffer straddles the song boundary.
    for (int loop = 0; loop < 5; loop++) {
        uint64_t played = (uint64_t)loop * length + rate * 10;
        assert(AudioStream_positionSamples(played, 137, length, true) == rate * 10 + 137);
        assert(AudioStream_positionSamples(played, 137, length, false) == played + 137);
    }
    assert(AudioStream_positionSamples(length - 100, 150, length, true) == 50);
    // Real Vorbis seek must agree with the reported clock for positions from
    // the Vita log, even when a requested position exceeds the track length.
    double positions[] = {0, 76.924, 181.756, 400.0, -1, NAN};
    for (int i = 0; i < 6; i++) {
        uint32_t sample = AudioStream_seekSample(positions[i], rate, length, true);
        assert(sample < length);
        assert(stb_vorbis_seek(v, sample));
        short actual[64];
        int got = stb_vorbis_get_samples_short_interleaved(v, channels, actual, 32 * channels);
        assert(got > 0);
        for (int j = 0; j < got * channels; j++)
            assert(abs(actual[j] - reference[(size_t)sample * channels + j]) <= 1);
    }
    assert(AudioStream_seekSample(1e20, rate, length, false) == length - 1);
    assert(AudioStream_positionSamples(42, -1, 0, true) == 42);
    printf("AUDIO_STREAM_CLOCK=pass length_samples=%u seconds=%.6f loops=5 seeks=6\n",
           length, (double)length / rate);
    stb_vorbis_close(v);
    free(reference);
    return 0;
}
