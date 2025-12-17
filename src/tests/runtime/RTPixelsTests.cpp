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

#include <cassert>
#include <cstdio>

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

int main()
{
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

    printf("\nAll tests passed!\n");
    return 0;
}
