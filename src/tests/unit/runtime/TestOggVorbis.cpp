//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestOggVorbis.cpp
// Purpose: Unit tests for the OGG container parser and Vorbis decoder.
// Key invariants:
//   - OGG reader correctly rejects non-OGG data
//   - Vorbis decoder accepts valid identification headers
//   - Vorbis decoder rejects malformed headers
// Ownership/Lifetime:
//   - Test-scoped
// Links: src/runtime/audio/rt_ogg.c, src/runtime/audio/rt_vorbis.c
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "runtime/audio/rt_ogg.h"
#include "runtime/audio/rt_vorbis.h"
}

TEST(OggReaderTest, RejectNullPath) {
    ogg_reader_t *r = ogg_reader_open_file(nullptr);
    EXPECT_EQ(r, nullptr);
}

TEST(OggReaderTest, RejectNonExistent) {
    ogg_reader_t *r = ogg_reader_open_file("/tmp/nonexistent_ogg_file.ogg");
    EXPECT_EQ(r, nullptr);
}

TEST(OggReaderTest, RejectInvalidData) {
    // Create a file with garbage data
    const char *path = "/tmp/viper_test_invalid.ogg";
    FILE *f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    fwrite(garbage, 1, sizeof(garbage), f);
    fclose(f);

    ogg_reader_t *r = ogg_reader_open_file(path);
    ASSERT_TRUE(r != nullptr);

    const uint8_t *pkt;
    size_t pkt_len;
    int got = ogg_reader_next_packet(r, &pkt, &pkt_len);
    EXPECT_EQ(got, 0); // Should fail to find OggS marker

    ogg_reader_free(r);
    remove(path);
}

TEST(OggReaderTest, MemReaderNullReject) {
    ogg_reader_t *r = ogg_reader_open_mem(nullptr, 0);
    EXPECT_EQ(r, nullptr);
}

TEST(VorbisDecoderTest, CreateAndFree) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    ASSERT_TRUE(dec != nullptr);
    EXPECT_EQ(vorbis_get_sample_rate(dec), 0);
    EXPECT_EQ(vorbis_get_channels(dec), 0);
    vorbis_decoder_free(dec);
}

TEST(VorbisDecoderTest, RejectInvalidHeader) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    // Feed garbage as identification header
    const uint8_t garbage[] = {0x00, 0x01, 0x02};
    int rc = vorbis_decode_header(dec, garbage, sizeof(garbage), 0);
    EXPECT_EQ(rc, -1);

    vorbis_decoder_free(dec);
}

TEST(VorbisDecoderTest, AcceptValidIdentHeader) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    // Construct a minimal valid Vorbis identification header
    // Byte layout: [type=1]["vorbis"][version=0][channels=2][sample_rate=44100]
    //              [bitrate_max=0][bitrate_nom=0][bitrate_min=0]
    //              [blocksize_0/1=8/11 packed][framing=1]
    uint8_t ident[30];
    memset(ident, 0, sizeof(ident));
    ident[0] = 1; // packet type = identification
    memcpy(ident + 1, "vorbis", 6);
    // version = 0 (bytes 7-10, LE)
    ident[11] = 2; // channels
    // sample rate = 44100 = 0x0000AC44
    ident[12] = 0x44;
    ident[13] = 0xAC;
    ident[14] = 0x00;
    ident[15] = 0x00;
    // bitrates (16-27): all zero
    // blocksize_0 = 8 (256 samples), blocksize_1 = 11 (2048 samples)
    ident[28] = (11 << 4) | 8; // packed: high nibble = bs1, low nibble = bs0
    ident[29] = 1; // framing bit

    int rc = vorbis_decode_header(dec, ident, sizeof(ident), 0);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(vorbis_get_sample_rate(dec), 44100);
    EXPECT_EQ(vorbis_get_channels(dec), 2);

    vorbis_decoder_free(dec);
}

TEST(VorbisDecoderTest, RejectDecodeWithoutSetup) {
    vorbis_decoder_t *dec = vorbis_decoder_new();
    ASSERT_TRUE(dec != nullptr);

    // Try to decode audio without having parsed headers
    const uint8_t pkt[] = {0x00, 0x01};
    int16_t *pcm = nullptr;
    int samples = 0;
    int rc = vorbis_decode_packet(dec, pkt, sizeof(pkt), &pcm, &samples);
    EXPECT_EQ(rc, -1);

    vorbis_decoder_free(dec);
}

int main() {
    return viper_test::run_all_tests();
}
