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

#include <cstdio>
#include <cstdint>
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

    ASSERT_TRUE(vaud_wav_open_stream(path,
                                     &file,
                                     &data_offset,
                                     &data_size,
                                     &frames,
                                     &sample_rate,
                                     &channels,
                                     &bits,
                                     &format));
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

    ASSERT_TRUE(vaud_wav_open_stream(path,
                                     &file,
                                     &data_offset,
                                     &data_size,
                                     &frames,
                                     &sample_rate,
                                     &channels,
                                     &bits,
                                     &format));
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

int main() {
    return viper_test::run_all_tests();
}
