//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_codeeditor.c
//
//===----------------------------------------------------------------------===//
// vg_codeeditor.c - Code editor widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../../graphics/src/vgfx_internal.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Cross-platform attribute for unused variables/functions
#ifdef _MSC_VER
#define VG_UNUSED
#else
#define VG_UNUSED __attribute__((unused))
#endif

//=============================================================================
// Constants
//=============================================================================

#define INITIAL_LINE_CAPACITY 64
#define INITIAL_TEXT_CAPACITY 256
#define LINE_CAPACITY_GROWTH 2
#define CURSOR_BLINK_RATE 0.5f
#define CODEEDITOR_SCROLLBAR_WIDTH 12.0f
#define CODEEDITOR_FOLD_GUTTER_MIN_WIDTH 14.0f
#define CODEEDITOR_MOUSE_WHEEL_LINES 1.0f

//=============================================================================
// Forward Declarations
//=============================================================================

static void codeeditor_destroy(vg_widget_t *widget);
static void codeeditor_measure(vg_widget_t *widget, float available_width, float available_height);
static void codeeditor_paint(vg_widget_t *widget, void *canvas);
static bool codeeditor_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool codeeditor_can_focus(vg_widget_t *widget);
static void codeeditor_on_focus(vg_widget_t *widget, bool gained);

static bool ensure_line_capacity(vg_codeeditor_t *editor, int needed);
static bool ensure_text_capacity(vg_code_line_t *line, size_t needed);
static void insert_text_at_internal(vg_codeeditor_t *editor, int line, int col, const char *text);
static void delete_text_range_internal(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col);
static void codeeditor_adjust_hidden_cursors(vg_codeeditor_t *editor);

//=============================================================================
// CodeEditor VTable
//=============================================================================

static vg_widget_vtable_t g_codeeditor_vtable = {.destroy = codeeditor_destroy,
                                                 .measure = codeeditor_measure,
                                                 .arrange = NULL,
                                                 .paint = codeeditor_paint,
                                                 .handle_event = codeeditor_handle_event,
                                                 .can_focus = codeeditor_can_focus,
                                                 .on_focus = codeeditor_on_focus};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_line_capacity(vg_codeeditor_t *editor, int needed) {
    if (needed <= editor->line_capacity)
        return true;

    int new_capacity = editor->line_capacity;
    while (new_capacity < needed) {
        new_capacity *= LINE_CAPACITY_GROWTH;
    }

    vg_code_line_t *new_lines = realloc(editor->lines, new_capacity * sizeof(vg_code_line_t));
    if (!new_lines)
        return false;

    // Zero new entries
    memset(new_lines + editor->line_capacity,
           0,
           (new_capacity - editor->line_capacity) * sizeof(vg_code_line_t));

    editor->lines = new_lines;
    editor->line_capacity = new_capacity;
    return true;
}

static bool ensure_text_capacity(vg_code_line_t *line, size_t needed) {
    if (needed <= line->capacity)
        return true;

    size_t new_capacity = line->capacity;
    if (new_capacity == 0)
        new_capacity = INITIAL_TEXT_CAPACITY;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    char *new_text = realloc(line->text, new_capacity);
    if (!new_text)
        return false;

    line->text = new_text;
    line->capacity = new_capacity;
    return true;
}

static void free_line(vg_code_line_t *line) {
    if (line->text) {
        free(line->text);
        line->text = NULL;
    }
    if (line->colors) {
        free(line->colors);
        line->colors = NULL;
    }
    line->length = 0;
    line->capacity = 0;
    line->colors_capacity = 0;
}

// Ensure the colors array for line_idx is allocated and call the syntax highlighter.
// Safe to call every frame — realloc only when capacity is insufficient.
static void highlight_line(vg_codeeditor_t *editor, size_t line_idx) {
    if (!editor->syntax_highlighter || line_idx >= (size_t)editor->line_count)
        return;

    vg_code_line_t *line = &editor->lines[line_idx];
    if (line->length == 0)
        return;

    // Grow the colors buffer if needed
    if (line->colors_capacity < line->length) {
        size_t new_cap = line->length + 16;
        uint32_t *nc = (uint32_t *)realloc(line->colors, new_cap * sizeof(uint32_t));
        if (!nc) {
            // Realloc failed: discard stale buffer so the paint path falls back to
            // monochrome rendering rather than reading past the old capacity.
            free(line->colors);
            line->colors = NULL;
            line->colors_capacity = 0;
            return;
        }
        line->colors = nc;
        line->colors_capacity = new_cap;
    }

    editor->syntax_highlighter(
        (vg_widget_t *)editor, (int)line_idx, line->text, line->colors, editor->syntax_data);
}

static void codeeditor_draw_text_slice(void *canvas,
                                       vg_font_t *font,
                                       float font_size,
                                       float x,
                                       float y,
                                       const char *text,
                                       size_t start,
                                       size_t len,
                                       uint32_t color) {
    if (!font || !text || len == 0)
        return;

    char stack_buf[256];
    char *buf = stack_buf;
    size_t copy_len = len;
    if (copy_len >= sizeof(stack_buf)) {
        buf = (char *)malloc(copy_len + 1);
        if (!buf)
            return;
    }
    memcpy(buf, text + start, copy_len);
    buf[copy_len] = '\0';
    vg_font_draw_text(canvas, font, font_size, x, y, buf, color);
    if (buf != stack_buf)
        free(buf);
}

static void codeeditor_draw_colored_slice(void *canvas,
                                          vg_font_t *font,
                                          float font_size,
                                          float origin_x,
                                          float y,
                                          const char *text,
                                          const uint32_t *colors,
                                          size_t colors_capacity,
                                          size_t start,
                                          size_t len,
                                          float char_width,
                                          uint32_t fallback_color) {
    if (!text || !colors || len == 0)
        return;

    size_t run_start = start;
    uint32_t run_color = run_start < colors_capacity ? colors[run_start] : fallback_color;
    size_t end = start + len;
    for (size_t i = start + 1; i < end; i++) {
        uint32_t color = i < colors_capacity ? colors[i] : fallback_color;
        if (color == run_color)
            continue;
        codeeditor_draw_text_slice(canvas,
                                   font,
                                   font_size,
                                   origin_x + (float)(run_start - start) * char_width,
                                   y,
                                   text,
                                   run_start,
                                   i - run_start,
                                   run_color);
        run_start = i;
        run_color = color;
    }
    codeeditor_draw_text_slice(canvas,
                               font,
                               font_size,
                               origin_x + (float)(run_start - start) * char_width,
                               y,
                               text,
                               run_start,
                               end - run_start,
                               run_color);
}

static float codeeditor_auto_line_number_gutter_width(const vg_codeeditor_t *editor) {
    if (!editor || !editor->show_line_numbers)
        return 0.0f;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", editor->line_count > 0 ? editor->line_count : 1);
    if (editor->font) {
        vg_text_metrics_t metrics = {0};
        vg_font_measure_text(editor->font, editor->font_size, buf, &metrics);
        return metrics.width + 20.0f;
    }
    return (float)strlen(buf) * editor->char_width + 20.0f;
}

static float codeeditor_fold_gutter_width(const vg_codeeditor_t *editor) {
    if (!editor || !editor->show_fold_gutter)
        return 0.0f;
    float width = editor->line_height > 0.0f ? editor->line_height : CODEEDITOR_FOLD_GUTTER_MIN_WIDTH;
    return width < CODEEDITOR_FOLD_GUTTER_MIN_WIDTH ? CODEEDITOR_FOLD_GUTTER_MIN_WIDTH : width;
}

static float codeeditor_line_number_gutter_width(const vg_codeeditor_t *editor) {
    if (!editor || !editor->show_line_numbers)
        return 0.0f;
    if (editor->line_number_width_override > 0.0f)
        return editor->line_number_width_override * editor->char_width;
    return codeeditor_auto_line_number_gutter_width(editor);
}

static const struct vg_fold_region *codeeditor_fold_region_starting_at(const vg_codeeditor_t *editor,
                                                                       int line) {
    if (!editor)
        return NULL;
    for (int i = 0; i < editor->fold_region_count; i++) {
        if (editor->fold_regions[i].start_line == line)
            return &editor->fold_regions[i];
    }
    return NULL;
}

static struct vg_fold_region *codeeditor_fold_region_starting_at_mut(vg_codeeditor_t *editor, int line) {
    if (!editor)
        return NULL;
    for (int i = 0; i < editor->fold_region_count; i++) {
        if (editor->fold_regions[i].start_line == line)
            return &editor->fold_regions[i];
    }
    return NULL;
}

static bool codeeditor_line_is_hidden(const vg_codeeditor_t *editor, int line) {
    if (!editor)
        return false;
    for (int i = 0; i < editor->fold_region_count; i++) {
        const struct vg_fold_region *region = &editor->fold_regions[i];
        if (region->folded && line > region->start_line && line <= region->end_line)
            return true;
    }
    return false;
}

static int codeeditor_visible_anchor_line(const vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return 0;
    if (line < 0)
        line = 0;
    if (line >= editor->line_count)
        line = editor->line_count - 1;
    if (!codeeditor_line_is_hidden(editor, line))
        return line;

    int best_start = line;
    bool found = false;
    for (int i = 0; i < editor->fold_region_count; i++) {
        const struct vg_fold_region *region = &editor->fold_regions[i];
        if (region->folded && line > region->start_line && line <= region->end_line) {
            if (!found || region->start_line < best_start) {
                best_start = region->start_line;
                found = true;
            }
        }
    }
    return found ? best_start : line;
}

static int codeeditor_next_visible_line(const vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return -1;
    for (int next = line + 1; next < editor->line_count; next++) {
        if (!codeeditor_line_is_hidden(editor, next))
            return next;
    }
    return editor->line_count;
}

VG_UNUSED static int codeeditor_prev_visible_line(const vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return -1;
    for (int prev = line - 1; prev >= 0; prev--) {
        if (!codeeditor_line_is_hidden(editor, prev))
            return prev;
    }
    return -1;
}

static void update_gutter_width(vg_codeeditor_t *editor) {
    if (!editor) {
        return;
    }
    editor->gutter_width =
        codeeditor_line_number_gutter_width(editor) + codeeditor_fold_gutter_width(editor);
}

static int codeeditor_chars_per_row(const vg_codeeditor_t *editor, float content_width) {
    if (!editor || !editor->word_wrap || editor->char_width <= 0.0f || content_width <= 0.0f)
        return 0;
    int chars = (int)(content_width / editor->char_width);
    return chars > 0 ? chars : 1;
}

static int codeeditor_wrapped_rows_for_line(const vg_codeeditor_t *editor,
                                            int line,
                                            float content_width) {
    if (!editor || line < 0 || line >= editor->line_count)
        return 1;
    int chars_per_row = codeeditor_chars_per_row(editor, content_width);
    if (chars_per_row <= 0)
        return 1;
    size_t len = editor->lines[line].length;
    if (len == 0)
        return 1;
    return (int)((len + (size_t)chars_per_row - 1) / (size_t)chars_per_row);
}

static int codeeditor_visual_rows_for_line(const vg_codeeditor_t *editor,
                                           int line,
                                           float content_width) {
    if (!editor || line < 0 || line >= editor->line_count || codeeditor_line_is_hidden(editor, line))
        return 0;
    if (!editor->word_wrap)
        return 1;
    return codeeditor_wrapped_rows_for_line(editor, line, content_width);
}

static float codeeditor_total_content_height_for_width(const vg_codeeditor_t *editor,
                                                       float content_width) {
    if (!editor)
        return 0.0f;

    float total_rows = 0.0f;
    for (int i = 0; i < editor->line_count; i++) {
        total_rows += (float)codeeditor_visual_rows_for_line(editor, i, content_width);
    }
    return total_rows * editor->line_height;
}

static float codeeditor_content_draw_width(const vg_codeeditor_t *editor, const vg_widget_t *widget) {
    if (!editor || !widget)
        return 0.0f;

    float base_width = widget->width - editor->gutter_width;
    if (base_width < 0.0f)
        base_width = 0.0f;
    if (!editor->word_wrap)
        return base_width;

    float content_width = base_width;
    for (int pass = 0; pass < 3; pass++) {
        float total_height = codeeditor_total_content_height_for_width(editor, content_width);
        float next_width =
            base_width - ((total_height > widget->height) ? CODEEDITOR_SCROLLBAR_WIDTH : 0.0f);
        if (next_width < 0.0f)
            next_width = 0.0f;
        if (next_width == content_width)
            break;
        content_width = next_width;
    }
    return content_width;
}

static float codeeditor_total_content_height(const vg_codeeditor_t *editor, const vg_widget_t *widget) {
    return codeeditor_total_content_height_for_width(editor, codeeditor_content_draw_width(editor, widget));
}

static int codeeditor_total_visual_rows_for_width(const vg_codeeditor_t *editor, float content_width) {
    if (!editor)
        return 0;

    int total_rows = 0;
    for (int i = 0; i < editor->line_count; i++) {
        total_rows += codeeditor_visual_rows_for_line(editor, i, content_width);
    }
    return total_rows;
}

static float codeeditor_max_scroll_y(const vg_codeeditor_t *editor, const vg_widget_t *widget) {
    if (!editor || !widget)
        return 0.0f;
    float max_scroll = codeeditor_total_content_height(editor, widget) - widget->height;
    return max_scroll > 0.0f ? max_scroll : 0.0f;
}

static void codeeditor_visual_offset_for_position(const vg_codeeditor_t *editor,
                                                  float content_width,
                                                  int line,
                                                  int col,
                                                  int *out_row_index,
                                                  int *out_col_in_row) {
    if (out_row_index)
        *out_row_index = 0;
    if (out_col_in_row)
        *out_col_in_row = 0;
    if (!editor || line < 0 || line >= editor->line_count)
        return;
    line = codeeditor_visible_anchor_line(editor, line);

    int chars_per_row = codeeditor_chars_per_row(editor, content_width);
    int row_index = 0;
    int col_in_row = col;
    if (chars_per_row > 0) {
        size_t len = editor->lines[line].length;
        row_index = col / chars_per_row;
        if (col > 0 && (size_t)col == len && len > 0 && (len % (size_t)chars_per_row) == 0) {
            row_index = (int)((len - 1) / (size_t)chars_per_row);
        }
        col_in_row = col - row_index * chars_per_row;
        if (col_in_row < 0)
            col_in_row = 0;
    }

    if (out_row_index)
        *out_row_index = row_index;
    if (out_col_in_row)
        *out_col_in_row = col_in_row;
}

static int codeeditor_visual_row_for_position(const vg_codeeditor_t *editor,
                                              float content_width,
                                              int line,
                                              int col) {
    if (!editor)
        return 0;
    if (line < 0)
        line = 0;
    if (line >= editor->line_count)
        line = editor->line_count - 1;
    line = codeeditor_visible_anchor_line(editor, line);

    int visual_row = 0;
    for (int i = 0; i < line; i++) {
        visual_row += codeeditor_visual_rows_for_line(editor, i, content_width);
    }

    int wrapped_row = 0;
    codeeditor_visual_offset_for_position(editor, content_width, line, col, &wrapped_row, NULL);
    visual_row += wrapped_row;
    return visual_row;
}

static void codeeditor_locate_visual_row(const vg_codeeditor_t *editor,
                                         float content_width,
                                         int visual_row,
                                         int *out_line,
                                         int *out_row_in_line) {
    if (out_line)
        *out_line = 0;
    if (out_row_in_line)
        *out_row_in_line = 0;
    if (!editor || editor->line_count <= 0)
        return;

    if (visual_row < 0)
        visual_row = 0;

    int accumulated = 0;
    for (int line = 0; line < editor->line_count; line++) {
        int row_count = codeeditor_visual_rows_for_line(editor, line, content_width);
        if (row_count == 0)
            continue;
        if (visual_row < accumulated + row_count) {
            if (out_line)
                *out_line = line;
            if (out_row_in_line)
                *out_row_in_line = visual_row - accumulated;
            return;
        }
        accumulated += row_count;
    }

    if (out_line)
        *out_line = codeeditor_visible_anchor_line(editor, editor->line_count - 1);
    if (out_row_in_line) {
        int last_rows = codeeditor_visual_rows_for_line(
            editor, codeeditor_visible_anchor_line(editor, editor->line_count - 1), content_width);
        *out_row_in_line = last_rows > 0 ? last_rows - 1 : 0;
    }
}

static void codeeditor_clamp_scroll(vg_codeeditor_t *editor, const vg_widget_t *widget) {
    if (!editor || !widget)
        return;
    if (editor->word_wrap)
        editor->scroll_x = 0.0f;
    if (editor->scroll_y < 0.0f)
        editor->scroll_y = 0.0f;
    float max_scroll = codeeditor_max_scroll_y(editor, widget);
    if (editor->scroll_y > max_scroll)
        editor->scroll_y = max_scroll;
}

static bool codeeditor_get_scrollbar_metrics(const vg_codeeditor_t *editor,
                                             const vg_widget_t *widget,
                                             float *out_track_x,
                                             float *out_thumb_y,
                                             float *out_thumb_height,
                                             float *out_max_scroll,
                                             float *out_thumb_travel) {
    if (out_track_x)
        *out_track_x = 0.0f;
    if (out_thumb_y)
        *out_thumb_y = 0.0f;
    if (out_thumb_height)
        *out_thumb_height = 0.0f;
    if (out_max_scroll)
        *out_max_scroll = 0.0f;
    if (out_thumb_travel)
        *out_thumb_travel = 0.0f;
    if (!editor || !widget || widget->height <= 0.0f)
        return false;

    float total_content_height = codeeditor_total_content_height(editor, widget);
    float visible_height = widget->height;
    if (total_content_height <= visible_height)
        return false;

    float thumb_ratio = visible_height / total_content_height;
    if (thumb_ratio > 1.0f)
        thumb_ratio = 1.0f;
    float thumb_height = visible_height * thumb_ratio;
    if (thumb_height < 20.0f)
        thumb_height = 20.0f;
    if (thumb_height > visible_height)
        thumb_height = visible_height;

    float max_scroll = total_content_height - visible_height;
    float thumb_travel = visible_height - thumb_height;
    float scroll_ratio = max_scroll > 0.0f ? editor->scroll_y / max_scroll : 0.0f;
    if (scroll_ratio < 0.0f)
        scroll_ratio = 0.0f;
    if (scroll_ratio > 1.0f)
        scroll_ratio = 1.0f;

    if (out_track_x)
        *out_track_x = widget->width - CODEEDITOR_SCROLLBAR_WIDTH;
    if (out_thumb_y)
        *out_thumb_y = scroll_ratio * thumb_travel;
    if (out_thumb_height)
        *out_thumb_height = thumb_height;
    if (out_max_scroll)
        *out_max_scroll = max_scroll;
    if (out_thumb_travel)
        *out_thumb_travel = thumb_travel;
    return true;
}

static void codeeditor_local_point_to_position(const vg_codeeditor_t *editor,
                                               const vg_widget_t *widget,
                                               float local_x,
                                               float local_y,
                                               int *out_line,
                                               int *out_col) {
    if (out_line)
        *out_line = 0;
    if (out_col)
        *out_col = 0;
    if (!editor || !widget || editor->line_count <= 0)
        return;

    if (editor->word_wrap) {
        float content_width = codeeditor_content_draw_width(editor, widget);
        int chars_per_row = codeeditor_chars_per_row(editor, content_width);
        float doc_y = local_y + editor->scroll_y;
        int visual_row =
            editor->line_height > 0.0f ? (int)(doc_y / editor->line_height) : 0;
        int line = 0;
        int row_in_line = 0;
        codeeditor_locate_visual_row(editor, content_width, visual_row, &line, &row_in_line);

        float content_local_x = local_x - editor->gutter_width;
        if (content_local_x < 0.0f)
            content_local_x = 0.0f;
        int col_in_row = editor->char_width > 0.0f
                             ? (int)(content_local_x / editor->char_width + 0.5f)
                             : 0;
        if (col_in_row < 0)
            col_in_row = 0;
        int col = row_in_line * chars_per_row + col_in_row;
        if (col > (int)editor->lines[line].length)
            col = (int)editor->lines[line].length;
        if (out_line)
            *out_line = line;
        if (out_col)
            *out_col = col;
        return;
    }

    float content_local_x = local_x - editor->gutter_width + editor->scroll_x;
    float doc_y = local_y + editor->scroll_y;
    int visual_row = editor->line_height > 0.0f ? (int)(doc_y / editor->line_height) : 0;
    int line = 0;
    codeeditor_locate_visual_row(editor, codeeditor_content_draw_width(editor, widget), visual_row, &line, NULL);

    int col = editor->char_width > 0.0f ? (int)(content_local_x / editor->char_width + 0.5f) : 0;
    if (col < 0)
        col = 0;
    if (col > (int)editor->lines[line].length)
        col = (int)editor->lines[line].length;
    if (out_line)
        *out_line = line;
    if (out_col)
        *out_col = col;
}

static void codeeditor_move_cursor_vertical(vg_codeeditor_t *editor,
                                            const vg_widget_t *widget,
                                            int visual_rows) {
    if (!editor || !widget || visual_rows == 0 || editor->line_count <= 0)
        return;

    float content_width = codeeditor_content_draw_width(editor, widget);
    int row_index = 0;
    int col_in_row = 0;
    codeeditor_visual_offset_for_position(
        editor, content_width, editor->cursor_line, editor->cursor_col, &row_index, &col_in_row);

    int visual_row =
        codeeditor_visual_row_for_position(editor, content_width, editor->cursor_line, editor->cursor_col);
    int target_visual_row = visual_row + visual_rows;
    int total_visual_rows = codeeditor_total_visual_rows_for_width(editor, content_width);
    if (total_visual_rows <= 0)
        return;
    if (target_visual_row < 0)
        target_visual_row = 0;
    if (target_visual_row >= total_visual_rows)
        target_visual_row = total_visual_rows - 1;

    int target_line = 0;
    int target_row_in_line = 0;
    codeeditor_locate_visual_row(
        editor, content_width, target_visual_row, &target_line, &target_row_in_line);

    int chars_per_row = codeeditor_chars_per_row(editor, content_width);
    int target_col = target_row_in_line * chars_per_row + col_in_row;
    if (target_col > (int)editor->lines[target_line].length)
        target_col = (int)editor->lines[target_line].length;

    editor->cursor_line = target_line;
    editor->cursor_col = target_col;
}

// Ensure the cursor line is visible by adjusting scroll position
static void ensure_cursor_visible(vg_codeeditor_t *editor) {
    if (!editor)
        return;

    float visible_height = editor->base.height;
    float content_width = codeeditor_content_draw_width(editor, &editor->base);
    int cursor_visual_row =
        codeeditor_visual_row_for_position(editor, content_width, editor->cursor_line, editor->cursor_col);
    float cursor_y = (float)cursor_visual_row * editor->line_height;

    // Scroll up if cursor is above visible area
    if (cursor_y < editor->scroll_y) {
        editor->scroll_y = cursor_y;
    }
    // Scroll down if cursor is below visible area
    else if (cursor_y + editor->line_height > editor->scroll_y + visible_height) {
        editor->scroll_y = cursor_y + editor->line_height - visible_height;
    }

    // Clamp scroll_y to valid range
    if (editor->scroll_y < 0)
        editor->scroll_y = 0;

    float max_scroll = codeeditor_max_scroll_y(editor, &editor->base);
    if (editor->scroll_y > max_scroll)
        editor->scroll_y = max_scroll;
}

void vg_codeeditor_refresh_layout_state(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    update_gutter_width(editor);
    codeeditor_adjust_hidden_cursors(editor);
    codeeditor_clamp_scroll(editor, &editor->base);
    editor->base.needs_layout = true;
    editor->base.needs_paint = true;
}

//=============================================================================
// Undo/Redo History Management
//=============================================================================

#define HISTORY_INITIAL_CAPACITY 64

static vg_edit_history_t *edit_history_create(void) {
    vg_edit_history_t *history = calloc(1, sizeof(vg_edit_history_t));
    if (!history)
        return NULL;

    history->operations = calloc(HISTORY_INITIAL_CAPACITY, sizeof(vg_edit_op_t *));
    if (!history->operations) {
        free(history);
        return NULL;
    }

    history->capacity = HISTORY_INITIAL_CAPACITY;
    history->count = 0;
    history->current_index = 0;
    history->next_group_id = 1;
    history->is_grouping = false;
    history->current_group = 0;

    return history;
}

static void edit_op_destroy(vg_edit_op_t *op) {
    if (!op)
        return;
    free(op->old_text);
    free(op->new_text);
    free(op);
}

static void edit_history_destroy(vg_edit_history_t *history) {
    if (!history)
        return;

    for (size_t i = 0; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
    }
    free(history->operations);
    free(history);
}

VG_UNUSED
static void edit_history_clear(vg_edit_history_t *history) {
    if (!history)
        return;

    for (size_t i = 0; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
        history->operations[i] = NULL;
    }
    history->count = 0;
    history->current_index = 0;
}

static void edit_history_push(vg_edit_history_t *history, vg_edit_op_t *op) {
    if (!history || !op)
        return;

    // Discard any redo operations
    for (size_t i = history->current_index; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
        history->operations[i] = NULL;
    }
    history->count = history->current_index;

    // Grow capacity if needed
    if (history->count >= history->capacity) {
        size_t new_capacity = history->capacity * 2;
        vg_edit_op_t **new_ops =
            realloc(history->operations, new_capacity * sizeof(vg_edit_op_t *));
        if (!new_ops) {
            edit_op_destroy(op);
            return;
        }
        history->operations = new_ops;
        history->capacity = new_capacity;
    }

    // Set group ID if grouping
    if (history->is_grouping) {
        op->group_id = history->current_group;
    }

    history->operations[history->count++] = op;
    history->current_index = history->count;
}

static vg_edit_op_t *edit_history_pop_undo(vg_edit_history_t *history) {
    if (!history || history->current_index == 0)
        return NULL;
    history->current_index--;
    return history->operations[history->current_index];
}

static vg_edit_op_t *edit_history_peek_undo(vg_edit_history_t *history) {
    if (!history || history->current_index == 0)
        return NULL;
    return history->operations[history->current_index - 1];
}

static vg_edit_op_t *edit_history_pop_redo(vg_edit_history_t *history) {
    if (!history || history->current_index >= history->count)
        return NULL;
    vg_edit_op_t *op = history->operations[history->current_index];
    history->current_index++;
    return op;
}

VG_UNUSED
static void edit_history_begin_group(vg_edit_history_t *history) {
    if (!history)
        return;
    history->is_grouping = true;
    history->current_group = history->next_group_id++;
}

VG_UNUSED
static void edit_history_end_group(vg_edit_history_t *history) {
    if (!history)
        return;
    history->is_grouping = false;
    history->current_group = 0;
}

static vg_edit_op_t *create_edit_op(vg_edit_op_type_t type,
                                    int cursor_id,
                                    int start_line,
                                    int start_col,
                                    int end_line,
                                    int end_col,
                                    const char *old_text,
                                    const char *new_text,
                                    int cursor_line_before,
                                    int cursor_col_before,
                                    int cursor_line_after,
                                    int cursor_col_after) {
    vg_edit_op_t *op = calloc(1, sizeof(vg_edit_op_t));
    if (!op)
        return NULL;

    op->type = type;
    op->cursor_id = cursor_id;
    op->start_line = start_line;
    op->start_col = start_col;
    op->end_line = end_line;
    op->end_col = end_col;
    op->old_text = old_text ? strdup(old_text) : NULL;
    op->new_text = new_text ? strdup(new_text) : NULL;
    op->cursor_line_before = cursor_line_before;
    op->cursor_col_before = cursor_col_before;
    op->cursor_line_after = cursor_line_after;
    op->cursor_col_after = cursor_col_after;
    op->group_id = 0;

    return op;
}

//=============================================================================
// Selection Helper Functions
//=============================================================================

static void normalize_selection(
    vg_codeeditor_t *editor, int *start_line, int *start_col, int *end_line, int *end_col) {
    *start_line = editor->selection.start_line;
    *start_col = editor->selection.start_col;
    *end_line = editor->selection.end_line;
    *end_col = editor->selection.end_col;

    // Normalize: start should be before end
    if (*start_line > *end_line || (*start_line == *end_line && *start_col > *end_col)) {
        int tmp = *start_line;
        *start_line = *end_line;
        *end_line = tmp;
        tmp = *start_col;
        *start_col = *end_col;
        *end_col = tmp;
    }
}

static void normalize_selection_range(int *start_line,
                                      int *start_col,
                                      int *end_line,
                                      int *end_col) {
    if (*start_line > *end_line || (*start_line == *end_line && *start_col > *end_col)) {
        int tmp = *start_line;
        *start_line = *end_line;
        *end_line = tmp;
        tmp = *start_col;
        *start_col = *end_col;
        *end_col = tmp;
    }
}

static char *copy_text_range(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    if (!editor)
        return NULL;

    normalize_selection_range(&start_line, &start_col, &end_line, &end_col);

    size_t total_len = 0;
    for (int line = start_line; line <= end_line; line++) {
        int col_start = (line == start_line) ? start_col : 0;
        int col_end = (line == end_line) ? end_col : (int)editor->lines[line].length;
        total_len += (size_t)(col_end - col_start);
        if (line < end_line)
            total_len++;
    }

    char *result = malloc(total_len + 1);
    if (!result)
        return NULL;

    char *ptr = result;
    for (int line = start_line; line <= end_line; line++) {
        int col_start = (line == start_line) ? start_col : 0;
        int col_end = (line == end_line) ? end_col : (int)editor->lines[line].length;
        size_t len = (size_t)(col_end - col_start);
        if (len > 0) {
            memcpy(ptr, editor->lines[line].text + col_start, len);
            ptr += len;
        }
        if (line < end_line)
            *ptr++ = '\n';
    }
    *ptr = '\0';
    return result;
}

static void clear_extra_cursor_selections(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    for (int i = 0; i < editor->extra_cursor_count; i++)
        editor->extra_cursors[i].has_selection = false;
}

// Clamp a single line index to [0, line_count). Use when the caller wants the
// column to stay untouched (e.g. extending an existing selection's anchor).
static void clamp_editor_line(vg_codeeditor_t *editor, int *line) {
    if (!editor || !line || editor->line_count <= 0)
        return;
    if (*line < 0)
        *line = 0;
    if (*line >= editor->line_count)
        *line = editor->line_count - 1;
}

// Clamp a column to [0, lines[line].length] for an already-clamped line index.
static void clamp_editor_col(vg_codeeditor_t *editor, int line, int *col) {
    if (!editor || !col || editor->line_count <= 0)
        return;
    if (line < 0 || line >= editor->line_count)
        return;
    if (*col < 0)
        *col = 0;
    if (*col > (int)editor->lines[line].length)
        *col = (int)editor->lines[line].length;
}

// Atomic clamp of both axes. Note that clamping the line index can implicitly
// shift the column (when the new line is shorter than the requested column).
// Callers that maintain an independent column on a selection anchor should use
// clamp_editor_line / clamp_editor_col directly so they control the order.
static void clamp_editor_position(vg_codeeditor_t *editor, int *line, int *col) {
    if (!editor || !line || !col || editor->line_count <= 0)
        return;
    clamp_editor_line(editor, line);
    clamp_editor_col(editor, *line, col);
}

static void codeeditor_clamp_cursor_to_visible(vg_codeeditor_t *editor, int *line, int *col) {
    if (!editor || !line || !col || editor->line_count <= 0)
        return;
    clamp_editor_position(editor, line, col);
    *line = codeeditor_visible_anchor_line(editor, *line);
    clamp_editor_col(editor, *line, col);
}

static void codeeditor_adjust_hidden_cursors(vg_codeeditor_t *editor) {
    if (!editor || editor->line_count <= 0)
        return;

    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        codeeditor_clamp_cursor_to_visible(
            editor, &editor->extra_cursors[i].line, &editor->extra_cursors[i].col);
    }
}

static int compare_positions(int lhs_line, int lhs_col, int rhs_line, int rhs_col) {
    if (lhs_line != rhs_line)
        return (lhs_line < rhs_line) ? -1 : 1;
    if (lhs_col != rhs_col)
        return (lhs_col < rhs_col) ? -1 : 1;
    return 0;
}

typedef struct vg_edit_target {
    int cursor_id;
    int cursor_line_before;
    int cursor_col_before;
    int start_line;
    int start_col;
    int end_line;
    int end_col;
} vg_edit_target_t;

static int edit_target_compare_desc(const void *lhs, const void *rhs) {
    const vg_edit_target_t *a = (const vg_edit_target_t *)lhs;
    const vg_edit_target_t *b = (const vg_edit_target_t *)rhs;
    int cmp = compare_positions(b->start_line, b->start_col, a->start_line, a->start_col);
    if (cmp != 0)
        return cmp;
    cmp = compare_positions(b->end_line, b->end_col, a->end_line, a->end_col);
    if (cmp != 0)
        return cmp;
    return a->cursor_id - b->cursor_id;
}

static int edit_target_compare_asc(const void *lhs, const void *rhs) {
    const vg_edit_target_t *a = (const vg_edit_target_t *)lhs;
    const vg_edit_target_t *b = (const vg_edit_target_t *)rhs;
    int cmp = compare_positions(a->start_line, a->start_col, b->start_line, b->start_col);
    if (cmp != 0)
        return cmp;
    cmp = compare_positions(a->end_line, a->end_col, b->end_line, b->end_col);
    if (cmp != 0)
        return cmp;
    return a->cursor_id - b->cursor_id;
}

static void set_cursor_position_by_id(vg_codeeditor_t *editor, int cursor_id, int line, int col) {
    if (cursor_id == 0) {
        editor->cursor_line = line;
        editor->cursor_col = col;
        return;
    }
    int extra_idx = cursor_id - 1;
    if (extra_idx < 0 || extra_idx >= editor->extra_cursor_count)
        return;
    editor->extra_cursors[extra_idx].line = line;
    editor->extra_cursors[extra_idx].col = col;
}

static void clear_cursor_selection_by_id(vg_codeeditor_t *editor, int cursor_id) {
    if (cursor_id == 0) {
        editor->has_selection = false;
        return;
    }
    int extra_idx = cursor_id - 1;
    if (extra_idx < 0 || extra_idx >= editor->extra_cursor_count)
        return;
    editor->extra_cursors[extra_idx].has_selection = false;
}

static void add_edit_target(vg_edit_target_t *targets,
                            int *count,
                            int cursor_id,
                            int cursor_line_before,
                            int cursor_col_before,
                            int start_line,
                            int start_col,
                            int end_line,
                            int end_col) {
    normalize_selection_range(&start_line, &start_col, &end_line, &end_col);
    for (int i = 0; i < *count; i++) {
        if (targets[i].start_line == start_line && targets[i].start_col == start_col &&
            targets[i].end_line == end_line && targets[i].end_col == end_col)
            return;
    }

    targets[*count].cursor_id = cursor_id;
    targets[*count].cursor_line_before = cursor_line_before;
    targets[*count].cursor_col_before = cursor_col_before;
    targets[*count].start_line = start_line;
    targets[*count].start_col = start_col;
    targets[*count].end_line = end_line;
    targets[*count].end_col = end_col;
    (*count)++;
}

static void record_edit_history(vg_codeeditor_t *editor,
                                vg_edit_op_type_t type,
                                int cursor_id,
                                int start_line,
                                int start_col,
                                int end_line,
                                int end_col,
                                const char *old_text,
                                const char *new_text,
                                int cursor_line_before,
                                int cursor_col_before,
                                int cursor_line_after,
                                int cursor_col_after) {
    if (!editor || !editor->history)
        return;
    vg_edit_op_t *op = create_edit_op(type,
                                      cursor_id,
                                      start_line,
                                      start_col,
                                      end_line,
                                      end_col,
                                      old_text,
                                      new_text,
                                      cursor_line_before,
                                      cursor_col_before,
                                      cursor_line_after,
                                      cursor_col_after);
    if (op)
        edit_history_push(editor->history, op);
}

static void apply_edit_targets(vg_codeeditor_t *editor,
                               vg_edit_target_t *targets,
                               int target_count,
                               const char *replacement_text) {
    if (!editor || target_count <= 0)
        return;

    const int cursor_count = 1 + editor->extra_cursor_count;
    int *new_lines = malloc((size_t)cursor_count * sizeof(int));
    int *new_cols = malloc((size_t)cursor_count * sizeof(int));
    if (!new_lines || !new_cols) {
        free(new_lines);
        free(new_cols);
        return;
    }

    new_lines[0] = editor->cursor_line;
    new_cols[0] = editor->cursor_col;
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        new_lines[i + 1] = editor->extra_cursors[i].line;
        new_cols[i + 1] = editor->extra_cursors[i].col;
    }

    if (editor->history && target_count > 1)
        edit_history_begin_group(editor->history);

    for (int i = 0; i < target_count; i++) {
        const vg_edit_target_t *target = &targets[i];
        char *old_text = NULL;
        int history_end_line = target->end_line;
        int history_end_col = target->end_col;
        const int has_old_text =
            compare_positions(
                target->start_line, target->start_col, target->end_line, target->end_col) != 0;

        if (has_old_text) {
            old_text = copy_text_range(
                editor, target->start_line, target->start_col, target->end_line, target->end_col);
            delete_text_range_internal(
                editor, target->start_line, target->start_col, target->end_line, target->end_col);
        } else {
            editor->cursor_line = target->start_line;
            editor->cursor_col = target->start_col;
        }

        if (replacement_text && replacement_text[0] != '\0')
            insert_text_at_internal(
                editor, target->start_line, target->start_col, replacement_text);

        new_lines[target->cursor_id] = editor->cursor_line;
        new_cols[target->cursor_id] = editor->cursor_col;
        clear_cursor_selection_by_id(editor, target->cursor_id);
        history_end_line = editor->cursor_line;
        history_end_col = editor->cursor_col;

        if (replacement_text && replacement_text[0] != '\0') {
            vg_edit_op_type_t type = has_old_text ? VG_EDIT_REPLACE : VG_EDIT_INSERT;
            record_edit_history(editor,
                                type,
                                target->cursor_id,
                                target->start_line,
                                target->start_col,
                                history_end_line,
                                history_end_col,
                                old_text,
                                replacement_text,
                                target->cursor_line_before,
                                target->cursor_col_before,
                                editor->cursor_line,
                                editor->cursor_col);
        } else if (has_old_text) {
            record_edit_history(editor,
                                VG_EDIT_DELETE,
                                target->cursor_id,
                                target->start_line,
                                target->start_col,
                                target->end_line,
                                target->end_col,
                                old_text,
                                NULL,
                                target->cursor_line_before,
                                target->cursor_col_before,
                                target->start_line,
                                target->start_col);
        }

        free(old_text);
    }

    if (editor->history && target_count > 1)
        edit_history_end_group(editor->history);

    for (int i = 0; i < cursor_count; i++)
        set_cursor_position_by_id(editor, i, new_lines[i], new_cols[i]);

    free(new_lines);
    free(new_cols);
    editor->modified = true;
    vg_codeeditor_refresh_layout_state(editor);
    ensure_cursor_visible(editor);
    editor->base.needs_paint = true;
}

//=============================================================================
// CodeEditor Implementation
//=============================================================================

vg_codeeditor_t *vg_codeeditor_create(vg_widget_t *parent) {
    vg_codeeditor_t *editor = calloc(1, sizeof(vg_codeeditor_t));
    if (!editor)
        return NULL;

    // Initialize base widget
    vg_widget_init(&editor->base, VG_WIDGET_CODEEDITOR, &g_codeeditor_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Allocate initial lines
    editor->lines = calloc(INITIAL_LINE_CAPACITY, sizeof(vg_code_line_t));
    if (!editor->lines) {
        free(editor);
        return NULL;
    }
    editor->line_capacity = INITIAL_LINE_CAPACITY;

    // Create first empty line
    editor->line_count = 1;
    editor->lines[0].text = malloc(INITIAL_TEXT_CAPACITY);
    if (!editor->lines[0].text) {
        free(editor->lines);
        free(editor);
        return NULL;
    }
    editor->lines[0].text[0] = '\0';
    editor->lines[0].length = 0;
    editor->lines[0].capacity = INITIAL_TEXT_CAPACITY;

    // Cursor and selection
    editor->cursor_line = 0;
    editor->cursor_col = 0;
    editor->has_selection = false;
    memset(&editor->selection, 0, sizeof(editor->selection));

    // Scroll
    editor->scroll_x = 0;
    editor->scroll_y = 0;
    editor->visible_first_line = 0;
    editor->visible_line_count = 0;

    // Font
    editor->font = NULL;
    editor->font_size = theme->typography.size_normal;
    editor->char_width = 8.0f; // Default, updated when font is set
    editor->line_height = 18.0f;

    // Gutter
    editor->show_line_numbers = true;
    editor->gutter_width = 50.0f;
    editor->line_number_width_override = 0.0f;
    editor->gutter_bg = theme->colors.bg_secondary;
    editor->line_number_color = theme->colors.fg_tertiary;

    // Appearance
    editor->bg_color = theme->colors.bg_primary;
    editor->text_color = theme->colors.fg_primary;
    editor->cursor_color = theme->colors.fg_primary;
    editor->selection_color = theme->colors.bg_selected;
    editor->current_line_bg = theme->colors.bg_tertiary;

    // Syntax highlighting
    editor->syntax_highlighter = NULL;
    editor->syntax_data = NULL;

    // Editing options
    editor->read_only = false;
    editor->insert_mode = true;
    editor->tab_width = 4;
    editor->use_spaces = true;
    editor->auto_indent = true;
    editor->word_wrap = false;

    // State
    editor->cursor_visible = true;
    editor->cursor_blink_time = 0;
    editor->modified = false;

    // Create undo/redo history
    editor->history = edit_history_create();
    if (!editor->history) {
        free(editor->lines[0].text);
        free(editor->lines);
        free(editor);
        return NULL;
    }

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &editor->base);
    }

    return editor;
}

static void codeeditor_destroy(vg_widget_t *widget) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;

    for (int i = 0; i < editor->line_count; i++) {
        free_line(&editor->lines[i]);
    }
    free(editor->lines);
    editor->lines = NULL;
    editor->line_count = 0;
    editor->line_capacity = 0;

    // Free undo/redo history
    edit_history_destroy(editor->history);
    editor->history = NULL;

    if (editor->custom_keywords) {
        for (int i = 0; i < editor->custom_keyword_count; i++)
            free(editor->custom_keywords[i]);
        free(editor->custom_keywords);
        editor->custom_keywords = NULL;
        editor->custom_keyword_count = 0;
    }

    free(editor->highlight_spans);
    editor->highlight_spans = NULL;
    editor->highlight_span_count = 0;
    editor->highlight_span_cap = 0;

    if (editor->gutter_icons) {
        for (int i = 0; i < editor->gutter_icon_count; i++)
            vg_icon_destroy(&editor->gutter_icons[i].image);
        free(editor->gutter_icons);
    }
    editor->gutter_icons = NULL;
    editor->gutter_icon_count = 0;
    editor->gutter_icon_cap = 0;

    free(editor->fold_regions);
    editor->fold_regions = NULL;
    editor->fold_region_count = 0;
    editor->fold_region_cap = 0;

    free(editor->extra_cursors);
    editor->extra_cursors = NULL;
    editor->extra_cursor_count = 0;
    editor->extra_cursor_cap = 0;
}

static void codeeditor_measure(vg_widget_t *widget, float available_width, float available_height) {
    // Code editor fills available space
    widget->measured_width = available_width > 0 ? available_width : 400;
    widget->measured_height = available_height > 0 ? available_height : 300;

    // Apply constraints
    if (widget->measured_width < widget->constraints.min_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height) {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void codeeditor_draw_gutter_icon_image(vgfx_window_t canvas,
                                              const struct vg_gutter_icon *icon,
                                              int32_t dst_x,
                                              int32_t dst_y,
                                              int32_t dst_w,
                                              int32_t dst_h) {
    if (!icon || icon->image.type != VG_ICON_IMAGE || !icon->image.data.image.pixels ||
        dst_w <= 0 || dst_h <= 0)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas, &fb))
        return;

    const uint8_t *src_pixels = icon->image.data.image.pixels;
    int src_w = (int)icon->image.data.image.width;
    int src_h = (int)icon->image.data.image.height;
    const struct vgfx_window *internal = (const struct vgfx_window *)canvas;
    int clip_x = 0;
    int clip_y = 0;
    int clip_w = fb.width;
    int clip_h = fb.height;
    if (src_w <= 0 || src_h <= 0)
        return;

    if (internal && internal->clip_enabled) {
        clip_x = internal->clip_x;
        clip_y = internal->clip_y;
        clip_w = internal->clip_w;
        clip_h = internal->clip_h;
    }

    for (int row = 0; row < dst_h; row++) {
        int src_y = row * src_h / dst_h;
        if (src_y >= src_h)
            src_y = src_h - 1;
        int fb_y = dst_y + row;
        if (fb_y < 0 || fb_y >= fb.height || fb_y < clip_y || fb_y >= clip_y + clip_h)
            continue;

        for (int col = 0; col < dst_w; col++) {
            int src_x = col * src_w / dst_w;
            if (src_x >= src_w)
                src_x = src_w - 1;
            int fb_x = dst_x + col;
            if (fb_x < 0 || fb_x >= fb.width || fb_x < clip_x || fb_x >= clip_x + clip_w)
                continue;

            const uint8_t *src = &src_pixels[(src_y * src_w + src_x) * 4];
            uint8_t alpha = src[3];
            if (alpha == 0)
                continue;

            int fb_idx = fb_y * fb.stride + fb_x * 4;
            if (alpha == 255) {
                fb.pixels[fb_idx + 0] = src[0];
                fb.pixels[fb_idx + 1] = src[1];
                fb.pixels[fb_idx + 2] = src[2];
                fb.pixels[fb_idx + 3] = 0xFF;
                continue;
            }

            uint8_t dst_r = fb.pixels[fb_idx + 0];
            uint8_t dst_g = fb.pixels[fb_idx + 1];
            uint8_t dst_b = fb.pixels[fb_idx + 2];
            uint8_t inv_alpha = (uint8_t)(255 - alpha);
            fb.pixels[fb_idx + 0] = (uint8_t)((src[0] * alpha + dst_r * inv_alpha) / 255);
            fb.pixels[fb_idx + 1] = (uint8_t)((src[1] * alpha + dst_g * inv_alpha) / 255);
            fb.pixels[fb_idx + 2] = (uint8_t)((src[2] * alpha + dst_b * inv_alpha) / 255);
            fb.pixels[fb_idx + 3] = 0xFF;
        }
    }
}

static void codeeditor_draw_fold_marker(vg_codeeditor_t *editor,
                                        void *canvas,
                                        float fold_gutter_x,
                                        float fold_gutter_width,
                                        float line_y,
                                        const vg_font_metrics_t *font_metrics,
                                        int line) {
    const struct vg_fold_region *region = codeeditor_fold_region_starting_at(editor, line);
    if (!editor || !region || fold_gutter_width <= 0.0f || !font_metrics)
        return;

    char marker[2] = {region->folded ? '+' : '-', '\0'};
    vg_text_metrics_t marker_metrics = {0};
    vg_font_measure_text(editor->font, editor->font_size, marker, &marker_metrics);
    float marker_x = fold_gutter_x + (fold_gutter_width - marker_metrics.width) * 0.5f;
    float marker_y = line_y + font_metrics->ascent;
    vg_font_draw_text(
        canvas, editor->font, editor->font_size, marker_x, marker_y, marker, editor->line_number_color);
}

static void codeeditor_draw_fold_ellipsis(vg_codeeditor_t *editor,
                                          void *canvas,
                                          float content_x,
                                          float baseline_y,
                                          int line,
                                          int start_col) {
    const struct vg_fold_region *region = codeeditor_fold_region_starting_at(editor, line);
    if (!editor || !region || !region->folded)
        return;

    float ellipsis_x = content_x + (float)start_col * editor->char_width + 4.0f;
    vg_font_draw_text(
        canvas, editor->font, editor->font_size, ellipsis_x, baseline_y, "...", editor->line_number_color);
}

static void codeeditor_paint(vg_widget_t *widget, void *canvas) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;

    if (!editor->font)
        return;

    float content_x = widget->x + editor->gutter_width;
    float content_width = codeeditor_content_draw_width(editor, widget);
    float total_content_height = codeeditor_total_content_height_for_width(editor, content_width);
    float visible_height = widget->height;
    float scrollbar_width = CODEEDITOR_SCROLLBAR_WIDTH;
    float line_number_gutter_width = codeeditor_line_number_gutter_width(editor);
    float fold_gutter_width = codeeditor_fold_gutter_width(editor);
    codeeditor_clamp_scroll(editor, widget);

    int first_visual_row = (int)(editor->scroll_y / editor->line_height);
    codeeditor_locate_visual_row(editor, content_width, first_visual_row, &editor->visible_first_line, NULL);
    editor->visible_line_count = (int)(widget->height / editor->line_height) + 2;

    // Draw background
    vgfx_fill_rect((vgfx_window_t)canvas,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   editor->bg_color);

    // Draw gutter background
    if (editor->gutter_width > 0.0f) {
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)widget->x,
                       (int32_t)widget->y,
                       (int32_t)editor->gutter_width,
                       (int32_t)widget->height,
                       editor->gutter_bg);
    }

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(editor->font, editor->font_size, &font_metrics);
    int32_t clip_x = (int32_t)content_x;
    int32_t clip_y = (int32_t)widget->y;
    int32_t clip_w = (int32_t)content_width;
    int32_t clip_h = (int32_t)widget->height;

    if (clip_w > 0 && clip_h > 0) {
        vgfx_set_clip((vgfx_window_t)canvas, clip_x, clip_y, clip_w, clip_h);
    }

    if (!editor->word_wrap) {
        float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
        float line_y = widget->y - row_offset;
        for (int i = editor->visible_first_line;
             i >= 0 && i < editor->line_count && line_y < widget->y + widget->height;
             i = codeeditor_next_visible_line(editor, i)) {
            if (codeeditor_line_is_hidden(editor, i))
                continue;

            if (i == editor->cursor_line && (widget->state & VG_STATE_FOCUSED)) {
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)content_x,
                               (int32_t)line_y,
                               (int32_t)content_width,
                               (int32_t)editor->line_height,
                               editor->current_line_bg);
            }

            for (int s = 0; s < editor->highlight_span_count; s++) {
                struct vg_highlight_span *span = &editor->highlight_spans[s];
                if (i < span->start_line || i > span->end_line)
                    continue;
                int col_start = (i == span->start_line) ? span->start_col : 0;
                int col_end = (i == span->end_line) ? span->end_col : (int)editor->lines[i].length;
                if (col_end <= col_start)
                    col_end = col_start + 1;
                float span_x = content_x + col_start * editor->char_width - editor->scroll_x;
                float span_w = (col_end - col_start) * editor->char_width;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)span_x,
                               (int32_t)line_y,
                               (int32_t)span_w,
                               (int32_t)editor->line_height,
                               (vgfx_color_t)span->color);
            }

            if (editor->has_selection && (widget->state & VG_STATE_FOCUSED)) {
                int sel_start_line = editor->selection.start_line;
                int sel_start_col = editor->selection.start_col;
                int sel_end_line = editor->selection.end_line;
                int sel_end_col = editor->selection.end_col;
                normalize_selection_range(
                    &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                if (i >= sel_start_line && i <= sel_end_line) {
                    int col_start = (i == sel_start_line) ? sel_start_col : 0;
                    int col_end = (i == sel_end_line) ? sel_end_col : (int)editor->lines[i].length;
                    float sel_x = content_x + col_start * editor->char_width - editor->scroll_x;
                    float sel_width = (col_end - col_start) * editor->char_width;
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)sel_x,
                                   (int32_t)line_y,
                                   (int32_t)sel_width,
                                   (int32_t)editor->line_height,
                                   editor->selection_color);
                }
            }

            if (widget->state & VG_STATE_FOCUSED) {
                for (int c = 0; c < editor->extra_cursor_count; c++) {
                    if (!editor->extra_cursors[c].has_selection)
                        continue;
                    int sel_start_line = editor->extra_cursors[c].selection.start_line;
                    int sel_start_col = editor->extra_cursors[c].selection.start_col;
                    int sel_end_line = editor->extra_cursors[c].selection.end_line;
                    int sel_end_col = editor->extra_cursors[c].selection.end_col;
                    normalize_selection_range(
                        &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                    if (i < sel_start_line || i > sel_end_line)
                        continue;
                    int col_start = (i == sel_start_line) ? sel_start_col : 0;
                    int col_end = (i == sel_end_line) ? sel_end_col : (int)editor->lines[i].length;
                    float sel_x = content_x + col_start * editor->char_width - editor->scroll_x;
                    float sel_width = (col_end - col_start) * editor->char_width;
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)sel_x,
                                   (int32_t)line_y,
                                   (int32_t)sel_width,
                                   (int32_t)editor->line_height,
                                   editor->selection_color);
                }
            }

            if (editor->lines[i].text && editor->lines[i].length > 0) {
                float text_y = line_y + font_metrics.ascent;
                highlight_line(editor, i);
                float text_x = content_x - editor->scroll_x;
                if (editor->lines[i].colors) {
                    codeeditor_draw_colored_slice(canvas,
                                                  editor->font,
                                                  editor->font_size,
                                                  text_x,
                                                  text_y,
                                                  editor->lines[i].text,
                                                  editor->lines[i].colors,
                                                  editor->lines[i].colors_capacity,
                                                  0,
                                                  editor->lines[i].length,
                                                  editor->char_width,
                                                  editor->text_color);
                } else {
                    vg_font_draw_text(canvas,
                                      editor->font,
                                      editor->font_size,
                                      text_x,
                                      text_y,
                                      editor->lines[i].text,
                                      editor->text_color);
                }
                codeeditor_draw_fold_ellipsis(
                    editor, canvas, content_x - editor->scroll_x, text_y, i, (int)editor->lines[i].length);
            }

            line_y += editor->line_height;
        }

        if ((widget->state & VG_STATE_FOCUSED) && editor->cursor_visible && !editor->read_only) {
            int visible_cursor_line =
                codeeditor_visual_row_for_position(editor, content_width, editor->cursor_line, editor->cursor_col);
            float cursor_y = widget->y + (float)visible_cursor_line * editor->line_height - editor->scroll_y;
            if (cursor_y + editor->line_height > widget->y && cursor_y < widget->y + widget->height) {
                float cursor_x =
                    content_x + editor->cursor_col * editor->char_width - editor->scroll_x;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)cursor_x,
                               (int32_t)cursor_y,
                               2,
                               (int32_t)editor->line_height,
                               editor->cursor_color);
            }

            for (int c = 0; c < editor->extra_cursor_count; c++) {
                int visible_extra_line = codeeditor_visual_row_for_position(
                    editor, content_width, editor->extra_cursors[c].line, editor->extra_cursors[c].col);
                float cursor_x = content_x +
                                 editor->extra_cursors[c].col * editor->char_width - editor->scroll_x;
                float cursor_y =
                    widget->y + (float)visible_extra_line * editor->line_height - editor->scroll_y;
                if (cursor_y + editor->line_height <= widget->y || cursor_y >= widget->y + widget->height)
                    continue;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)cursor_x,
                               (int32_t)cursor_y,
                               2,
                               (int32_t)editor->line_height,
                               editor->cursor_color);
            }
        }
    } else {
        int chars_per_row = codeeditor_chars_per_row(editor, content_width);
        int first_visual_row = (int)(editor->scroll_y / editor->line_height);
        float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
        int line = 0;
        int row_in_line = 0;
        codeeditor_locate_visual_row(editor, content_width, first_visual_row, &line, &row_in_line);

        float row_y = widget->y - row_offset;
        while (line < editor->line_count && row_y < widget->y + widget->height) {
            if (line >= 0 && line < editor->line_count) {
                size_t line_len = editor->lines[line].length;
                int row_count = codeeditor_visual_rows_for_line(editor, line, content_width);
                if (row_count == 0) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                    continue;
                }
                int seg_start = chars_per_row > 0 ? row_in_line * chars_per_row : 0;
                size_t remaining = line_len > (size_t)seg_start ? line_len - (size_t)seg_start : 0;
                int seg_len = chars_per_row > 0
                                  ? (int)((remaining < (size_t)chars_per_row) ? remaining
                                                                              : (size_t)chars_per_row)
                                  : (int)line_len;

                if (line == editor->cursor_line && (widget->state & VG_STATE_FOCUSED)) {
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)content_x,
                                   (int32_t)row_y,
                                   (int32_t)content_width,
                                   (int32_t)editor->line_height,
                                   editor->current_line_bg);
                }

                for (int s = 0; s < editor->highlight_span_count; s++) {
                    struct vg_highlight_span *span = &editor->highlight_spans[s];
                    if (line < span->start_line || line > span->end_line)
                        continue;
                    int span_start = (line == span->start_line) ? span->start_col : 0;
                    int span_end = (line == span->end_line) ? span->end_col : (int)line_len;
                    if (span_end <= span_start)
                        span_end = span_start + 1;
                    int overlap_start = span_start > seg_start ? span_start : seg_start;
                    int overlap_end = span_end < seg_start + seg_len ? span_end : seg_start + seg_len;
                    if (overlap_end <= overlap_start)
                        continue;
                    float span_x =
                        content_x + (float)(overlap_start - seg_start) * editor->char_width;
                    float span_w = (float)(overlap_end - overlap_start) * editor->char_width;
                    vgfx_fill_rect((vgfx_window_t)canvas,
                                   (int32_t)span_x,
                                   (int32_t)row_y,
                                   (int32_t)span_w,
                                   (int32_t)editor->line_height,
                                   (vgfx_color_t)span->color);
                }

                if (editor->has_selection && (widget->state & VG_STATE_FOCUSED)) {
                    int sel_start_line = editor->selection.start_line;
                    int sel_start_col = editor->selection.start_col;
                    int sel_end_line = editor->selection.end_line;
                    int sel_end_col = editor->selection.end_col;
                    normalize_selection_range(
                        &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                    if (line >= sel_start_line && line <= sel_end_line) {
                        int sel_start = (line == sel_start_line) ? sel_start_col : 0;
                        int sel_end = (line == sel_end_line) ? sel_end_col : (int)line_len;
                        int overlap_start = sel_start > seg_start ? sel_start : seg_start;
                        int overlap_end = sel_end < seg_start + seg_len ? sel_end : seg_start + seg_len;
                        if (overlap_end > overlap_start) {
                            float sel_x =
                                content_x + (float)(overlap_start - seg_start) * editor->char_width;
                            float sel_w = (float)(overlap_end - overlap_start) * editor->char_width;
                            vgfx_fill_rect((vgfx_window_t)canvas,
                                           (int32_t)sel_x,
                                           (int32_t)row_y,
                                           (int32_t)sel_w,
                                           (int32_t)editor->line_height,
                                           editor->selection_color);
                        }
                    }
                }

                if (widget->state & VG_STATE_FOCUSED) {
                    for (int c = 0; c < editor->extra_cursor_count; c++) {
                        if (!editor->extra_cursors[c].has_selection)
                            continue;
                        int sel_start_line = editor->extra_cursors[c].selection.start_line;
                        int sel_start_col = editor->extra_cursors[c].selection.start_col;
                        int sel_end_line = editor->extra_cursors[c].selection.end_line;
                        int sel_end_col = editor->extra_cursors[c].selection.end_col;
                        normalize_selection_range(
                            &sel_start_line, &sel_start_col, &sel_end_line, &sel_end_col);
                        if (line < sel_start_line || line > sel_end_line)
                            continue;
                        int sel_start = (line == sel_start_line) ? sel_start_col : 0;
                        int sel_end = (line == sel_end_line) ? sel_end_col : (int)line_len;
                        int overlap_start = sel_start > seg_start ? sel_start : seg_start;
                        int overlap_end = sel_end < seg_start + seg_len ? sel_end : seg_start + seg_len;
                        if (overlap_end > overlap_start) {
                            float sel_x =
                                content_x + (float)(overlap_start - seg_start) * editor->char_width;
                            float sel_w = (float)(overlap_end - overlap_start) * editor->char_width;
                            vgfx_fill_rect((vgfx_window_t)canvas,
                                           (int32_t)sel_x,
                                           (int32_t)row_y,
                                           (int32_t)sel_w,
                                           (int32_t)editor->line_height,
                                           editor->selection_color);
                        }
                    }
                }

                highlight_line(editor, line);
                if (editor->lines[line].text && line_len > 0 && seg_len > 0) {
                    float text_y = row_y + font_metrics.ascent;
                    if (editor->lines[line].colors) {
                        codeeditor_draw_colored_slice(canvas,
                                                      editor->font,
                                                      editor->font_size,
                                                      content_x,
                                                      text_y,
                                                      editor->lines[line].text,
                                                      editor->lines[line].colors,
                                                      editor->lines[line].colors_capacity,
                                                      (size_t)seg_start,
                                                      (size_t)seg_len,
                                                      editor->char_width,
                                                      editor->text_color);
                    } else {
                        char stack_buf[256];
                        char *seg_buf = stack_buf;
                        size_t copy_len = (size_t)seg_len;
                        if (copy_len >= sizeof(stack_buf)) {
                            seg_buf = (char *)malloc(copy_len + 1);
                            if (!seg_buf)
                                seg_buf = stack_buf;
                        }
                        if (seg_buf) {
                            if (seg_buf == stack_buf && copy_len >= sizeof(stack_buf)) {
                                copy_len = sizeof(stack_buf) - 1;
                            }
                            memcpy(seg_buf, editor->lines[line].text + seg_start, copy_len);
                            seg_buf[copy_len] = '\0';
                            vg_font_draw_text(canvas,
                                              editor->font,
                                              editor->font_size,
                                              content_x,
                                              text_y,
                                              seg_buf,
                                              editor->text_color);
                            if (seg_buf != stack_buf)
                                free(seg_buf);
                        }
                    }
                }
                if (row_in_line == 0) {
                    codeeditor_draw_fold_ellipsis(
                        editor, canvas, content_x, row_y + font_metrics.ascent, line, seg_len);
                }

                row_in_line++;
                if (row_in_line >= row_count) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                }
            } else {
                line = codeeditor_next_visible_line(editor, line);
                row_in_line = 0;
            }
            row_y += editor->line_height;
        }

        if ((widget->state & VG_STATE_FOCUSED) && editor->cursor_visible && !editor->read_only) {
            int cursor_visual_row =
                codeeditor_visual_row_for_position(editor, content_width, editor->cursor_line, editor->cursor_col);
            float cursor_y = widget->y + (float)cursor_visual_row * editor->line_height - editor->scroll_y;
            int col_in_row = editor->cursor_col;
            codeeditor_visual_offset_for_position(
                editor, content_width, editor->cursor_line, editor->cursor_col, NULL, &col_in_row);
            float cursor_x = content_x + (float)col_in_row * editor->char_width;
            vgfx_fill_rect((vgfx_window_t)canvas,
                           (int32_t)cursor_x,
                           (int32_t)cursor_y,
                           2,
                           (int32_t)editor->line_height,
                           editor->cursor_color);

            for (int c = 0; c < editor->extra_cursor_count; c++) {
                int extra_visual_row = codeeditor_visual_row_for_position(
                    editor, content_width, editor->extra_cursors[c].line, editor->extra_cursors[c].col);
                float extra_y =
                    widget->y + (float)extra_visual_row * editor->line_height - editor->scroll_y;
                int extra_col_in_row = editor->extra_cursors[c].col;
                codeeditor_visual_offset_for_position(editor,
                                                      content_width,
                                                      editor->extra_cursors[c].line,
                                                      editor->extra_cursors[c].col,
                                                      NULL,
                                                      &extra_col_in_row);
                float extra_x = content_x + (float)extra_col_in_row * editor->char_width;
                vgfx_fill_rect((vgfx_window_t)canvas,
                               (int32_t)extra_x,
                               (int32_t)extra_y,
                               2,
                               (int32_t)editor->line_height,
                               editor->cursor_color);
            }
        }
    }

    if (clip_w > 0 && clip_h > 0) {
        vgfx_clear_clip((vgfx_window_t)canvas);
    }

    if (editor->gutter_width > 0.0f) {
        float fold_gutter_x = widget->x + line_number_gutter_width;
        if (!editor->word_wrap) {
            float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
            float line_y = widget->y - row_offset;
            for (int i = editor->visible_first_line;
                 i >= 0 && i < editor->line_count && line_y < widget->y + widget->height;
                 i = codeeditor_next_visible_line(editor, i)) {
                if (codeeditor_line_is_hidden(editor, i))
                    continue;

                if (editor->show_line_numbers) {
                    char line_num[16];
                    snprintf(line_num, sizeof(line_num), "%d", i + 1);
                    vg_text_metrics_t num_metrics = {0};
                    vg_font_measure_text(editor->font, editor->font_size, line_num, &num_metrics);
                    float num_x = widget->x + line_number_gutter_width - num_metrics.width - 8.0f;
                    float num_y = line_y + font_metrics.ascent;
                    vg_font_draw_text(canvas,
                                      editor->font,
                                      editor->font_size,
                                      num_x,
                                      num_y,
                                      line_num,
                                      editor->line_number_color);
                }

                for (int g = 0; g < editor->gutter_icon_count; g++) {
                    struct vg_gutter_icon *icon = &editor->gutter_icons[g];
                    if (icon->line != i)
                        continue;
                    int32_t icon_box = (int32_t)editor->line_height - 4;
                    if (icon_box < 8)
                        icon_box = 8;
                    int32_t icon_x = (int32_t)widget->x + 2;
                    int32_t icon_y = (int32_t)line_y + ((int32_t)editor->line_height - icon_box) / 2;
                    if (icon->image.type == VG_ICON_IMAGE && icon->image.data.image.pixels) {
                        codeeditor_draw_gutter_icon_image(
                            (vgfx_window_t)canvas, icon, icon_x, icon_y, icon_box, icon_box);
                    } else {
                        int32_t icon_r = icon_box / 2;
                        int32_t icon_cx = icon_x + icon_r;
                        int32_t icon_cy = icon_y + icon_r;
                        vgfx_fill_circle((vgfx_window_t)canvas,
                                         icon_cx,
                                         icon_cy,
                                         icon_r,
                                         (vgfx_color_t)icon->color);
                    }
                    break;
                }

                if (editor->show_fold_gutter) {
                    codeeditor_draw_fold_marker(editor,
                                                canvas,
                                                fold_gutter_x,
                                                fold_gutter_width,
                                                line_y,
                                                &font_metrics,
                                                i);
                }

                line_y += editor->line_height;
            }
        } else {
            float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
            int line = 0;
            int row_in_line = 0;
            codeeditor_locate_visual_row(editor, content_width, first_visual_row, &line, &row_in_line);
            float row_y = widget->y - row_offset;
            while (line < editor->line_count && row_y < widget->y + widget->height) {
                int row_count = codeeditor_visual_rows_for_line(editor, line, content_width);
                if (row_count == 0) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                    continue;
                }
                if (row_in_line == 0) {
                    if (editor->show_line_numbers) {
                        char line_num[16];
                        snprintf(line_num, sizeof(line_num), "%d", line + 1);
                        vg_text_metrics_t num_metrics = {0};
                        vg_font_measure_text(editor->font, editor->font_size, line_num, &num_metrics);
                        float num_x = widget->x + line_number_gutter_width - num_metrics.width - 8.0f;
                        float num_y = row_y + font_metrics.ascent;
                        vg_font_draw_text(canvas,
                                          editor->font,
                                          editor->font_size,
                                          num_x,
                                          num_y,
                                          line_num,
                                          editor->line_number_color);
                    }

                    for (int g = 0; g < editor->gutter_icon_count; g++) {
                        struct vg_gutter_icon *icon = &editor->gutter_icons[g];
                        if (icon->line != line)
                            continue;
                        int32_t icon_box = (int32_t)editor->line_height - 4;
                        if (icon_box < 8)
                            icon_box = 8;
                        int32_t icon_x = (int32_t)widget->x + 2;
                        int32_t icon_y = (int32_t)row_y + ((int32_t)editor->line_height - icon_box) / 2;
                        if (icon->image.type == VG_ICON_IMAGE && icon->image.data.image.pixels) {
                            codeeditor_draw_gutter_icon_image(
                                (vgfx_window_t)canvas, icon, icon_x, icon_y, icon_box, icon_box);
                        } else {
                            int32_t icon_r = icon_box / 2;
                            int32_t icon_cx = icon_x + icon_r;
                            int32_t icon_cy = icon_y + icon_r;
                            vgfx_fill_circle((vgfx_window_t)canvas,
                                             icon_cx,
                                             icon_cy,
                                             icon_r,
                                             (vgfx_color_t)icon->color);
                        }
                        break;
                    }

                    if (editor->show_fold_gutter) {
                        codeeditor_draw_fold_marker(editor,
                                                    canvas,
                                                    fold_gutter_x,
                                                    fold_gutter_width,
                                                    row_y,
                                                    &font_metrics,
                                                    line);
                    }
                }

                row_in_line++;
                if (row_in_line >= row_count) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                }
                row_y += editor->line_height;
            }
        }
    }

    if (total_content_height > visible_height) {
        // Scrollbar track background
        float track_x_local = 0.0f;
        float thumb_y_local = 0.0f;
        float thumb_height = 0.0f;
        if (!codeeditor_get_scrollbar_metrics(
                editor, widget, &track_x_local, &thumb_y_local, &thumb_height, NULL, NULL))
            return;
        float track_x = widget->x + track_x_local;
        uint32_t track_color = 0xFF3C3C3C; // Dark gray
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)track_x,
                       (int32_t)widget->y,
                       (int32_t)scrollbar_width,
                       (int32_t)visible_height,
                       track_color);
        float thumb_y = widget->y + thumb_y_local;

        // Scrollbar thumb
        uint32_t thumb_color = 0xFF6C6C6C; // Medium gray
        vgfx_fill_rect((vgfx_window_t)canvas,
                       (int32_t)(track_x + 2),
                       (int32_t)thumb_y,
                       (int32_t)(scrollbar_width - 4),
                       (int32_t)thumb_height,
                       thumb_color);
    }
}

// Encode a Unicode codepoint as UTF-8 into buf (must be at least 4 bytes).
// Returns the number of bytes written, or 0 for invalid codepoints.
static int encode_utf8(uint32_t cp, char *buf) {
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        // Reject UTF-16 surrogates (U+D800–U+DFFF)
        if (cp >= 0xD800 && cp <= 0xDFFF)
            return 0;
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0; // Out-of-range
}

// Insert n raw bytes at the current cursor position and advance cursor_col by n.
VG_UNUSED static void insert_bytes(vg_codeeditor_t *editor, const char *bytes, size_t n) {
    if (!n)
        return;
    vg_code_line_t *line = &editor->lines[editor->cursor_line];

    if (!ensure_text_capacity(line, line->length + n + 1))
        return;

    memmove(line->text + editor->cursor_col + n,
            line->text + editor->cursor_col,
            line->length - editor->cursor_col + 1);

    memcpy(line->text + editor->cursor_col, bytes, n);
    line->length += n;
    editor->cursor_col += n;
    editor->modified = true;
    line->modified = true;
}

VG_UNUSED static void insert_char(vg_codeeditor_t *editor, char c) {
    vg_code_line_t *line = &editor->lines[editor->cursor_line];

    if (!ensure_text_capacity(line, line->length + 2))
        return;

    // Make room for new char
    memmove(line->text + editor->cursor_col + 1,
            line->text + editor->cursor_col,
            line->length - editor->cursor_col + 1);

    line->text[editor->cursor_col] = c;
    line->length++;
    editor->cursor_col++;
    editor->modified = true;
    line->modified = true;
}

VG_UNUSED static void insert_newline(vg_codeeditor_t *editor) {
    if (!ensure_line_capacity(editor, editor->line_count + 1))
        return;

    // Move lines down
    memmove(&editor->lines[editor->cursor_line + 2],
            &editor->lines[editor->cursor_line + 1],
            (editor->line_count - editor->cursor_line - 1) * sizeof(vg_code_line_t));

    // Split current line
    vg_code_line_t *current = &editor->lines[editor->cursor_line];
    vg_code_line_t *next = &editor->lines[editor->cursor_line + 1];

    memset(next, 0, sizeof(vg_code_line_t));
    size_t remaining = current->length - editor->cursor_col;

    if (remaining > 0) {
        next->text = malloc(remaining + 1);
        if (next->text) {
            memcpy(next->text, current->text + editor->cursor_col, remaining);
            next->text[remaining] = '\0';
            next->length = remaining;
            next->capacity = remaining + 1;
        }
    } else {
        next->text = malloc(1);
        if (next->text) {
            next->text[0] = '\0';
            next->length = 0;
            next->capacity = 1;
        }
    }

    current->text[editor->cursor_col] = '\0';
    current->length = editor->cursor_col;

    editor->line_count++;
    editor->cursor_line++;
    editor->cursor_col = 0;
    editor->modified = true;
    next->modified = true;

    vg_codeeditor_refresh_layout_state(editor);
}

VG_UNUSED static void delete_char_backward(vg_codeeditor_t *editor) {
    if (editor->cursor_col > 0) {
        vg_code_line_t *line = &editor->lines[editor->cursor_line];
        memmove(line->text + editor->cursor_col - 1,
                line->text + editor->cursor_col,
                line->length - editor->cursor_col + 1);
        line->length--;
        editor->cursor_col--;
        editor->modified = true;
        line->modified = true;
    } else if (editor->cursor_line > 0) {
        // Join with previous line
        vg_code_line_t *current = &editor->lines[editor->cursor_line];
        vg_code_line_t *prev = &editor->lines[editor->cursor_line - 1];
        int old_line_count = editor->line_count;

        size_t new_col = prev->length;

        if (!ensure_text_capacity(prev, prev->length + current->length + 1))
            return;

        memcpy(prev->text + prev->length, current->text, current->length + 1);
        prev->length += current->length;

        // Free current line
        free_line(current);

        // Move lines up
        memmove(&editor->lines[editor->cursor_line],
                &editor->lines[editor->cursor_line + 1],
                (editor->line_count - editor->cursor_line - 1) * sizeof(vg_code_line_t));

        editor->line_count--;
        if (old_line_count > editor->line_count) {
            memset(&editor->lines[editor->line_count], 0, sizeof(vg_code_line_t));
        }
        editor->cursor_line--;
        editor->cursor_col = (int)new_col;
        editor->modified = true;
        prev->modified = true;

        vg_codeeditor_refresh_layout_state(editor);
    }
}

static void delete_backspace_targets(vg_codeeditor_t *editor) {
    if (!editor)
        return;

    const int max_targets = 1 + editor->extra_cursor_count;
    vg_edit_target_t *targets = calloc((size_t)max_targets, sizeof(vg_edit_target_t));
    if (!targets)
        return;

    int target_count = 0;
    if (editor->has_selection) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->selection.start_line,
                        editor->selection.start_col,
                        editor->selection.end_line,
                        editor->selection.end_col);
    } else if (editor->cursor_col > 0) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        editor->cursor_col - 1,
                        editor->cursor_line,
                        editor->cursor_col);
    } else if (editor->cursor_line > 0) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line - 1,
                        (int)editor->lines[editor->cursor_line - 1].length,
                        editor->cursor_line,
                        0);
    }

    for (int i = 0; i < editor->extra_cursor_count; i++) {
        struct vg_extra_cursor *cursor = &editor->extra_cursors[i];
        if (cursor->has_selection) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->selection.start_line,
                            cursor->selection.start_col,
                            cursor->selection.end_line,
                            cursor->selection.end_col);
        } else if (cursor->col > 0) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            cursor->col - 1,
                            cursor->line,
                            cursor->col);
        } else if (cursor->line > 0) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line - 1,
                            (int)editor->lines[cursor->line - 1].length,
                            cursor->line,
                            0);
        }
    }

    qsort(targets, (size_t)target_count, sizeof(*targets), edit_target_compare_desc);
    apply_edit_targets(editor, targets, target_count, NULL);
    free(targets);
}

static bool codeeditor_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_UP:
            if (editor->scrollbar_dragging) {
                editor->scrollbar_dragging = false;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                widget->needs_paint = true;
                return true;
            }
            break;

        case VG_EVENT_MOUSE_MOVE:
            if (editor->scrollbar_dragging) {
                float thumb_y = 0.0f;
                float thumb_height = 0.0f;
                float max_scroll = 0.0f;
                float thumb_travel = 0.0f;
                if (codeeditor_get_scrollbar_metrics(
                        editor, widget, NULL, &thumb_y, &thumb_height, &max_scroll, &thumb_travel) &&
                    thumb_travel > 0.0f && max_scroll > 0.0f) {
                    float target_thumb_y = event->mouse.y - editor->scrollbar_drag_offset;
                    if (target_thumb_y < 0.0f)
                        target_thumb_y = 0.0f;
                    if (target_thumb_y > thumb_travel)
                        target_thumb_y = thumb_travel;
                    editor->scroll_y = (target_thumb_y / thumb_travel) * max_scroll;
                }
                codeeditor_clamp_scroll(editor, widget);
                widget->needs_paint = true;
                return true;
            }
            break;

        case VG_EVENT_MOUSE_DOWN: {
            float scrollbar_width = CODEEDITOR_SCROLLBAR_WIDTH;

            // Check if click is on scrollbar
            float track_x = 0.0f;
            float thumb_y = 0.0f;
            float thumb_height = 0.0f;
            float max_scroll = 0.0f;
            float thumb_travel = 0.0f;
            if (codeeditor_get_scrollbar_metrics(
                    editor, widget, &track_x, &thumb_y, &thumb_height, &max_scroll, &thumb_travel) &&
                event->mouse.x >= track_x && event->mouse.x < track_x + scrollbar_width &&
                event->mouse.y >= 0.0f && event->mouse.y < widget->height) {
                if (event->mouse.y >= thumb_y && event->mouse.y <= thumb_y + thumb_height) {
                    // Click on thumb — start drag
                    editor->scrollbar_dragging = true;
                    editor->scrollbar_drag_offset = event->mouse.y - thumb_y;
                    editor->scrollbar_drag_start_scroll = editor->scroll_y;
                    vg_widget_set_input_capture(widget);
                } else {
                    // Click on track — jump to position
                    float target_thumb_y = event->mouse.y - thumb_height * 0.5f;
                    if (target_thumb_y < 0.0f)
                        target_thumb_y = 0.0f;
                    if (target_thumb_y > thumb_travel)
                        target_thumb_y = thumb_travel;
                    editor->scroll_y =
                        thumb_travel > 0.0f ? (target_thumb_y / thumb_travel) * max_scroll : 0.0f;
                    codeeditor_clamp_scroll(editor, widget);
                }
                widget->needs_paint = true;
                return true;
            }

            if (editor->gutter_width > 0.0f && event->mouse.x < editor->gutter_width) {
                int line = 0;
                codeeditor_local_point_to_position(editor, widget, event->mouse.x, event->mouse.y, &line, NULL);
                float line_number_gutter_width = codeeditor_line_number_gutter_width(editor);
                float fold_gutter_width = codeeditor_fold_gutter_width(editor);

                if (editor->show_fold_gutter && fold_gutter_width > 0.0f &&
                    event->mouse.x >= line_number_gutter_width) {
                    struct vg_fold_region *region = codeeditor_fold_region_starting_at_mut(editor, line);
                    if (region) {
                        region->folded = !region->folded;
                        vg_codeeditor_refresh_layout_state(editor);
                        return true;
                    }
                }

                int slot = -1;
                for (int i = 0; i < editor->gutter_icon_count; i++) {
                    if (editor->gutter_icons[i].line == line) {
                        slot = editor->gutter_icons[i].type;
                        break;
                    }
                }
                editor->gutter_clicked = true;
                editor->gutter_clicked_line = line;
                editor->gutter_clicked_slot = slot;
                widget->needs_paint = true;
                return true;
            }

            int line = 0;
            int col = 0;
            codeeditor_local_point_to_position(editor, widget, event->mouse.x, event->mouse.y, &line, &col);
            editor->cursor_line = line;
            editor->cursor_col = col;
            editor->has_selection = false;
            clear_extra_cursor_selections(editor);
            editor->cursor_visible = true;
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_DOWN: {
            // Check for modifier key shortcuts first
            uint32_t mods = event->modifiers;
            bool has_ctrl = (mods & VG_MOD_CTRL) != 0 || (mods & VG_MOD_SUPER) != 0; // Ctrl or Cmd

            // Clipboard and editing shortcuts (Ctrl/Cmd + key)
            if (has_ctrl) {
                switch (event->key.key) {
                    case VG_KEY_C: // Copy
                        vg_codeeditor_copy(editor);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_X: // Cut
                        if (!editor->read_only)
                            vg_codeeditor_cut(editor);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_V: // Paste
                        if (!editor->read_only)
                            vg_codeeditor_paste(editor);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_Z: // Undo
                        if (!editor->read_only) {
                            if ((event->modifiers & VG_MOD_SHIFT) != 0)
                                vg_codeeditor_redo(editor);
                            else
                                vg_codeeditor_undo(editor);
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_Y: // Redo
                        if (!editor->read_only) {
                            vg_codeeditor_redo(editor);
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_A: // Select all
                        vg_codeeditor_select_all(editor);
                        widget->needs_paint = true;
                        return true;

                    default:
                        break;
                }
            }

            if (editor->read_only) {
                // Navigation only
                switch (event->key.key) {
                    case VG_KEY_UP:
                        codeeditor_move_cursor_vertical(editor, widget, -1);
                        break;
                    case VG_KEY_DOWN:
                        codeeditor_move_cursor_vertical(editor, widget, 1);
                        break;
                    case VG_KEY_LEFT:
                        if (editor->cursor_col > 0)
                            editor->cursor_col--;
                        break;
                    case VG_KEY_RIGHT:
                        if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length)
                            editor->cursor_col++;
                        break;
                    case VG_KEY_PAGE_UP: {
                        int visible_lines = (int)(widget->height / editor->line_height);
                        if (visible_lines < 1)
                            visible_lines = 1;
                        codeeditor_move_cursor_vertical(editor, widget, -visible_lines);
                        break;
                    }
                    case VG_KEY_PAGE_DOWN: {
                        int visible_lines = (int)(widget->height / editor->line_height);
                        if (visible_lines < 1)
                            visible_lines = 1;
                        codeeditor_move_cursor_vertical(editor, widget, visible_lines);
                        break;
                    }
                    default:
                        break;
                }
                ensure_cursor_visible(editor);
                widget->needs_paint = true;
                return true;
            }

            switch (event->key.key) {
                case VG_KEY_UP:
                    codeeditor_move_cursor_vertical(editor, widget, -1);
                    break;
                case VG_KEY_DOWN:
                    codeeditor_move_cursor_vertical(editor, widget, 1);
                    break;
                case VG_KEY_LEFT:
                    if (editor->cursor_col > 0) {
                        editor->cursor_col--;
                    } else if (editor->cursor_line > 0) {
                        editor->cursor_line--;
                        editor->cursor_col = (int)editor->lines[editor->cursor_line].length;
                    }
                    break;
                case VG_KEY_RIGHT:
                    if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length) {
                        editor->cursor_col++;
                    } else if (editor->cursor_line < editor->line_count - 1) {
                        editor->cursor_line++;
                        editor->cursor_col = 0;
                    }
                    break;
                case VG_KEY_HOME:
                    editor->cursor_col = 0;
                    break;
                case VG_KEY_END:
                    editor->cursor_col = (int)editor->lines[editor->cursor_line].length;
                    break;
                case VG_KEY_PAGE_UP: {
                    int visible_lines = (int)(widget->height / editor->line_height);
                    if (visible_lines < 1)
                        visible_lines = 1;
                    codeeditor_move_cursor_vertical(editor, widget, -visible_lines);
                    break;
                }
                case VG_KEY_PAGE_DOWN: {
                    int visible_lines = (int)(widget->height / editor->line_height);
                    if (visible_lines < 1)
                        visible_lines = 1;
                    codeeditor_move_cursor_vertical(editor, widget, visible_lines);
                    break;
                }
                case VG_KEY_BACKSPACE:
                    delete_backspace_targets(editor);
                    break;
                case VG_KEY_ENTER:
                    vg_codeeditor_insert_text(editor, "\n");
                    break;
                case VG_KEY_TAB:
                    if (editor->use_spaces) {
                        char spaces[32];
                        int space_count = editor->tab_width;
                        if (space_count < 0)
                            space_count = 0;
                        if (space_count >= (int)sizeof(spaces))
                            space_count = (int)sizeof(spaces) - 1;
                        memset(spaces, ' ', (size_t)space_count);
                        spaces[space_count] = '\0';
                        vg_codeeditor_insert_text(editor, spaces);
                    } else {
                        vg_codeeditor_insert_text(editor, "\t");
                    }
                    break;
                default:
                    break;
            }

            // Ensure cursor stays visible after movement
            ensure_cursor_visible(editor);

            editor->cursor_visible = true;
            editor->has_selection = false;
            clear_extra_cursor_selections(editor);
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_CHAR: {
            uint32_t cp = event->key.codepoint;
            // Accept all printable codepoints: U+0020–U+10FFFF excluding surrogates.
            bool printable = (cp >= 0x20 && !(cp >= 0xD800 && cp <= 0xDFFF) && cp <= 0x10FFFF);
            if (!editor->read_only && printable) {
                if (cp < 0x80) {
                    char text[2] = {(char)cp, '\0'};
                    vg_codeeditor_insert_text(editor, text);
                } else {
                    char utf8[5] = {0};
                    int n = encode_utf8(cp, utf8);
                    if (n > 0) {
                        utf8[n] = '\0';
                        vg_codeeditor_insert_text(editor, utf8);
                    }
                }
                ensure_cursor_visible(editor);
                widget->needs_paint = true;
            }
            return true;
        }

        case VG_EVENT_MOUSE_WHEEL:
            editor->scroll_y -= event->wheel.delta_y * editor->line_height * CODEEDITOR_MOUSE_WHEEL_LINES;
            codeeditor_clamp_scroll(editor, widget);
            widget->needs_paint = true;
            return true;

        default:
            break;
    }

    return false;
}

static bool codeeditor_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

static void codeeditor_on_focus(vg_widget_t *widget, bool gained) {
    vg_codeeditor_t *editor = (vg_codeeditor_t *)widget;
    if (gained) {
        editor->cursor_visible = true;
        editor->cursor_blink_time = 0;
        widget->needs_paint = true;
    }
}

//=============================================================================
// CodeEditor API
//=============================================================================

void vg_codeeditor_tick(vg_codeeditor_t *editor, float dt) {
    if (!editor || !(editor->base.state & VG_STATE_FOCUSED))
        return;

    editor->cursor_blink_time += dt;
    if (editor->cursor_blink_time >= CURSOR_BLINK_RATE) {
        editor->cursor_blink_time -= CURSOR_BLINK_RATE;
        editor->cursor_visible = !editor->cursor_visible;
        editor->base.needs_paint = true;
    }
}

void vg_codeeditor_set_text(vg_codeeditor_t *editor, const char *text) {
    if (!editor)
        return;

    // Clear existing lines
    for (int i = 0; i < editor->line_count; i++) {
        free_line(&editor->lines[i]);
    }
    editor->line_count = 0;

    // Compaction paths can leave stale structs past line_count. Clear the full
    // slot array before reusing line entries so old colors/text metadata cannot
    // bleed into a new document load or syntax pass.
    if (editor->lines && editor->line_capacity > 0) {
        memset(editor->lines, 0, (size_t)editor->line_capacity * sizeof(vg_code_line_t));
    }

    if (!text || !text[0]) {
        // Create empty line
        if (!ensure_line_capacity(editor, 1))
            return;
        memset(&editor->lines[0], 0, sizeof(vg_code_line_t));
        editor->lines[0].text = malloc(1);
        if (editor->lines[0].text) {
            editor->lines[0].text[0] = '\0';
            editor->lines[0].length = 0;
            editor->lines[0].capacity = 1;
        }
        editor->line_count = 1;
    } else {
        // Parse text into lines
        const char *start = text;
        while (*start) {
            const char *end = strchr(start, '\n');
            size_t len = end ? (size_t)(end - start) : strlen(start);

            if (!ensure_line_capacity(editor, editor->line_count + 1))
                break;

            vg_code_line_t *line = &editor->lines[editor->line_count];
            memset(line, 0, sizeof(*line));
            line->text = malloc(len + 1);
            if (line->text) {
                memcpy(line->text, start, len);
                line->text[len] = '\0';
                line->length = len;
                line->capacity = len + 1;
            }
            editor->line_count++;

            if (!end)
                break;
            start = end + 1;
        }

        // Ensure at least one line
        if (editor->line_count == 0) {
            if (ensure_line_capacity(editor, 1)) {
                memset(&editor->lines[0], 0, sizeof(vg_code_line_t));
                editor->lines[0].text = malloc(1);
                if (editor->lines[0].text) {
                    editor->lines[0].text[0] = '\0';
                    editor->lines[0].length = 0;
                    editor->lines[0].capacity = 1;
                }
                editor->line_count = 1;
            }
        }
    }

    editor->cursor_line = 0;
    editor->cursor_col = 0;
    editor->has_selection = false;
    clear_extra_cursor_selections(editor);
    editor->extra_cursor_count = 0;
    editor->scroll_x = 0;
    editor->scroll_y = 0;
    editor->modified = false;

    vg_codeeditor_refresh_layout_state(editor);
    editor->base.needs_paint = true;
}

/// @brief Codeeditor get text.
char *vg_codeeditor_get_text(vg_codeeditor_t *editor) {
    if (!editor)
        return NULL;

    // Calculate total size
    size_t total = 0;
    for (int i = 0; i < editor->line_count; i++) {
        total += editor->lines[i].length + 1; // +1 for newline or null
    }

    char *result = malloc(total);
    if (!result)
        return NULL;

    char *p = result;
    for (int i = 0; i < editor->line_count; i++) {
        memcpy(p, editor->lines[i].text, editor->lines[i].length);
        p += editor->lines[i].length;
        if (i < editor->line_count - 1) {
            *p++ = '\n';
        }
    }
    *p = '\0';

    return result;
}

/// @brief Codeeditor get selection.
char *vg_codeeditor_get_selection(vg_codeeditor_t *editor) {
    if (!editor || !editor->has_selection)
        return NULL;

    int start_line, start_col, end_line, end_col;
    normalize_selection(editor, &start_line, &start_col, &end_line, &end_col);
    return copy_text_range(editor, start_line, start_col, end_line, end_col);
}

/// @brief Codeeditor set cursor.
void vg_codeeditor_set_cursor(vg_codeeditor_t *editor, int line, int col) {
    if (!editor)
        return;

    codeeditor_clamp_cursor_to_visible(editor, &line, &col);

    editor->cursor_line = line;
    editor->cursor_col = col;
    editor->has_selection = false;
    clear_extra_cursor_selections(editor);
    editor->base.needs_paint = true;
}

/// @brief Codeeditor get cursor.
void vg_codeeditor_get_cursor(vg_codeeditor_t *editor, int *out_line, int *out_col) {
    if (!editor)
        return;
    if (out_line)
        *out_line = editor->cursor_line;
    if (out_col)
        *out_col = editor->cursor_col;
}

/// @brief Codeeditor set selection.
void vg_codeeditor_set_selection(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    if (!editor)
        return;

    clamp_editor_position(editor, &start_line, &start_col);
    clamp_editor_position(editor, &end_line, &end_col);
    editor->selection.start_line = start_line;
    editor->selection.start_col = start_col;
    editor->selection.end_line = end_line;
    editor->selection.end_col = end_col;
    editor->has_selection = true;
    editor->cursor_line = end_line;
    editor->cursor_col = end_col;
    editor->base.needs_paint = true;
}

/// @brief Codeeditor insetext.
void vg_codeeditor_insert_text(vg_codeeditor_t *editor, const char *text) {
    if (!editor || !text || text[0] == '\0')
        return;

    const int max_targets = 1 + editor->extra_cursor_count;
    vg_edit_target_t *targets = calloc((size_t)max_targets, sizeof(vg_edit_target_t));
    if (!targets)
        return;

    int target_count = 0;
    if (editor->has_selection) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->selection.start_line,
                        editor->selection.start_col,
                        editor->selection.end_line,
                        editor->selection.end_col);
    } else {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        editor->cursor_col);
    }

    for (int i = 0; i < editor->extra_cursor_count; i++) {
        struct vg_extra_cursor *cursor = &editor->extra_cursors[i];
        if (cursor->has_selection) {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->selection.start_line,
                            cursor->selection.start_col,
                            cursor->selection.end_line,
                            cursor->selection.end_col);
        } else {
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            cursor->col);
        }
    }

    qsort(targets, (size_t)target_count, sizeof(*targets), edit_target_compare_desc);
    apply_edit_targets(editor, targets, target_count, text);
    free(targets);
}

/// @brief Codeeditor delete selection.
void vg_codeeditor_delete_selection(vg_codeeditor_t *editor) {
    if (!editor)
        return;

    const int max_targets = 1 + editor->extra_cursor_count;
    vg_edit_target_t *targets = calloc((size_t)max_targets, sizeof(vg_edit_target_t));
    if (!targets)
        return;

    int target_count = 0;
    if (editor->has_selection) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->selection.start_line,
                        editor->selection.start_col,
                        editor->selection.end_line,
                        editor->selection.end_col);
    }

    for (int i = 0; i < editor->extra_cursor_count; i++) {
        struct vg_extra_cursor *cursor = &editor->extra_cursors[i];
        if (!cursor->has_selection)
            continue;
        add_edit_target(targets,
                        &target_count,
                        i + 1,
                        cursor->line,
                        cursor->col,
                        cursor->selection.start_line,
                        cursor->selection.start_col,
                        cursor->selection.end_line,
                        cursor->selection.end_col);
    }

    if (target_count > 0) {
        qsort(targets, (size_t)target_count, sizeof(*targets), edit_target_compare_desc);
        apply_edit_targets(editor, targets, target_count, NULL);
    }
    free(targets);
}

/// @brief Codeeditor scroll to line.
void vg_codeeditor_scroll_to_line(vg_codeeditor_t *editor, int line) {
    if (!editor)
        return;

    if (line < 0)
        line = 0;
    if (line >= editor->line_count)
        line = editor->line_count - 1;
    line = codeeditor_visible_anchor_line(editor, line);

    float content_width = codeeditor_content_draw_width(editor, &editor->base);
    int target_visual_row = codeeditor_visual_row_for_position(editor, content_width, line, 0);
    float target_y = (float)target_visual_row * editor->line_height;
    float visible_height = editor->base.height;

    if (target_y < editor->scroll_y) {
        editor->scroll_y = target_y;
    } else if (target_y + editor->line_height > editor->scroll_y + visible_height) {
        editor->scroll_y = target_y + editor->line_height - visible_height;
    }

    codeeditor_clamp_scroll(editor, &editor->base);
    editor->base.needs_paint = true;
}

/// @brief Codeeditor set syntax.
void vg_codeeditor_set_syntax(vg_codeeditor_t *editor,
                              vg_syntax_callback_t callback,
                              void *user_data) {
    if (!editor)
        return;
    editor->syntax_highlighter = callback;
    editor->syntax_data = user_data;
    // Invalidate cached colors so the new highlighter runs on the next paint
    for (int i = 0; i < editor->line_count; i++) {
        if (editor->lines[i].colors) {
            free(editor->lines[i].colors);
            editor->lines[i].colors = NULL;
            editor->lines[i].colors_capacity = 0;
        }
    }
    editor->base.needs_paint = true;
}

// Internal helper: insert text at position without recording to history
static void insert_text_at_internal(vg_codeeditor_t *editor, int line, int col, const char *text) {
    if (!editor || !text || line < 0 || line >= editor->line_count)
        return;

    // Process character by character
    int cur_line = line;
    int cur_col = col;

    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            // Insert newline
            if (!ensure_line_capacity(editor, editor->line_count + 1))
                return;

            // Move lines down
            memmove(&editor->lines[cur_line + 2],
                    &editor->lines[cur_line + 1],
                    (editor->line_count - cur_line - 1) * sizeof(vg_code_line_t));

            vg_code_line_t *current = &editor->lines[cur_line];
            vg_code_line_t *next = &editor->lines[cur_line + 1];

            memset(next, 0, sizeof(vg_code_line_t));
            size_t remaining = current->length - cur_col;

            if (remaining > 0) {
                next->text = malloc(remaining + 1);
                if (next->text) {
                    memcpy(next->text, current->text + cur_col, remaining);
                    next->text[remaining] = '\0';
                    next->length = remaining;
                    next->capacity = remaining + 1;
                }
            } else {
                next->text = malloc(1);
                if (next->text) {
                    next->text[0] = '\0';
                    next->length = 0;
                    next->capacity = 1;
                }
            }

            current->text[cur_col] = '\0';
            current->length = cur_col;
            editor->line_count++;

            cur_line++;
            cur_col = 0;
        } else if (*p >= 32 || *p == '\t') {
            // Insert character
            vg_code_line_t *ln = &editor->lines[cur_line];
            if (!ensure_text_capacity(ln, ln->length + 2))
                return;

            memmove(ln->text + cur_col + 1, ln->text + cur_col, ln->length - cur_col + 1);
            ln->text[cur_col] = *p;
            ln->length++;
            cur_col++;
        }
    }

    editor->cursor_line = cur_line;
    editor->cursor_col = cur_col;
    editor->modified = true;
    vg_codeeditor_refresh_layout_state(editor);
}

// Internal helper: delete text range without recording to history
static void delete_text_range_internal(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    if (!editor || start_line < 0 || start_line >= editor->line_count)
        return;
    if (end_line < 0 || end_line >= editor->line_count)
        return;

    if (start_line == end_line) {
        // Single line deletion
        vg_code_line_t *line = &editor->lines[start_line];
        if (start_col >= (int)line->length)
            return;
        if (end_col > (int)line->length)
            end_col = (int)line->length;

        memmove(line->text + start_col, line->text + end_col, line->length - end_col + 1);
        line->length -= (end_col - start_col);
    } else {
        // Multi-line deletion
        vg_code_line_t *first = &editor->lines[start_line];
        vg_code_line_t *last = &editor->lines[end_line];
        int old_line_count = editor->line_count;

        size_t new_len = start_col + (last->length - end_col);
        if (!ensure_text_capacity(first, new_len + 1))
            return;

        memcpy(first->text + start_col, last->text + end_col, last->length - end_col + 1);
        first->length = new_len;

        for (int i = start_line + 1; i <= end_line; i++) {
            free_line(&editor->lines[i]);
        }

        int lines_removed = end_line - start_line;
        if (end_line + 1 < editor->line_count) {
            memmove(&editor->lines[start_line + 1],
                    &editor->lines[end_line + 1],
                    (editor->line_count - end_line - 1) * sizeof(vg_code_line_t));
        }
        editor->line_count -= lines_removed;
        if (old_line_count > editor->line_count) {
            memset(&editor->lines[editor->line_count],
                   0,
                   (size_t)(old_line_count - editor->line_count) * sizeof(vg_code_line_t));
        }
        vg_codeeditor_refresh_layout_state(editor);
    }

    editor->cursor_line = start_line;
    editor->cursor_col = start_col;
    editor->modified = true;
}

static void compute_text_end_position(
    int start_line, int start_col, const char *text, int *out_line, int *out_col) {
    int line = start_line;
    int col = start_col;

    if (text) {
        for (const char *p = text; *p; p++) {
            if (*p == '\n') {
                line++;
                col = 0;
            } else if (*p >= 32 || *p == '\t') {
                col++;
            }
        }
    }

    if (out_line)
        *out_line = line;
    if (out_col)
        *out_col = col;
}

/// @brief Codeeditor undo.
void vg_codeeditor_undo(vg_codeeditor_t *editor) {
    if (!editor || !editor->history)
        return;

    vg_edit_op_t *op = edit_history_pop_undo(editor->history);
    if (!op)
        return;

    // Handle grouped operations
    uint32_t group = op->group_id;
    do {
        switch (op->type) {
            case VG_EDIT_INSERT:
                compute_text_end_position(
                    op->start_line, op->start_col, op->new_text, &op->end_line, &op->end_col);
                // Undo insert = delete the inserted text
                delete_text_range_internal(
                    editor, op->start_line, op->start_col, op->end_line, op->end_col);
                break;

            case VG_EDIT_DELETE:
                // Undo delete = re-insert the deleted text
                insert_text_at_internal(editor, op->start_line, op->start_col, op->old_text);
                break;

            case VG_EDIT_REPLACE:
                compute_text_end_position(
                    op->start_line, op->start_col, op->new_text, &op->end_line, &op->end_col);
                // Undo replace = delete new text, insert old text
                delete_text_range_internal(
                    editor, op->start_line, op->start_col, op->end_line, op->end_col);
                insert_text_at_internal(editor, op->start_line, op->start_col, op->old_text);
                break;
        }

        // Restore cursor
        set_cursor_position_by_id(
            editor, op->cursor_id, op->cursor_line_before, op->cursor_col_before);

        // Check for more operations in same group
        if (group != 0) {
            vg_edit_op_t *next_op = edit_history_peek_undo(editor->history);
            if (next_op && next_op->group_id == group) {
                op = edit_history_pop_undo(editor->history);
            } else {
                break;
            }
        } else {
            break;
        }
    } while (op);

    editor->has_selection = false;
    clear_extra_cursor_selections(editor);
    editor->base.needs_paint = true;
}

/// @brief Codeeditor redo.
void vg_codeeditor_redo(vg_codeeditor_t *editor) {
    if (!editor || !editor->history)
        return;

    vg_edit_op_t *op = edit_history_pop_redo(editor->history);
    if (!op)
        return;

    // Handle grouped operations
    uint32_t group = op->group_id;

    // Collect all ops in this group
    do {
        switch (op->type) {
            case VG_EDIT_INSERT:
                // Redo insert = insert the text again
                insert_text_at_internal(editor, op->start_line, op->start_col, op->new_text);
                break;

            case VG_EDIT_DELETE:
                compute_text_end_position(
                    op->start_line, op->start_col, op->old_text, &op->end_line, &op->end_col);
                // Redo delete = delete the text again
                delete_text_range_internal(
                    editor, op->start_line, op->start_col, op->end_line, op->end_col);
                break;

            case VG_EDIT_REPLACE:
                compute_text_end_position(
                    op->start_line, op->start_col, op->old_text, &op->end_line, &op->end_col);
                // Redo replace = delete old text, insert new text
                delete_text_range_internal(
                    editor, op->start_line, op->start_col, op->end_line, op->end_col);
                insert_text_at_internal(editor, op->start_line, op->start_col, op->new_text);
                break;
        }

        // Restore cursor
        set_cursor_position_by_id(
            editor, op->cursor_id, op->cursor_line_after, op->cursor_col_after);

        // Check for more operations in same group
        if (group != 0 && editor->history->current_index < editor->history->count) {
            vg_edit_op_t *next_op = editor->history->operations[editor->history->current_index];
            if (next_op && next_op->group_id == group) {
                op = edit_history_pop_redo(editor->history);
            } else {
                break;
            }
        } else {
            break;
        }
    } while (op);

    editor->has_selection = false;
    clear_extra_cursor_selections(editor);
    editor->base.needs_paint = true;
}

bool vg_codeeditor_copy(vg_codeeditor_t *editor) {
    if (!editor)
        return false;

    const int max_targets = 1 + editor->extra_cursor_count;
    vg_edit_target_t *targets = calloc((size_t)max_targets, sizeof(vg_edit_target_t));
    char **parts = NULL;
    size_t *part_lengths = NULL;
    char *clipboard_text = NULL;
    bool copied = false;

    if (!targets)
        return false;

    int target_count = 0;
    if (editor->has_selection) {
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->selection.start_line,
                        editor->selection.start_col,
                        editor->selection.end_line,
                        editor->selection.end_col);
    }
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        struct vg_extra_cursor *cursor = &editor->extra_cursors[i];
        if (!cursor->has_selection)
            continue;
        add_edit_target(targets,
                        &target_count,
                        i + 1,
                        cursor->line,
                        cursor->col,
                        cursor->selection.start_line,
                        cursor->selection.start_col,
                        cursor->selection.end_line,
                        cursor->selection.end_col);
    }

    if (target_count <= 0)
        goto cleanup;

    qsort(targets, (size_t)target_count, sizeof(*targets), edit_target_compare_asc);

    parts = calloc((size_t)target_count, sizeof(char *));
    part_lengths = calloc((size_t)target_count, sizeof(size_t));
    if (!parts || !part_lengths)
        goto cleanup;

    size_t total_len = 0;
    for (int i = 0; i < target_count; i++) {
        parts[i] = copy_text_range(editor,
                                   targets[i].start_line,
                                   targets[i].start_col,
                                   targets[i].end_line,
                                   targets[i].end_col);
        if (!parts[i])
            goto cleanup;
        part_lengths[i] = strlen(parts[i]);
        total_len += part_lengths[i];
        if (i + 1 < target_count)
            total_len++;
    }

    clipboard_text = malloc(total_len + 1);
    if (!clipboard_text)
        goto cleanup;

    char *dst = clipboard_text;
    for (int i = 0; i < target_count; i++) {
        if (part_lengths[i] > 0) {
            memcpy(dst, parts[i], part_lengths[i]);
            dst += part_lengths[i];
        }
        if (i + 1 < target_count)
            *dst++ = '\n';
    }
    *dst = '\0';

    vgfx_clipboard_set_text(clipboard_text);
    copied = true;

cleanup:
    if (parts) {
        for (int i = 0; i < target_count; i++)
            free(parts[i]);
    }
    free(clipboard_text);
    free(part_lengths);
    free(parts);
    free(targets);
    return copied;
}

bool vg_codeeditor_cut(vg_codeeditor_t *editor) {
    if (!editor || editor->read_only)
        return false;

    if (!vg_codeeditor_copy(editor))
        return false;

    vg_codeeditor_delete_selection(editor);
    editor->base.needs_paint = true;
    return true;
}

bool vg_codeeditor_paste(vg_codeeditor_t *editor) {
    if (!editor || editor->read_only)
        return false;

    char *text = vgfx_clipboard_get_text();
    if (text) {
        vg_codeeditor_insert_text(editor, text);
        free(text);
        editor->base.needs_paint = true;
        return true;
    }
    return false;
}

/// @brief Codeeditor select all.
void vg_codeeditor_select_all(vg_codeeditor_t *editor) {
    if (!editor || editor->line_count == 0)
        return;

    editor->selection.start_line = 0;
    editor->selection.start_col = 0;
    editor->selection.end_line = editor->line_count - 1;
    editor->selection.end_col = editor->lines[editor->line_count - 1].length;
    editor->has_selection = true;
    editor->cursor_line = editor->selection.end_line;
    editor->cursor_col = editor->selection.end_col;
    clear_extra_cursor_selections(editor);
    editor->base.needs_paint = true;
}

/// @brief Codeeditor set font.
void vg_codeeditor_set_font(vg_codeeditor_t *editor, vg_font_t *font, float size) {
    if (!editor)
        return;

    editor->font = font;
    editor->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;

    if (font) {
        // Calculate char width (assuming monospace)
        vg_text_metrics_t metrics;
        vg_font_measure_text(font, editor->font_size, "M", &metrics);
        editor->char_width = metrics.width;

        vg_font_metrics_t font_metrics;
        vg_font_get_metrics(font, editor->font_size, &font_metrics);
        editor->line_height = font_metrics.line_height;
    }

    vg_codeeditor_refresh_layout_state(editor);
}

/// @brief Codeeditor get line count.
int vg_codeeditor_get_line_count(vg_codeeditor_t *editor) {
    return editor ? editor->line_count : 0;
}

bool vg_codeeditor_is_modified(vg_codeeditor_t *editor) {
    return editor ? editor->modified : false;
}

/// @brief Codeeditor clear modified.
void vg_codeeditor_clear_modified(vg_codeeditor_t *editor) {
    if (editor) {
        editor->modified = false;
        for (int i = 0; i < editor->line_count; i++) {
            editor->lines[i].modified = false;
        }
    }
}
