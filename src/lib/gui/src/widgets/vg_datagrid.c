//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_datagrid.c
// Purpose: Tabular data grid (exposed as Viper.GUI.Grid) — rows of text cells in
//          columns that auto-size to their widest cell, with optional column headers.
//          A non-interactive display widget for property and data panels.
// Key invariants:
//   - cells is a flat [row_capacity * col_count] array; entry (r,c) at index
//     r * col_count + c. Each header/cell string is heap-owned (or NULL).
//   - A column's width is the widest of its header and cells plus 2 * cell_padding.
// Ownership/Lifetime:
//   - Headers and cells are owned by the grid and freed on replace/clear/destroy.
// Links: lib/gui/include/vg_ide_widgets_panels.h, lib/gui/include/vg_font.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_font.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward declarations
//=============================================================================

static void datagrid_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void datagrid_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void datagrid_paint(vg_widget_t *widget, void *canvas);
static void datagrid_destroy(vg_widget_t *widget);

static vg_widget_vtable_t g_datagrid_vtable = {
    .destroy = datagrid_destroy,
    .measure = datagrid_measure,
    .arrange = datagrid_arrange,
    .paint = datagrid_paint,
    .handle_event = NULL,
    .can_focus = NULL,
    .on_focus = NULL,
};

//=============================================================================
// Internal helpers
//=============================================================================

/// @brief Heap-copy @p text, returning NULL for NULL/empty input.
static char *datagrid_dup(const char *text) {
    if (!text || !text[0])
        return NULL;
    size_t n = strlen(text);
    char *copy = (char *)malloc(n + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, n + 1);
    return copy;
}

/// @brief Free the cell array and reset the row counters.
static void datagrid_free_cells(vg_datagrid_t *grid) {
    if (grid->cells) {
        long total = (long)grid->row_capacity * grid->col_count;
        for (long i = 0; i < total; i++)
            free(grid->cells[i]);
        free(grid->cells);
        grid->cells = NULL;
    }
    grid->row_count = 0;
    grid->row_capacity = 0;
}

/// @brief Free the header array.
static void datagrid_free_headers(vg_datagrid_t *grid) {
    if (grid->headers) {
        for (int c = 0; c < grid->col_count; c++)
            free(grid->headers[c]);
        free(grid->headers);
        grid->headers = NULL;
    }
    grid->has_headers = false;
}

/// @brief Grow the cell array to hold at least @p min_rows rows. Returns false on overflow/OOM.
static bool datagrid_ensure_rows(vg_datagrid_t *grid, int min_rows) {
    if (grid->col_count <= 0)
        return false;
    if (min_rows <= grid->row_capacity)
        return true;
    int new_cap = grid->row_capacity > 0 ? grid->row_capacity * 2 : 8;
    if (new_cap < min_rows)
        new_cap = min_rows;
    if (new_cap > INT_MAX / grid->col_count)
        return false;
    char **new_cells = (char **)calloc((size_t)new_cap * grid->col_count, sizeof(char *));
    if (!new_cells)
        return false;
    if (grid->cells) {
        memcpy(new_cells,
               grid->cells,
               (size_t)grid->row_capacity * grid->col_count * sizeof(char *));
        free(grid->cells);
    }
    grid->cells = new_cells;
    grid->row_capacity = new_cap;
    return true;
}

//=============================================================================
// VTable implementations
//=============================================================================

static void datagrid_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    vg_datagrid_t *grid = (vg_datagrid_t *)widget;
    (void)avail_w;
    (void)avail_h;
    float total_w = 0.0f;
    for (int c = 0; c < grid->col_count; c++)
        total_w += (float)vg_datagrid_column_width(grid, c);
    float lh = grid->line_height > 0.0f ? grid->line_height : 18.0f;
    int total_rows = grid->row_count + (grid->has_headers ? 1 : 0);
    widget->measured_width = total_w;
    widget->measured_height = (float)total_rows * lh;
    vg_widget_apply_constraints(widget);
}

static void datagrid_arrange(vg_widget_t *widget, float x, float y, float w, float h) {
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

static void datagrid_paint(vg_widget_t *widget, void *canvas) {
    vg_datagrid_t *grid = (vg_datagrid_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;
    if (grid->col_count <= 0)
        return;

    float ox = widget->x;
    float oy = widget->y;
    float lh = grid->line_height > 0.0f ? grid->line_height : 18.0f;

    vgfx_fill_rect(win,
                   (int32_t)ox,
                   (int32_t)oy,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   grid->bg_color);

    if (!grid->font || grid->font_size <= 0.0f)
        return;

    // Vertically center text within a row using the font's metrics.
    vg_font_metrics_t fm = {0};
    vg_font_get_metrics(grid->font, grid->font_size, &fm);
    float baseline = (lh - (float)(fm.ascent - fm.descent)) * 0.5f + (float)fm.ascent;

    float row_y = oy;

    // Header row.
    if (grid->has_headers && grid->headers) {
        float cx = ox;
        for (int c = 0; c < grid->col_count; c++) {
            int cw = vg_datagrid_column_width(grid, c);
            const char *h = grid->headers[c];
            if (h)
                vg_font_draw_text(canvas,
                                  grid->font,
                                  grid->font_size,
                                  cx + grid->cell_padding,
                                  row_y + baseline,
                                  h,
                                  grid->header_color);
            cx += (float)cw;
        }
        vgfx_fill_rect(win,
                       (int32_t)ox,
                       (int32_t)(row_y + lh) - 1,
                       (int32_t)widget->width,
                       1,
                       grid->grid_color);
        row_y += lh;
    }

    // Data rows.
    if (grid->cells) {
        for (int r = 0; r < grid->row_count; r++) {
            float cx = ox;
            for (int c = 0; c < grid->col_count; c++) {
                int cw = vg_datagrid_column_width(grid, c);
                const char *cell = grid->cells[(long)r * grid->col_count + c];
                if (cell)
                    vg_font_draw_text(canvas,
                                      grid->font,
                                      grid->font_size,
                                      cx + grid->cell_padding,
                                      row_y + baseline,
                                      cell,
                                      grid->fg_color);
                cx += (float)cw;
            }
            row_y += lh;
        }
    }
}

static void datagrid_destroy(vg_widget_t *widget) {
    vg_datagrid_t *grid = (vg_datagrid_t *)widget;
    datagrid_free_cells(grid);
    datagrid_free_headers(grid);
}

//=============================================================================
// Public API
//=============================================================================

vg_datagrid_t *vg_datagrid_create(vg_widget_t *parent) {
    vg_datagrid_t *grid = (vg_datagrid_t *)calloc(1, sizeof(vg_datagrid_t));
    if (!grid)
        return NULL;

    vg_widget_init(&grid->base, VG_WIDGET_DATAGRID, &g_datagrid_vtable);

    grid->cell_padding = 6.0f;

    vg_theme_t *theme = vg_theme_get_current();
    grid->font = theme->typography.font_regular;
    grid->font_size = theme->typography.size_normal;
    if (grid->font && grid->font_size > 0.0f) {
        vg_font_metrics_t metrics = {0};
        vg_font_get_metrics(grid->font, grid->font_size, &metrics);
        grid->line_height =
            metrics.line_height > 0 ? (float)metrics.line_height : grid->font_size * 1.4f;
    } else {
        grid->line_height = 18.0f;
    }
    grid->bg_color = theme->colors.bg_primary;
    grid->fg_color = theme->colors.fg_primary;
    grid->header_color = theme->colors.accent_primary;
    grid->grid_color = theme->colors.border_secondary;

    if (parent)
        vg_widget_add_child(parent, &grid->base);

    return grid;
}

void vg_datagrid_destroy(vg_datagrid_t *grid) {
    if (!grid)
        return;
    vg_widget_destroy(&grid->base);
}

void vg_datagrid_set_columns(vg_datagrid_t *grid, int count) {
    if (!grid)
        return;
    if (count < 0)
        count = 0;
    datagrid_free_cells(grid);
    datagrid_free_headers(grid);
    grid->col_count = count;
    if (count > 0) {
        grid->headers = (char **)calloc((size_t)count, sizeof(char *));
        if (!grid->headers)
            grid->col_count = 0;
    }
    grid->base.needs_layout = true;
    grid->base.needs_paint = true;
}

void vg_datagrid_set_header(vg_datagrid_t *grid, int col, const char *text) {
    if (!grid || !grid->headers || col < 0 || col >= grid->col_count)
        return;
    free(grid->headers[col]);
    grid->headers[col] = datagrid_dup(text);
    if (grid->headers[col])
        grid->has_headers = true;
    grid->base.needs_layout = true;
    grid->base.needs_paint = true;
}

void vg_datagrid_set_cell(vg_datagrid_t *grid, int row, int col, const char *text) {
    if (!grid || col < 0 || col >= grid->col_count || row < 0)
        return;
    if (!datagrid_ensure_rows(grid, row + 1))
        return;
    if (row + 1 > grid->row_count)
        grid->row_count = row + 1;
    long idx = (long)row * grid->col_count + col;
    free(grid->cells[idx]);
    grid->cells[idx] = datagrid_dup(text);
    grid->base.needs_layout = true;
    grid->base.needs_paint = true;
}

const char *vg_datagrid_get_cell(const vg_datagrid_t *grid, int row, int col) {
    if (!grid || !grid->cells || col < 0 || col >= grid->col_count || row < 0 ||
        row >= grid->row_count)
        return NULL;
    return grid->cells[(long)row * grid->col_count + col];
}

void vg_datagrid_clear(vg_datagrid_t *grid) {
    if (!grid)
        return;
    datagrid_free_cells(grid);
    grid->base.needs_layout = true;
    grid->base.needs_paint = true;
}

void vg_datagrid_set_font(vg_datagrid_t *grid, vg_font_t *font, float size) {
    if (!grid)
        return;
    grid->font = font;
    grid->font_size = size > 0.0f ? size : grid->font_size;
    if (font && grid->font_size > 0.0f) {
        vg_font_metrics_t metrics = {0};
        vg_font_get_metrics(font, grid->font_size, &metrics);
        if (metrics.line_height > 0)
            grid->line_height = (float)metrics.line_height;
    }
    grid->base.needs_layout = true;
    grid->base.needs_paint = true;
}

int vg_datagrid_column_width(const vg_datagrid_t *grid, int col) {
    if (!grid || !grid->font || grid->font_size <= 0.0f || col < 0 || col >= grid->col_count)
        return 0;
    float maxw = 0.0f;
    if (grid->headers && grid->headers[col]) {
        vg_text_metrics_t m = {0};
        vg_font_measure_text(grid->font, grid->font_size, grid->headers[col], &m);
        if (m.width > maxw)
            maxw = m.width;
    }
    if (grid->cells) {
        for (int r = 0; r < grid->row_count; r++) {
            const char *cell = grid->cells[(long)r * grid->col_count + col];
            if (!cell)
                continue;
            vg_text_metrics_t m = {0};
            vg_font_measure_text(grid->font, grid->font_size, cell, &m);
            if (m.width > maxw)
                maxw = m.width;
        }
    }
    return (int)(maxw + 0.5f) + (int)(grid->cell_padding * 2.0f);
}

int vg_datagrid_row_count(const vg_datagrid_t *grid) {
    return grid ? grid->row_count : 0;
}

int vg_datagrid_column_count(const vg_datagrid_t *grid) {
    return grid ? grid->col_count : 0;
}
