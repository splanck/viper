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

struct mp3_stream {
    int sample_rate;
    int channels;
    int total_samples;
    int decoded_frames;
    int freed;
};

static mp3_stream_t g_fake_zero_total_mp3;
static int16_t g_fake_mp3_pcm[2] = {100, -100};
static vaud_context_t g_decode_probe_ctx = NULL;
static int g_decode_probe_saw_unlocked = 0;

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
    if (filepath && strcmp(filepath, "zero-total.mp3") == 0) {
        g_fake_zero_total_mp3.sample_rate = VAUD_SAMPLE_RATE;
        g_fake_zero_total_mp3.channels = 2;
        g_fake_zero_total_mp3.total_samples = 0;
        g_fake_zero_total_mp3.decoded_frames = 0;
        g_fake_zero_total_mp3.freed = 0;
        return &g_fake_zero_total_mp3;
    }
    return NULL;
}

int mp3_stream_decode_frame(mp3_stream_t *stream, int16_t **out_pcm) {
    if (out_pcm)
        *out_pcm = NULL;
    if (!stream || stream->decoded_frames++ > 0)
        return 0;
    if (g_decode_probe_ctx && vaud_mutex_trylock(&g_decode_probe_ctx->mutex)) {
        g_decode_probe_saw_unlocked = 1;
        vaud_mutex_unlock(&g_decode_probe_ctx->mutex);
    }
    if (out_pcm)
        *out_pcm = g_fake_mp3_pcm;
    return 1;
}

void mp3_stream_rewind(mp3_stream_t *stream) {
    if (stream)
        stream->decoded_frames = 0;
}

void mp3_stream_free(mp3_stream_t *stream) {
    if (stream)
        stream->freed = 1;
}

int mp3_stream_sample_rate(const mp3_stream_t *stream) {
    return stream ? stream->sample_rate : 0;
}

int mp3_stream_channels(const mp3_stream_t *stream) {
    return stream ? stream->channels : 0;
}

int mp3_stream_total_samples(const mp3_stream_t *stream) {
    return stream ? stream->total_samples : 0;
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

#define EXPECT_TRUE(expr)                                                                          \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                                 \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
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

static void test_music_seek_failure_stops_stream_and_clears_buffers(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    struct vaud_music music;
    memset(&music, 0, sizeof(music));
    music.ctx = ctx;
    music.sample_rate = VAUD_SAMPLE_RATE;
    music.state = VAUD_MUSIC_PLAYING;
    music.buffer_frames[0] = 32;
    music.buffer_frames[1] = 16;
    music.buffer_position = 7;
    music.current_buffer = 1;

    vaud_music_seek(&music, 1.0f);
    EXPECT_TRUE(music.state == VAUD_MUSIC_STOPPED);
    EXPECT_TRUE(music.stream_eof == 1);
    EXPECT_TRUE(music.buffer_frames[0] == 0);
    EXPECT_TRUE(music.buffer_frames[1] == 0);
    EXPECT_TRUE(music.buffer_position == 0);
    EXPECT_TRUE(music.current_buffer == 0);

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

static void test_mp3_music_allows_unknown_total_samples(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);
    vaud_music_t music = vaud_load_music_mp3(ctx, "zero-total.mp3");
    EXPECT_TRUE(music != NULL);
    EXPECT_TRUE(music->frame_count == 0);
    EXPECT_TRUE(music->state == VAUD_MUSIC_STOPPED);
    vaud_free_music(music);
    EXPECT_TRUE(g_fake_zero_total_mp3.freed == 1);
    vaud_destroy(ctx);
}

static void test_buffered_stream_read_rejects_partial_frame(void) {
    FILE *f = tmpfile();
    EXPECT_TRUE(f != NULL);
    uint8_t bytes[3] = {0x00, 0x00, 0x00};
    EXPECT_TRUE(fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes));
    rewind(f);

    uint8_t scratch[4];
    int16_t out[2] = {123, 456};
    int32_t frames = vaud_wav_read_frames_buffered(f, out, 1, 2, 16, 1, scratch, sizeof(scratch));
    EXPECT_TRUE(frames == 0);
    EXPECT_TRUE(out[0] == 123 && out[1] == 456);
    fclose(f);
}

static void test_mixer_does_not_decode_empty_music_buffers(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    struct vaud_music music;
    memset(&music, 0, sizeof(music));
    music.ctx = ctx;
    music.state = VAUD_MUSIC_PLAYING;
    music.loop = 0;
    music.stream_eof = 0;

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = &music;
    ctx->music_count = 1;
    vaud_mutex_unlock(&ctx->mutex);

    int16_t out[VAUD_CHANNELS * 8];
    memset(out, 0x7F, sizeof(out));
    vaud_mixer_render(ctx, out, 8);

    EXPECT_TRUE(music.stream_eof == 0);
    EXPECT_TRUE(music.state == VAUD_MUSIC_PLAYING);

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = NULL;
    ctx->music_count = 0;
    vaud_mutex_unlock(&ctx->mutex);
    vaud_destroy(ctx);
}

static void test_mixer_renders_oversized_requests_in_chunks(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    int32_t frames = VAUD_BUFFER_FRAMES + 8;
    int16_t *samples = (int16_t *)calloc((size_t)frames * VAUD_CHANNELS, sizeof(int16_t));
    int16_t *out = (int16_t *)calloc((size_t)frames * VAUD_CHANNELS, sizeof(int16_t));
    EXPECT_TRUE(samples != NULL && out != NULL);
    for (int32_t i = 0; i < frames; i++) {
        samples[i * 2] = 4000;
        samples[i * 2 + 1] = -4000;
    }

    struct vaud_sound sound;
    memset(&sound, 0, sizeof(sound));
    sound.ctx = ctx;
    sound.samples = samples;
    sound.frame_count = frames;
    sound.sample_rate = VAUD_SAMPLE_RATE;
    sound.channels = VAUD_CHANNELS;
    sound.source_channels = VAUD_CHANNELS;

    vaud_mutex_lock(&ctx->mutex);
    ctx->voices[0].state = VAUD_VOICE_PLAYING;
    ctx->voices[0].sound = &sound;
    ctx->voices[0].position = 0;
    ctx->voices[0].volume = 1.0f;
    ctx->voices[0].pan = 0.0f;
    ctx->voices[0].loop = 0;
    vaud_mutex_unlock(&ctx->mutex);

    vaud_mixer_render(ctx, out, frames);
    EXPECT_TRUE(out[0] != 0);
    EXPECT_TRUE(out[(size_t)VAUD_BUFFER_FRAMES * VAUD_CHANNELS] != 0);

    free(out);
    free(samples);
    vaud_destroy(ctx);
}

static void test_refill_in_progress_still_mixes_ready_current_buffer(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    struct vaud_music music;
    memset(&music, 0, sizeof(music));
    music.ctx = ctx;
    music.state = VAUD_MUSIC_PLAYING;
    music.refill_in_progress = 1;
    music.buffer_frames[0] = 1;
    music.volume = 1.0f;
    music.buffers[0] = (int16_t *)calloc(2, sizeof(int16_t));
    EXPECT_TRUE(music.buffers[0] != NULL);
    music.buffers[0][0] = 4096;
    music.buffers[0][1] = 4096;

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = &music;
    ctx->music_count = 1;
    vaud_mutex_unlock(&ctx->mutex);

    int16_t out[VAUD_CHANNELS] = {0, 0};
    vaud_mixer_render(ctx, out, 1);
    EXPECT_TRUE(out[0] != 0 && out[1] != 0);
    EXPECT_TRUE(music.buffer_position == 1);

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = NULL;
    ctx->music_count = 0;
    vaud_mutex_unlock(&ctx->mutex);
    free(music.buffers[0]);
    vaud_destroy(ctx);
}

static void test_mixer_skips_music_when_current_buffer_is_refilling(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    struct vaud_music music;
    memset(&music, 0, sizeof(music));
    music.ctx = ctx;
    music.state = VAUD_MUSIC_PLAYING;
    music.refill_in_progress = 1;
    music.buffer_refilling[0] = 1;
    music.buffer_frames[0] = 1;
    music.volume = 1.0f;
    music.buffers[0] = (int16_t *)calloc(2, sizeof(int16_t));
    EXPECT_TRUE(music.buffers[0] != NULL);
    music.buffers[0][0] = 4096;
    music.buffers[0][1] = 4096;

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = &music;
    ctx->music_count = 1;
    vaud_mutex_unlock(&ctx->mutex);

    int16_t out[VAUD_CHANNELS] = {123, 456};
    vaud_mixer_render(ctx, out, 1);
    EXPECT_TRUE(out[0] == 0 && out[1] == 0);
    EXPECT_TRUE(music.buffer_position == 0);

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = NULL;
    ctx->music_count = 0;
    vaud_mutex_unlock(&ctx->mutex);
    free(music.buffers[0]);
    vaud_destroy(ctx);
}

static void test_mixer_outputs_silence_when_context_paused(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    int16_t samples[VAUD_CHANNELS] = {8000, 8000};
    struct vaud_sound sound;
    memset(&sound, 0, sizeof(sound));
    sound.ctx = ctx;
    sound.samples = samples;
    sound.frame_count = 1;
    sound.sample_rate = VAUD_SAMPLE_RATE;
    sound.channels = VAUD_CHANNELS;
    sound.source_channels = VAUD_CHANNELS;

    vaud_mutex_lock(&ctx->mutex);
    ctx->voices[0].state = VAUD_VOICE_PLAYING;
    ctx->voices[0].sound = &sound;
    ctx->voices[0].volume = 1.0f;
    ctx->voices[0].pan = 0.0f;
    ctx->paused = 1;
    vaud_mutex_unlock(&ctx->mutex);

    int16_t out[VAUD_CHANNELS] = {111, 222};
    vaud_mixer_render(ctx, out, 1);
    EXPECT_TRUE(out[0] == 0 && out[1] == 0);

    vaud_destroy(ctx);
}

static void test_vaud_update_refills_without_holding_state_mutex(void) {
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    struct vaud_music music;
    memset(&music, 0, sizeof(music));
    music.ctx = ctx;
    music.format = 2;
    music.mp3_stream = &g_fake_zero_total_mp3;
    g_fake_zero_total_mp3.sample_rate = VAUD_SAMPLE_RATE;
    g_fake_zero_total_mp3.channels = VAUD_CHANNELS;
    g_fake_zero_total_mp3.total_samples = 0;
    g_fake_zero_total_mp3.decoded_frames = 0;
    music.sample_rate = VAUD_SAMPLE_RATE;
    music.source_sample_rate = VAUD_SAMPLE_RATE;
    music.channels = VAUD_CHANNELS;
    music.state = VAUD_MUSIC_PLAYING;
    music.volume = 1.0f;
    music.buffer_frames[0] = 1;
    music.buffers[0] = (int16_t *)calloc(2, sizeof(int16_t));
    music.buffers[1] = (int16_t *)calloc(2, sizeof(int16_t));
    EXPECT_TRUE(music.buffers[0] != NULL && music.buffers[1] != NULL);

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = &music;
    ctx->music_count = 1;
    vaud_mutex_unlock(&ctx->mutex);

    g_decode_probe_ctx = ctx;
    g_decode_probe_saw_unlocked = 0;
    vaud_update(ctx);
    g_decode_probe_ctx = NULL;

    EXPECT_TRUE(g_decode_probe_saw_unlocked == 1);
    EXPECT_TRUE(music.buffer_frames[1] == 1);
    EXPECT_TRUE(music.refill_in_progress == 0);
    EXPECT_TRUE(music.buffer_refilling[1] == 0);

    vaud_mutex_lock(&ctx->mutex);
    ctx->active_music[0] = NULL;
    ctx->music_count = 0;
    vaud_mutex_unlock(&ctx->mutex);
    free(music.buffers[0]);
    free(music.buffers[1]);
    vaud_destroy(ctx);
}

static void test_mixer_outputs_silence_when_state_lock_is_busy(void) {
#if !defined(VAUD_PLATFORM_WINDOWS)
    vaud_context_t ctx = vaud_create();
    EXPECT_TRUE(ctx != NULL);

    int16_t out[VAUD_CHANNELS * 4];
    for (size_t i = 0; i < sizeof(out) / sizeof(out[0]); i++)
        out[i] = 777;

    vaud_mutex_lock(&ctx->mutex);
    vaud_mixer_render(ctx, out, 4);
    vaud_mutex_unlock(&ctx->mutex);

    for (size_t i = 0; i < sizeof(out) / sizeof(out[0]); i++)
        EXPECT_TRUE(out[i] == 0);

    vaud_destroy(ctx);
#endif
}

int main(void) {
    srand(1);
    test_destroy_detaches_loaded_sounds();
    test_music_seek_rejects_nan_without_touching_position();
    test_music_seek_failure_stops_stream_and_clears_buffers();
    test_playback_parameters_sanitize_nonfinite_values();
    test_music_play_restarts_stopped_eof_stream();
    test_wav_rejects_invalid_block_align_and_byte_rate();
    test_wav_rejects_partial_pcm_frame();
    test_streaming_resample_preserves_total_frame_count();
    test_mp3_music_allows_unknown_total_samples();
    test_buffered_stream_read_rejects_partial_frame();
    test_mixer_does_not_decode_empty_music_buffers();
    test_mixer_renders_oversized_requests_in_chunks();
    test_refill_in_progress_still_mixes_ready_current_buffer();
    test_mixer_skips_music_when_current_buffer_is_refilling();
    test_mixer_outputs_silence_when_context_paused();
    test_vaud_update_refills_without_holding_state_mutex();
    test_mixer_outputs_silence_when_state_lock_is_busy();

    if (tests_failed != 0)
        return 1;

    printf("test_vaud_core_fixes: PASS\n");
    return 0;
}
