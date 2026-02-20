//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTPixelsTests.cpp
// Purpose: Tests for Viper.Graphics.Pixels software image buffer.
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include "tests/common/PosixCompat.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Constructor Tests
// ============================================================================

static void test_new()
{
    void *p = rt_pixels_new(100, 50);
    assert(p != nullptr);
    assert(rt_pixels_width(p) == 100);
    assert(rt_pixels_height(p) == 50);
    printf("test_new: PASSED\n");
}

static void test_new_zero_dimensions()
{
    void *p = rt_pixels_new(0, 0);
    assert(p != nullptr);
    assert(rt_pixels_width(p) == 0);
    assert(rt_pixels_height(p) == 0);
    printf("test_new_zero_dimensions: PASSED\n");
}

static void test_new_negative_dimensions()
{
    void *p = rt_pixels_new(-10, -20);
    assert(p != nullptr);
    assert(rt_pixels_width(p) == 0);
    assert(rt_pixels_height(p) == 0);
    printf("test_new_negative_dimensions: PASSED\n");
}

// ============================================================================
// Pixel Access Tests
// ============================================================================

static void test_get_set()
{
    void *p = rt_pixels_new(10, 10);

    // Initially should be 0 (transparent black)
    assert(rt_pixels_get(p, 5, 5) == 0);

    // Set a pixel
    int64_t red = 0xFF0000FF; // Red with full alpha
    rt_pixels_set(p, 5, 5, red);
    assert(rt_pixels_get(p, 5, 5) == red);

    printf("test_get_set: PASSED\n");
}

static void test_get_out_of_bounds()
{
    void *p = rt_pixels_new(10, 10);

    // Out of bounds should return 0
    assert(rt_pixels_get(p, -1, 0) == 0);
    assert(rt_pixels_get(p, 0, -1) == 0);
    assert(rt_pixels_get(p, 10, 0) == 0);
    assert(rt_pixels_get(p, 0, 10) == 0);
    assert(rt_pixels_get(p, 100, 100) == 0);

    printf("test_get_out_of_bounds: PASSED\n");
}

static void test_set_out_of_bounds()
{
    void *p = rt_pixels_new(10, 10);

    // Set out of bounds - should be silently ignored
    rt_pixels_set(p, -1, 0, 0xFFFFFFFF);
    rt_pixels_set(p, 0, -1, 0xFFFFFFFF);
    rt_pixels_set(p, 10, 0, 0xFFFFFFFF);
    rt_pixels_set(p, 0, 10, 0xFFFFFFFF);

    // All pixels should still be 0
    for (int64_t y = 0; y < 10; y++)
    {
        for (int64_t x = 0; x < 10; x++)
        {
            assert(rt_pixels_get(p, x, y) == 0);
        }
    }

    printf("test_set_out_of_bounds: PASSED\n");
}

static void test_corners()
{
    void *p = rt_pixels_new(5, 5);

    int64_t tl = 0x11111111;
    int64_t tr = 0x22222222;
    int64_t bl = 0x33333333;
    int64_t br = 0x44444444;

    rt_pixels_set(p, 0, 0, tl);
    rt_pixels_set(p, 4, 0, tr);
    rt_pixels_set(p, 0, 4, bl);
    rt_pixels_set(p, 4, 4, br);

    assert(rt_pixels_get(p, 0, 0) == tl);
    assert(rt_pixels_get(p, 4, 0) == tr);
    assert(rt_pixels_get(p, 0, 4) == bl);
    assert(rt_pixels_get(p, 4, 4) == br);

    printf("test_corners: PASSED\n");
}

// ============================================================================
// Fill Operations Tests
// ============================================================================

static void test_fill()
{
    void *p = rt_pixels_new(5, 5);
    int64_t color = 0xAABBCCDD;

    rt_pixels_fill(p, color);

    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            assert(rt_pixels_get(p, x, y) == color);
        }
    }

    printf("test_fill: PASSED\n");
}

static void test_clear()
{
    void *p = rt_pixels_new(5, 5);

    // Fill with non-zero color
    rt_pixels_fill(p, 0xFFFFFFFF);

    // Clear to transparent black
    rt_pixels_clear(p);

    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            assert(rt_pixels_get(p, x, y) == 0);
        }
    }

    printf("test_clear: PASSED\n");
}

// ============================================================================
// Copy Operations Tests
// ============================================================================

static void test_copy_basic()
{
    void *src = rt_pixels_new(10, 10);
    void *dst = rt_pixels_new(10, 10);

    // Create a pattern in source
    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            rt_pixels_set(src, x, y, (int64_t)(y * 5 + x));
        }
    }

    // Copy 5x5 block from (0,0) to (2,2)
    rt_pixels_copy(dst, 2, 2, src, 0, 0, 5, 5);

    // Verify copy
    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            int64_t expected = y * 5 + x;
            assert(rt_pixels_get(dst, x + 2, y + 2) == expected);
        }
    }

    printf("test_copy_basic: PASSED\n");
}

static void test_copy_clipping()
{
    void *src = rt_pixels_new(10, 10);
    void *dst = rt_pixels_new(5, 5);

    // Fill source with a value
    rt_pixels_fill(src, 0x12345678);

    // Copy with clipping (requesting more than fits)
    rt_pixels_copy(dst, 0, 0, src, 0, 0, 10, 10);

    // Destination should be filled (clipped to its size)
    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            assert(rt_pixels_get(dst, x, y) == 0x12345678);
        }
    }

    printf("test_copy_clipping: PASSED\n");
}

static void test_copy_negative_dest()
{
    void *src = rt_pixels_new(10, 10);
    void *dst = rt_pixels_new(10, 10);

    // Fill source
    rt_pixels_fill(src, 0xABCDEF00);

    // Copy with negative destination offset (should clip from source)
    rt_pixels_copy(dst, -2, -2, src, 0, 0, 5, 5);

    // Only 3x3 pixels should be copied to (0,0)-(2,2)
    for (int64_t y = 0; y < 3; y++)
    {
        for (int64_t x = 0; x < 3; x++)
        {
            assert(rt_pixels_get(dst, x, y) == 0xABCDEF00);
        }
    }

    // Rest should be 0
    assert(rt_pixels_get(dst, 5, 5) == 0);

    printf("test_copy_negative_dest: PASSED\n");
}

static void test_clone()
{
    void *p = rt_pixels_new(5, 5);

    // Create a pattern
    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            rt_pixels_set(p, x, y, (int64_t)(y * 5 + x));
        }
    }

    void *clone = rt_pixels_clone(p);

    assert(clone != nullptr);
    assert(clone != p); // Different object
    assert(rt_pixels_width(clone) == rt_pixels_width(p));
    assert(rt_pixels_height(clone) == rt_pixels_height(p));

    // Verify all pixels match
    for (int64_t y = 0; y < 5; y++)
    {
        for (int64_t x = 0; x < 5; x++)
        {
            assert(rt_pixels_get(clone, x, y) == rt_pixels_get(p, x, y));
        }
    }

    // Modify original, clone should not change
    rt_pixels_set(p, 0, 0, 0xFFFFFFFF);
    assert(rt_pixels_get(clone, 0, 0) == 0);

    printf("test_clone: PASSED\n");
}

// ============================================================================
// Byte Conversion Tests
// ============================================================================

static void test_to_bytes()
{
    void *p = rt_pixels_new(2, 2);

    // Set 4 pixels with distinct RGBA values
    rt_pixels_set(p, 0, 0, 0x11223344);
    rt_pixels_set(p, 1, 0, 0x55667788);
    rt_pixels_set(p, 0, 1, 0x99AABBCC);
    rt_pixels_set(p, 1, 1, 0xDDEEFF00);

    void *bytes = rt_pixels_to_bytes(p);
    assert(bytes != nullptr);
    assert(rt_bytes_len(bytes) == 16); // 2x2 * 4 bytes per pixel

    printf("test_to_bytes: PASSED\n");
}

static void test_from_bytes()
{
    // Create bytes for a 2x2 image
    void *bytes = rt_bytes_new(16);

    // Manually set pixel data (little-endian uint32_t)
    // Pixel (0,0) = 0x11223344
    rt_bytes_set(bytes, 0, 0x44);
    rt_bytes_set(bytes, 1, 0x33);
    rt_bytes_set(bytes, 2, 0x22);
    rt_bytes_set(bytes, 3, 0x11);
    // Pixel (1,0) = 0x55667788
    rt_bytes_set(bytes, 4, 0x88);
    rt_bytes_set(bytes, 5, 0x77);
    rt_bytes_set(bytes, 6, 0x66);
    rt_bytes_set(bytes, 7, 0x55);
    // Pixel (0,1) = 0x99AABBCC
    rt_bytes_set(bytes, 8, 0xCC);
    rt_bytes_set(bytes, 9, 0xBB);
    rt_bytes_set(bytes, 10, 0xAA);
    rt_bytes_set(bytes, 11, 0x99);
    // Pixel (1,1) = 0xDDEEFF00
    rt_bytes_set(bytes, 12, 0x00);
    rt_bytes_set(bytes, 13, 0xFF);
    rt_bytes_set(bytes, 14, 0xEE);
    rt_bytes_set(bytes, 15, 0xDD);

    void *p = rt_pixels_from_bytes(2, 2, bytes);
    assert(p != nullptr);
    assert(rt_pixels_width(p) == 2);
    assert(rt_pixels_height(p) == 2);

    assert(rt_pixels_get(p, 0, 0) == 0x11223344);
    assert(rt_pixels_get(p, 1, 0) == 0x55667788);
    assert(rt_pixels_get(p, 0, 1) == (int64_t)0x99AABBCC);
    assert(rt_pixels_get(p, 1, 1) == (int64_t)0xDDEEFF00);

    printf("test_from_bytes: PASSED\n");
}

static void test_round_trip()
{
    void *original = rt_pixels_new(10, 10);

    // Create a pattern
    for (int64_t y = 0; y < 10; y++)
    {
        for (int64_t x = 0; x < 10; x++)
        {
            rt_pixels_set(original, x, y, (int64_t)((y << 24) | (x << 16) | 0xFF));
        }
    }

    // Convert to bytes and back
    void *bytes = rt_pixels_to_bytes(original);
    void *restored = rt_pixels_from_bytes(10, 10, bytes);

    // Verify all pixels match
    for (int64_t y = 0; y < 10; y++)
    {
        for (int64_t x = 0; x < 10; x++)
        {
            assert(rt_pixels_get(restored, x, y) == rt_pixels_get(original, x, y));
        }
    }

    printf("test_round_trip: PASSED\n");
}

// ============================================================================
// Edge Case Tests
// ============================================================================

static void test_large_image()
{
    // Create a reasonably large image
    void *p = rt_pixels_new(1000, 1000);
    assert(p != nullptr);
    assert(rt_pixels_width(p) == 1000);
    assert(rt_pixels_height(p) == 1000);

    // Set corner pixels
    rt_pixels_set(p, 0, 0, 0x11111111);
    rt_pixels_set(p, 999, 0, 0x22222222);
    rt_pixels_set(p, 0, 999, 0x33333333);
    rt_pixels_set(p, 999, 999, 0x44444444);

    assert(rt_pixels_get(p, 0, 0) == 0x11111111);
    assert(rt_pixels_get(p, 999, 0) == 0x22222222);
    assert(rt_pixels_get(p, 0, 999) == 0x33333333);
    assert(rt_pixels_get(p, 999, 999) == 0x44444444);

    printf("test_large_image: PASSED\n");
}

static void test_single_pixel()
{
    void *p = rt_pixels_new(1, 1);
    assert(p != nullptr);
    assert(rt_pixels_width(p) == 1);
    assert(rt_pixels_height(p) == 1);

    rt_pixels_set(p, 0, 0, 0xDEADBEEF);
    assert(rt_pixels_get(p, 0, 0) == (int64_t)0xDEADBEEF);

    printf("test_single_pixel: PASSED\n");
}

// ============================================================================
// BMP Load/Save Tests
// ============================================================================

static void test_bmp_save_load_roundtrip()
{
    // Create a test image with known colors
    void *p = rt_pixels_new(10, 10);
    assert(p != nullptr);

    // Fill with a pattern - Red at (0,0), Green at (9,0), Blue at (0,9), White at (9,9)
    // Using 0xRRGGBBAA format
    rt_pixels_set(p, 0, 0, 0xFF0000FF); // Red
    rt_pixels_set(p, 9, 0, 0x00FF00FF); // Green
    rt_pixels_set(p, 0, 9, 0x0000FFFF); // Blue
    rt_pixels_set(p, 9, 9, 0xFFFFFFFF); // White

    // Fill middle with gray
    for (int y = 3; y < 7; y++)
    {
        for (int x = 3; x < 7; x++)
        {
            rt_pixels_set(p, x, y, 0x808080FF); // Gray
        }
    }

    // Create temp file path
    char tmpfile[] = "/tmp/viper_test_bmp_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    close(fd);

    // Add .bmp extension
    char bmppath[256];
    snprintf(bmppath, sizeof(bmppath), "%s.bmp", tmpfile);
    rename(tmpfile, bmppath);

    // Save to BMP
    rt_string path = rt_string_from_bytes(bmppath, strlen(bmppath));
    int64_t result = rt_pixels_save_bmp(p, path);
    assert(result == 1);

    // Load BMP back
    void *loaded = rt_pixels_load_bmp(path);
    assert(loaded != nullptr);
    assert(rt_pixels_width(loaded) == 10);
    assert(rt_pixels_height(loaded) == 10);

    // Verify colors (BMP is 24-bit, so alpha is always 0xFF on load)
    int64_t red_loaded = rt_pixels_get(loaded, 0, 0);
    int64_t green_loaded = rt_pixels_get(loaded, 9, 0);
    int64_t blue_loaded = rt_pixels_get(loaded, 0, 9);
    int64_t white_loaded = rt_pixels_get(loaded, 9, 9);

    // Check RGB components (ignore alpha differences)
    assert((red_loaded & 0xFFFFFF00) == 0xFF000000);   // Red
    assert((green_loaded & 0xFFFFFF00) == 0x00FF0000); // Green
    assert((blue_loaded & 0xFFFFFF00) == 0x0000FF00);  // Blue
    assert((white_loaded & 0xFFFFFF00) == 0xFFFFFF00); // White

    // Verify gray in middle
    int64_t gray = rt_pixels_get(loaded, 5, 5);
    assert((gray & 0xFFFFFF00) == 0x80808000);

    // Cleanup
    unlink(bmppath);

    printf("test_bmp_save_load_roundtrip: PASSED\n");
}

static void test_bmp_load_invalid_path()
{
    const char *invalid = "/nonexistent/path/file.bmp";
    rt_string path = rt_string_from_bytes(invalid, strlen(invalid));
    void *p = rt_pixels_load_bmp(path);
    assert(p == nullptr);

    printf("test_bmp_load_invalid_path: PASSED\n");
}

static void test_bmp_save_null_inputs()
{
    // Save with null pixels should return 0
    const char *tmp = "/tmp/test.bmp";
    rt_string path = rt_string_from_bytes(tmp, strlen(tmp));
    assert(rt_pixels_save_bmp(nullptr, path) == 0);

    // Save with null path should return 0
    void *p = rt_pixels_new(10, 10);
    assert(rt_pixels_save_bmp(p, nullptr) == 0);

    printf("test_bmp_save_null_inputs: PASSED\n");
}

static void test_bmp_odd_dimensions()
{
    // BMP row padding test - use width that requires padding
    void *p = rt_pixels_new(7, 5); // 7 pixels = 21 bytes, needs 3 bytes padding to reach 24
    assert(p != nullptr);

    // Fill with a checkerboard pattern
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 7; x++)
        {
            if ((x + y) % 2 == 0)
                rt_pixels_set(p, x, y, 0xFF0000FF); // Red
            else
                rt_pixels_set(p, x, y, 0x00FF00FF); // Green
        }
    }

    // Create temp file
    char tmpfile[] = "/tmp/viper_test_bmp_odd_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert(fd >= 0);
    close(fd);

    char bmppath[256];
    snprintf(bmppath, sizeof(bmppath), "%s.bmp", tmpfile);
    rename(tmpfile, bmppath);

    // Save and reload
    rt_string path = rt_string_from_bytes(bmppath, strlen(bmppath));
    assert(rt_pixels_save_bmp(p, path) == 1);

    void *loaded = rt_pixels_load_bmp(path);
    assert(loaded != nullptr);
    assert(rt_pixels_width(loaded) == 7);
    assert(rt_pixels_height(loaded) == 5);

    // Verify checkerboard pattern
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 7; x++)
        {
            int64_t color = rt_pixels_get(loaded, x, y);
            if ((x + y) % 2 == 0)
                assert((color & 0xFFFFFF00) == 0xFF000000); // Red
            else
                assert((color & 0xFFFFFF00) == 0x00FF0000); // Green
        }
    }

    // Cleanup
    unlink(bmppath);

    printf("test_bmp_odd_dimensions: PASSED\n");
}

// ============================================================================
// Transform Tests
// ============================================================================

static void test_flip_h()
{
    // Create a 3x2 image with distinct colors in each corner
    // [R G B]
    // [C M Y]
    void *p = rt_pixels_new(3, 2);
    rt_pixels_set(p, 0, 0, 0x11111111); // top-left
    rt_pixels_set(p, 1, 0, 0x22222222); // top-middle
    rt_pixels_set(p, 2, 0, 0x33333333); // top-right
    rt_pixels_set(p, 0, 1, 0x44444444); // bottom-left
    rt_pixels_set(p, 1, 1, 0x55555555); // bottom-middle
    rt_pixels_set(p, 2, 1, 0x66666666); // bottom-right

    void *flipped = rt_pixels_flip_h(p);
    assert(flipped != nullptr);
    assert(rt_pixels_width(flipped) == 3);
    assert(rt_pixels_height(flipped) == 2);

    // After horizontal flip:
    // [B G R]
    // [Y M C]
    assert(rt_pixels_get(flipped, 0, 0) == 0x33333333); // was top-right
    assert(rt_pixels_get(flipped, 1, 0) == 0x22222222); // middle unchanged
    assert(rt_pixels_get(flipped, 2, 0) == 0x11111111); // was top-left
    assert(rt_pixels_get(flipped, 0, 1) == 0x66666666); // was bottom-right
    assert(rt_pixels_get(flipped, 1, 1) == 0x55555555); // middle unchanged
    assert(rt_pixels_get(flipped, 2, 1) == 0x44444444); // was bottom-left

    printf("test_flip_h: PASSED\n");
}

static void test_flip_v()
{
    // Create a 2x3 image
    void *p = rt_pixels_new(2, 3);
    rt_pixels_set(p, 0, 0, 0x11111111); // row 0
    rt_pixels_set(p, 1, 0, 0x22222222);
    rt_pixels_set(p, 0, 1, 0x33333333); // row 1
    rt_pixels_set(p, 1, 1, 0x44444444);
    rt_pixels_set(p, 0, 2, 0x55555555); // row 2
    rt_pixels_set(p, 1, 2, 0x66666666);

    void *flipped = rt_pixels_flip_v(p);
    assert(flipped != nullptr);
    assert(rt_pixels_width(flipped) == 2);
    assert(rt_pixels_height(flipped) == 3);

    // After vertical flip, row 0 becomes row 2, row 2 becomes row 0
    assert(rt_pixels_get(flipped, 0, 0) == 0x55555555); // was row 2
    assert(rt_pixels_get(flipped, 1, 0) == 0x66666666);
    assert(rt_pixels_get(flipped, 0, 1) == 0x33333333); // row 1 unchanged
    assert(rt_pixels_get(flipped, 1, 1) == 0x44444444);
    assert(rt_pixels_get(flipped, 0, 2) == 0x11111111); // was row 0
    assert(rt_pixels_get(flipped, 1, 2) == 0x22222222);

    printf("test_flip_v: PASSED\n");
}

static void test_rotate_cw()
{
    // Create a 3x2 image
    // [A B C]
    // [D E F]
    void *p = rt_pixels_new(3, 2);
    rt_pixels_set(p, 0, 0, 0xAAAAAAAA); // A
    rt_pixels_set(p, 1, 0, 0xBBBBBBBB); // B
    rt_pixels_set(p, 2, 0, 0xCCCCCCCC); // C
    rt_pixels_set(p, 0, 1, 0xDDDDDDDD); // D
    rt_pixels_set(p, 1, 1, 0xEEEEEEEE); // E
    rt_pixels_set(p, 2, 1, 0xFFFFFFFF); // F

    void *rotated = rt_pixels_rotate_cw(p);
    assert(rotated != nullptr);
    // After 90 CW rotation, dimensions swap: 3x2 -> 2x3
    assert(rt_pixels_width(rotated) == 2);
    assert(rt_pixels_height(rotated) == 3);

    // After 90 CW:
    // [D A]
    // [E B]
    // [F C]
    assert(rt_pixels_get(rotated, 0, 0) == 0xDDDDDDDD); // D
    assert(rt_pixels_get(rotated, 1, 0) == 0xAAAAAAAA); // A
    assert(rt_pixels_get(rotated, 0, 1) == 0xEEEEEEEE); // E
    assert(rt_pixels_get(rotated, 1, 1) == 0xBBBBBBBB); // B
    assert(rt_pixels_get(rotated, 0, 2) == 0xFFFFFFFF); // F
    assert(rt_pixels_get(rotated, 1, 2) == 0xCCCCCCCC); // C

    printf("test_rotate_cw: PASSED\n");
}

static void test_rotate_ccw()
{
    // Create a 3x2 image
    // [A B C]
    // [D E F]
    void *p = rt_pixels_new(3, 2);
    rt_pixels_set(p, 0, 0, 0xAAAAAAAA); // A
    rt_pixels_set(p, 1, 0, 0xBBBBBBBB); // B
    rt_pixels_set(p, 2, 0, 0xCCCCCCCC); // C
    rt_pixels_set(p, 0, 1, 0xDDDDDDDD); // D
    rt_pixels_set(p, 1, 1, 0xEEEEEEEE); // E
    rt_pixels_set(p, 2, 1, 0xFFFFFFFF); // F

    void *rotated = rt_pixels_rotate_ccw(p);
    assert(rotated != nullptr);
    // After 90 CCW rotation, dimensions swap: 3x2 -> 2x3
    assert(rt_pixels_width(rotated) == 2);
    assert(rt_pixels_height(rotated) == 3);

    // After 90 CCW:
    // [C F]
    // [B E]
    // [A D]
    assert(rt_pixels_get(rotated, 0, 0) == 0xCCCCCCCC); // C
    assert(rt_pixels_get(rotated, 1, 0) == 0xFFFFFFFF); // F
    assert(rt_pixels_get(rotated, 0, 1) == 0xBBBBBBBB); // B
    assert(rt_pixels_get(rotated, 1, 1) == 0xEEEEEEEE); // E
    assert(rt_pixels_get(rotated, 0, 2) == 0xAAAAAAAA); // A
    assert(rt_pixels_get(rotated, 1, 2) == 0xDDDDDDDD); // D

    printf("test_rotate_ccw: PASSED\n");
}

static void test_rotate_180()
{
    // Create a 3x2 image
    // [A B C]
    // [D E F]
    void *p = rt_pixels_new(3, 2);
    rt_pixels_set(p, 0, 0, 0xAAAAAAAA); // A
    rt_pixels_set(p, 1, 0, 0xBBBBBBBB); // B
    rt_pixels_set(p, 2, 0, 0xCCCCCCCC); // C
    rt_pixels_set(p, 0, 1, 0xDDDDDDDD); // D
    rt_pixels_set(p, 1, 1, 0xEEEEEEEE); // E
    rt_pixels_set(p, 2, 1, 0xFFFFFFFF); // F

    void *rotated = rt_pixels_rotate_180(p);
    assert(rotated != nullptr);
    // After 180 rotation, dimensions stay same
    assert(rt_pixels_width(rotated) == 3);
    assert(rt_pixels_height(rotated) == 2);

    // After 180:
    // [F E D]
    // [C B A]
    assert(rt_pixels_get(rotated, 0, 0) == 0xFFFFFFFF); // F
    assert(rt_pixels_get(rotated, 1, 0) == 0xEEEEEEEE); // E
    assert(rt_pixels_get(rotated, 2, 0) == 0xDDDDDDDD); // D
    assert(rt_pixels_get(rotated, 0, 1) == 0xCCCCCCCC); // C
    assert(rt_pixels_get(rotated, 1, 1) == 0xBBBBBBBB); // B
    assert(rt_pixels_get(rotated, 2, 1) == 0xAAAAAAAA); // A

    printf("test_rotate_180: PASSED\n");
}

static void test_scale_up()
{
    // Create a 2x2 image and scale to 4x4
    void *p = rt_pixels_new(2, 2);
    rt_pixels_set(p, 0, 0, 0x11111111); // top-left
    rt_pixels_set(p, 1, 0, 0x22222222); // top-right
    rt_pixels_set(p, 0, 1, 0x33333333); // bottom-left
    rt_pixels_set(p, 1, 1, 0x44444444); // bottom-right

    void *scaled = rt_pixels_scale(p, 4, 4);
    assert(scaled != nullptr);
    assert(rt_pixels_width(scaled) == 4);
    assert(rt_pixels_height(scaled) == 4);

    // Each 2x2 block should have the same color (nearest neighbor)
    // Top-left quadrant
    assert(rt_pixels_get(scaled, 0, 0) == 0x11111111);
    assert(rt_pixels_get(scaled, 1, 0) == 0x11111111);
    assert(rt_pixels_get(scaled, 0, 1) == 0x11111111);
    assert(rt_pixels_get(scaled, 1, 1) == 0x11111111);

    // Top-right quadrant
    assert(rt_pixels_get(scaled, 2, 0) == 0x22222222);
    assert(rt_pixels_get(scaled, 3, 0) == 0x22222222);
    assert(rt_pixels_get(scaled, 2, 1) == 0x22222222);
    assert(rt_pixels_get(scaled, 3, 1) == 0x22222222);

    // Bottom-left quadrant
    assert(rt_pixels_get(scaled, 0, 2) == 0x33333333);
    assert(rt_pixels_get(scaled, 1, 2) == 0x33333333);
    assert(rt_pixels_get(scaled, 0, 3) == 0x33333333);
    assert(rt_pixels_get(scaled, 1, 3) == 0x33333333);

    // Bottom-right quadrant
    assert(rt_pixels_get(scaled, 2, 2) == 0x44444444);
    assert(rt_pixels_get(scaled, 3, 2) == 0x44444444);
    assert(rt_pixels_get(scaled, 2, 3) == 0x44444444);
    assert(rt_pixels_get(scaled, 3, 3) == 0x44444444);

    printf("test_scale_up: PASSED\n");
}

static void test_scale_down()
{
    // Create a 4x4 image with different colors in each quadrant
    void *p = rt_pixels_new(4, 4);

    // Fill top-left quadrant
    rt_pixels_set(p, 0, 0, 0x11111111);
    rt_pixels_set(p, 1, 0, 0x11111111);
    rt_pixels_set(p, 0, 1, 0x11111111);
    rt_pixels_set(p, 1, 1, 0x11111111);

    // Fill top-right quadrant
    rt_pixels_set(p, 2, 0, 0x22222222);
    rt_pixels_set(p, 3, 0, 0x22222222);
    rt_pixels_set(p, 2, 1, 0x22222222);
    rt_pixels_set(p, 3, 1, 0x22222222);

    // Fill bottom-left quadrant
    rt_pixels_set(p, 0, 2, 0x33333333);
    rt_pixels_set(p, 1, 2, 0x33333333);
    rt_pixels_set(p, 0, 3, 0x33333333);
    rt_pixels_set(p, 1, 3, 0x33333333);

    // Fill bottom-right quadrant
    rt_pixels_set(p, 2, 2, 0x44444444);
    rt_pixels_set(p, 3, 2, 0x44444444);
    rt_pixels_set(p, 2, 3, 0x44444444);
    rt_pixels_set(p, 3, 3, 0x44444444);

    // Scale down to 2x2
    void *scaled = rt_pixels_scale(p, 2, 2);
    assert(scaled != nullptr);
    assert(rt_pixels_width(scaled) == 2);
    assert(rt_pixels_height(scaled) == 2);

    // Each pixel should sample from the corresponding quadrant
    assert(rt_pixels_get(scaled, 0, 0) == 0x11111111);
    assert(rt_pixels_get(scaled, 1, 0) == 0x22222222);
    assert(rt_pixels_get(scaled, 0, 1) == 0x33333333);
    assert(rt_pixels_get(scaled, 1, 1) == 0x44444444);

    printf("test_scale_down: PASSED\n");
}

// ============================================================================
// BlendPixel Tests
// ============================================================================

static void test_blend_fully_opaque()
{
    void *p = rt_pixels_new(4, 4);
    // Black canvas; blend fully-opaque red (alpha=255) at (1,1)
    rt_pixels_blend_pixel(p, 1, 1, 0x00FF0000, 255);
    // Should be identical to set_rgb (fully opaque fast path)
    int64_t got = rt_pixels_get_rgb(p, 1, 1);
    assert(got == 0x00FF0000);
    printf("test_blend_fully_opaque: PASSED\n");
}

static void test_blend_transparent()
{
    void *p = rt_pixels_new(4, 4);
    rt_pixels_fill(p, (int64_t)0xFF000000); // red opaque background
    // Blend with alpha=0 — no change expected
    rt_pixels_blend_pixel(p, 0, 0, 0x0000FF00, 0);
    int64_t got = rt_pixels_get(p, 0, 0);
    // Background (red RGBA) should be preserved
    assert((got >> 24) == 0xFF && ((got >> 16) & 0xFF) == 0 && ((got >> 8) & 0xFF) == 0);
    printf("test_blend_transparent: PASSED\n");
}

static void test_blend_50_percent()
{
    void *p = rt_pixels_new(4, 4);
    // Set background pixel to opaque white (0xFFFFFFFF in RGBA)
    rt_pixels_set(p, 2, 2, (int64_t)0xFFFFFFFF);
    // Blend opaque black (0x000000) at 50% alpha
    rt_pixels_blend_pixel(p, 2, 2, 0x00000000, 128);
    // Result: ~50% grey — channels should be near 127 (within rounding ±2)
    int64_t rgba = rt_pixels_get(p, 2, 2);
    int r = (int)((rgba >> 24) & 0xFF);
    int g = (int)((rgba >> 16) & 0xFF);
    int b = (int)((rgba >>  8) & 0xFF);
    assert(r >= 125 && r <= 130);
    assert(g >= 125 && g <= 130);
    assert(b >= 125 && b <= 130);
    printf("test_blend_50_percent: PASSED\n");
}

static void test_blend_out_of_bounds()
{
    // Should silently clip — no crash
    void *p = rt_pixels_new(4, 4);
    rt_pixels_blend_pixel(p, -1, -1, 0x00FF0000, 255);
    rt_pixels_blend_pixel(p, 100, 100, 0x00FF0000, 255);
    printf("test_blend_out_of_bounds: PASSED\n");
}

int main()
{
#ifdef _WIN32
    // Skip on Windows: test uses /tmp paths not available on Windows
    printf("Test skipped: POSIX temp paths not available on Windows\n");
    return 0;
#endif
    printf("=== Viper.Graphics.Pixels Tests ===\n\n");

    // Constructors
    test_new();
    test_new_zero_dimensions();
    test_new_negative_dimensions();

    // Pixel access
    test_get_set();
    test_get_out_of_bounds();
    test_set_out_of_bounds();
    test_corners();

    // Fill operations
    test_fill();
    test_clear();

    // Copy operations
    test_copy_basic();
    test_copy_clipping();
    test_copy_negative_dest();
    test_clone();

    // Byte conversion
    test_to_bytes();
    test_from_bytes();
    test_round_trip();

    // Edge cases
    test_large_image();
    test_single_pixel();

    // BMP I/O
    test_bmp_save_load_roundtrip();
    test_bmp_load_invalid_path();
    test_bmp_save_null_inputs();
    test_bmp_odd_dimensions();

    // Transforms
    test_flip_h();
    test_flip_v();
    test_rotate_cw();
    test_rotate_ccw();
    test_rotate_180();
    test_scale_up();
    test_scale_down();

    // BlendPixel
    test_blend_fully_opaque();
    test_blend_transparent();
    test_blend_50_percent();
    test_blend_out_of_bounds();

    printf("\nAll tests passed!\n");
    return 0;
}
