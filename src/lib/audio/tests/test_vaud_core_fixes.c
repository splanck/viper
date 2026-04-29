//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "vaud_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ogg_reader ogg_reader_t;
typedef struct vorbis_decoder vorbis_decoder_t;
typedef struct mp3_stream mp3_stream_t;

typedef struct {
    uint32_t serial_number;
    int64_t granule_position;
    uint8_t bos;
    uint8_t eos;
} ogg_packet_info_t;

ogg_reader_t *ogg_reader_open_file(const char *path) {
    (void)path;
    return NULL;
}
void ogg_reader_free(ogg_reader_t *r) {
    (void)r;
}
void ogg_reader_rewind(ogg_reader_t *r) {
    (void)r;
}
int ogg_reader_next_packet_ex(ogg_reader_t *r,
                              const uint8_t **out_data,
                              size_t *out_len,
                              ogg_packet_info_t *out_info) {
    (void)r;
    (void)out_data;
    (void)out_len;
    (void)out_info;
    return 0;
}
vorbis_decoder_t *vorbis_decoder_new(void) {
    return NULL;
}
void vorbis_decoder_free(vorbis_decoder_t *dec) {
    (void)dec;
}
int vorbis_decode_header(vorbis_decoder_t *dec, const uint8_t *data, size_t len, int num) {
    (void)dec;
    (void)data;
    (void)len;
    (void)num;
    return -1;
}
int vorbis_decode_packet(
    vorbis_decoder_t *dec, const uint8_t *data, size_t len, int16_t **out_pcm, int *out_samples) {
    (void)dec;
    (void)data;
    (void)len;
    (void)out_pcm;
    (void)out_samples;
    return -1;
}
int vorbis_get_sample_rate(const vorbis_decoder_t *dec) {
    (void)dec;
    return 0;
}
int vorbis_get_channels(const vorbis_decoder_t *dec) {
    (void)dec;
    return 0;
}
mp3_stream_t *mp3_stream_open(const char *filepath) {
    (void)filepath;
    return NULL;
}
int mp3_stream_decode_frame(mp3_stream_t *stream, int16_t **out_pcm) {
    (void)stream;
    (void)out_pcm;
    return 0;
}
int mp3_stream_sample_rate(const mp3_stream_t *stream) {
    (void)stream;
    return 0;
}
int mp3_stream_channels(const mp3_stream_t *stream) {
    (void)stream;
    return 0;
}
int mp3_stream_total_samples(const mp3_stream_t *stream) {
    (void)stream;
    return 0;
}
void mp3_stream_rewind(mp3_stream_t *stream) {
    (void)stream;
}
void mp3_stream_free(mp3_stream_t *stream) {
    (void)stream;
}

int vaud_platform_init(vaud_context_t ctx) {
    (void)ctx;
    return 1;
}
void vaud_platform_shutdown(vaud_context_t ctx) {
    (void)ctx;
}
void vaud_platform_pause(vaud_context_t ctx) {
    (void)ctx;
}
void vaud_platform_resume(vaud_context_t ctx) {
    (void)ctx;
}
int64_t vaud_platform_now_ms(void) {
    return 0;
}

static int tests_failed = 0;

#define EXPECT_TRUE(expr)                                                                           \
    do {                                                                                            \
        if (!(expr)) {                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                                  \
            tests_failed++;                                                                         \
            return;                                                                                 \
        }                                                                                           \
    } while (0)

static void write_u16_le(FILE *f, uint16_t v) {
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
}

static void write_u32_le(FILE *f, uint32_t v) {
    fputc((int)(v & 0xFFu), f);
    fputc((int)((v >> 8) & 0xFFu), f);
    fputc((int)((v >> 16) & 0xFFu), f);
    fputc((int)((v >> 24) & 0xFFu), f);
}

static void write_u16_mem(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void write_u32_mem(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static size_t make_wav_mem(uint8_t *buf,
                           size_t cap,
                           uint16_t channels,
                           uint32_t sample_rate,
                           uint16_t bits_per_sample,
                           uint32_t byte_rate,
                           uint16_t block_align,
                           uint32_t data_size) {
    size_t total = 44u + (size_t)data_size;
    if (!buf || cap < total)
        return 0;

    memcpy(buf, "RIFF", 4);
    write_u32_mem(buf + 4, 36u + data_size);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    write_u32_mem(buf + 16, 16);
    write_u16_mem(buf + 20, 1);
    write_u16_mem(buf + 22, channels);
    write_u32_mem(buf + 24, sample_rate);
    write_u32_mem(buf + 28, byte_rate);
    write_u16_mem(buf + 32, block_align);
    write_u16_mem(buf + 34, bits_per_sample);
    memcpy(buf + 36, "data", 4);
    write_u32_mem(buf + 40, data_size);
    memset(buf + 44, 0, data_size);
    return total;
}

static int write_mono_wav(const char *path, int32_t sample_rate, int32_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;

    uint32_t data_bytes = (uint32_t)frames * sizeof(int16_t);
    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, 36u + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, 1);
    write_u16_le(f, 1);
    write_u32_le(f, (uint32_t)sample_rate);
    write_u32_le(f, (uint32_t)sample_rate * sizeof(int16_t));
    write_u16_le(f, sizeof(int16_t));
    write_u16_le(f, 16);
    fwrite("data", 1, 4, f);
    write_u32_le(f, data_bytes);
    for (int32_t i = 0; i < frames; i++)
        write_u16_le(f, (uint16_t)(int16_t)(i % 32767));

    int ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

static void make_temp_wav_path(char *path, size_t path_size, const char *tag) {
    const char *dir = getenv("TMPDIR");
    if (!dir || !dir[0])
        dir = getenv("TEMP");
    if (!dir || !dir[0])
        dir = ".";
    snprintf(path, path_size, "%s/viper_vaud_%s_%lu.wav", dir, tag, (unsigned long)rand());
}

static vaud_voice *find_voice_by_id(vaud_context_t ctx, vaud_voice_id id) {
    if (!ctx || id == VAUD_INVALID_VOICE)
        return NULL;

    for (int32_t i = 0; i < VAUD_MAX_VOICES; i++) {
        if (ctx->voices[i].id == id && ctx->voices[i].state != VAUD_VOICE_INACTIVE)
            return &ctx->voices[i];
    }
    return NULL;
}

static void test_destroy_detaches_loaded_sounds(void) {
    char path[128];
    make_temp_wav_path(path, sizeof(path), "sound");
    EXPECT_TRUE(write_mono_wav(path, 44100, 16));

    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);
    vaud_sound_t sound = vaud_load_sound(ctx, path);
    EXPECT_TRUE(sound != NULL);
    EXPECT_TRUE(vaud_sound_is_attached(sound));

    vaud_destroy(ctx);
    EXPECT_TRUE(!vaud_sound_is_attached(sound));
    vaud_free_sound(sound);
    remove(path);
}

static void test_music_seek_rejects_nan_without_touching_position(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    struct vaud_music music;
    memset(&music, 0, sizeof(music));
    music.ctx = ctx;
    music.sample_rate = VAUD_SAMPLE_RATE;
    music.position = 77;

    vaud_music_seek(&music, NAN);
    EXPECT_TRUE(music.position == 77);

    vaud_destroy(ctx);
}

static void test_playback_parameters_sanitize_nonfinite_values(void) {
    char path[128];
    make_temp_wav_path(path, sizeof(path), "params");
    EXPECT_TRUE(write_mono_wav(path, 44100, 16));

    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);
    vaud_sound_t sound = vaud_load_sound(ctx, path);
    EXPECT_TRUE(sound != NULL);

    vaud_set_master_volume(ctx, NAN);
    EXPECT_TRUE(vaud_get_master_volume(ctx) == 0.0f);
    vaud_set_master_volume(ctx, 2.0f);
    EXPECT_TRUE(vaud_get_master_volume(ctx) == 1.0f);

    vaud_voice_id id = vaud_play_ex(sound, NAN, INFINITY);
    EXPECT_TRUE(id != VAUD_INVALID_VOICE);
    vaud_voice *voice = find_voice_by_id(ctx, id);
    EXPECT_TRUE(voice != NULL);
    EXPECT_TRUE(voice->volume == 0.0f);
    EXPECT_TRUE(voice->pan == 0.0f);

    vaud_set_voice_volume(ctx, id, 2.0f);
    EXPECT_TRUE(voice->volume == 1.0f);
    vaud_set_voice_volume(ctx, id, -INFINITY);
    EXPECT_TRUE(voice->volume == 0.0f);

    vaud_set_voice_pan(ctx, id, 2.0f);
    EXPECT_TRUE(voice->pan == 1.0f);
    vaud_set_voice_pan(ctx, id, -2.0f);
    EXPECT_TRUE(voice->pan == -1.0f);
    vaud_set_voice_pan(ctx, id, NAN);
    EXPECT_TRUE(voice->pan == 0.0f);

    vaud_free_sound(sound);
    vaud_destroy(ctx);
    remove(path);
}

static void test_music_play_restarts_stopped_eof_stream(void) {
    char path[128];
    make_temp_wav_path(path, sizeof(path), "replay");
    EXPECT_TRUE(write_mono_wav(path, 44100, 64));

    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);
    vaud_music_t music = vaud_load_music(ctx, path);
    EXPECT_TRUE(music != NULL);

    vaud_music_set_volume(music, NAN);
    EXPECT_TRUE(music->volume == 0.0f);
    vaud_music_set_volume(music, 3.0f);
    EXPECT_TRUE(music->volume == 1.0f);

    music->state = VAUD_MUSIC_STOPPED;
    music->position = music->frame_count;
    music->stream_output_generated = music->frame_count;
    music->stream_eof = 1;
    music->source_position = music->data_size / (music->channels * (music->bits_per_sample / 8));
    for (int32_t i = 0; i < VAUD_MUSIC_BUFFER_COUNT; i++)
        music->buffer_frames[i] = 0;

    vaud_music_play(music, 0);
    EXPECT_TRUE(music->state == VAUD_MUSIC_PLAYING);
    EXPECT_TRUE(music->position == 0);
    EXPECT_TRUE(music->stream_output_generated > 0);
    EXPECT_TRUE(music->buffer_frames[0] > 0);

    vaud_free_music(music);
    vaud_destroy(ctx);
    remove(path);
}

static void test_wav_rejects_invalid_block_align_and_byte_rate(void) {
    uint8_t wav[128];
    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t rate = 0;
    int32_t channels = 0;

    size_t size = make_wav_mem(wav, sizeof(wav), 2, 44100, 16, 44100u * 4u, 2, 4);
    EXPECT_TRUE(size > 0);
    int ok = vaud_wav_load_mem(wav, size, &samples, &frames, &rate, &channels);
    if (ok)
        free(samples);
    EXPECT_TRUE(!ok);

    samples = NULL;
    size = make_wav_mem(wav, sizeof(wav), 2, 44100, 16, 1234, 4, 4);
    EXPECT_TRUE(size > 0);
    ok = vaud_wav_load_mem(wav, size, &samples, &frames, &rate, &channels);
    if (ok)
        free(samples);
    EXPECT_TRUE(!ok);
}

static void test_wav_rejects_partial_pcm_frame(void) {
    uint8_t wav[128];
    int16_t *samples = NULL;
    int64_t frames = 0;
    int32_t rate = 0;
    int32_t channels = 0;

    size_t size = make_wav_mem(wav, sizeof(wav), 2, 44100, 16, 44100u * 4u, 4, 3);
    EXPECT_TRUE(size > 0);
    int ok = vaud_wav_load_mem(wav, size, &samples, &frames, &rate, &channels);
    if (ok)
        free(samples);
    EXPECT_TRUE(!ok);
}

static void test_streaming_resample_preserves_total_frame_count(void) {
    char path[128];
    make_temp_wav_path(path, sizeof(path), "music");
    EXPECT_TRUE(write_mono_wav(path, 48000, 48000));

    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);
    vaud_music_t music = vaud_load_music(ctx, path);
    EXPECT_TRUE(music != NULL);
    EXPECT_TRUE(music->frame_count == 44100);
    EXPECT_TRUE(vaud_music_seek_output_frame(music, 0));

    int64_t total = music->buffer_frames[0];
    int32_t got = 0;
    while ((got = vaud_music_fill_buffer(music, 0)) > 0) {
        total += got;
    }

    EXPECT_TRUE(total == music->frame_count);

    vaud_free_music(music);
    vaud_destroy(ctx);
    remove(path);
}

int main(void) {
    srand(1);
    test_destroy_detaches_loaded_sounds();
    test_music_seek_rejects_nan_without_touching_position();
    test_playback_parameters_sanitize_nonfinite_values();
    test_music_play_restarts_stopped_eof_stream();
    test_wav_rejects_invalid_block_align_and_byte_rate();
    test_wav_rejects_partial_pcm_frame();
    test_streaming_resample_preserves_total_frame_count();

    if (tests_failed != 0)
        return 1;

    printf("test_vaud_core_fixes: PASS\n");
    return 0;
}
