//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_datagrid.c
// Purpose: Headless tests for the data grid widget (Zanna.GUI.Grid), including
//          compatibility storage, sparse virtualization, selection, sorting,
//          resizing, editing, scrolling, keyboard input, and event edges.
// Key invariants:
//   - A column's width is its widest header/cell text plus 2 * cell_padding.
//   - With no font, column width is 0.
//   - A 10,000-row virtual table allocates only explicitly materialized cells.
//   - Selection, activation, sort, resize, and edit edges are independent.
// Ownership/Lifetime:
//   - Each grid owns copied header/cell strings and is destroyed before its
//     borrowed test font. Direct event calls use stack-owned payloads.
// Links: lib/gui/include/vg_ide_widgets_panels.h,
//        lib/gui/src/widgets/vg_datagrid.c
//
//===----------------------------------------------------------------------===//

#include "vg_event.h"
#include "vg_font.h"
#include "vg_ide_widgets.h"

#include <limits.h>
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

/// @brief Exercise the compatibility dense-storage and cached auto-width surface.
/// @param grid Empty grid under test.
/// @param font Borrowed live font used for deterministic measurement.
/// @return Zero on success, or one after emitting the first failed check.
static int test_dense_compatibility(vg_datagrid_t *grid, vg_font_t *font) {
    CHECK(grid != NULL);
    CHECK(vg_datagrid_column_count(grid) == 0);
    CHECK(vg_datagrid_row_count(grid) == 0);
    CHECK(vg_datagrid_logical_row_count(grid) == 0u);
    CHECK(grid->base.vtable->can_focus != NULL);
    CHECK(!grid->base.vtable->can_focus(&grid->base));

    vg_datagrid_set_columns(grid, 3);
    CHECK(vg_datagrid_column_count(grid) == 3);
    CHECK(vg_datagrid_column_width(grid, 0) == 0); // no font yet
    vg_datagrid_set_font(grid, font, 16.0f);

    vg_datagrid_set_header(grid, 0, "ID");
    vg_datagrid_set_cell(grid, 0, 0, "1");
    vg_datagrid_set_cell(grid, 1, 0, "22");
    vg_datagrid_set_cell(grid, 0, 1, "short");
    vg_datagrid_set_cell(grid, 1, 1, "a-longer-value");
    vg_datagrid_set_cell(grid, 0, 2, "x");

    CHECK(vg_datagrid_row_count(grid) == 2);
    CHECK(vg_datagrid_logical_row_count(grid) == 2u);
    const char *got = vg_datagrid_get_cell(grid, 1, 1);
    CHECK(got != NULL && strcmp(got, "a-longer-value") == 0);
    CHECK(vg_datagrid_get_cell(grid, 5, 0) == NULL);

    int w0 = vg_datagrid_column_width(grid, 0);
    int w1 = vg_datagrid_column_width(grid, 1);
    int w2 = vg_datagrid_column_width(grid, 2);
    CHECK(w0 > 0 && w1 > 0 && w2 > 0);
    CHECK(w1 > w0);
    CHECK(w1 > w2);

    vg_datagrid_set_cell(grid, 1, 1, "s");
    CHECK(vg_datagrid_column_width(grid, 1) < w1);
    uint64_t revision = vg_widget_get_revision(&grid->base);
    vg_datagrid_set_cell(grid, INT_MAX, 0, "overflow");
    CHECK(vg_datagrid_logical_row_count(grid) == 2u);
    CHECK(vg_widget_get_revision(&grid->base) == revision);

    vg_datagrid_clear(grid);
    CHECK(vg_datagrid_row_count(grid) == 0);
    CHECK(vg_datagrid_column_count(grid) == 3);
    CHECK(vg_datagrid_column_width(grid, 0) > vg_datagrid_column_width(grid, 1));
    return 0;
}

/// @brief Exercise sparse 10,000-by-20 virtualization and exact large row counts.
/// @param grid Grid under test; existing dense state is intentionally replaced.
/// @return Zero on success, or one after emitting the first failed check.
static int test_sparse_virtualization(vg_datagrid_t *grid) {
    vg_datagrid_set_columns(grid, 20);
    for (int col = 0; col < 20; ++col)
        CHECK(vg_datagrid_set_column_width(grid, col, 40.0f));
    vg_datagrid_set_virtual_row_count(grid, 10000u);

    CHECK(grid->virtual_mode);
    CHECK(grid->cells == NULL);
    CHECK(grid->row_capacity == 0);
    CHECK(grid->virtual_cell_count == 0u);
    CHECK(vg_datagrid_logical_row_count(grid) == 10000u);
    CHECK(vg_datagrid_row_count(grid) == 10000);

    CHECK(vg_datagrid_set_virtual_cell(grid, 0u, 0, "first"));
    CHECK(vg_datagrid_set_virtual_cell(grid, 1234u, 5, "middle"));
    CHECK(vg_datagrid_set_virtual_cell(grid, 9999u, 19, "last"));
    CHECK(grid->virtual_cell_count == 3u);
    CHECK(strcmp(vg_datagrid_get_cell(grid, 1234u, 5), "middle") == 0);
    CHECK(!vg_datagrid_set_virtual_cell(grid, 10000u, 0, "invalid"));
    CHECK(!vg_datagrid_set_virtual_cell(grid, 0u, 20, "invalid"));
    CHECK(grid->virtual_cell_count == 3u);

    uint64_t revision = vg_widget_get_revision(&grid->base);
    CHECK(vg_datagrid_set_virtual_cell(grid, 1234u, 5, "middle"));
    CHECK(vg_widget_get_revision(&grid->base) == revision);
    CHECK(vg_datagrid_set_virtual_cell(grid, 1234u, 5, NULL));
    CHECK(grid->virtual_cell_count == 2u);
    CHECK(vg_datagrid_get_cell(grid, 1234u, 5) == NULL);

    vg_datagrid_set_viewport_rows(grid, 1230u, 5u);
    CHECK(vg_datagrid_get_scroll_row(grid) == 1230u);
    CHECK(grid->viewport_row_count == 5u);
    vg_datagrid_scroll_to_row(grid, 20000u);
    CHECK(vg_datagrid_get_scroll_row(grid) == 9999u);

    vg_datagrid_set_virtual_row_count(grid, SIZE_MAX);
    CHECK(vg_datagrid_logical_row_count(grid) == SIZE_MAX);
    CHECK(vg_datagrid_row_count(grid) == INT_MAX);
    CHECK(vg_datagrid_set_virtual_cell(grid, SIZE_MAX - 1u, 1, "huge"));
    CHECK(strcmp(vg_datagrid_get_cell(grid, SIZE_MAX - 1u, 1), "huge") == 0);

    vg_datagrid_set_virtual_row_count(grid, 10000u);
    CHECK(vg_datagrid_logical_row_count(grid) == 10000u);
    CHECK(vg_datagrid_get_cell(grid, SIZE_MAX - 1u, 1) == NULL);
    CHECK(grid->virtual_cell_count == 2u);
    return 0;
}

/// @brief Exercise selection, keyboard activation, sorting, resizing, editing, and scroll edges.
/// @param grid Virtual grid with 10,000 rows and twenty fixed-width columns.
/// @return Zero on success, or one after emitting the first failed check.
static int test_interactive_surface(vg_datagrid_t *grid) {
    while (vg_widget_was_changed(&grid->base)) {
    }
    vg_datagrid_set_selectable(grid, true);
    CHECK(grid->base.vtable->can_focus(&grid->base));
    CHECK(vg_datagrid_select_cell(grid, 1234u, 5));
    CHECK(vg_datagrid_get_selected_row(grid) == 1234u);
    CHECK(vg_datagrid_get_selected_column(grid) == 5);
    CHECK(vg_datagrid_was_selection_changed(grid));
    CHECK(!vg_datagrid_was_selection_changed(grid));
    CHECK(strstr(vg_widget_get_accessible_value(&grid->base), "row 1235 column 6") != NULL);

    vg_datagrid_set_viewport_rows(grid, 1233u, 2u);
    vg_event_t down = {.type = VG_EVENT_KEY_DOWN, .key.key = VG_KEY_DOWN};
    CHECK(grid->base.vtable->handle_event(&grid->base, &down));
    CHECK(vg_datagrid_get_selected_row(grid) == 1235u);
    CHECK(vg_datagrid_get_scroll_row(grid) == 1234u);
    CHECK(vg_datagrid_was_selection_changed(grid));

    vg_event_t enter = {.type = VG_EVENT_KEY_DOWN, .key.key = VG_KEY_ENTER};
    CHECK(grid->base.vtable->handle_event(&grid->base, &enter));
    CHECK(vg_widget_was_activated(&grid->base));
    CHECK(!vg_widget_was_activated(&grid->base));

    vg_datagrid_set_editable(grid, true);
    vg_datagrid_set_selectable(grid, false);
    vg_event_t f2 = {.type = VG_EVENT_KEY_DOWN, .key.key = VG_KEY_F2};
    CHECK(grid->base.vtable->handle_event(&grid->base, &f2));
    CHECK(vg_datagrid_is_editing(grid));
    CHECK(vg_datagrid_commit_edit(grid, "keyboard edit"));
    CHECK(strcmp(vg_datagrid_get_cell(grid, 0u, 0), "keyboard edit") == 0);
    CHECK(vg_datagrid_was_cell_edited(grid));
    CHECK(!vg_datagrid_was_cell_edited(grid));
    CHECK(vg_datagrid_begin_edit(grid, 1u, 1));
    vg_datagrid_cancel_edit(grid);
    CHECK(!vg_datagrid_is_editing(grid));
    CHECK(!vg_datagrid_was_cell_edited(grid));

    vg_datagrid_set_header(grid, 0, "A");
    vg_datagrid_set_sortable(grid, 2, true);
    CHECK(vg_datagrid_set_sort(grid, 2, 99));
    CHECK(vg_datagrid_get_sort_column(grid) == 2);
    CHECK(vg_datagrid_get_sort_direction(grid) == 1);
    CHECK(vg_datagrid_was_sort_changed(grid));
    uint64_t revision = vg_widget_get_revision(&grid->base);
    CHECK(vg_datagrid_set_sort(grid, 2, 1));
    CHECK(vg_widget_get_revision(&grid->base) == revision);
    CHECK(!vg_datagrid_was_sort_changed(grid));

    vg_event_t header_click = {.type = VG_EVENT_CLICK,
                               .mouse = {.x = 90.0f, .y = 5.0f, .button = VG_MOUSE_LEFT}};
    CHECK(grid->base.vtable->handle_event(&grid->base, &header_click));
    CHECK(vg_datagrid_get_sort_direction(grid) == -1);
    CHECK(vg_datagrid_was_sort_changed(grid));

    vg_datagrid_set_column_resizable(grid, 1, true);
    while (vg_datagrid_was_column_resized(grid)) {
    }
    vg_event_t resize_down = {.type = VG_EVENT_MOUSE_DOWN,
                              .mouse = {.x = 80.0f, .y = 5.0f, .button = VG_MOUSE_LEFT}};
    vg_event_t resize_move = {.type = VG_EVENT_MOUSE_MOVE,
                              .mouse = {.x = 100.0f, .y = 5.0f, .button = VG_MOUSE_LEFT}};
    vg_event_t resize_up = {.type = VG_EVENT_MOUSE_UP,
                            .mouse = {.x = 100.0f, .y = 5.0f, .button = VG_MOUSE_LEFT}};
    CHECK(grid->base.vtable->handle_event(&grid->base, &resize_down));
    CHECK(vg_widget_get_input_capture() == &grid->base);
    CHECK(grid->base.vtable->handle_event(&grid->base, &resize_move));
    CHECK(grid->base.vtable->handle_event(&grid->base, &resize_up));
    CHECK(vg_widget_get_input_capture() == NULL);
    CHECK(vg_datagrid_column_width(grid, 1) == 60);
    CHECK(vg_datagrid_was_column_resized(grid));
    CHECK(vg_datagrid_get_resized_column(grid) == 1);
    int sort_direction = vg_datagrid_get_sort_direction(grid);
    vg_event_t suppressed_click = {.type = VG_EVENT_CLICK,
                                   .mouse = {.x = 100.0f, .y = 5.0f, .button = VG_MOUSE_LEFT}};
    CHECK(grid->base.vtable->handle_event(&grid->base, &suppressed_click));
    CHECK(vg_datagrid_get_sort_direction(grid) == sort_direction);

    vg_datagrid_scroll_to_row(grid, 9999u);
    vg_event_t wheel_down = {.type = VG_EVENT_MOUSE_WHEEL, .wheel.delta_y = -1.0f};
    CHECK(!grid->base.vtable->handle_event(&grid->base, &wheel_down));
    vg_event_t wheel_up = {.type = VG_EVENT_MOUSE_WHEEL, .wheel.delta_y = 1.0f};
    CHECK(grid->base.vtable->handle_event(&grid->base, &wheel_up));
    CHECK(vg_datagrid_get_scroll_row(grid) == 9996u);

    vg_datagrid_set_editable(grid, false);
    vg_datagrid_set_sortable(grid, 2, false);
    vg_datagrid_set_column_resizable(grid, 1, false);
    CHECK(!grid->base.vtable->can_focus(&grid->base));
    return 0;
}

int main(void) {
    vg_datagrid_t *grid = vg_datagrid_create(NULL);
    uint8_t blob[512];
    size_t blob_size = build_minimal_test_font(blob);
    vg_font_t *font = vg_font_load(blob, blob_size);
    CHECK(font != NULL);

    CHECK(test_dense_compatibility(grid, font) == 0);
    CHECK(test_sparse_virtualization(grid) == 0);
    CHECK(test_interactive_surface(grid) == 0);

    vg_datagrid_destroy(grid);
    vg_font_destroy(font);

    printf("test_vg_datagrid: OK\n");
    return 0;
}
