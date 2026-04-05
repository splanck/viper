//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestGifDecode.cpp
// Purpose: Unit tests for the GIF decoder using synthetic GIF data.
// Key invariants:
//   - Decoder accepts valid GIF87a/89a files
//   - Decoder rejects non-GIF data gracefully (returns 0)
//   - LZW decompression produces correct color indices
//   - Multi-frame GIFs produce correct frame count and per-frame delays
// Ownership/Lifetime:
//   - Test-scoped
// Links: src/runtime/graphics/rt_gif.c
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "runtime/graphics/rt_gif.h"
void *rt_pixels_load_gif(void *path);
int64_t rt_pixels_width(void *pixels);
int64_t rt_pixels_height(void *pixels);
int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);
int64_t rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
void *rt_const_cstr(const char *str);
}

static void free_pixels(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

/// @brief Write bytes to a temp file and return the path.
static const char *write_temp(const char *name, const uint8_t *data, size_t len) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/viper_test_%s", name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return nullptr;
    fwrite(data, 1, len, f);
    fclose(f);
    return path;
}

TEST(GifDecodeTest, RejectNull) {
    gif_frame_t *frames = nullptr;
    int count = 0, w = 0, h = 0;
    int rc = gif_decode_file(nullptr, &frames, &count, &w, &h);
    EXPECT_EQ(rc, 0);
}

TEST(GifDecodeTest, RejectNonExistent) {
    gif_frame_t *frames = nullptr;
    int count = 0, w = 0, h = 0;
    int rc = gif_decode_file("/tmp/nonexistent_gif_test.gif", &frames, &count, &w, &h);
    EXPECT_EQ(rc, 0);
}

TEST(GifDecodeTest, RejectGarbage) {
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    const char *path = write_temp("garbage.gif", garbage, sizeof(garbage));

    gif_frame_t *frames = nullptr;
    int count = 0, w = 0, h = 0;
    int rc = gif_decode_file(path, &frames, &count, &w, &h);
    EXPECT_EQ(rc, 0);
    remove(path);
}

TEST(GifDecodeTest, Minimal1x1Gif87a) {
    // Construct a minimal valid 1x1 GIF87a with a red pixel.
    // GIF87a header + 1-color GCT (red) + 1x1 image + LZW data + trailer
    uint8_t gif[] = {
        // Header: "GIF87a"
        'G',
        'I',
        'F',
        '8',
        '7',
        'a',
        // Logical screen descriptor: width=1, height=1, GCT flag=1, GCT size=0 (2 colors)
        0x01,
        0x00,
        0x01,
        0x00,
        0x80,
        0x00,
        0x00,
        // Global color table: 2 entries (red, black)
        0xFF,
        0x00,
        0x00, // color 0: red
        0x00,
        0x00,
        0x00, // color 1: black
        // Image descriptor: left=0, top=0, width=1, height=1, no LCT, not interlaced
        0x2C,
        0x00,
        0x00,
        0x00,
        0x00,
        0x01,
        0x00,
        0x01,
        0x00,
        0x00,
        // LZW min code size = 2
        0x02,
        // Sub-block: 2 bytes of LZW data (clear=4, index_0=0, end=5 packed LSB-first)
        0x02,
        0x44,
        0x01,
        // Block terminator
        0x00,
        // Trailer
        0x3B};

    const char *path = write_temp("minimal.gif", gif, sizeof(gif));

    gif_frame_t *frames = nullptr;
    int count = 0, w = 0, h = 0;
    int rc = gif_decode_file(path, &frames, &count, &w, &h);

    EXPECT_GT(rc, 0);
    if (rc > 0) {
        EXPECT_EQ(count, 1);
        EXPECT_EQ(w, 1);
        EXPECT_EQ(h, 1);

        // Check pixel: should be red (0xFF0000FF in RGBA)
        if (frames && frames[0].pixels) {
            int64_t pixel = rt_pixels_get(frames[0].pixels, 0, 0);
            uint8_t r = (uint8_t)((pixel >> 24) & 0xFF);
            EXPECT_EQ(r, 0xFF); // Red channel should be 0xFF
            free_pixels(frames[0].pixels);
        }
        free(frames);
    }
    remove(path);
}

TEST(GifDecodeTest, RejectPngAsGif) {
    // A PNG file should not decode as GIF
    const uint8_t png_sig[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 0};
    const char *path = write_temp("not_a_gif.png", png_sig, sizeof(png_sig));

    gif_frame_t *frames = nullptr;
    int count = 0, w = 0, h = 0;
    int rc = gif_decode_file(path, &frames, &count, &w, &h);
    EXPECT_EQ(rc, 0);
    remove(path);
}

int main() {
    return viper_test::run_all_tests();
}
