//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "vg_font.h"

#include <stdint.h>
#include <stdio.h>
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

static void put_u16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put_i16(uint8_t *p, int16_t value) {
    put_u16(p, (uint16_t)value);
}

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_table(uint8_t *dir, int index, const char tag[4], uint32_t offset, uint32_t length) {
    uint8_t *entry = dir + 12 + index * 16;
    memcpy(entry, tag, 4);
    put_u32(entry + 4, 0);
    put_u32(entry + 8, offset);
    put_u32(entry + 12, length);
}

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

static void test_wrapped_table_bounds_rejected(void) {
    uint8_t font[64] = {0};
    put_u32(font + 0, 0x00010000);
    put_u16(font + 4, 1);
    put_table(font, 0, "head", UINT32_MAX, 32);
    EXPECT_NULL(vg_font_load(font, sizeof(font)));
}

static void test_truncated_cmap4_rejected(void) {
    uint8_t font[512];
    size_t size = build_minimal_font(font, CMAP_TRUNCATED_FORMAT4, 10, 0, 2, 8);
    EXPECT_NULL(vg_font_load(font, size));
}

static void test_bad_cmap12_rejected(void) {
    uint8_t font[512];
    size_t size = build_minimal_font(font, CMAP_BAD_FORMAT12, 10, 0, 2, 8);
    EXPECT_NULL(vg_font_load(font, size));
}

static void test_truncated_glyf_rejected_on_rasterize(void) {
    uint8_t font_data[512];
    size_t size = build_minimal_font(font_data, CMAP_VALID_FORMAT4, 9, 9, 2, 8);
    vg_font_t *font = vg_font_load(font_data, size);
    EXPECT_NOT_NULL(font);
    EXPECT_TRUE(vg_font_has_glyph(font, 'A'));
    EXPECT_NULL(vg_font_get_glyph(font, 16.0f, 'A'));
    vg_font_destroy(font);
}

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

int main(void) {
    test_wrapped_table_bounds_rejected();
    test_truncated_cmap4_rejected();
    test_bad_cmap12_rejected();
    test_truncated_glyf_rejected_on_rasterize();
    test_zero_hmtx_metrics_use_fallback_advance();
    if (tests_failed != 0)
        return 1;
    printf("test_vg_font_bounds: PASS\n");
    return 0;
}
