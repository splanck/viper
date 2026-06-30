//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_datagrid.c
// Purpose: Headless tests for the data grid widget (Viper.GUI.Grid) — column
//          auto-sizing, row/column counts, cell storage, and clear.
// Key invariants:
//   - A column's width is its widest header/cell text plus 2 * cell_padding.
//   - With no font, column width is 0.
//
//===----------------------------------------------------------------------===//

#include "vg_font.h"
#include "vg_ide_widgets.h"

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

// --- Minimal in-memory TrueType font (monospace, 1000-unit advances). ---

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
    vg_datagrid_t *grid = vg_datagrid_create(NULL);
    CHECK(grid != NULL);
    CHECK(vg_datagrid_column_count(grid) == 0);
    CHECK(vg_datagrid_row_count(grid) == 0);

    vg_datagrid_set_columns(grid, 3);
    CHECK(vg_datagrid_column_count(grid) == 3);
    CHECK(vg_datagrid_column_width(grid, 0) == 0); // no font yet

    // Real monospace font (1000-unit advances) at 16px.
    uint8_t blob[512];
    size_t blob_size = build_minimal_test_font(blob);
    vg_font_t *font = vg_font_load(blob, blob_size);
    CHECK(font != NULL);
    vg_datagrid_set_font(grid, font, 16.0f);

    vg_datagrid_set_header(grid, 0, "ID");
    vg_datagrid_set_cell(grid, 0, 0, "1");
    vg_datagrid_set_cell(grid, 1, 0, "22");
    vg_datagrid_set_cell(grid, 0, 1, "short");
    vg_datagrid_set_cell(grid, 1, 1, "a-longer-value"); // widest cell in column 1
    vg_datagrid_set_cell(grid, 0, 2, "x");

    CHECK(vg_datagrid_row_count(grid) == 2);
    const char *got = vg_datagrid_get_cell(grid, 1, 1);
    CHECK(got != NULL && strcmp(got, "a-longer-value") == 0);
    CHECK(vg_datagrid_get_cell(grid, 5, 0) == NULL); // out of range

    // Auto-sizing: a column tracks its widest content.
    int w0 = vg_datagrid_column_width(grid, 0);
    int w1 = vg_datagrid_column_width(grid, 1);
    int w2 = vg_datagrid_column_width(grid, 2);
    CHECK(w0 > 0 && w1 > 0 && w2 > 0);
    CHECK(w1 > w0); // "a-longer-value" wider than "22"/"ID"
    CHECK(w1 > w2); // wider than "x"

    // Replacing the widest cell with a shorter one shrinks the column.
    vg_datagrid_set_cell(grid, 1, 1, "s");
    int w1_after = vg_datagrid_column_width(grid, 1);
    CHECK(w1_after < w1);

    // Clear removes rows but keeps columns and headers.
    vg_datagrid_clear(grid);
    CHECK(vg_datagrid_row_count(grid) == 0);
    CHECK(vg_datagrid_column_count(grid) == 3);
    CHECK(vg_datagrid_column_width(grid, 0) > vg_datagrid_column_width(grid, 1)); // header vs empty

    vg_datagrid_destroy(grid);
    vg_font_destroy(font);

    printf("test_vg_datagrid: OK\n");
    return 0;
}
