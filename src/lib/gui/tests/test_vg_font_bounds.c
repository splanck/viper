//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_font_bounds.c
// Purpose: TTF parser bounds-checking tests — table offset overflow, truncated
//          cmap/glyf tables, hmtx fallback, UTF-8 scalar validation, and
//          composite-glyph point-alignment.
// Key invariants:
//   - All font blobs are synthesised in-memory (no real font file required)
//   - vg_font_load must return NULL for every out-of-bounds or malformed input
//   - vg_utf8_decode must return U+FFFD for overlong, surrogate, and too-large
//     sequences and advance the pointer by exactly one byte
// Ownership/Lifetime:
//   - Fonts created within a test are destroyed before the function returns
//   - Outline buffers (x, y, flags, contours) are freed by the test that
//     called ttf_get_glyph_outline
// Links: lib/gui/include/vg_font.h, lib/gui/src/vg_ttf_internal.h
//
//===----------------------------------------------------------------------===//

#include "vg_font.h"
#include "vg_ttf_internal.h"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_failed = 0;

#define EXPECT_TRUE(expr)                                                                            \
    do {                                                                                             \
        if (!(expr)) {                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                                   \
            tests_failed++;                                                                          \
            return;                                                                                  \
        }                                                                                            \
    } while (0)

#define EXPECT_NULL(expr) EXPECT_TRUE((expr) == NULL)
#define EXPECT_NOT_NULL(expr) EXPECT_TRUE((expr) != NULL)

/// @brief Write a big-endian uint16 to p.
static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

/// @brief Write a big-endian int16 to p (reuses put_u16 via unsigned reinterpret).
static void put_i16(uint8_t *p, int16_t value) {
    put_u16(p, (uint16_t)value);
}

/// @brief Write a big-endian uint32 to p.
static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

/// @brief Write a 16-byte TTF table directory entry at the given index slot.
static void put_table(uint8_t *dir, int index, const char tag[4], uint32_t offset, uint32_t length) {
    uint8_t *entry = dir + 12 + index * 16;
    memcpy(entry, tag, 4);
    put_u32(entry + 4, 0);
    put_u32(entry + 8, offset);
    put_u32(entry + 12, length);
}

/// @brief Copy table data into font at *offset, register it in the directory, and advance *offset.
static uint32_t append_table(uint8_t *font,
                             uint8_t *dir,
                             int *index,
                             uint32_t *offset,
                             const char tag[4],
                             const uint8_t *data,
                             uint32_t length) {
    uint32_t table_offset = *offset;
    memcpy(font + table_offset, data, length);
    put_table(dir, (*index)++, tag, table_offset, length);
    *offset += length;
    return table_offset;
}

enum cmap_mode {
    CMAP_VALID_FORMAT4,
    CMAP_TRUNCATED_FORMAT4,
    CMAP_BAD_FORMAT12,
};

/// @brief Serialise a cmap table into out according to mode; returns the byte length written.
static uint32_t build_cmap(uint8_t *out, enum cmap_mode mode) {
    put_u16(out + 0, 0);
    put_u16(out + 2, 1);
    put_u16(out + 4, 3);
    put_u16(out + 6, 1);
    put_u32(out + 8, 12);

    uint8_t *sub = out + 12;
    if (mode == CMAP_TRUNCATED_FORMAT4) {
        put_u16(sub + 0, 4);
        put_u16(sub + 2, 32);
        return 24;
    }

    if (mode == CMAP_BAD_FORMAT12) {
        put_u16(sub + 0, 12);
        put_u16(sub + 2, 0);
        put_u32(sub + 4, 16);
        put_u32(sub + 8, 0);
        put_u32(sub + 12, 1);
        return 28;
    }

    put_u16(sub + 0, 4);
    put_u16(sub + 2, 32);
    put_u16(sub + 4, 0);
    put_u16(sub + 6, 4);  // segCountX2, two segments
    put_u16(sub + 8, 4);
    put_u16(sub + 10, 1);
    put_u16(sub + 12, 0);
    put_u16(sub + 14, 0x0041);
    put_u16(sub + 16, 0xFFFF);
    put_u16(sub + 18, 0);
    put_u16(sub + 20, 0x0041);
    put_u16(sub + 22, 0xFFFF);
    put_i16(sub + 24, -64); // 'A' -> glyph 1
    put_i16(sub + 26, 1);   // sentinel -> glyph 0
    put_u16(sub + 28, 0);
    put_u16(sub + 30, 0);
    return 44;
}

/// @brief Build a 7-table minimal TTF into font; returns total byte length used.
static size_t build_minimal_font(uint8_t *font,
                                 enum cmap_mode cmap_mode,
                                 uint32_t glyf_len,
                                 uint32_t glyph1_next,
                                 uint16_t num_h_metrics,
                                 uint32_t hmtx_len) {
    memset(font, 0, 512);
    put_u32(font + 0, 0x00010000);
    put_u16(font + 4, 7);

    int index = 0;
    uint32_t offset = 12 + 7 * 16;

    uint8_t head[54] = {0};
    put_u16(head + 18, 1000);
    put_i16(head + 36, 0);
    put_i16(head + 38, 0);
    put_i16(head + 40, 1000);
    put_i16(head + 42, 1000);
    put_i16(head + 50, 1);
    append_table(font, font, &index, &offset, "head", head, sizeof(head));

    uint8_t hhea[36] = {0};
    put_i16(hhea + 4, 800);
    put_i16(hhea + 6, -200);
    put_i16(hhea + 8, 200);
    put_u16(hhea + 34, num_h_metrics);
    append_table(font, font, &index, &offset, "hhea", hhea, sizeof(hhea));

    uint8_t maxp[6] = {0};
    put_u16(maxp + 4, 2);
    append_table(font, font, &index, &offset, "maxp", maxp, sizeof(maxp));

    uint8_t cmap[64] = {0};
    uint32_t cmap_len = build_cmap(cmap, cmap_mode);
    append_table(font, font, &index, &offset, "cmap", cmap, cmap_len);

    uint8_t glyf[16] = {0};
    append_table(font, font, &index, &offset, "glyf", glyf, glyf_len);

    uint8_t loca[12] = {0};
    put_u32(loca + 0, 0);
    put_u32(loca + 4, 0);
    put_u32(loca + 8, glyph1_next);
    append_table(font, font, &index, &offset, "loca", loca, sizeof(loca));

    uint8_t hmtx[8] = {0};
    put_u16(hmtx + 0, 1000);
    put_i16(hmtx + 2, 0);
    put_u16(hmtx + 4, 1000);
    put_i16(hmtx + 6, 0);
    append_table(font, font, &index, &offset, "hmtx", hmtx, hmtx_len);

    return offset;
}

/// @brief A table with offset UINT32_MAX causes an integer-overflow wrap; vg_font_load must reject it.
static void test_wrapped_table_bounds_rejected(void) {
    uint8_t font[64] = {0};
    put_u32(font + 0, 0x00010000);
    put_u16(font + 4, 1);
    put_table(font, 0, "head", UINT32_MAX, 32);
    EXPECT_NULL(vg_font_load(font, sizeof(font)));
}

/// @brief A cmap format-4 subtable shorter than its declared length must cause vg_font_load to return NULL.
static void test_truncated_cmap4_rejected(void) {
    uint8_t font[512];
    size_t size = build_minimal_font(font, CMAP_TRUNCATED_FORMAT4, 10, 0, 2, 8);
    EXPECT_NULL(vg_font_load(font, size));
}

/// @brief A cmap format-12 subtable with an invalid group count must cause vg_font_load to return NULL.
static void test_bad_cmap12_rejected(void) {
    uint8_t font[512];
    size_t size = build_minimal_font(font, CMAP_BAD_FORMAT12, 10, 0, 2, 8);
    EXPECT_NULL(vg_font_load(font, size));
}

/// @brief Load succeeds for a font whose glyf data is too short; rasterize returns NULL without crashing.
static void test_truncated_glyf_rejected_on_rasterize(void) {
    uint8_t font_data[512];
    size_t size = build_minimal_font(font_data, CMAP_VALID_FORMAT4, 9, 9, 2, 8);
    vg_font_t *font = vg_font_load(font_data, size);
    EXPECT_NOT_NULL(font);
    EXPECT_TRUE(vg_font_has_glyph(font, 'A'));
    EXPECT_NULL(vg_font_get_glyph(font, 16.0f, 'A'));
    vg_font_destroy(font);
}

/// @brief A font with 0 hmtx metrics still returns a glyph with a non-zero fallback advance width.
static void test_zero_hmtx_metrics_use_fallback_advance(void) {
    uint8_t font_data[512];
    size_t size = build_minimal_font(font_data, CMAP_VALID_FORMAT4, 10, 0, 0, 0);
    vg_font_t *font = vg_font_load(font_data, size);
    EXPECT_NOT_NULL(font);
    const vg_glyph_t *glyph = vg_font_get_glyph(font, 16.0f, 'A');
    EXPECT_NOT_NULL(glyph);
    EXPECT_TRUE(glyph->advance == 16);
    vg_font_destroy(font);
}

/// @brief vg_utf8_decode returns U+FFFD and advances one byte for overlong, surrogate, and too-large sequences.
static void test_utf8_decoder_rejects_invalid_scalar_values(void) {
    const char overlong[] = {(char)0xC0, (char)0xAF, 0};
    const char surrogate[] = {(char)0xED, (char)0xA0, (char)0x80, 0};
    const char too_large[] = {(char)0xF4, (char)0x90, (char)0x80, (char)0x80, 0};

    const char *p = overlong;
    EXPECT_TRUE(vg_utf8_decode(&p) == 0xFFFD);
    EXPECT_TRUE(p == overlong + 1);

    p = surrogate;
    EXPECT_TRUE(vg_utf8_decode(&p) == 0xFFFD);
    EXPECT_TRUE(p == surrogate + 1);

    p = too_large;
    EXPECT_TRUE(vg_utf8_decode(&p) == 0xFFFD);
    EXPECT_TRUE(p == too_large + 1);
}

/// @brief A name-table Windows string with an odd byte length is parsed safely; family_name stays empty.
static void test_name_table_odd_utf16_length_does_not_overread(void) {
    uint8_t name[19] = {0};
    put_u16(name + 0, 0);  // format
    put_u16(name + 2, 1);  // count
    put_u16(name + 4, 18); // stringOffset
    put_u16(name + 6, 3);  // platformID: Windows
    put_u16(name + 8, 1);  // encodingID: Unicode BMP
    put_u16(name + 10, 0); // languageID
    put_u16(name + 12, 1); // nameID: family
    put_u16(name + 14, 1); // odd byte length
    put_u16(name + 16, 0); // offset
    name[18] = 0;

    vg_font_t font;
    memset(&font, 0, sizeof(font));
    EXPECT_TRUE(ttf_parse_name(&font, name, sizeof(name)));
    EXPECT_TRUE(font.family_name[0] == '\0');
}

/// @brief Serialise a minimal simple contour with two on-curve points at (0,0) and (10,0); returns byte length.
static size_t append_simple_two_point_glyph(uint8_t *out) {
    put_i16(out + 0, 1);
    put_i16(out + 2, 0);
    put_i16(out + 4, 0);
    put_i16(out + 6, 10);
    put_i16(out + 8, 0);
    put_u16(out + 10, 1); // end point index
    put_u16(out + 12, 0); // instruction length
    out[14] = 0x31;       // on-curve, x/y unchanged
    out[15] = 0x33;       // on-curve, +1-byte x, y unchanged
    out[16] = 10;         // x delta
    return 17;
}

/// @brief Serialise a composite glyph that aligns its component via point indices; returns byte length.
static size_t append_composite_point_aligned_glyph(uint8_t *out) {
    put_i16(out + 0, -1);
    put_i16(out + 2, 0);
    put_i16(out + 4, 0);
    put_i16(out + 6, 20);
    put_i16(out + 8, 0);

    put_u16(out + 10, 0x0023); // words, xy values, more components
    put_u16(out + 12, 0);      // glyph 0
    put_i16(out + 14, 0);
    put_i16(out + 16, 0);

    put_u16(out + 18, 0x0001); // words, point indices
    put_u16(out + 20, 0);      // glyph 0
    put_u16(out + 22, 1);      // parent point index
    put_u16(out + 24, 0);      // component point index
    return 26;
}

/// @brief Point-aligned composite glyph: component offset is derived from parent point 1 → child point 0 alignment.
static void test_composite_glyph_point_indices_align_component(void) {
    uint8_t data[128] = {0};
    uint32_t loca_offset = 0;
    uint32_t glyf_offset = 12;
    uint8_t *loca = data + loca_offset;
    uint8_t *glyf = data + glyf_offset;

    size_t glyph0_len = append_simple_two_point_glyph(glyf);
    size_t glyph1_len = append_composite_point_aligned_glyph(glyf + glyph0_len);
    put_u32(loca + 0, 0);
    put_u32(loca + 4, (uint32_t)glyph0_len);
    put_u32(loca + 8, (uint32_t)(glyph0_len + glyph1_len));

    vg_font_t font;
    memset(&font, 0, sizeof(font));
    font.data = data;
    font.data_size = sizeof(data);
    font.loca_offset = loca_offset;
    font.loca_len = 12;
    font.glyf_offset = glyf_offset;
    font.glyf_len = (uint32_t)(glyph0_len + glyph1_len);
    font.head.index_to_loc_format = 1;
    font.maxp.num_glyphs = 2;

    float *x = NULL;
    float *y = NULL;
    uint8_t *flags = NULL;
    int *contours = NULL;
    int num_points = 0;
    int num_contours = 0;
    EXPECT_TRUE(ttf_get_glyph_outline(
        &font, 1, &x, &y, &flags, &contours, &num_points, &num_contours));
    EXPECT_TRUE(num_points == 4);
    EXPECT_TRUE(num_contours == 2);
    EXPECT_TRUE(x[0] == 0.0f);
    EXPECT_TRUE(x[1] == 10.0f);
    EXPECT_TRUE(x[2] == 10.0f);
    EXPECT_TRUE(x[3] == 20.0f);
    EXPECT_TRUE(y[0] == 0.0f && y[1] == 0.0f && y[2] == 0.0f && y[3] == 0.0f);

    free(x);
    free(y);
    free(flags);
    free(contours);
}

static void test_nonfinite_and_huge_font_sizes_are_rejected(void) {
    vg_font_t font;
    memset(&font, 0, sizeof(font));
    font.head.units_per_em = 1000;
    font.hhea.ascent = 800;
    font.hhea.descent = -200;
    font.hhea.line_gap = 100;

    vg_font_metrics_t metrics;
    memset(&metrics, 0x7F, sizeof(metrics));
    vg_font_get_metrics(&font, NAN, &metrics);
    EXPECT_TRUE(metrics.ascent == 0);
    EXPECT_TRUE(metrics.descent == 0);
    EXPECT_TRUE(metrics.line_height == 0);
    EXPECT_TRUE(metrics.units_per_em == 1000);

    vg_text_metrics_t text_metrics;
    vg_font_measure_text(&font, INFINITY, "abc", &text_metrics);
    EXPECT_TRUE(text_metrics.width == 0.0f);
    EXPECT_TRUE(text_metrics.height == 0.0f);
    EXPECT_TRUE(text_metrics.glyph_count == 0);
    EXPECT_TRUE(vg_font_hit_test(&font, -INFINITY, "abc", 1.0f) == -1);
    EXPECT_TRUE(vg_font_get_cursor_x(&font, INFINITY, "abc", 1) == 0.0f);
}

/// @brief Run all font bounds-checking tests and print a single PASS line on success.
int main(void) {
    test_wrapped_table_bounds_rejected();
    test_truncated_cmap4_rejected();
    test_bad_cmap12_rejected();
    test_truncated_glyf_rejected_on_rasterize();
    test_zero_hmtx_metrics_use_fallback_advance();
    test_utf8_decoder_rejects_invalid_scalar_values();
    test_name_table_odd_utf16_length_does_not_overread();
    test_composite_glyph_point_indices_align_component();
    test_nonfinite_and_huge_font_sizes_are_rejected();
    if (tests_failed != 0)
        return 1;
    printf("test_vg_font_bounds: PASS\n");
    return 0;
}
