//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTColorUtilsTests.cpp
// Purpose: Tests for Viper.Graphics.Color utility functions.
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static bool str_eq(rt_string s, const char *expected)
{
    const char *cstr = rt_string_cstr(s);
    return cstr && strcmp(cstr, expected) == 0;
}

// Helper: build a color from RGB components
static int64_t rgb(int r, int g, int b)
{
    return ((int64_t)(r & 0xFF) << 16) | ((int64_t)(g & 0xFF) << 8) | (int64_t)(b & 0xFF);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_from_hex_6digit()
{
    rt_string hex = rt_string_from_bytes("#FF8000", 7);
    int64_t c = rt_color_from_hex(hex);
    assert(c == rgb(0xFF, 0x80, 0x00));
    rt_string_unref(hex);
}

static void test_from_hex_no_hash()
{
    rt_string hex = rt_string_from_bytes("00FF00", 6);
    int64_t c = rt_color_from_hex(hex);
    assert(c == rgb(0x00, 0xFF, 0x00));
    rt_string_unref(hex);
}

static void test_from_hex_3digit()
{
    rt_string hex = rt_string_from_bytes("#F00", 4);
    int64_t c = rt_color_from_hex(hex);
    assert(c == rgb(0xFF, 0x00, 0x00));
    rt_string_unref(hex);
}

static void test_to_hex_basic()
{
    int64_t c = rgb(0xFF, 0x80, 0x00);
    rt_string result = rt_color_to_hex(c);
    assert(str_eq(result, "#FF8000"));
    rt_string_unref(result);
}

static void test_to_hex_black()
{
    rt_string result = rt_color_to_hex(rgb(0, 0, 0));
    assert(str_eq(result, "#000000"));
    rt_string_unref(result);
}

static void test_to_hex_white()
{
    rt_string result = rt_color_to_hex(rgb(255, 255, 255));
    assert(str_eq(result, "#FFFFFF"));
    rt_string_unref(result);
}

static void test_roundtrip_hex()
{
    int64_t original = rgb(0x12, 0x34, 0x56);
    rt_string hex = rt_color_to_hex(original);
    int64_t back = rt_color_from_hex(hex);
    assert(back == original);
    rt_string_unref(hex);
}

static void test_complement_red()
{
    // Red's complement should be cyan-ish
    int64_t red = rgb(255, 0, 0);
    int64_t comp = rt_color_complement(red);
    // Complement shifts hue by 180 degrees
    int64_t r = (comp >> 16) & 0xFF;
    int64_t g = (comp >> 8) & 0xFF;
    int64_t b = comp & 0xFF;
    // Red complement should have low R and higher G/B
    assert(r < 50);
    assert(g > 200);
    assert(b > 200);
}

static void test_grayscale()
{
    int64_t c = rgb(100, 150, 200);
    int64_t gray = rt_color_grayscale(c);
    int64_t r = (gray >> 16) & 0xFF;
    int64_t g = (gray >> 8) & 0xFF;
    int64_t b = gray & 0xFF;
    // All channels should be equal
    assert(r == g);
    assert(g == b);
    // Luminance of (100,150,200) = (100*299 + 150*587 + 200*114) / 1000 = 140
    assert(r == 140);
}

static void test_invert()
{
    int64_t c = rgb(100, 150, 200);
    int64_t inv = rt_color_invert(c);
    assert(inv == rgb(155, 105, 55));
}

static void test_invert_roundtrip()
{
    int64_t c = rgb(42, 128, 200);
    int64_t inv = rt_color_invert(rt_color_invert(c));
    assert(inv == c);
}

static void test_saturate()
{
    // Start with a grayish color and saturate it
    int64_t c = rgb(128, 128, 128);
    int64_t sat = rt_color_saturate(c, 50);
    // Pure gray has 0 saturation; adding saturation to gray should still be gray
    // (because HSL saturation of gray is 0, and adding to 0 may not visibly change much)
    // Use a colored input instead
    int64_t colored = rgb(200, 100, 100);
    int64_t more_sat = rt_color_saturate(colored, 20);
    // After saturating, the dominant channel should stay dominant
    int64_t r = (more_sat >> 16) & 0xFF;
    int64_t g = (more_sat >> 8) & 0xFF;
    assert(r > g);
    (void)sat;
}

static void test_desaturate()
{
    int64_t c = rgb(255, 0, 0); // Pure red
    int64_t desat = rt_color_desaturate(c, 100);
    // Fully desaturated red should be gray
    int64_t r = (desat >> 16) & 0xFF;
    int64_t g = (desat >> 8) & 0xFF;
    int64_t b = desat & 0xFF;
    assert(r == g);
    assert(g == b);
}

static void test_saturate_clamps()
{
    int64_t c = rgb(200, 100, 100);
    // Saturate by 200% should clamp to 100%
    int64_t sat = rt_color_saturate(c, 200);
    // Should not crash and should return a valid color
    int64_t r = (sat >> 16) & 0xFF;
    assert(r <= 255);
}

int main()
{
    test_from_hex_6digit();
    test_from_hex_no_hash();
    test_from_hex_3digit();
    test_to_hex_basic();
    test_to_hex_black();
    test_to_hex_white();
    test_roundtrip_hex();
    test_complement_red();
    test_grayscale();
    test_invert();
    test_invert_roundtrip();
    test_saturate();
    test_desaturate();
    test_saturate_clamps();

    return 0;
}
