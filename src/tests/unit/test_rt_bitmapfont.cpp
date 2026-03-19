//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_bitmapfont.cpp
// Purpose: Unit tests for BitmapFont BDF/PSF parsing, glyph properties, and
//   text measurement. Tests the parser logic without requiring a graphics
//   context (no Canvas drawing tests — those require VIPER_ENABLE_GRAPHICS).
//
// Key invariants:
//   - BDF parser correctly reads ENCODING, BBX, BITMAP, DWIDTH fields.
//   - PSF parser correctly reads v1/v2 headers and glyph bitmaps.
//   - Text width measurement sums glyph advances.
//   - Invalid files return NULL without crashing.
//
// Ownership/Lifetime:
//   - Uses runtime library. Font objects are GC-managed.
//
// Links: src/runtime/graphics/rt_bitmapfont.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_bitmapfont.h"
#include "rt_internal.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Trap handler for runtime
extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

//=============================================================================
// Helper: create a minimal BDF font file on disk for testing
//=============================================================================

static const char *test_bdf_content = "STARTFONT 2.1\n"
                                      "FONT -Test-Medium-R-Normal--8-80-75-75-C-80-ISO10646-1\n"
                                      "SIZE 8 75 75\n"
                                      "FONTBOUNDINGBOX 8 8 0 0\n"
                                      "FONT_ASCENT 7\n"
                                      "FONT_DESCENT 1\n"
                                      "STARTPROPERTIES 2\n"
                                      "FONT_ASCENT 7\n"
                                      "FONT_DESCENT 1\n"
                                      "ENDPROPERTIES\n"
                                      "CHARS 4\n"
                                      // Space (encoding 32)
                                      "STARTCHAR space\n"
                                      "ENCODING 32\n"
                                      "SWIDTH 500 0\n"
                                      "DWIDTH 8 0\n"
                                      "BBX 8 8 0 0\n"
                                      "BITMAP\n"
                                      "00\n"
                                      "00\n"
                                      "00\n"
                                      "00\n"
                                      "00\n"
                                      "00\n"
                                      "00\n"
                                      "00\n"
                                      "ENDCHAR\n"
                                      // 'A' (encoding 65)
                                      "STARTCHAR A\n"
                                      "ENCODING 65\n"
                                      "SWIDTH 500 0\n"
                                      "DWIDTH 8 0\n"
                                      "BBX 8 8 0 0\n"
                                      "BITMAP\n"
                                      "38\n"
                                      "6C\n"
                                      "C6\n"
                                      "FE\n"
                                      "C6\n"
                                      "C6\n"
                                      "C6\n"
                                      "00\n"
                                      "ENDCHAR\n"
                                      // 'B' (encoding 66)
                                      "STARTCHAR B\n"
                                      "ENCODING 66\n"
                                      "SWIDTH 500 0\n"
                                      "DWIDTH 8 0\n"
                                      "BBX 8 8 0 0\n"
                                      "BITMAP\n"
                                      "FC\n"
                                      "66\n"
                                      "66\n"
                                      "7C\n"
                                      "66\n"
                                      "66\n"
                                      "FC\n"
                                      "00\n"
                                      "ENDCHAR\n"
                                      // '?' (encoding 63) — fallback glyph
                                      "STARTCHAR question\n"
                                      "ENCODING 63\n"
                                      "SWIDTH 500 0\n"
                                      "DWIDTH 8 0\n"
                                      "BBX 8 8 0 0\n"
                                      "BITMAP\n"
                                      "7C\n"
                                      "C6\n"
                                      "0C\n"
                                      "18\n"
                                      "18\n"
                                      "00\n"
                                      "18\n"
                                      "00\n"
                                      "ENDCHAR\n"
                                      "ENDFONT\n";

/// @brief Write the test BDF content to a temporary file.
static const char *create_test_bdf(void)
{
    static char path[256];
    snprintf(path, sizeof(path), "%s", "/tmp/viper_test_font.bdf");
    FILE *f = fopen(path, "w");
    if (!f)
        return NULL;
    fputs(test_bdf_content, f);
    fclose(f);
    return path;
}

//=============================================================================
// Helper: create a minimal PSF v2 font file on disk for testing
//=============================================================================

static const char *create_test_psf(void)
{
    static char path[256];
    snprintf(path, sizeof(path), "%s", "/tmp/viper_test_font.psf");
    FILE *f = fopen(path, "wb");
    if (!f)
        return NULL;

    // PSF v2 header: 32 bytes
    // Magic: 72 B5 4A 86
    uint8_t hdr[32];
    memset(hdr, 0, 32);
    hdr[0] = 0x72;
    hdr[1] = 0xB5;
    hdr[2] = 0x4A;
    hdr[3] = 0x86;
    // Version = 0
    // Header size = 32
    hdr[8] = 32;
    // Flags = 0
    // Number of glyphs = 4
    hdr[16] = 4;
    // Bytes per glyph = 8 (8 rows × 1 byte per row = 8)
    hdr[20] = 8;
    // Height = 8
    hdr[24] = 8;
    // Width = 8
    hdr[28] = 8;

    fwrite(hdr, 1, 32, f);

    // 4 glyphs, each 8 bytes (8×8 monospace, 1 byte per row)
    // Glyph 0: all zeros (null/space)
    uint8_t glyph0[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    fwrite(glyph0, 1, 8, f);

    // Glyph 1: vertical bar pattern
    uint8_t glyph1[8] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18};
    fwrite(glyph1, 1, 8, f);

    // Glyph 2: horizontal bar
    uint8_t glyph2[8] = {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00};
    fwrite(glyph2, 1, 8, f);

    // Glyph 3: cross
    uint8_t glyph3[8] = {0x18, 0x18, 0x18, 0xFF, 0x18, 0x18, 0x18, 0x18};
    fwrite(glyph3, 1, 8, f);

    fclose(f);
    return path;
}

//=============================================================================
// Tests
//=============================================================================

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

static void test_bdf_load_valid(void)
{
    TEST("BDF load valid font");
    const char *path = create_test_bdf();
    assert(path);

    rt_string rpath = rt_const_cstr(path);
    void *font = rt_bitmapfont_load_bdf(rpath);
    assert(font != NULL);

    // Should have loaded 4 glyphs (space, A, B, ?)
    assert(rt_bitmapfont_glyph_count(font) == 4);

    // Line height should be ascent + descent = 7 + 1 = 8
    assert(rt_bitmapfont_char_height(font) == 8);

    // All glyphs have DWIDTH 8, so monospace
    assert(rt_bitmapfont_is_monospace(font) == 1);

    // Max width should be 8
    assert(rt_bitmapfont_char_width(font) == 8);

    PASS();
}

static void test_bdf_text_width(void)
{
    TEST("BDF text width measurement");
    const char *path = create_test_bdf();
    rt_string rpath = rt_const_cstr(path);
    void *font = rt_bitmapfont_load_bdf(rpath);
    assert(font);

    // "AB" should be 8 + 8 = 16 pixels
    rt_string ab = rt_const_cstr("AB");
    assert(rt_bitmapfont_text_width(font, ab) == 16);

    // Empty string should be 0
    rt_string empty = rt_const_cstr("");
    assert(rt_bitmapfont_text_width(font, empty) == 0);

    // " A " (space, A, space) should be 24
    rt_string spaced = rt_const_cstr(" A ");
    assert(rt_bitmapfont_text_width(font, spaced) == 24);

    PASS();
}

static void test_bdf_text_height(void)
{
    TEST("BDF text height");
    const char *path = create_test_bdf();
    rt_string rpath = rt_const_cstr(path);
    void *font = rt_bitmapfont_load_bdf(rpath);
    assert(font);

    assert(rt_bitmapfont_text_height(font) == 8);

    PASS();
}

static void test_bdf_load_invalid(void)
{
    TEST("BDF load nonexistent file returns NULL");
    rt_string bad_path = rt_const_cstr("/tmp/nonexistent_font_abc123.bdf");
    void *font = rt_bitmapfont_load_bdf(bad_path);
    assert(font == NULL);
    PASS();
}

static void test_bdf_null_input(void)
{
    TEST("BDF NULL input safety");
    // NULL path should return NULL
    void *font = rt_bitmapfont_load_bdf(NULL);
    assert(font == NULL);

    // NULL font should return 0 for all properties
    assert(rt_bitmapfont_char_width(NULL) == 0);
    assert(rt_bitmapfont_char_height(NULL) == 0);
    assert(rt_bitmapfont_glyph_count(NULL) == 0);
    assert(rt_bitmapfont_is_monospace(NULL) == 0);
    assert(rt_bitmapfont_text_width(NULL, rt_const_cstr("test")) == 0);
    assert(rt_bitmapfont_text_height(NULL) == 0);

    PASS();
}

static void test_psf_load_valid(void)
{
    TEST("PSF v2 load valid font");
    const char *path = create_test_psf();
    assert(path);

    rt_string rpath = rt_const_cstr(path);
    void *font = rt_bitmapfont_load_psf(rpath);
    assert(font != NULL);

    // Should have 4 glyphs
    assert(rt_bitmapfont_glyph_count(font) == 4);

    // 8×8 monospace
    assert(rt_bitmapfont_char_height(font) == 8);
    assert(rt_bitmapfont_char_width(font) == 8);
    assert(rt_bitmapfont_is_monospace(font) == 1);

    PASS();
}

static void test_psf_load_invalid(void)
{
    TEST("PSF load nonexistent file returns NULL");
    rt_string bad_path = rt_const_cstr("/tmp/nonexistent_font_xyz789.psf");
    void *font = rt_bitmapfont_load_psf(bad_path);
    assert(font == NULL);
    PASS();
}

static void test_psf_load_bad_magic(void)
{
    TEST("PSF load file with bad magic returns NULL");
    // Create a file with wrong magic bytes
    const char *path = "/tmp/viper_test_bad_magic.psf";
    FILE *f = fopen(path, "wb");
    assert(f);
    uint8_t bad[32] = {0xDE, 0xAD, 0xBE, 0xEF};
    fwrite(bad, 1, 32, f);
    fclose(f);

    rt_string rpath = rt_const_cstr(path);
    void *font = rt_bitmapfont_load_psf(rpath);
    assert(font == NULL);
    PASS();
}

static void test_fallback_glyph(void)
{
    TEST("Text width with unmapped codepoints uses fallback");
    const char *path = create_test_bdf();
    rt_string rpath = rt_const_cstr(path);
    void *font = rt_bitmapfont_load_bdf(rpath);
    assert(font);

    // Character 'C' (encoding 67) is NOT in our test font
    // It should use the fallback '?' glyph (advance 8)
    rt_string text = rt_const_cstr("C");
    int64_t width = rt_bitmapfont_text_width(font, text);
    // Fallback glyph '?' has advance 8
    assert(width == 8);

    PASS();
}

static void test_destroy_safety(void)
{
    TEST("Destroy NULL font is safe");
    rt_bitmapfont_destroy(NULL); // Should not crash
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("test_rt_bitmapfont:\n");

    test_bdf_load_valid();
    test_bdf_text_width();
    test_bdf_text_height();
    test_bdf_load_invalid();
    test_bdf_null_input();
    test_psf_load_valid();
    test_psf_load_invalid();
    test_psf_load_bad_magic();
    test_fallback_glyph();
    test_destroy_safety();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
