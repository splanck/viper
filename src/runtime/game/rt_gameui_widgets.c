//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_gameui_widgets.c
// Purpose: Interactive GameUI widgets — UITable, UISlider, UIDropdown,
//          UITooltip, and UIModal — for the immediate-mode game UI. Split out
//          of rt_gameui.c; shares geometry/text/key helpers via
//          rt_gameui_internal.h.
//
// Key invariants:
//   - Widgets are immediate-mode: each call validates its canvas and draws
//     against the current frame; no retained widget tree.
//   - Input handling uses the UI_KEY_* codes and the shared hit-test helpers.
//
// Ownership/Lifetime:
//   - Borrows caller-owned canvas/font/state objects; releases temporaries via
//     ui_release_obj.
//
// Links: src/runtime/game/rt_gameui.c (core widgets + shared helpers),
//        src/runtime/game/rt_gameui_internal.h (shared helpers + key codes)
//
//===----------------------------------------------------------------------===//

#include "rt_gameui.h"
#include "rt_gameui_internal.h"

#include "rt_bitmapfont.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_numeric.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_pixels.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// UITable
//=============================================================================

#define RT_UITABLE_DEFAULT_COLUMNS 16
#define RT_UITABLE_DEFAULT_ROWS 512
#define RT_UITABLE_MAX_CELL_BYTES 64

typedef struct {
    char title[64];
    int64_t width;
    int8_t align;
    int8_t sortable;
    int8_t sort_numeric;
} rt_uitable_column_t;

typedef struct {
    char *cells;
} rt_uitable_row_t;

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    rt_uitable_column_t *columns;
    int64_t column_count;
    int64_t column_capacity;
    rt_uitable_row_t *rows;
    int64_t row_count;
    int64_t row_capacity;
    int64_t header_height;
    int64_t row_height;
    int64_t sort_column;
    int8_t sort_descending;
    int64_t scroll_offset;
    int64_t selected_row;
    int64_t last_header_click;
    int64_t text_color;
    int64_t header_text_color;
    int64_t header_bg_color;
    int64_t row_bg_color;
    int64_t row_alt_bg_color;
    int64_t selected_bg_color;
    int64_t border_color;
    void *font;
    int8_t visible;
    int8_t striped;
    int8_t show_header;
    int8_t show_borders;
} rt_uitable_impl;

/// @brief Immutable snapshot describing the outcome of a table click.
typedef struct {
    void *vptr;
    int64_t kind;
    int64_t row;
    int64_t column;
} rt_table_click_result_impl;

/// @brief Parse a table cell as a finite C-locale number for numeric sorting.
/// @details Invalid or non-finite text is reported separately so the comparator
///          does not accidentally treat malformed values as 0.0.
/// @param text Cell text to parse.
/// @param out_value Receives the parsed double on success.
/// @return 1 for a valid finite number; otherwise 0.
static int table_parse_sort_number(const char *text, double *out_value) {
    if (!text || !out_value)
        return 0;
    if (rt_parse_double(text, out_value) != (int32_t)Err_None)
        return 0;
    return isfinite(*out_value) ? 1 : 0;
}

/// @brief Safe-cast a handle to the UITable impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_uitable_impl *checked_table(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITABLE_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uitable_impl *)ptr;
}

/// @brief Safe-cast a handle to a TableClickResult impl.
/// @param ptr Candidate TableClickResult object.
/// @param api Public API name for trap diagnostics.
/// @return The impl, or NULL if @p ptr is NULL.
static rt_table_click_result_impl *checked_table_click_result(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITABLE_CLICK_RESULT_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_table_click_result_impl *)ptr;
}

/// @brief Create a TableClickResult snapshot.
/// @param kind One of RT_UITABLE_CLICK_NONE, RT_UITABLE_CLICK_ROW, or RT_UITABLE_CLICK_HEADER.
/// @param row Row index for row clicks; ignored for other kinds.
/// @param column Column index for header clicks; ignored for other kinds.
/// @return New Zanna.Game.UI.TableClickResult object.
static void *table_click_result_new(int64_t kind, int64_t row, int64_t column) {
    rt_table_click_result_impl *result = (rt_table_click_result_impl *)rt_obj_new_i64(
        RT_UITABLE_CLICK_RESULT_CLASS_ID, (int64_t)sizeof(rt_table_click_result_impl));
    if (!result) {
        rt_trap("UITable.HandleClickResult: allocation failed");
        return NULL;
    }
    result->kind = kind;
    result->row = kind == RT_UITABLE_CLICK_ROW ? row : -1;
    result->column = kind == RT_UITABLE_CLICK_HEADER ? column : -1;
    return result;
}

/// @brief GC finalizer: free the table's column/row/cell storage.
static void uitable_finalizer(void *obj) {
    rt_uitable_impl *table = (rt_uitable_impl *)obj;
    if (!table)
        return;
    if (table->rows) {
        for (int64_t i = 0; i < table->row_count; i++)
            free(table->rows[i].cells);
    }
    free(table->rows);
    table->rows = NULL;
    table->row_count = 0;
    table->row_capacity = 0;
    free(table->columns);
    table->columns = NULL;
    table->column_count = 0;
    table->column_capacity = 0;
    ui_release_obj(table->font);
    table->font = NULL;
}

/// @brief Number of data rows that fit in the table's body area (height
///        minus the header row, divided by row height).
static int64_t table_visible_rows(rt_uitable_impl *table) {
    if (!table || table->row_height <= 0)
        return 0;
    int64_t usable = table->h - (table->show_header ? table->header_height : 0);
    if (usable <= 0)
        return 0;
    return usable / table->row_height;
}

/// @brief Clamp the scroll offset so the view never scrolls past the last
///        page of rows (called after row add/remove or resize).
static void table_clamp_scroll(rt_uitable_impl *table) {
    if (!table)
        return;
    int64_t visible_rows = table_visible_rows(table);
    int64_t max_scroll = table->row_count > visible_rows ? table->row_count - visible_rows : 0;
    if (table->scroll_offset < 0)
        table->scroll_offset = 0;
    if (table->scroll_offset > max_scroll)
        table->scroll_offset = max_scroll;
    if (table->selected_row >= table->row_count)
        table->selected_row = table->row_count - 1;
    if (table->row_count <= 0)
        table->selected_row = -1;
}

/// @brief Advance an x cursor by a column @p width with saturation
///        (next column's left edge during header/cell layout).
static int64_t table_column_next_x(int64_t x, int64_t width) {
    if (width <= 0)
        return x;
    return ui_add_sat_i64(x, width);
}

static char *table_cell_ptr(const rt_uitable_impl *table,
                            const rt_uitable_row_t *row,
                            int64_t col) {
    if (!table || !row || !row->cells || col < 0 || col >= table->column_capacity)
        return NULL;
    return row->cells + (size_t)col * RT_UITABLE_MAX_CELL_BYTES;
}

static int8_t ensure_table_row_capacity(rt_uitable_impl *table, int64_t needed) {
    if (!table || needed <= table->row_capacity)
        return 1;
    int64_t new_capacity = table->row_capacity > 0 ? table->row_capacity : 1;
    while (new_capacity < needed) {
        if (new_capacity > INT64_MAX / 2 ||
            (uint64_t)new_capacity > SIZE_MAX / (2 * sizeof(rt_uitable_row_t)))
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > SIZE_MAX / sizeof(rt_uitable_row_t))
        return 0;
    rt_uitable_row_t *resized =
        (rt_uitable_row_t *)realloc(table->rows, (size_t)new_capacity * sizeof(rt_uitable_row_t));
    if (!resized)
        return 0;
    memset(resized + table->row_capacity,
           0,
           (size_t)(new_capacity - table->row_capacity) * sizeof(rt_uitable_row_t));
    table->rows = resized;
    table->row_capacity = new_capacity;
    return 1;
}

static int8_t ensure_table_column_capacity(rt_uitable_impl *table, int64_t needed) {
    if (!table || needed <= table->column_capacity)
        return 1;
    int64_t new_capacity = table->column_capacity > 0 ? table->column_capacity : 1;
    while (new_capacity < needed) {
        if (new_capacity > INT64_MAX / 2 ||
            (uint64_t)new_capacity > SIZE_MAX / (2 * sizeof(rt_uitable_column_t)))
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > SIZE_MAX / sizeof(rt_uitable_column_t) ||
        (uint64_t)new_capacity > SIZE_MAX / RT_UITABLE_MAX_CELL_BYTES)
        return 0;
    rt_uitable_column_t *columns = (rt_uitable_column_t *)realloc(
        table->columns, (size_t)new_capacity * sizeof(rt_uitable_column_t));
    if (!columns)
        return 0;
    memset(columns + table->column_capacity,
           0,
           (size_t)(new_capacity - table->column_capacity) * sizeof(rt_uitable_column_t));
    table->columns = columns;

    for (int64_t i = 0; i < table->row_count; i++) {
        char *cells =
            (char *)realloc(table->rows[i].cells, (size_t)new_capacity * RT_UITABLE_MAX_CELL_BYTES);
        if (!cells)
            return 0;
        memset(cells + (size_t)table->column_capacity * RT_UITABLE_MAX_CELL_BYTES,
               0,
               (size_t)(new_capacity - table->column_capacity) * RT_UITABLE_MAX_CELL_BYTES);
        table->rows[i].cells = cells;
    }

    table->column_capacity = new_capacity;
    return 1;
}

static int8_t table_alloc_row_cells(rt_uitable_impl *table, rt_uitable_row_t *row) {
    if (!table || !row)
        return 0;
    if (table->column_capacity <= 0 &&
        !ensure_table_column_capacity(table, RT_UITABLE_DEFAULT_COLUMNS))
        return 0;
    row->cells = (char *)calloc((size_t)table->column_capacity, RT_UITABLE_MAX_CELL_BYTES);
    return row->cells ? 1 : 0;
}

void *rt_uitable_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uitable_impl *table =
        (rt_uitable_impl *)rt_obj_new_i64(RT_UITABLE_CLASS_ID, (int64_t)sizeof(*table));
    if (!table)
        return NULL;
    memset(table, 0, sizeof(*table));
    table->x = x;
    table->y = y;
    table->w = ui_clamp_dim(w);
    table->h = ui_clamp_dim(h);
    table->header_height = 22;
    table->row_height = 20;
    table->sort_column = -1;
    table->selected_row = -1;
    table->last_header_click = -1;
    table->text_color = 0xFFFFFF;
    table->header_text_color = 0xFFFFFF;
    table->header_bg_color = 0x303030;
    table->row_bg_color = 0x181818;
    table->row_alt_bg_color = 0x202020;
    table->selected_bg_color = 0x304A70;
    table->border_color = 0x606060;
    table->visible = 1;
    table->striped = 1;
    table->show_header = 1;
    table->show_borders = 1;
    rt_obj_set_finalizer(table, uitable_finalizer);
    if (!ensure_table_column_capacity(table, RT_UITABLE_DEFAULT_COLUMNS) ||
        !ensure_table_row_capacity(table, RT_UITABLE_DEFAULT_ROWS)) {
        if (rt_obj_release_check0(table))
            rt_obj_free(table);
        return NULL;
    }
    return table;
}

int64_t rt_uitable_add_column(void *ptr, rt_string title, int64_t width, int64_t align) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.AddColumn: expected Zanna.Game.UI.Table");
    if (!table)
        return -1;
    if (!ensure_table_column_capacity(table, table->column_count + 1)) {
        rt_trap("UITable.AddColumn: column allocation failed");
        return -1;
    }
    int64_t idx = table->column_count++;
    ui_copy_text(table->columns[idx].title, sizeof(table->columns[idx].title), title);
    table->columns[idx].width = width > 0 ? width : 80;
    table->columns[idx].align = (int8_t)(align < 0 || align > 2 ? 0 : align);
    table->columns[idx].sortable = 0;
    table->columns[idx].sort_numeric = 0;
    return idx;
}

void rt_uitable_set_column_sortable(void *ptr, int64_t col, int8_t sortable, int8_t numeric) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SetColumnSortable: expected Zanna.Game.UI.Table");
    if (!table || col < 0 || col >= table->column_count)
        return;
    table->columns[col].sortable = sortable ? 1 : 0;
    table->columns[col].sort_numeric = numeric ? 1 : 0;
}

int64_t rt_uitable_column_count(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.ColumnCount: expected Zanna.Game.UI.Table");
    return table ? table->column_count : 0;
}

int64_t rt_uitable_add_row(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.AddRow: expected Zanna.Game.UI.Table");
    if (!table)
        return -1;
    if (!ensure_table_row_capacity(table, table->row_count + 1)) {
        rt_trap("UITable.AddRow: row allocation failed");
        return -1;
    }
    int64_t idx = table->row_count++;
    memset(&table->rows[idx], 0, sizeof(table->rows[idx]));
    if (!table_alloc_row_cells(table, &table->rows[idx])) {
        table->row_count--;
        rt_trap("UITable.AddRow: cell allocation failed");
        return -1;
    }
    if (table->selected_row < 0)
        table->selected_row = 0;
    table_clamp_scroll(table);
    return idx;
}

void rt_uitable_set_cell(void *ptr, int64_t row, int64_t col, rt_string text) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SetCell: expected Zanna.Game.UI.Table");
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count)
        return;
    char *cell = table_cell_ptr(table, &table->rows[row], col);
    if (cell)
        ui_copy_text(cell, RT_UITABLE_MAX_CELL_BYTES, text);
}

rt_string rt_uitable_get_cell(void *ptr, int64_t row, int64_t col) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.GetCell: expected Zanna.Game.UI.Table");
    if (!table || row < 0 || row >= table->row_count || col < 0 || col >= table->column_count)
        return rt_str_empty();
    char *cell = table_cell_ptr(table, &table->rows[row], col);
    return cell ? rt_const_cstr(cell) : rt_str_empty();
}

void rt_uitable_remove_row(void *ptr, int64_t row) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.RemoveRow: expected Zanna.Game.UI.Table");
    if (!table || row < 0 || row >= table->row_count)
        return;
    free(table->rows[row].cells);
    for (int64_t i = row; i < table->row_count - 1; i++)
        table->rows[i] = table->rows[i + 1];
    table->row_count--;
    memset(&table->rows[table->row_count], 0, sizeof(table->rows[table->row_count]));
    table_clamp_scroll(table);
}

void rt_uitable_clear_rows(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.ClearRows: expected Zanna.Game.UI.Table");
    if (!table)
        return;
    for (int64_t i = 0; i < table->row_count; i++) {
        free(table->rows[i].cells);
        table->rows[i].cells = NULL;
    }
    table->row_count = 0;
    table->scroll_offset = 0;
    table->selected_row = -1;
}

int64_t rt_uitable_row_count(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.RowCount: expected Zanna.Game.UI.Table");
    return table ? table->row_count : 0;
}

/// @brief Comparator for sorting two rows by the table's active sort column
///        (numeric or lexicographic per the column's flag).
/// @return <0, 0, >0 like strcmp (caller applies the descending flip).
static int table_compare_rows(rt_uitable_impl *table,
                              const rt_uitable_row_t *a,
                              const rt_uitable_row_t *b) {
    int64_t col = table->sort_column;
    if (col < 0 || col >= table->column_count)
        return 0;
    const char *sa = table_cell_ptr(table, a, col);
    const char *sb = table_cell_ptr(table, b, col);
    if (!sa)
        sa = "";
    if (!sb)
        sb = "";
    int cmp = 0;
    if (table->columns[col].sort_numeric) {
        double da = 0.0;
        double db = 0.0;
        int a_valid = table_parse_sort_number(sa, &da);
        int b_valid = table_parse_sort_number(sb, &db);
        if (a_valid && b_valid)
            cmp = da < db ? -1 : (da > db ? 1 : 0);
        else if (a_valid != b_valid)
            cmp = a_valid ? -1 : 1;
        else
            cmp = strcmp(sa, sb);
    } else {
        cmp = strcmp(sa, sb);
    }
    return table->sort_descending ? -cmp : cmp;
}

void rt_uitable_sort_by(void *ptr, int64_t col, int8_t descending) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SortBy: expected Zanna.Game.UI.Table");
    if (!table || col < 0 || col >= table->column_count)
        return;
    if (!table->columns[col].sortable)
        table->columns[col].sortable = 1;
    table->sort_column = col;
    table->sort_descending = descending ? 1 : 0;
    for (int64_t i = 1; i < table->row_count; i++) {
        rt_uitable_row_t key = table->rows[i];
        int64_t j = i - 1;
        while (j >= 0 && table_compare_rows(table, &table->rows[j], &key) > 0) {
            table->rows[j + 1] = table->rows[j];
            j--;
        }
        table->rows[j + 1] = key;
    }
}

int64_t rt_uitable_get_sort_column(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SortColumn: expected Zanna.Game.UI.Table");
    return table ? table->sort_column : -1;
}

int8_t rt_uitable_get_sort_descending(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SortDescending: expected Zanna.Game.UI.Table");
    return table ? table->sort_descending : 0;
}

void rt_uitable_set_scroll(void *ptr, int64_t row) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.SetScroll: expected Zanna.Game.UI.Table");
    if (!table)
        return;
    table->scroll_offset = row;
    table_clamp_scroll(table);
}

int64_t rt_uitable_get_scroll(void *ptr) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.Scroll: expected Zanna.Game.UI.Table");
    return table ? table->scroll_offset : 0;
}

void rt_uitable_set_selected_row(void *ptr, int64_t row) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SetSelectedRow: expected Zanna.Game.UI.Table");
    if (!table)
        return;
    table->selected_row = row < 0 || row >= table->row_count ? -1 : row;
}

int64_t rt_uitable_get_selected_row(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.SelectedRow: expected Zanna.Game.UI.Table");
    return table ? table->selected_row : -1;
}

int64_t rt_uitable_handle_click(void *ptr, int64_t mx, int64_t my) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.HandleClick: expected Zanna.Game.UI.Table");
    if (!table || !table->visible ||
        !ui_point_inside(table->x, table->y, table->w, table->h, mx, my))
        return -1;
    table->last_header_click = -1;
    int64_t local_y = my - table->y;
    if (table->show_header && local_y < table->header_height) {
        int64_t cx = table->x;
        for (int64_t c = 0; c < table->column_count; c++) {
            int64_t cw = table->columns[c].width;
            if (ui_coord_inside(cx, cw, mx)) {
                table->last_header_click = c;
                if (table->columns[c].sortable)
                    rt_uitable_sort_by(
                        ptr, c, table->sort_column == c ? !table->sort_descending : 0);
                return -2;
            }
            cx = table_column_next_x(cx, cw);
        }
        return -1;
    }
    int64_t row_y = local_y - (table->show_header ? table->header_height : 0);
    if (table->row_height <= 0)
        return -1;
    int64_t row = ui_add_sat_i64(table->scroll_offset, row_y / table->row_height);
    if (row < 0 || row >= table->row_count)
        return -1;
    table->selected_row = row;
    return row;
}

/// @brief Process a table click and return a structured click outcome.
/// @details Delegates to rt_uitable_handle_click() so row selection, header
///          sorting, and legacy LastHeaderClick state remain identical.
/// @param ptr Opaque Zanna.Game.UI.Table object.
/// @param mx Click x-coordinate in canvas pixels.
/// @param my Click y-coordinate in canvas pixels.
/// @return New Zanna.Game.UI.TableClickResult object.
void *rt_uitable_handle_click_result(void *ptr, int64_t mx, int64_t my) {
    int64_t clicked = rt_uitable_handle_click(ptr, mx, my);
    if (clicked >= 0)
        return table_click_result_new(RT_UITABLE_CLICK_ROW, clicked, -1);
    if (clicked == -2) {
        rt_uitable_impl *table =
            checked_table(ptr, "UITable.HandleClickResult: expected Zanna.Game.UI.Table");
        int64_t column = table ? table->last_header_click : -1;
        return table_click_result_new(RT_UITABLE_CLICK_HEADER, -1, column);
    }
    return table_click_result_new(RT_UITABLE_CLICK_NONE, -1, -1);
}

int64_t rt_uitable_last_header_click(void *ptr) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.LastHeaderClick: expected Zanna.Game.UI.Table");
    return table ? table->last_header_click : -1;
}

/// @brief Return the raw TableClickResult kind value.
/// @param ptr Opaque Zanna.Game.UI.TableClickResult object.
/// @return RT_UITABLE_CLICK_NONE, RT_UITABLE_CLICK_ROW, or RT_UITABLE_CLICK_HEADER.
int64_t rt_table_click_result_kind(void *ptr) {
    rt_table_click_result_impl *result = checked_table_click_result(
        ptr, "TableClickResult.Kind: expected Zanna.Game.UI.TableClickResult");
    return result ? result->kind : RT_UITABLE_CLICK_NONE;
}

/// @brief Return whether a TableClickResult represents a miss.
/// @param ptr Opaque Zanna.Game.UI.TableClickResult object.
/// @return 1 for no-hit results, otherwise 0.
int8_t rt_table_click_result_is_none(void *ptr) {
    return rt_table_click_result_kind(ptr) == RT_UITABLE_CLICK_NONE ? 1 : 0;
}

/// @brief Return whether a TableClickResult represents a row hit.
/// @param ptr Opaque Zanna.Game.UI.TableClickResult object.
/// @return 1 for row-hit results, otherwise 0.
int8_t rt_table_click_result_is_row(void *ptr) {
    return rt_table_click_result_kind(ptr) == RT_UITABLE_CLICK_ROW ? 1 : 0;
}

/// @brief Return whether a TableClickResult represents a header hit.
/// @param ptr Opaque Zanna.Game.UI.TableClickResult object.
/// @return 1 for header-hit results, otherwise 0.
int8_t rt_table_click_result_is_header(void *ptr) {
    return rt_table_click_result_kind(ptr) == RT_UITABLE_CLICK_HEADER ? 1 : 0;
}

/// @brief Return the row index stored in a row-hit result.
/// @param ptr Opaque Zanna.Game.UI.TableClickResult object.
/// @return Zanna.Option.SomeI64(row) for row hits, otherwise Zanna.Option.None().
void *rt_table_click_result_row_option(void *ptr) {
    rt_table_click_result_impl *result = checked_table_click_result(
        ptr, "TableClickResult.RowOption: expected Zanna.Game.UI.TableClickResult");
    if (!result || result->kind != RT_UITABLE_CLICK_ROW)
        return rt_option_none();
    return rt_option_some_i64(result->row);
}

/// @brief Return the header column index stored in a header-hit result.
/// @param ptr Opaque Zanna.Game.UI.TableClickResult object.
/// @return Zanna.Option.SomeI64(column) for header hits, otherwise Zanna.Option.None().
void *rt_table_click_result_column_option(void *ptr) {
    rt_table_click_result_impl *result = checked_table_click_result(
        ptr, "TableClickResult.ColumnOption: expected Zanna.Game.UI.TableClickResult");
    if (!result || result->kind != RT_UITABLE_CLICK_HEADER)
        return rt_option_none();
    return rt_option_some_i64(result->column);
}

void rt_uitable_handle_scroll(void *ptr, int64_t delta) {
    rt_uitable_impl *table =
        checked_table(ptr, "UITable.HandleScroll: expected Zanna.Game.UI.Table");
    if (!table)
        return;
    table->scroll_offset = ui_add_sat_i64(table->scroll_offset, delta);
    table_clamp_scroll(table);
}

void rt_uitable_handle_key(void *ptr, int64_t key_code) {
    rt_uitable_impl *table = checked_table(ptr, "UITable.HandleKey: expected Zanna.Game.UI.Table");
    if (!table || table->row_count <= 0)
        return;
    if (table->selected_row < 0)
        table->selected_row = 0;
    int64_t page = table_visible_rows(table);
    if (page < 1)
        page = 1;
    if (key_code == UI_KEY_UP)
        table->selected_row--;
    else if (key_code == UI_KEY_DOWN)
        table->selected_row++;
    else if (key_code == UI_KEY_HOME)
        table->selected_row = 0;
    else if (key_code == UI_KEY_END)
        table->selected_row = table->row_count - 1;
    else if (key_code == UI_KEY_PAGE_UP)
        table->selected_row -= page;
    else if (key_code == UI_KEY_PAGE_DOWN)
        table->selected_row += page;
    if (table->selected_row < 0)
        table->selected_row = 0;
    if (table->selected_row >= table->row_count)
        table->selected_row = table->row_count - 1;
    if (table->selected_row < table->scroll_offset)
        table->scroll_offset = table->selected_row;
    if (table->selected_row >= table->scroll_offset + page)
        table->scroll_offset = table->selected_row - page + 1;
    table_clamp_scroll(table);
}

void rt_uitable_draw(void *ptr, void *canvas) {
    rt_gameui_draw_ops_t ops;
    rt_uitable_impl *table = checked_table(ptr, "UITable.Draw: expected Zanna.Game.UI.Table");
    if (!table || !canvas || !ui_resolve_draw_ops(canvas, "UITable.Draw: expected Canvas or Canvas3D", &ops))
        return;
    if (!table->visible)
        return;
    int64_t y = table->y;
    if (table->show_header) {
        ops.box(canvas, table->x, y, table->w, table->header_height, table->header_bg_color);
        int64_t x = table->x;
        for (int64_t c = 0; c < table->column_count; c++) {
            ui_draw_text_basic(&ops,
                               x + 4,
                               y + 4,
                               table->columns[c].title,
                               table->font,
                               1,
                               table->header_text_color);
            x += table->columns[c].width;
        }
        y += table->header_height;
    }
    int64_t visible_rows = table_visible_rows(table);
    for (int64_t vr = 0; vr < visible_rows; vr++) {
        int64_t row = table->scroll_offset + vr;
        if (row >= table->row_count)
            break;
        int64_t ry = y + vr * table->row_height;
        int64_t bg =
            row == table->selected_row
                ? table->selected_bg_color
                : (table->striped && (row & 1) ? table->row_alt_bg_color : table->row_bg_color);
        ops.box(canvas, table->x, ry, table->w, table->row_height, bg);
        int64_t x = table->x;
        for (int64_t c = 0; c < table->column_count; c++) {
            const char *cell = table_cell_ptr(table, &table->rows[row], c);
            ui_draw_text_basic(
                &ops, x + 4, ry + 4, cell ? cell : "", table->font, 1, table->text_color);
            x += table->columns[c].width;
        }
    }
    if (table->show_borders)
        ops.frame(canvas, table->x, table->y, table->w, table->h, table->border_color);
}

//=============================================================================
// UISlider
//=============================================================================

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    int64_t min_value, max_value, current_value, step;
    char label[64];
    int8_t show_value;
    int8_t show_label;
    int64_t track_color, fill_color, thumb_color, text_color;
    int8_t visible, enabled, dragging;
} rt_uislider_impl;

/// @brief Safe-cast a handle to the UISlider impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_uislider_impl *checked_slider(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UISLIDER_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uislider_impl *)ptr;
}

/// @brief Clamp @p value to the slider's [min, max] range and snap it to the
///        nearest step boundary.
static int64_t slider_clamp_value(rt_uislider_impl *s, int64_t value) {
    if (!s)
        return 0;
    if (value < s->min_value)
        value = s->min_value;
    if (value > s->max_value)
        value = s->max_value;
    if (s->step > 1) {
        long double offset = (long double)value - (long double)s->min_value;
        if (offset < 0.0L)
            offset = 0.0L;
        long double steps = floorl((offset + (long double)s->step / 2.0L) / (long double)s->step);
        value = ui_ld_to_i64_sat((long double)s->min_value + steps * (long double)s->step);
        if (value > s->max_value)
            value = s->max_value;
        if (value < s->min_value)
            value = s->min_value;
    }
    return value;
}

/// @brief Set the slider value from a mouse x-coordinate along the track.
/// @return non-zero if the value changed.
static int8_t slider_set_from_mouse(rt_uislider_impl *s, int64_t mx) {
    if (!s || s->w <= 1)
        return 0;
    int64_t offset = ui_coord_offset_clamped(s->x, s->w, mx);
    long double t = (long double)offset / (long double)s->w;
    long double range = (long double)s->max_value - (long double)s->min_value;
    int64_t value = ui_ld_to_i64_sat((long double)s->min_value + range * t + 0.5L);
    value = slider_clamp_value(s, value);
    if (value == s->current_value)
        return 0;
    s->current_value = value;
    return 1;
}

void *rt_uislider_new(int64_t x, int64_t y, int64_t w, int64_t h, int64_t min_v, int64_t max_v) {
    rt_uislider_impl *s =
        (rt_uislider_impl *)rt_obj_new_i64(RT_UISLIDER_CLASS_ID, (int64_t)sizeof(*s));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->x = x;
    s->y = y;
    s->w = ui_clamp_dim(w);
    s->h = ui_clamp_dim(h);
    if (max_v < min_v) {
        int64_t tmp = min_v;
        min_v = max_v;
        max_v = tmp;
    }
    s->min_value = min_v;
    s->max_value = max_v;
    s->current_value = min_v;
    s->step = 1;
    s->show_value = 1;
    s->show_label = 0;
    s->track_color = 0x505050;
    s->fill_color = 0x4A90E2;
    s->thumb_color = 0xFFFFFF;
    s->text_color = 0xFFFFFF;
    s->visible = 1;
    s->enabled = 1;
    return s;
}

void rt_uislider_set_value(void *ptr, int64_t v) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.SetValue: expected Zanna.Game.UI.HudSlider");
    if (s)
        s->current_value = slider_clamp_value(s, v);
}

int64_t rt_uislider_get_value(void *ptr) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.Value: expected Zanna.Game.UI.HudSlider");
    return s ? s->current_value : 0;
}

void rt_uislider_set_step(void *ptr, int64_t step) {
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.SetStep: expected Zanna.Game.UI.HudSlider");
    if (!s)
        return;
    s->step = step > 0 ? step : 1;
    s->current_value = slider_clamp_value(s, s->current_value);
}

void rt_uislider_set_label(void *ptr, rt_string label) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.SetLabel: expected Zanna.Game.UI.HudSlider");
    if (!s)
        return;
    ui_copy_text(s->label, sizeof(s->label), label);
    s->show_label = s->label[0] != '\0';
}

int8_t rt_uislider_handle_key(void *ptr, int64_t key_code) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleKey: expected Zanna.Game.UI.HudSlider");
    if (!s || !s->visible || !s->enabled)
        return 0;
    int64_t before = s->current_value;
    if (key_code == UI_KEY_LEFT || key_code == UI_KEY_DOWN)
        s->current_value = slider_clamp_value(s, ui_add_sat_i64(s->current_value, -s->step));
    else if (key_code == UI_KEY_RIGHT || key_code == UI_KEY_UP)
        s->current_value = slider_clamp_value(s, ui_add_sat_i64(s->current_value, s->step));
    else if (key_code == UI_KEY_HOME)
        s->current_value = s->min_value;
    else if (key_code == UI_KEY_END)
        s->current_value = s->max_value;
    return before != s->current_value;
}

int8_t rt_uislider_handle_mouse_down(void *ptr, int64_t mx, int64_t my) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleMouseDown: expected Zanna.Game.UI.HudSlider");
    if (!s || !s->visible || !s->enabled || !ui_point_inside(s->x, s->y, s->w, s->h, mx, my))
        return 0;
    s->dragging = 1;
    return slider_set_from_mouse(s, mx);
}

int8_t rt_uislider_handle_mouse_drag(void *ptr, int64_t mx) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleMouseDrag: expected Zanna.Game.UI.HudSlider");
    if (!s || !s->dragging || !s->enabled)
        return 0;
    return slider_set_from_mouse(s, mx);
}

int8_t rt_uislider_handle_mouse_up(void *ptr) {
    rt_uislider_impl *s =
        checked_slider(ptr, "UISlider.HandleMouseUp: expected Zanna.Game.UI.HudSlider");
    if (!s)
        return 0;
    int8_t was = s->dragging;
    s->dragging = 0;
    return was;
}

void rt_uislider_draw(void *ptr, void *canvas) {
    rt_gameui_draw_ops_t ops;
    rt_uislider_impl *s = checked_slider(ptr, "UISlider.Draw: expected Zanna.Game.UI.HudSlider");
    if (!s || !canvas || !ui_resolve_draw_ops(canvas, "UISlider.Draw: expected Canvas or Canvas3D", &ops))
        return;
    if (!s->visible)
        return;
    int64_t cy = s->y + s->h / 2;
    ops.box(canvas, s->x, cy - 2, s->w, 4, s->track_color);
    int64_t fill = 0;
    if (s->max_value > s->min_value)
        fill = (int64_t)(((long double)(s->current_value - s->min_value) * (long double)s->w) /
                         (long double)(s->max_value - s->min_value));
    ops.box(canvas, s->x, cy - 2, fill, 4, s->fill_color);
    ops.box(canvas, s->x + fill - 4, s->y, 8, s->h, s->thumb_color);
    if (s->show_label)
        ui_draw_text_basic(&ops, s->x, s->y - 12, s->label, NULL, 1, s->text_color);
}

//=============================================================================
// UIDropdown
//=============================================================================

#define RT_UIDROPDOWN_DEFAULT_OPTIONS 32
#define RT_UIDROPDOWN_MAX_TEXT 64

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    char (*options)[RT_UIDROPDOWN_MAX_TEXT];
    int64_t option_count;
    int64_t option_capacity;
    int64_t selected;
    int8_t open;
    int64_t text_color, bg_color, caret_color, border_color, selected_bg_color;
    void *font;
    int8_t visible, enabled;
} rt_uidropdown_impl;

/// @brief Safe-cast a handle to the UIDropdown impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_uidropdown_impl *checked_dropdown(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIDROPDOWN_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uidropdown_impl *)ptr;
}

/// @brief GC finalizer: free the dropdown's option strings/array.
static void uidropdown_finalizer(void *obj) {
    rt_uidropdown_impl *dd = (rt_uidropdown_impl *)obj;
    if (!dd)
        return;
    free(dd->options);
    dd->options = NULL;
    dd->option_count = 0;
    dd->option_capacity = 0;
    ui_release_obj(dd->font);
    dd->font = NULL;
}

static int8_t ensure_dropdown_option_capacity(rt_uidropdown_impl *dd, int64_t needed) {
    if (!dd || needed <= dd->option_capacity)
        return 1;
    int64_t new_capacity = dd->option_capacity > 0 ? dd->option_capacity : 1;
    while (new_capacity < needed) {
        if (new_capacity > INT64_MAX / 2 ||
            (uint64_t)new_capacity > SIZE_MAX / (2 * RT_UIDROPDOWN_MAX_TEXT))
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > SIZE_MAX / RT_UIDROPDOWN_MAX_TEXT)
        return 0;
    char (*options)[RT_UIDROPDOWN_MAX_TEXT] = (char (*)[RT_UIDROPDOWN_MAX_TEXT])realloc(
        dd->options, (size_t)new_capacity * RT_UIDROPDOWN_MAX_TEXT);
    if (!options)
        return 0;
    memset(options + dd->option_capacity,
           0,
           (size_t)(new_capacity - dd->option_capacity) * RT_UIDROPDOWN_MAX_TEXT);
    dd->options = options;
    dd->option_capacity = new_capacity;
    return 1;
}

void *rt_uidropdown_new(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uidropdown_impl *dd =
        (rt_uidropdown_impl *)rt_obj_new_i64(RT_UIDROPDOWN_CLASS_ID, (int64_t)sizeof(*dd));
    if (!dd)
        return NULL;
    memset(dd, 0, sizeof(*dd));
    dd->x = x;
    dd->y = y;
    dd->w = ui_clamp_dim(w);
    dd->h = ui_clamp_dim(h);
    dd->selected = -1;
    dd->text_color = 0xFFFFFF;
    dd->bg_color = 0x202020;
    dd->caret_color = 0xFFFFFF;
    dd->border_color = 0x606060;
    dd->selected_bg_color = 0x304A70;
    dd->visible = 1;
    dd->enabled = 1;
    rt_obj_set_finalizer(dd, uidropdown_finalizer);
    if (!ensure_dropdown_option_capacity(dd, RT_UIDROPDOWN_DEFAULT_OPTIONS)) {
        if (rt_obj_release_check0(dd))
            rt_obj_free(dd);
        return NULL;
    }
    return dd;
}

void rt_uidropdown_add_option(void *ptr, rt_string text) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.AddOption: expected Zanna.Game.UI.HudDropdown");
    if (!dd)
        return;
    if (!ensure_dropdown_option_capacity(dd, dd->option_count + 1)) {
        rt_trap("UIDropdown.AddOption: option allocation failed");
        return;
    }
    ui_copy_text(dd->options[dd->option_count], sizeof(dd->options[dd->option_count]), text);
    if (dd->selected < 0)
        dd->selected = 0;
    dd->option_count++;
}

void rt_uidropdown_clear_options(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.ClearOptions: expected Zanna.Game.UI.HudDropdown");
    if (!dd)
        return;
    if (dd->options && dd->option_capacity > 0)
        memset(dd->options, 0, (size_t)dd->option_capacity * RT_UIDROPDOWN_MAX_TEXT);
    dd->option_count = 0;
    dd->selected = -1;
    dd->open = 0;
}

int64_t rt_uidropdown_get_selected(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Selected: expected Zanna.Game.UI.HudDropdown");
    return dd ? dd->selected : -1;
}

void rt_uidropdown_set_selected(void *ptr, int64_t index) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.SetSelected: expected Zanna.Game.UI.HudDropdown");
    if (!dd)
        return;
    dd->selected = index < 0 || index >= dd->option_count ? -1 : index;
}

rt_string rt_uidropdown_get_selected_text(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.SelectedText: expected Zanna.Game.UI.HudDropdown");
    if (!dd || dd->selected < 0 || dd->selected >= dd->option_count)
        return rt_str_empty();
    return rt_const_cstr(dd->options[dd->selected]);
}

int8_t rt_uidropdown_is_open(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.IsOpen: expected Zanna.Game.UI.HudDropdown");
    return dd ? dd->open : 0;
}

void rt_uidropdown_open(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Open: expected Zanna.Game.UI.HudDropdown");
    if (dd && dd->enabled && dd->visible)
        dd->open = 1;
}

void rt_uidropdown_close(void *ptr) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Close: expected Zanna.Game.UI.HudDropdown");
    if (dd)
        dd->open = 0;
}

int8_t rt_uidropdown_handle_click(void *ptr, int64_t mx, int64_t my) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.HandleClick: expected Zanna.Game.UI.HudDropdown");
    if (!dd || !dd->enabled || !dd->visible)
        return 0;
    if (ui_point_inside(dd->x, dd->y, dd->w, dd->h, mx, my)) {
        dd->open = !dd->open;
        return 1;
    }
    int64_t list_y = ui_add_sat_i64(dd->y, dd->h);
    int64_t list_h = ui_mul_sat_i64(dd->h, dd->option_count);
    if (dd->open && ui_point_inside(dd->x, list_y, dd->w, list_h, mx, my)) {
        int64_t idx = (my - list_y) / dd->h;
        if (idx >= 0 && idx < dd->option_count)
            dd->selected = idx;
        dd->open = 0;
        return 1;
    }
    dd->open = 0;
    return 0;
}

int8_t rt_uidropdown_handle_key(void *ptr, int64_t key_code) {
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.HandleKey: expected Zanna.Game.UI.HudDropdown");
    if (!dd || !dd->enabled || !dd->visible || dd->option_count <= 0)
        return 0;
    if (key_code == UI_KEY_ESCAPE) {
        dd->open = 0;
        return 1;
    }
    if (key_code == UI_KEY_ENTER) {
        dd->open = !dd->open;
        return 1;
    }
    if (key_code == UI_KEY_UP) {
        dd->selected = dd->selected <= 0 ? dd->option_count - 1 : dd->selected - 1;
        return 1;
    }
    if (key_code == UI_KEY_DOWN) {
        dd->selected = dd->selected >= dd->option_count - 1 ? 0 : dd->selected + 1;
        return 1;
    }
    return 0;
}

void rt_uidropdown_draw(void *ptr, void *canvas) {
    rt_gameui_draw_ops_t ops;
    rt_uidropdown_impl *dd =
        checked_dropdown(ptr, "UIDropdown.Draw: expected Zanna.Game.UI.HudDropdown");
    if (!dd || !canvas || !ui_resolve_draw_ops(canvas, "UIDropdown.Draw: expected Canvas or Canvas3D", &ops))
        return;
    if (!dd->visible)
        return;
    ops.box(canvas, dd->x, dd->y, dd->w, dd->h, dd->bg_color);
    ops.frame(canvas, dd->x, dd->y, dd->w, dd->h, dd->border_color);
    if (dd->selected >= 0 && dd->selected < dd->option_count)
        ui_draw_text_basic(
            &ops, dd->x + 4, dd->y + 4, dd->options[dd->selected], dd->font, 1, dd->text_color);
    ops.line(canvas,
                   dd->x + dd->w - 14,
                   dd->y + dd->h / 2 - 2,
                   dd->x + dd->w - 8,
                   dd->y + dd->h / 2 + 4,
                   dd->caret_color);
    ops.line(canvas,
                   dd->x + dd->w - 8,
                   dd->y + dd->h / 2 + 4,
                   dd->x + dd->w - 2,
                   dd->y + dd->h / 2 - 2,
                   dd->caret_color);
    if (dd->open) {
        for (int64_t i = 0; i < dd->option_count; i++) {
            int64_t y = ui_add_sat_i64(dd->y, ui_mul_sat_i64(dd->h, i + 1));
            ops.box(canvas,
                          dd->x,
                          y,
                          dd->w,
                          dd->h,
                          i == dd->selected ? dd->selected_bg_color : dd->bg_color);
            ops.frame(canvas, dd->x, y, dd->w, dd->h, dd->border_color);
            ui_draw_text_basic(
                &ops, dd->x + 4, y + 4, dd->options[i], dd->font, 1, dd->text_color);
        }
    }
}

//=============================================================================
// UITooltip
//=============================================================================

typedef struct {
    void *vptr;
    int64_t x, y;
    char text[256];
    int64_t bg_color, text_color, border_color;
    int64_t padding;
    int8_t visible;
    void *font;
    int64_t hover_delay_ms, hover_elapsed_ms;
    int64_t target_x, target_y;
    int8_t hovered;
} rt_uitooltip_impl;

/// @brief Safe-cast a handle to the UITooltip impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_uitooltip_impl *checked_tooltip(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UITOOLTIP_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uitooltip_impl *)ptr;
}

/// @brief GC finalizer: free the tooltip's text buffer.
static void uitooltip_finalizer(void *obj) {
    rt_uitooltip_impl *t = (rt_uitooltip_impl *)obj;
    if (!t)
        return;
    ui_release_obj(t->font);
    t->font = NULL;
}

void *rt_uitooltip_new(void) {
    rt_uitooltip_impl *t =
        (rt_uitooltip_impl *)rt_obj_new_i64(RT_UITOOLTIP_CLASS_ID, (int64_t)sizeof(*t));
    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->bg_color = 0x202020;
    t->text_color = 0xFFFFFF;
    t->border_color = 0x606060;
    t->padding = 6;
    t->hover_delay_ms = 500;
    rt_obj_set_finalizer(t, uitooltip_finalizer);
    return t;
}

void rt_uitooltip_set_text(void *ptr, rt_string text) {
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.SetText: expected Zanna.Game.UI.HudTooltip");
    if (t)
        ui_copy_text(t->text, sizeof(t->text), text);
}

void rt_uitooltip_set_hover_delay_ms(void *ptr, int64_t ms) {
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.SetHoverDelayMs: expected Zanna.Game.UI.HudTooltip");
    if (t)
        t->hover_delay_ms = ms < 0 ? 0 : ms;
}

void rt_uitooltip_update(
    void *ptr, int64_t mx, int64_t my, int8_t hovered_target, int64_t delta_ms) {
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.Update: expected Zanna.Game.UI.HudTooltip");
    if (!t)
        return;
    t->target_x = mx;
    t->target_y = my;
    t->x = ui_add_sat_i64(mx, 14);
    t->y = ui_add_sat_i64(my, 18);
    if (!hovered_target) {
        t->hovered = 0;
        t->hover_elapsed_ms = 0;
        t->visible = 0;
        return;
    }
    t->hovered = 1;
    if (delta_ms > 0)
        t->hover_elapsed_ms = ui_add_sat_i64(t->hover_elapsed_ms, delta_ms);
    t->visible = t->hover_elapsed_ms >= t->hover_delay_ms;
}

void rt_uitooltip_draw(void *ptr, void *canvas) {
    rt_gameui_draw_ops_t ops;
    rt_uitooltip_impl *t =
        checked_tooltip(ptr, "UITooltip.Draw: expected Zanna.Game.UI.HudTooltip");
    if (!t || !canvas || !ui_resolve_draw_ops(canvas, "UITooltip.Draw: expected Canvas or Canvas3D", &ops))
        return;
    if (!t->visible || t->text[0] == '\0')
        return;
    int64_t w =
        ui_text_prefix_width(t->text, (int64_t)strlen(t->text), t->font, 1) + t->padding * 2;
    int64_t h = 8 + t->padding * 2;
    int64_t cw = ops.width(canvas);
    int64_t ch = ops.height(canvas);
    int64_t x = t->x;
    int64_t y = t->y;
    if (ui_add_sat_i64(x, w) > cw)
        x = cw - w;
    if (ui_add_sat_i64(y, h) > ch)
        y = ch - h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    ops.box(canvas, x, y, w, h, t->bg_color);
    ops.frame(canvas, x, y, w, h, t->border_color);
    ui_draw_text_basic(&ops, x + t->padding, y + t->padding, t->text, t->font, 1, t->text_color);
}

//=============================================================================
// UIModal
//=============================================================================

#define RT_UIMODAL_DEFAULT_CHILDREN 16
#define RT_UIMODAL_DEFAULT_BUTTONS 4

typedef struct {
    char text[64];
    int64_t return_value;
    int8_t is_default;
    int8_t is_cancel;
} rt_uimodal_button_t;

typedef struct {
    void *vptr;
    int64_t x, y, w, h;
    char title[128];
    char content_text[512];
    void **children;
    int64_t child_count;
    int64_t child_capacity;
    rt_uimodal_button_t *buttons;
    int64_t button_count;
    int64_t button_capacity;
    int64_t selected_button;
    int64_t result;
    int8_t open;
    int8_t visible;
    int8_t modal;
    int64_t bg_color;
    int64_t title_bar_color;
    int64_t title_text_color;
    int64_t content_text_color;
    int64_t overlay_color;
    int64_t overlay_alpha;
    void *font;
} rt_uimodal_impl;

/// @brief Safe-cast a handle to the UIModal impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p ptr is NULL.
static rt_uimodal_impl *checked_modal(void *ptr, const char *api) {
    if (!ptr)
        return NULL;
    if (rt_obj_class_id(ptr) != RT_UIMODAL_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return (rt_uimodal_impl *)ptr;
}

/// @brief GC finalizer: free the modal's buttons/strings and release children.
static void uimodal_finalizer(void *obj) {
    rt_uimodal_impl *m = (rt_uimodal_impl *)obj;
    if (!m)
        return;
    for (int64_t i = 0; i < m->child_count; i++)
        ui_release_obj(m->children[i]);
    free(m->children);
    m->children = NULL;
    ui_release_obj(m->font);
    m->font = NULL;
    m->child_count = 0;
    m->child_capacity = 0;
    free(m->buttons);
    m->buttons = NULL;
    m->button_count = 0;
    m->button_capacity = 0;
}

static int8_t ensure_modal_child_capacity(rt_uimodal_impl *m, int64_t needed) {
    if (!m || needed <= m->child_capacity)
        return 1;
    int64_t new_capacity = m->child_capacity > 0 ? m->child_capacity : 1;
    while (new_capacity < needed) {
        if (new_capacity > INT64_MAX / 2 ||
            (uint64_t)new_capacity > SIZE_MAX / (2 * sizeof(void *)))
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > SIZE_MAX / sizeof(void *))
        return 0;
    void **children = (void **)realloc(m->children, (size_t)new_capacity * sizeof(void *));
    if (!children)
        return 0;
    memset(children + m->child_capacity,
           0,
           (size_t)(new_capacity - m->child_capacity) * sizeof(void *));
    m->children = children;
    m->child_capacity = new_capacity;
    return 1;
}

static int8_t ensure_modal_button_capacity(rt_uimodal_impl *m, int64_t needed) {
    if (!m || needed <= m->button_capacity)
        return 1;
    int64_t new_capacity = m->button_capacity > 0 ? m->button_capacity : 1;
    while (new_capacity < needed) {
        if (new_capacity > INT64_MAX / 2 ||
            (uint64_t)new_capacity > SIZE_MAX / (2 * sizeof(rt_uimodal_button_t)))
            return 0;
        new_capacity *= 2;
    }
    if ((uint64_t)new_capacity > SIZE_MAX / sizeof(rt_uimodal_button_t))
        return 0;
    rt_uimodal_button_t *buttons = (rt_uimodal_button_t *)realloc(
        m->buttons, (size_t)new_capacity * sizeof(rt_uimodal_button_t));
    if (!buttons)
        return 0;
    memset(buttons + m->button_capacity,
           0,
           (size_t)(new_capacity - m->button_capacity) * sizeof(rt_uimodal_button_t));
    m->buttons = buttons;
    m->button_capacity = new_capacity;
    return 1;
}

/// @brief Initialize a freshly allocated modal's geometry and default state
///        (shared by rt_uimodal_new and rt_uimodal_new_at).
static void modal_init(rt_uimodal_impl *m, int64_t x, int64_t y, int64_t w, int64_t h) {
    memset(m, 0, sizeof(*m));
    m->x = x;
    m->y = y;
    m->w = ui_clamp_dim(w);
    m->h = ui_clamp_dim(h);
    m->selected_button = -1;
    m->result = -1;
    m->visible = 1;
    m->modal = 1;
    m->bg_color = 0x202020;
    m->title_bar_color = 0x303030;
    m->title_text_color = 0xFFFFFF;
    m->content_text_color = 0xDDDDDD;
    m->overlay_color = 0x000000;
    m->overlay_alpha = 128;
}

void *rt_uimodal_new(int64_t w, int64_t h) {
    return rt_uimodal_new_at(0, 0, w, h);
}

void *rt_uimodal_new_at(int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_uimodal_impl *m =
        (rt_uimodal_impl *)rt_obj_new_i64(RT_UIMODAL_CLASS_ID, (int64_t)sizeof(*m));
    if (!m)
        return NULL;
    modal_init(m, x, y, w, h);
    rt_obj_set_finalizer(m, uimodal_finalizer);
    if (!ensure_modal_child_capacity(m, RT_UIMODAL_DEFAULT_CHILDREN) ||
        !ensure_modal_button_capacity(m, RT_UIMODAL_DEFAULT_BUTTONS)) {
        if (rt_obj_release_check0(m))
            rt_obj_free(m);
        return NULL;
    }
    return m;
}

void rt_uimodal_set_title(void *ptr, rt_string title) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.SetTitle: expected Zanna.Game.UI.Modal");
    if (m)
        ui_copy_text(m->title, sizeof(m->title), title);
}

void rt_uimodal_set_content(void *ptr, rt_string text) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.SetContent: expected Zanna.Game.UI.Modal");
    if (m)
        ui_copy_text(m->content_text, sizeof(m->content_text), text);
}

int64_t rt_uimodal_add_button(void *ptr, rt_string text, int64_t return_value) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.AddButton: expected Zanna.Game.UI.Modal");
    if (!m)
        return -1;
    if (!ensure_modal_button_capacity(m, m->button_count + 1)) {
        rt_trap("UIModal.AddButton: button allocation failed");
        return -1;
    }
    int64_t idx = m->button_count++;
    ui_copy_text(m->buttons[idx].text, sizeof(m->buttons[idx].text), text);
    m->buttons[idx].return_value = return_value;
    if (m->selected_button < 0)
        m->selected_button = idx;
    return idx;
}

void rt_uimodal_set_default_button(void *ptr, int64_t index) {
    rt_uimodal_impl *m =
        checked_modal(ptr, "UIModal.SetDefaultButton: expected Zanna.Game.UI.Modal");
    if (!m || index < 0 || index >= m->button_count)
        return;
    for (int64_t i = 0; i < m->button_count; i++)
        m->buttons[i].is_default = 0;
    m->buttons[index].is_default = 1;
    m->selected_button = index;
}

void rt_uimodal_set_cancel_button(void *ptr, int64_t index) {
    rt_uimodal_impl *m =
        checked_modal(ptr, "UIModal.SetCancelButton: expected Zanna.Game.UI.Modal");
    if (!m || index < 0 || index >= m->button_count)
        return;
    for (int64_t i = 0; i < m->button_count; i++)
        m->buttons[i].is_cancel = 0;
    m->buttons[index].is_cancel = 1;
}

void rt_uimodal_add_child(void *ptr, void *child_widget) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.AddChild: expected Zanna.Game.UI.Modal");
    if (!m || !child_widget)
        return;
    if (!ensure_modal_child_capacity(m, m->child_count + 1)) {
        rt_trap("UIModal.AddChild: child allocation failed");
        return;
    }
    rt_obj_retain_maybe(child_widget);
    m->children[m->child_count++] = child_widget;
}

void rt_uimodal_open(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Open: expected Zanna.Game.UI.Modal");
    if (!m)
        return;
    m->open = 1;
    m->visible = 1;
    m->result = -1;
}

void rt_uimodal_close(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Close: expected Zanna.Game.UI.Modal");
    if (m)
        m->open = 0;
}

int8_t rt_uimodal_is_open(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.IsOpen: expected Zanna.Game.UI.Modal");
    return m ? m->open : 0;
}

int64_t rt_uimodal_get_result(void *ptr) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Result: expected Zanna.Game.UI.Modal");
    return m ? m->result : -1;
}

/// @brief Activate button @p index: record its return value, close the modal.
/// @return the button's return value (or a sentinel if @p index is invalid).
static int64_t modal_trigger_button(rt_uimodal_impl *m, int64_t index) {
    if (!m || index < 0 || index >= m->button_count)
        return -1;
    m->result = m->buttons[index].return_value;
    m->open = 0;
    m->visible = 0;
    return m->result;
}

int64_t rt_uimodal_handle_key(void *ptr, int64_t key_code, int8_t shift_held) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.HandleKey: expected Zanna.Game.UI.Modal");
    if (!m || !m->open)
        return -1;
    if (key_code == UI_KEY_TAB && m->button_count > 0) {
        if (m->selected_button < 0)
            m->selected_button = 0;
        else if (shift_held)
            m->selected_button =
                m->selected_button <= 0 ? m->button_count - 1 : m->selected_button - 1;
        else
            m->selected_button = (m->selected_button + 1) % m->button_count;
        return -1;
    }
    if (key_code == UI_KEY_ENTER) {
        int64_t target = m->selected_button;
        if (target < 0) {
            for (int64_t i = 0; i < m->button_count; i++) {
                if (m->buttons[i].is_default) {
                    target = i;
                    break;
                }
            }
        }
        return modal_trigger_button(m, target);
    }
    if (key_code == UI_KEY_ESCAPE) {
        for (int64_t i = 0; i < m->button_count; i++) {
            if (m->buttons[i].is_cancel)
                return modal_trigger_button(m, i);
        }
    }
    for (int64_t i = 0; i < m->child_count; i++) {
        if (m->children[i] && rt_obj_class_id(m->children[i]) == RT_UITEXTINPUT_CLASS_ID)
            rt_uitextinput_handle_key(m->children[i], key_code, shift_held);
    }
    return -1;
}

int64_t rt_uimodal_handle_click(void *ptr, int64_t mx, int64_t my) {
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.HandleClick: expected Zanna.Game.UI.Modal");
    if (!m || !m->open)
        return -1;
    int64_t button_w = 88;
    int64_t button_h = 24;
    int64_t gap = 8;
    int64_t total_w =
        ui_add_sat_i64(ui_mul_sat_i64(m->button_count, button_w),
                       m->button_count > 0 ? ui_mul_sat_i64(m->button_count - 1, gap) : 0);
    int64_t bx = m->x + m->w - total_w - 12;
    int64_t by = m->y + m->h - button_h - 12;
    for (int64_t i = 0; i < m->button_count; i++) {
        int64_t x = bx + i * (button_w + gap);
        if (ui_point_inside(x, by, button_w, button_h, mx, my))
            return modal_trigger_button(m, i);
    }
    for (int64_t i = 0; i < m->child_count; i++) {
        if (m->children[i] && rt_obj_class_id(m->children[i]) == RT_UITEXTINPUT_CLASS_ID)
            rt_uitextinput_handle_mouse_click(m->children[i], mx, my, 0);
    }
    return -1;
}

void rt_uimodal_draw(void *ptr, void *canvas) {
    rt_gameui_draw_ops_t ops;
    rt_uimodal_impl *m = checked_modal(ptr, "UIModal.Draw: expected Zanna.Game.UI.Modal");
    if (!m || !canvas || !ui_resolve_draw_ops(canvas, "UIModal.Draw: expected Canvas or Canvas3D", &ops))
        return;
    if (!m->open || !m->visible)
        return;
    ops.box_alpha(canvas,
                        0,
                        0,
                        ops.width(canvas),
                        ops.height(canvas),
                        m->overlay_color,
                        m->overlay_alpha);
    ops.box(canvas, m->x, m->y, m->w, m->h, m->bg_color);
    ops.frame(canvas, m->x, m->y, m->w, m->h, 0x707070);
    ops.box(canvas, m->x, m->y, m->w, 28, m->title_bar_color);
    ui_draw_text_basic(&ops, m->x + 8, m->y + 8, m->title, m->font, 1, m->title_text_color);
    ui_draw_text_basic(
        &ops, m->x + 12, m->y + 40, m->content_text, m->font, 1, m->content_text_color);
    for (int64_t i = 0; i < m->child_count; i++) {
        void *child = m->children[i];
        if (!child)
            continue;
        int64_t cid = rt_obj_class_id(child);
        if (cid == RT_UITEXTINPUT_CLASS_ID)
            rt_uitextinput_draw(child, canvas);
        else if (cid == RT_UISLIDER_CLASS_ID)
            rt_uislider_draw(child, canvas);
        else if (cid == RT_UIDROPDOWN_CLASS_ID)
            rt_uidropdown_draw(child, canvas);
        else if (cid == RT_UITABLE_CLASS_ID)
            rt_uitable_draw(child, canvas);
        else if (cid == RT_UIMENULIST_CLASS_ID)
            rt_uimenulist_draw(child, canvas);
    }
    int64_t button_w = 88;
    int64_t button_h = 24;
    int64_t gap = 8;
    int64_t total_w =
        ui_add_sat_i64(ui_mul_sat_i64(m->button_count, button_w),
                       m->button_count > 0 ? ui_mul_sat_i64(m->button_count - 1, gap) : 0);
    int64_t bx = m->x + m->w - total_w - 12;
    int64_t by = m->y + m->h - button_h - 12;
    for (int64_t i = 0; i < m->button_count; i++) {
        int64_t x = bx + i * (button_w + gap);
        ops.box(
            canvas, x, by, button_w, button_h, i == m->selected_button ? 0x405A86 : 0x303030);
        ops.frame(canvas, x, by, button_w, button_h, 0x808080);
        ui_draw_text_basic(&ops, x + 8, by + 8, m->buttons[i].text, m->font, 1, 0xFFFFFF);
    }
}
