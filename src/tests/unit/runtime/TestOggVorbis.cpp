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
#include "tests/common/PosixCompat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "runtime/audio/rt_ogg.h"
#include "runtime/audio/rt_vorbis.h"
void *rt_videoplayer_open(void *path);
void rt_videoplayer_play(void *vp);
void rt_videoplayer_pause(void *vp);
void rt_videoplayer_stop(void *vp);
void rt_videoplayer_seek(void *vp, double seconds);
void rt_videoplayer_update(void *vp, double dt);
int64_t rt_videoplayer_get_width(void *vp);
int64_t rt_videoplayer_get_height(void *vp);
double rt_videoplayer_get_duration(void *vp);
double rt_videoplayer_get_position(void *vp);
int64_t rt_videoplayer_get_is_playing(void *vp);
void *rt_videoplayer_get_frame(void *vp);
void *rt_const_cstr(const char *s);
int64_t rt_pixels_width(void *pixels);
int64_t rt_pixels_height(void *pixels);
int64_t rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}

static void append_u32_le(std::vector<uint8_t> &out, uint32_t v) {
    out.push_back((uint8_t)(v & 0xFF));
    out.push_back((uint8_t)((v >> 8) & 0xFF));
    out.push_back((uint8_t)((v >> 16) & 0xFF));
    out.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void append_i64_le(std::vector<uint8_t> &out, int64_t v) {
    uint64_t u = (uint64_t)v;
    for (int i = 0; i < 8; i++)
        out.push_back((uint8_t)((u >> (i * 8)) & 0xFF));
}

static void append_ogg_page(std::vector<uint8_t> &out,
                            uint8_t header_type,
                            int64_t granule_position,
                            uint32_t serial,
                            uint32_t sequence,
                            const std::vector<uint8_t> &lacing,
                            const std::vector<uint8_t> &body) {
    out.push_back('O');
    out.push_back('g');
    out.push_back('g');
    out.push_back('S');
    out.push_back(0);
    out.push_back(header_type);
    append_i64_le(out, granule_position);
    append_u32_le(out, serial);
    append_u32_le(out, sequence);
    append_u32_le(out, 0); // checksum intentionally left zero; reader is permissive
    out.push_back((uint8_t)lacing.size());
    out.insert(out.end(), lacing.begin(), lacing.end());
    out.insert(out.end(), body.begin(), body.end());
}

static void append_single_packet_page(std::vector<uint8_t> &out,
                                      uint8_t header_type,
                                      int64_t granule_position,
                                      uint32_t serial,
                                      uint32_t sequence,
                                      const std::vector<uint8_t> &packet) {
    std::vector<uint8_t> lacing;
    size_t remaining = packet.size();
    while (remaining >= 255) {
        lacing.push_back(255);
        remaining -= 255;
    }
    lacing.push_back((uint8_t)remaining);
    append_ogg_page(out, header_type, granule_position, serial, sequence, lacing, packet);
}

static std::vector<uint8_t> make_theora_ident_packet() {
    std::vector<uint8_t> packet(42, 0);
    packet[0] = 0x80;
    memcpy(packet.data() + 1, "theora", 6);
    packet[7] = 3;
    packet[8] = 2;
    packet[9] = 1;
    packet[11] = 1; // frame width in 16px blocks
    packet[13] = 1; // frame height in 16px blocks
    packet[16] = 16; // visible picture width
    packet[19] = 16; // visible picture height
    packet[25] = 24; // fps numerator
    packet[29] = 1;  // fps denominator
    packet[33] = 1;  // aspect numerator
    packet[37] = 1;  // aspect denominator
    return packet;
}

static std::vector<uint8_t> make_theora_comment_packet() {
    std::vector<uint8_t> packet(7, 0);
    packet[0] = 0x81;
    memcpy(packet.data() + 1, "theora", 6);
    return packet;
}

static std::vector<uint8_t> make_theora_setup_packet() {
    std::vector<uint8_t> packet(7, 0);
    packet[0] = 0x82;
    memcpy(packet.data() + 1, "theora", 6);
    return packet;
}

static std::vector<uint8_t> make_dummy_packet(const char *text) {
    return std::vector<uint8_t>(text, text + strlen(text));
}

static std::vector<uint8_t> make_synthetic_ogv() {
    const uint32_t theora_serial = 0x11111111u;
    const uint32_t other_serial = 0x22222222u;
    std::vector<uint8_t> out;
    append_single_packet_page(out, 0x02, 0, theora_serial, 0, make_theora_ident_packet());
    append_single_packet_page(out, 0x02, 0, other_serial, 0, make_dummy_packet("vorbis-bos"));
    append_single_packet_page(out, 0x00, 0, theora_serial, 1, make_theora_comment_packet());
    append_single_packet_page(out, 0x00, 0, theora_serial, 2, make_theora_setup_packet());
    append_single_packet_page(out, 0x00, 0, other_serial, 1, make_dummy_packet("vorbis-data"));
    append_single_packet_page(out, 0x00, 0, theora_serial, 3, std::vector<uint8_t>{0x00});
    append_single_packet_page(out, 0x04, 1, theora_serial, 4, std::vector<uint8_t>{0x00});
    return out;
}

static std::string write_temp_file(const char *prefix, const std::vector<uint8_t> &bytes) {
    char path[] = "/tmp/viper_ogv_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return {};
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
        remove(path);
        return {};
    }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    std::string final_path = std::string(path) + prefix;
    remove(final_path.c_str());
    rename(path, final_path.c_str());
    return final_path;
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

TEST(OggReaderTest, InterleavedStreamsPreservePacketMetadata) {
    const uint32_t serial_a = 0xABCDEF01u;
    const uint32_t serial_b = 0x10203040u;
    std::vector<uint8_t> bytes;

    append_single_packet_page(bytes, 0x02, 0, serial_a, 0, make_dummy_packet("A0"));
    append_single_packet_page(bytes, 0x02, 0, serial_b, 0, make_dummy_packet("B0"));

    std::vector<uint8_t> long_packet(300, 'X');
    append_ogg_page(bytes,
                    0x00,
                    -1,
                    serial_a,
                    1,
                    std::vector<uint8_t>{255},
                    std::vector<uint8_t>(long_packet.begin(), long_packet.begin() + 255));
    append_single_packet_page(bytes, 0x00, 5, serial_b, 1, make_dummy_packet("B1"));
    append_ogg_page(bytes,
                    0x05,
                    123,
                    serial_a,
                    2,
                    std::vector<uint8_t>{45},
                    std::vector<uint8_t>(long_packet.begin() + 255, long_packet.end()));

    ogg_reader_t *r = ogg_reader_open_mem(bytes.data(), bytes.size());
    ASSERT_TRUE(r != nullptr);

    const uint8_t *pkt = nullptr;
    size_t pkt_len = 0;
    ogg_packet_info_t info = {};

    ASSERT_TRUE(ogg_reader_next_packet_ex(r, &pkt, &pkt_len, &info));
    EXPECT_EQ(info.serial_number, serial_a);
    EXPECT_EQ(info.bos, 1);
    EXPECT_EQ(std::string((const char *)pkt, pkt_len), "A0");

    ASSERT_TRUE(ogg_reader_next_packet_ex(r, &pkt, &pkt_len, &info));
    EXPECT_EQ(info.serial_number, serial_b);
    EXPECT_EQ(info.bos, 1);
    EXPECT_EQ(std::string((const char *)pkt, pkt_len), "B0");

    ASSERT_TRUE(ogg_reader_next_packet_ex(r, &pkt, &pkt_len, &info));
    EXPECT_EQ(info.serial_number, serial_b);
    EXPECT_EQ(info.granule_position, 5);
    EXPECT_EQ(std::string((const char *)pkt, pkt_len), "B1");

    ASSERT_TRUE(ogg_reader_next_packet_ex(r, &pkt, &pkt_len, &info));
    EXPECT_EQ(info.serial_number, serial_a);
    EXPECT_EQ(info.granule_position, 123);
    EXPECT_EQ(info.eos, 1);
    EXPECT_EQ(pkt_len, long_packet.size());
    EXPECT_EQ(memcmp(pkt, long_packet.data(), long_packet.size()), 0);

    ogg_reader_free(r);
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

TEST(VideoPlayerTest, OpenSyntheticOgvAndAdvanceFrames) {
    std::vector<uint8_t> bytes = make_synthetic_ogv();
    std::string path = write_temp_file(".ogv", bytes);
    ASSERT_FALSE(path.empty());

    void *rts_path = rt_const_cstr(path.c_str());
    ASSERT_TRUE(rts_path != nullptr);

    void *player = rt_videoplayer_open(rts_path);
    ASSERT_TRUE(player != nullptr);
    EXPECT_EQ(rt_videoplayer_get_width(player), 16);
    EXPECT_EQ(rt_videoplayer_get_height(player), 16);
    EXPECT_TRUE(rt_videoplayer_get_duration(player) > 0.08);
    EXPECT_TRUE(rt_videoplayer_get_duration(player) < 0.09);

    void *frame = rt_videoplayer_get_frame(player);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_EQ(rt_pixels_width(frame), 16);
    EXPECT_EQ(rt_pixels_height(frame), 16);

    rt_videoplayer_play(player);
    EXPECT_EQ(rt_videoplayer_get_is_playing(player), 1);

    rt_videoplayer_update(player, 1.0 / 24.0);
    EXPECT_TRUE(rt_videoplayer_get_position(player) > 0.04);
    EXPECT_EQ(rt_videoplayer_get_is_playing(player), 1);

    rt_videoplayer_update(player, 1.0 / 24.0);
    EXPECT_EQ(rt_videoplayer_get_is_playing(player), 0);
    EXPECT_TRUE(rt_videoplayer_get_position(player) >= rt_videoplayer_get_duration(player));

    rt_videoplayer_seek(player, 0.0);
    EXPECT_TRUE(rt_videoplayer_get_position(player) <= 0.0001);
    EXPECT_TRUE(rt_videoplayer_get_frame(player) != nullptr);

    rt_videoplayer_stop(player);
    if (rt_obj_release_check0(player))
        rt_obj_free(player);
    remove(path.c_str());
}

int main() {
    return viper_test::run_all_tests();
}
