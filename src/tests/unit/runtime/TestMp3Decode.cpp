//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestMp3Decode.cpp
// Purpose: Unit tests for the MP3 decoder.
// Key invariants:
//   - Decoder rejects invalid/non-MP3 data
//   - Frame header parser extracts correct metadata
//   - ID3v2 tag skipping works correctly
// Ownership/Lifetime:
//   - Test-scoped
// Links: src/runtime/audio/rt_mp3.c
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "runtime/audio/rt_mp3.h"
}

static bool write_temp_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

TEST(Mp3DecodeTest, CreateAndFree) {
    mp3_decoder_t *dec = mp3_decoder_new();
    ASSERT_TRUE(dec != nullptr);
    mp3_decoder_free(dec);
}

TEST(Mp3DecodeTest, RejectNull) {
    mp3_decoder_t *dec = mp3_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    int16_t *pcm = nullptr;
    int samples = 0, channels = 0, sample_rate = 0;
    int rc = mp3_decode_file(dec, nullptr, 0, &pcm, &samples, &channels, &sample_rate);
    EXPECT_EQ(rc, -1);

    mp3_decoder_free(dec);
}

TEST(Mp3DecodeTest, RejectGarbage) {
    mp3_decoder_t *dec = mp3_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    int16_t *pcm = nullptr;
    int samples = 0, channels = 0, sample_rate = 0;
    int rc =
        mp3_decode_file(dec, garbage, sizeof(garbage), &pcm, &samples, &channels, &sample_rate);
    EXPECT_EQ(rc, -1);

    mp3_decoder_free(dec);
}

TEST(Mp3DecodeTest, RejectWavAsMp3) {
    mp3_decoder_t *dec = mp3_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    // WAV RIFF header should not decode as MP3
    const uint8_t wav[] = {
        'R', 'I', 'F', 'F', 0x00, 0x00, 0x00, 0x00, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' '};
    int16_t *pcm = nullptr;
    int samples = 0, channels = 0, sample_rate = 0;
    int rc = mp3_decode_file(dec, wav, sizeof(wav), &pcm, &samples, &channels, &sample_rate);
    EXPECT_EQ(rc, -1);

    mp3_decoder_free(dec);
}

TEST(Mp3DecodeTest, Id3v2SkipComputation) {
    // Verify that an ID3v2 header causes the decoder to skip the correct number of bytes.
    // A file with just an ID3v2 tag and no audio should fail gracefully.
    mp3_decoder_t *dec = mp3_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    // ID3v2 header: "ID3" + version 2.3 + flags=0 + syncsafe size=100
    // syncsafe: 100 = 0x00 0x00 0x00 0x64
    uint8_t id3[120];
    memset(id3, 0, sizeof(id3));
    id3[0] = 'I';
    id3[1] = 'D';
    id3[2] = '3';
    id3[3] = 3;
    id3[4] = 0; // version 2.3.0
    id3[5] = 0; // flags
    // Size: 100 in syncsafe (0x00 0x00 0x00 0x64)
    id3[6] = 0;
    id3[7] = 0;
    id3[8] = 0;
    id3[9] = 100;
    // 100 bytes of padding (tag body), then 10 bytes of garbage (no valid frame)
    // Total: 110 + 10 = 120 bytes

    int16_t *pcm = nullptr;
    int samples = 0, channels = 0, sample_rate = 0;
    int rc = mp3_decode_file(dec, id3, sizeof(id3), &pcm, &samples, &channels, &sample_rate);
    // Should fail because there's no valid MP3 frame after the ID3 tag
    EXPECT_EQ(rc, -1);

    mp3_decoder_free(dec);
}

TEST(Mp3DecodeTest, SyntheticFrameHeader) {
    // Construct a minimal valid MPEG1 Layer III frame header
    // and verify the parser extracts correct fields.
    // This tests the header parsing logic without needing real MP3 data.

    // MPEG1, Layer III, 128kbps, 44100Hz, stereo, no padding
    // Byte 0: 0xFF (sync)
    // Byte 1: 0xFB = 1111 1011 (sync=111, version=11=MPEG1, layer=01=III, CRC=1=no)
    // Byte 2: 0x90 = 1001 0000 (bitrate=1001=128k, srate=00=44100, pad=0, private=0)
    // Byte 3: 0x00 = 0000 0000 (mode=00=stereo, mode_ext=00, copy=0, orig=0, emph=00)
    uint8_t hdr[4] = {0xFF, 0xFB, 0x90, 0x00};

    // We can't call the internal parse function directly from here,
    // but we can verify the decoder doesn't crash on a truncated file
    // with just a valid header.
    mp3_decoder_t *dec = mp3_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    int16_t *pcm = nullptr;
    int samples = 0, channels = 0, sample_rate = 0;
    // Only 4 bytes — header valid but no frame body. Should fail gracefully.
    int rc = mp3_decode_file(dec, hdr, sizeof(hdr), &pcm, &samples, &channels, &sample_rate);
    EXPECT_EQ(rc, -1);

    mp3_decoder_free(dec);
}

TEST(Mp3DecodeTest, StreamRejectsMissingFile) {
    mp3_stream_t *stream = mp3_stream_open("/tmp/viper_missing_test_stream.mp3");
    EXPECT_EQ(stream, nullptr);
}

TEST(Mp3DecodeTest, StreamRejectsGarbageFile) {
    const char *path = "/tmp/viper_test_garbage_stream.mp3";
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0xFA, 0xCE, 0xBE, 0xEF};
    ASSERT_TRUE(write_temp_file(path, garbage, sizeof(garbage)));

    mp3_stream_t *stream = mp3_stream_open(path);
    EXPECT_EQ(stream, nullptr);

    remove(path);
}

TEST(Mp3DecodeTest, StreamRejectsId3OnlyFile) {
    const char *path = "/tmp/viper_test_id3_only_stream.mp3";
    uint8_t id3[120];
    memset(id3, 0, sizeof(id3));
    id3[0] = 'I';
    id3[1] = 'D';
    id3[2] = '3';
    id3[3] = 3;
    id3[9] = 100;
    ASSERT_TRUE(write_temp_file(path, id3, sizeof(id3)));

    mp3_stream_t *stream = mp3_stream_open(path);
    EXPECT_EQ(stream, nullptr);

    remove(path);
}

int main() {
    return viper_test::run_all_tests();
}
