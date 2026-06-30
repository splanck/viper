//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_outputpane_metrics.c
// Purpose: Headless tests for OutputPane text/cell metrics (cell width/height,
//          MeasureText, ColumnsForWidth, RowsForHeight) backing runtime R2.
// Key invariants:
//   - With no font every metric returns 0 (safe degenerate state).
//   - MeasureText("M") == cell width; ColumnsForWidth == floor(width / cellWidth).
//
//===----------------------------------------------------------------------===//

#include "vg_font.h"
#include "vg_ide_widgets_panels.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// --- Minimal in-memory TrueType font (monospace, 1000-unit advances), so the
//     metric paths can be exercised without a real font file or window. ---

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

static void put_table(
    uint8_t *dir, int index, const char tag[4], uint32_t offset, uint32_t length) {
    uint8_t *entry = dir + 12 + index * 16;
    memcpy(entry, tag, 4);
    put_u32(entry + 4, 0);
    put_u32(entry + 8, offset);
    put_u32(entry + 12, length);
}

static void append_table(uint8_t *font,
                         uint8_t *dir,
                         int *index,
                         uint32_t *offset,
                         const char tag[4],
                         const uint8_t *data,
                         uint32_t length) {
    memcpy(font + *offset, data, length);
    put_table(dir, (*index)++, tag, *offset, length);
    *offset += length;
}

static size_t build_minimal_test_font(uint8_t *font) {
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
    put_u16(hhea + 34, 2);
    append_table(font, font, &index, &offset, "hhea", hhea, sizeof(hhea));

    uint8_t maxp[6] = {0};
    put_u16(maxp + 4, 2);
    append_table(font, font, &index, &offset, "maxp", maxp, sizeof(maxp));

    uint8_t cmap[64] = {0};
    put_u16(cmap + 0, 0);
    put_u16(cmap + 2, 1);
    put_u16(cmap + 4, 3);
    put_u16(cmap + 6, 1);
    put_u32(cmap + 8, 12);
    uint8_t *sub = cmap + 12;
    put_u16(sub + 0, 4);
    put_u16(sub + 2, 32);
    put_u16(sub + 4, 0);
    put_u16(sub + 6, 4);
    put_u16(sub + 8, 4);
    put_u16(sub + 10, 1);
    put_u16(sub + 12, 0);
    put_u16(sub + 14, 0x0041);
    put_u16(sub + 16, 0xFFFF);
    put_u16(sub + 18, 0);
    put_u16(sub + 20, 0x0041);
    put_u16(sub + 22, 0xFFFF);
    put_i16(sub + 24, -64);
    put_i16(sub + 26, 1);
    put_u16(sub + 28, 0);
    put_u16(sub + 30, 0);
    append_table(font, font, &index, &offset, "cmap", cmap, 44);

    uint8_t glyf[10] = {0};
    append_table(font, font, &index, &offset, "glyf", glyf, sizeof(glyf));

    uint8_t loca[12] = {0};
    put_u32(loca + 0, 0);
    put_u32(loca + 4, 0);
    put_u32(loca + 8, 0);
    append_table(font, font, &index, &offset, "loca", loca, sizeof(loca));

    uint8_t hmtx[8] = {0};
    put_u16(hmtx + 0, 1000);
    put_i16(hmtx + 2, 0);
    put_u16(hmtx + 4, 1000);
    put_i16(hmtx + 6, 0);
    append_table(font, font, &index, &offset, "hmtx", hmtx, sizeof(hmtx));

    return offset;
}

int main(void) {
    // 1. No font set: every metric is a safe 0.
    vg_outputpane_t *pane = vg_outputpane_create();
    CHECK(pane != NULL);
    CHECK(vg_outputpane_cell_width(pane) == 0);
    CHECK(vg_outputpane_cell_height(pane) == 0);
    CHECK(vg_outputpane_measure_text(pane, "Mxyz") == 0);
    CHECK(vg_outputpane_columns_for_width(pane) == 0);
    CHECK(vg_outputpane_rows_for_height(pane) == 0);

    // 2. A real monospace font (1000-unit advances) at 16px.
    uint8_t blob[512];
    size_t blob_size = build_minimal_test_font(blob);
    vg_font_t *font = vg_font_load(blob, blob_size);
    CHECK(font != NULL);
    vg_outputpane_set_font(pane, font, 16.0f);

    int cw = vg_outputpane_cell_width(pane);
    int ch = vg_outputpane_cell_height(pane);
    CHECK(cw > 0);
    CHECK(ch > 0);

    // MeasureText: empty is 0, one glyph is exactly one cell, longer text grows.
    CHECK(vg_outputpane_measure_text(pane, "") == 0);
    CHECK(vg_outputpane_measure_text(pane, "M") == cw);
    CHECK(vg_outputpane_measure_text(pane, "MMMM") > cw);

    // 3. Columns/rows floor the arranged area by the cell metrics.
    pane->base.width = (float)(cw * 20);
    pane->base.height = (float)(ch * 8);
    CHECK(vg_outputpane_columns_for_width(pane) == 20);
    CHECK(vg_outputpane_rows_for_height(pane) == 8);

    // A partial trailing cell floors down rather than rounding up.
    pane->base.width = (float)(cw * 20) + (float)cw * 0.5f;
    pane->base.height = (float)(ch * 8) + (float)ch * 0.5f;
    CHECK(vg_outputpane_columns_for_width(pane) == 20);
    CHECK(vg_outputpane_rows_for_height(pane) == 8);

    // Sub-cell area yields zero columns/rows.
    pane->base.width = (float)(cw - 1);
    pane->base.height = (float)(ch - 1);
    CHECK(vg_outputpane_columns_for_width(pane) == 0);
    CHECK(vg_outputpane_rows_for_height(pane) == 0);

    vg_outputpane_destroy(pane);
    vg_font_destroy(font);

    printf("test_vg_outputpane_metrics: OK\n");
    return 0;
}
