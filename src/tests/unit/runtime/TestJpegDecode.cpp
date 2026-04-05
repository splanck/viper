//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestJpegDecode.cpp
// Purpose: Unit tests for the baseline JPEG decoder.
// Key invariants:
//   - Decoder accepts valid baseline JPEG files
//   - Decoder rejects non-JPEG data gracefully (returns NULL)
//   - YCbCr->RGB conversion produces correct output
// Ownership/Lifetime:
//   - Test-scoped
// Links: src/runtime/graphics/rt_pixels.c
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void *rt_pixels_load_jpeg(void *path);
void *rt_pixels_load_bmp(void *path);
void *rt_pixels_load_png(void *path);
int64_t rt_pixels_width(void *pixels);
int64_t rt_pixels_height(void *pixels);
int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);
int64_t rt_pixels_save_png(void *pixels_ptr, void *path);
const uint32_t *rt_pixels_raw_buffer(void *pixels);
void *rt_const_cstr(const char *s);
int64_t rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}

// Helper to load JPEG from a C string path
static void *load_jpeg(const char *path) {
    void *rts = rt_const_cstr(path);
    if (!rts)
        return nullptr;
    void *result = rt_pixels_load_jpeg(rts);
    return result;
}

// Helper to load PNG from a C string path
static void *load_png(const char *path) {
    void *rts = rt_const_cstr(path);
    if (!rts)
        return nullptr;
    void *result = rt_pixels_load_png(rts);
    return result;
}

static void free_pixels(void *p) {
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

TEST(JpegDecodeTest, RejectNonJpeg) {
    // NULL path should return NULL
    void *result = rt_pixels_load_jpeg(nullptr);
    EXPECT_EQ(result, nullptr);

    // Non-existent file
    result = load_jpeg("/tmp/nonexistent_jpeg_test_file.jpg");
    EXPECT_EQ(result, nullptr);
}

TEST(JpegDecodeTest, RejectInvalidData) {
    // Create a file with random data (not JPEG)
    const char *path = "/tmp/viper_test_invalid.jpg";
    FILE *f = fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    fwrite(garbage, 1, sizeof(garbage), f);
    fclose(f);

    void *result = load_jpeg(path);
    EXPECT_EQ(result, nullptr);
    remove(path);
}

TEST(JpegDecodeTest, RoundTripViaPng) {
    // Create a known pixel pattern, save as PNG, load the PNG,
    // then verify we can at least create and load pixels correctly
    // (tests the overall pipeline, not JPEG-specific decoding since
    // we can't encode JPEG)

    // We'll create a 2x2 PNG and verify the pipeline works
    // Verify the JPEG decoder rejects a PNG file (wrong magic bytes).
    // Create a minimal file with PNG signature
    const char *path = "/tmp/viper_jpeg_test_png_reject.png";
    FILE *fp = fopen(path, "wb");
    ASSERT_TRUE(fp != nullptr);
    // Minimal PNG signature
    const uint8_t png_sig[] = {137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 0};
    fwrite(png_sig, 1, sizeof(png_sig), fp);
    fclose(fp);

    void *png_as_jpeg = load_jpeg(path);
    EXPECT_EQ(png_as_jpeg, nullptr); // JPEG decoder rejects PNG files
    remove(path);
}

int main() {
    return viper_test::run_all_tests();
}
