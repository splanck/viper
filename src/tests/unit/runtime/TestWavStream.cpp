//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestWavStream.cpp
// Purpose: Regression tests for WAV streaming header parsing and float decode.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" {
#include "vaud_internal.h"
}

static void write_u16_le(FILE *f, uint16_t value) {
    uint8_t bytes[2] = {(uint8_t)(value & 0xFFu), (uint8_t)((value >> 8) & 0xFFu)};
    fwrite(bytes, 1, sizeof(bytes), f);
}

static void write_u32_le(FILE *f, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)(value & 0xFFu),
                        (uint8_t)((value >> 8) & 0xFFu),
                        (uint8_t)((value >> 16) & 0xFFu),
                        (uint8_t)((value >> 24) & 0xFFu)};
    fwrite(bytes, 1, sizeof(bytes), f);
}

static void put_u32_le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static bool write_float_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    const uint32_t channels = 2;
    const uint32_t sample_rate = 48000;
    const uint32_t bits_per_sample = 32;
    const uint32_t frame_count = 2;
    const uint32_t data_size = frame_count * channels * (bits_per_sample / 8);
    const uint32_t riff_size = 36 + data_size;
    const float samples[] = {0.25f, -0.25f, 1.0f, 0.0f};

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, 3);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, sample_rate * channels * (bits_per_sample / 8));
    write_u16_le(f, (uint16_t)(channels * (bits_per_sample / 8)));
    write_u16_le(f, (uint16_t)bits_per_sample);

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_size);
    fwrite(samples, sizeof(float), sizeof(samples) / sizeof(samples[0]), f);

    fclose(f);
    return true;
}

static bool write_float_nonfinite_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    const uint32_t channels = 1;
    const uint32_t sample_rate = 44100;
    const uint32_t bits_per_sample = 32;
    const uint32_t frame_count = 2;
    const uint32_t data_size = frame_count * channels * (bits_per_sample / 8);
    const uint32_t riff_size = 36 + data_size;
    const uint32_t samples[] = {0x7FC00000u, 0x7F800000u};

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, 3);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, sample_rate * channels * (bits_per_sample / 8));
    write_u16_le(f, (uint16_t)(channels * (bits_per_sample / 8)));
    write_u16_le(f, (uint16_t)bits_per_sample);

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_size);
    fwrite(samples, sizeof(uint32_t), sizeof(samples) / sizeof(samples[0]), f);

    fclose(f);
    return true;
}

static bool write_metadata_heavy_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    const uint32_t sample_rate = 22050;
    const uint32_t channels = 1;
    const uint32_t bits_per_sample = 16;
    const uint32_t metadata_size = 400;
    const int16_t samples[] = {1000, -1000};
    const uint32_t data_size = (uint32_t)sizeof(samples);
    const uint32_t riff_size = 4 + (8 + 16) + (8 + metadata_size) + (8 + data_size);
    uint8_t metadata[metadata_size];
    memset(metadata, 0xAB, sizeof(metadata));

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, 1);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, sample_rate * channels * (bits_per_sample / 8));
    write_u16_le(f, (uint16_t)(channels * (bits_per_sample / 8)));
    write_u16_le(f, (uint16_t)bits_per_sample);

    fwrite("JUNK", 1, 4, f);
    write_u32_le(f, metadata_size);
    fwrite(metadata, 1, sizeof(metadata), f);

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_size);
    fwrite(samples, 1, sizeof(samples), f);

    fclose(f);
    return true;
}

static bool write_pcm16_wav(const char *path, uint32_t frame_count) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    const uint32_t channels = 1;
    const uint32_t sample_rate = 44100;
    const uint32_t bits_per_sample = 16;
    const uint32_t data_size = frame_count * channels * (bits_per_sample / 8);
    const uint32_t riff_size = 36 + data_size;

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, 1);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, sample_rate * channels * (bits_per_sample / 8));
    write_u16_le(f, (uint16_t)(channels * (bits_per_sample / 8)));
    write_u16_le(f, (uint16_t)bits_per_sample);

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_size);
    int16_t sample = 0;
    for (uint32_t i = 0; i < frame_count; i++)
        fwrite(&sample, sizeof(sample), 1, f);

    fclose(f);
    return true;
}

static bool write_pcm16_wav_with_trailer(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    const uint32_t channels = 1;
    const uint32_t sample_rate = 44100;
    const uint32_t bits_per_sample = 16;
    const int16_t samples[] = {1234, -1234};
    const uint8_t trailer[8] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F};
    const uint32_t data_size = (uint32_t)sizeof(samples);
    const uint32_t riff_size = 4 + (8 + 16) + (8 + data_size) + (8 + sizeof(trailer));

    fwrite("RIFF", 1, 4, f);
    write_u32_le(f, riff_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    write_u32_le(f, 16);
    write_u16_le(f, 1);
    write_u16_le(f, (uint16_t)channels);
    write_u32_le(f, sample_rate);
    write_u32_le(f, sample_rate * channels * (bits_per_sample / 8));
    write_u16_le(f, (uint16_t)(channels * (bits_per_sample / 8)));
    write_u16_le(f, (uint16_t)bits_per_sample);

    fwrite("data", 1, 4, f);
    write_u32_le(f, data_size);
    fwrite(samples, 1, sizeof(samples), f);

    fwrite("JUNK", 1, 4, f);
    write_u32_le(f, sizeof(trailer));
    fwrite(trailer, 1, sizeof(trailer), f);

    fclose(f);
    return true;
}

TEST(WavStreamTest, FloatStreamDecodeUsesFormat) {
    const char *path = "/tmp/viper_test_float_stream.wav";
    ASSERT_TRUE(write_float_wav(path));

    void *file = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));
    EXPECT_TRUE(file != nullptr);
    EXPECT_EQ(sample_rate, 48000);
    EXPECT_EQ(channels, 2);
    EXPECT_EQ(bits, 32);
    EXPECT_EQ(format, 3);
    EXPECT_EQ(frames, 2);

    int16_t decoded[4] = {};
    ASSERT_EQ(vaud_wav_read_frames(file, decoded, 2, channels, bits, format), 2);
    EXPECT_NEAR(decoded[0], 8191, 256);
    EXPECT_NEAR(decoded[1], -8191, 256);
    EXPECT_NEAR(decoded[2], 32767, 1);
    EXPECT_NEAR(decoded[3], 0, 1);

    fclose((FILE *)file);
    remove(path);
}

TEST(WavStreamTest, MetadataHeavyHeaderScansPast256Bytes) {
    const char *path = "/tmp/viper_test_metadata_stream.wav";
    ASSERT_TRUE(write_metadata_heavy_wav(path));

    void *file = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));
    EXPECT_TRUE(file != nullptr);
    EXPECT_TRUE(data_offset > 256);
    EXPECT_EQ(frames, 2);
    EXPECT_EQ(sample_rate, 22050);
    EXPECT_EQ(channels, 1);
    EXPECT_EQ(bits, 16);
    EXPECT_EQ(format, 1);

    int16_t decoded[4] = {};
    ASSERT_EQ(vaud_wav_read_frames(file, decoded, 2, channels, bits, format), 2);
    EXPECT_EQ(decoded[0], 1000);
    EXPECT_EQ(decoded[1], 1000);
    EXPECT_EQ(decoded[2], -1000);
    EXPECT_EQ(decoded[3], -1000);

    fclose((FILE *)file);
    remove(path);
}

TEST(WavStreamTest, BufferedReaderMatchesAllocatingReader) {
    const char *path = "/tmp/viper_test_buffered_stream.wav";
    ASSERT_TRUE(write_float_wav(path));

    void *file_a = nullptr;
    void *file_b = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file_a, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));
    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file_b, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));

    int16_t allocated[4] = {};
    int16_t buffered[4] = {};
    uint8_t scratch[2 * 2 * sizeof(float)] = {};

    ASSERT_EQ(vaud_wav_read_frames(file_a, allocated, 2, channels, bits, format), 2);
    ASSERT_EQ(vaud_wav_read_frames_buffered(
                  file_b, buffered, 2, channels, bits, format, scratch, sizeof(scratch)),
              2);
    EXPECT_EQ(memcmp(allocated, buffered, sizeof(allocated)), 0);

    fclose((FILE *)file_a);
    fclose((FILE *)file_b);
    remove(path);
}

TEST(WavStreamTest, EmptyStreamSeekFailsInsteadOfPretendingBuffered) {
    const char *path = "/tmp/viper_test_empty_stream.wav";
    ASSERT_TRUE(write_pcm16_wav(path, 0));

    void *file = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));
    EXPECT_EQ(frames, 0);

    struct vaud_music music = {};
    int16_t decoded[VAUD_MUSIC_BUFFER_FRAMES * 2] = {};
    uint8_t scratch[VAUD_MUSIC_BUFFER_FRAMES * 2] = {};
    music.file = file;
    music.data_offset = data_offset;
    music.data_size = data_size;
    music.frame_count = 0;
    music.sample_rate = VAUD_SAMPLE_RATE;
    music.source_sample_rate = sample_rate;
    music.channels = channels;
    music.bits_per_sample = bits;
    music.audio_format = format;
    music.buffers[0] = decoded;
    music.wav_read_buf = scratch;
    music.wav_read_cap = sizeof(scratch);

    EXPECT_EQ(vaud_music_seek_output_frame(&music, 0), 0);

    fclose((FILE *)file);
    remove(path);
}

TEST(WavStreamTest, ExactEndSeekPositionsAtEof) {
    const char *path = "/tmp/viper_test_exact_end_seek.wav";
    ASSERT_TRUE(write_pcm16_wav(path, 4));

    void *file = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));

    struct vaud_music music = {};
    int16_t decoded[VAUD_MUSIC_BUFFER_FRAMES * 2] = {};
    uint8_t scratch[VAUD_MUSIC_BUFFER_FRAMES * 2] = {};
    music.file = file;
    music.data_offset = data_offset;
    music.data_size = data_size;
    music.frame_count = frames;
    music.sample_rate = VAUD_SAMPLE_RATE;
    music.source_sample_rate = sample_rate;
    music.channels = channels;
    music.bits_per_sample = bits;
    music.audio_format = format;
    music.buffers[0] = decoded;
    music.wav_read_buf = scratch;
    music.wav_read_cap = sizeof(scratch);

    EXPECT_EQ(vaud_music_seek_output_frame(&music, frames), 1);
    EXPECT_EQ(music.position, frames);
    EXPECT_EQ(music.buffer_frames[0], 0);
    EXPECT_EQ(music.stream_eof, 1);

    fclose((FILE *)file);
    remove(path);
}

TEST(WavStreamTest, MusicFillStopsAtWavDataChunk) {
    const char *path = "/tmp/viper_test_stream_trailer.wav";
    ASSERT_TRUE(write_pcm16_wav_with_trailer(path));

    void *file = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));
    EXPECT_EQ(frames, 2);

    struct vaud_music music = {};
    int16_t decoded[VAUD_MUSIC_BUFFER_FRAMES * 2] = {};
    uint8_t scratch[VAUD_MUSIC_BUFFER_FRAMES * 2] = {};
    music.file = file;
    music.data_offset = data_offset;
    music.data_size = data_size;
    music.frame_count = frames;
    music.sample_rate = VAUD_SAMPLE_RATE;
    music.source_sample_rate = sample_rate;
    music.channels = channels;
    music.bits_per_sample = bits;
    music.audio_format = format;
    music.buffers[0] = decoded;
    music.wav_read_buf = scratch;
    music.wav_read_cap = sizeof(scratch);

    EXPECT_EQ(vaud_music_fill_buffer(&music, 0), 2);
    EXPECT_EQ(decoded[0], 1234);
    EXPECT_EQ(decoded[1], 1234);
    EXPECT_EQ(decoded[2], -1234);
    EXPECT_EQ(decoded[3], -1234);
    EXPECT_EQ(vaud_music_fill_buffer(&music, 0), 0);

    fclose((FILE *)file);
    remove(path);
}

TEST(WavStreamTest, MalformedMemoryChunkSizeIsRejected) {
    uint8_t wav[44] = {};
    memcpy(wav + 0, "RIFF", 4);
    put_u32_le(wav + 4, 36);
    memcpy(wav + 8, "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    put_u32_le(wav + 16, 0xFFFFFF00u);

    int16_t *samples = nullptr;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    EXPECT_EQ(vaud_wav_load_mem(wav, sizeof(wav), &samples, &frames, &sample_rate, &channels), 0);
    EXPECT_TRUE(samples == nullptr);
}

TEST(WavStreamTest, NonFiniteFloatSamplesDecodeAsSilence) {
    const char *path = "/tmp/viper_test_nonfinite_float.wav";
    ASSERT_TRUE(write_float_nonfinite_wav(path));

    void *file = nullptr;
    int64_t data_offset = 0;
    int64_t data_size = 0;
    int64_t frames = 0;
    int32_t sample_rate = 0;
    int32_t channels = 0;
    int32_t bits = 0;
    int32_t format = 0;

    ASSERT_TRUE(vaud_wav_open_stream(
        path, &file, &data_offset, &data_size, &frames, &sample_rate, &channels, &bits, &format));
    EXPECT_EQ(frames, 2);
    EXPECT_EQ(format, 3);

    int16_t decoded[4] = {};
    ASSERT_EQ(vaud_wav_read_frames(file, decoded, 2, channels, bits, format), 2);
    EXPECT_EQ(decoded[0], 0);
    EXPECT_EQ(decoded[1], 0);
    EXPECT_EQ(decoded[2], 0);
    EXPECT_EQ(decoded[3], 0);

    fclose((FILE *)file);
    remove(path);
}

TEST(WavStreamTest, SoundPanUsesOriginalChannelSemantics) {
    struct vaud_context ctx = {};
    vaud_mutex_init(&ctx.mutex);
    ctx.master_volume = 1.0f;
    ctx.running = 1;
    ctx.next_voice_id = 1;

    int16_t samples[2] = {10000, 10000};
    struct vaud_sound sound = {};
    sound.samples = samples;
    sound.frame_count = 1;
    sound.sample_rate = VAUD_SAMPLE_RATE;
    sound.channels = VAUD_CHANNELS;
    sound.default_volume = 1.0f;

    int16_t out[2] = {};

    sound.source_channels = 1;
    ctx.voices[0].state = VAUD_VOICE_PLAYING;
    ctx.voices[0].sound = &sound;
    ctx.voices[0].volume = 1.0f;
    ctx.voices[0].pan = 0.0f;
    ctx.voices[0].id = 1;
    vaud_mixer_render(&ctx, out, 1);
    EXPECT_NEAR(out[0], 7070, 80);
    EXPECT_NEAR(out[1], 7070, 80);

    out[0] = 0;
    out[1] = 0;
    sound.source_channels = 2;
    ctx.voices[0].state = VAUD_VOICE_PLAYING;
    ctx.voices[0].sound = &sound;
    ctx.voices[0].position = 0;
    ctx.voices[0].volume = 1.0f;
    ctx.voices[0].pan = 0.0f;
    ctx.voices[0].id = 2;
    vaud_mixer_render(&ctx, out, 1);
    EXPECT_NEAR(out[0], 10000, 80);
    EXPECT_NEAR(out[1], 10000, 80);

    vaud_mutex_destroy(&ctx.mutex);
}

TEST(WavStreamTest, VoiceIdsWrapWithoutReusingActiveId) {
    struct vaud_context ctx = {};
    ctx.next_voice_id = INT32_MAX;

    vaud_voice *first = vaud_alloc_voice(&ctx);
    ASSERT_TRUE(first != nullptr);
    EXPECT_EQ(first->id, INT32_MAX);
    first->state = VAUD_VOICE_PLAYING;

    vaud_voice *second = vaud_alloc_voice(&ctx);
    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(second->id, 1);
    EXPECT_TRUE(second->id != first->id);
}

int main() {
    return viper_test::run_all_tests();
}
