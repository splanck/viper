//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_outputpane.c
// Purpose: Output pane widget — an append-only, ANSI-aware terminal-style log
//          view with styled segments, selection, and auto-scroll behaviour.
// Key invariants:
//   - lines[] is a flat ring buffer capped at max_lines; when full, the oldest
//     entry is evicted via memmove and selection coordinates are adjusted.
//   - Each vg_output_line_t holds a dynamic array of vg_styled_segment_t, each
//     owning its text via strdup/malloc; freed in free_output_line.
//   - ANSI escape state (in_escape, escape_buf, current_fg/bg, ansi_bold) is
//     preserved across append calls so multi-chunk writes render correctly.
//   - scroll_locked is set when the user scrolls up; auto_scroll is suppressed
//     until scroll_locked is cleared by vg_outputpane_scroll_to_bottom.
//   - sel_start/end coordinates are absolute line indices into lines[]; they are
//     decremented by outputpane_note_evicted_first_line on each eviction.
// Ownership/Lifetime:
//   - pane->lines and every segment text pointer are heap-allocated and freed in
//     outputpane_destroy / vg_outputpane_clear.
//   - The widget does not own the linked vg_font_t.
// Links: lib/gui/include/vg_ide_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void outputpane_destroy(vg_widget_t *widget);
static void outputpane_measure(vg_widget_t *widget, float available_width, float available_height);
static void outputpane_paint(vg_widget_t *widget, void *canvas);
static bool outputpane_handle_event(vg_widget_t *widget, vg_event_t *event);
static void outputpane_set_font_widget(vg_widget_t *widget, void *font, float size);
static bool outputpane_can_focus(vg_widget_t *widget);
static void outputpane_on_focus(vg_widget_t *widget, bool gained);

//=============================================================================
// OutputPane VTable
//=============================================================================

static vg_widget_vtable_t g_outputpane_vtable = {.destroy = outputpane_destroy,
                                                 .measure = outputpane_measure,
                                                 .arrange = NULL,
                                                 .paint = outputpane_paint,
                                                 .handle_event = outputpane_handle_event,
                                                 .can_focus = outputpane_can_focus,
                                                 .on_focus = outputpane_on_focus,
                                                 .set_font = outputpane_set_font_widget};

//=============================================================================
// ANSI Color Tables
//=============================================================================

static const uint32_t g_ansi_colors[] = {
    0xFF000000, // Black
    0xFFCC0000, // Red
    0xFF00CC00, // Green
    0xFFCCCC00, // Yellow
    0xFF0000CC, // Blue
    0xFFCC00CC, // Magenta
    0xFF00CCCC, // Cyan
    0xFFCCCCCC, // White
};

static const uint32_t g_ansi_bright_colors[] = {
    0xFF666666, // Bright Black
    0xFFFF6666, // Bright Red
    0xFF66FF66, // Bright Green
    0xFFFFFF66, // Bright Yellow
    0xFF6666FF, // Bright Blue
    0xFFFF66FF, // Bright Magenta
    0xFF66FFFF, // Bright Cyan
    0xFFFFFFFF, // Bright White
};

/// @brief Map an ANSI SGR color code to a packed AARRGGBB value.
static uint32_t ansi_code_to_color(int code) {
    if (code >= 30 && code <= 37) {
        return g_ansi_colors[code - 30];
    } else if (code >= 90 && code <= 97) {
        return g_ansi_bright_colors[code - 90];
    }
    return 0xFFCCCCCC; // Default
}

/// @brief VTable set_font trampoline — forwards to vg_outputpane_set_font.
static void outputpane_set_font_widget(vg_widget_t *widget, void *font, float size) {
    if (!widget || !font)
        return;
    vg_outputpane_set_font((vg_outputpane_t *)widget, (vg_font_t *)font, size);
}

/// @brief Return the total character count across all segments of a line.
/// @details Counts UTF-8 codepoint starts rather than raw bytes so selection
///          columns cannot split a multibyte sequence.
/// @param text UTF-8 text; may be NULL.
/// @return Number of codepoint columns in the string.
static size_t outputpane_utf8_column_count(const char *text) {
    size_t count = 0;
    if (!text)
        return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if ((*p & 0xC0u) != 0x80u)
            count++;
    }
    return count;
}

/// @brief Return the byte length of the UTF-8 sequence starting at text.
/// @details Invalid or truncated sequences are treated as one byte so selection
///          extraction always makes progress and never reads past the NUL
///          terminator.
/// @param text Pointer to a NUL-terminated UTF-8 sequence.
/// @return Number of bytes in the next codepoint-like unit.
static size_t outputpane_utf8_next_len(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    if (!p || p[0] == '\0')
        return 0;
    if (p[0] < 0x80u)
        return 1;
    size_t expected = 1;
    if ((p[0] & 0xE0u) == 0xC0u)
        expected = 2;
    else if ((p[0] & 0xF0u) == 0xE0u)
        expected = 3;
    else if ((p[0] & 0xF8u) == 0xF0u)
        expected = 4;
    for (size_t i = 1; i < expected; i++) {
        if (p[i] == '\0')
            return 1;
        if ((p[i] & 0xC0u) != 0x80u)
            return 1;
    }
    return expected;
}

/// @brief Map a UTF-8 codepoint column to a byte offset.
/// @param text UTF-8 text to inspect.
/// @param target_col Codepoint column to locate.
/// @return Byte offset into @p text at the requested column or string end.
static size_t outputpane_utf8_byte_offset_for_column(const char *text, size_t target_col) {
    size_t col = 0;
    size_t offset = 0;
    if (!text)
        return 0;
    while (text[offset] && col < target_col) {
        size_t cp_len = outputpane_utf8_next_len(text + offset);
        if (cp_len == 0)
            break;
        offset += cp_len;
        col++;
    }
    return offset;
}

/// @brief Return the total character count across all segments of a line.
static size_t outputpane_line_length(const vg_output_line_t *line) {
    size_t len = 0;
    if (!line)
        return 0;
    for (size_t i = 0; i < line->segment_count; i++) {
        if (line->segments[i].text) {
            size_t seg_cols = outputpane_utf8_column_count(line->segments[i].text);
            if (seg_cols > SIZE_MAX - len)
                return SIZE_MAX;
            len += seg_cols;
        }
    }
    return len;
}

/// @brief Measure the pixel width of the text in a line up to target_col characters.
static float outputpane_prefix_width(vg_outputpane_t *pane,
                                     const vg_output_line_t *line,
                                     uint32_t target_col) {
    float width = 0.0f;
    size_t col = 0;

    if (!pane || !pane->font || !line)
        return 0.0f;

    for (size_t i = 0; i < line->segment_count; i++) {
        const char *text = line->segments[i].text;
        size_t seg_len = 0;
        vg_text_metrics_t metrics = {0};

        if (!text)
            continue;
        seg_len = outputpane_utf8_column_count(text);
        if ((size_t)target_col <= col)
            return width;
        size_t remaining_cols = (size_t)target_col - col;
        if (seg_len <= remaining_cols) {
            vg_font_measure_text(pane->font, pane->font_size, text, &metrics);
            width += metrics.width;
            col += seg_len;
            continue;
        }

        if (target_col > col) {
            size_t partial_len =
                outputpane_utf8_byte_offset_for_column(text, (size_t)(target_col - col));
            char *partial = malloc(partial_len + 1);
            if (partial) {
                memcpy(partial, text, partial_len);
                partial[partial_len] = '\0';
                vg_font_measure_text(pane->font, pane->font_size, partial, &metrics);
                width += metrics.width;
                free(partial);
            }
        }
        return width;
    }

    return width;
}

//=============================================================================
// Output Line Management
//=============================================================================

/// @brief Free all segment text buffers and the segments array owned by line.
static void free_output_line(vg_output_line_t *line) {
    if (!line)
        return;

    for (size_t i = 0; i < line->segment_count; i++) {
        free(line->segments[i].text);
    }
    free(line->segments);
}

/// @brief Reset all selection state fields to the unselected state.
static void outputpane_clear_selection(vg_outputpane_t *pane) {
    if (!pane)
        return;
    pane->has_selection = false;
    pane->sel_start_line = 0;
    pane->sel_start_col = 0;
    pane->sel_end_line = 0;
    pane->sel_end_col = 0;
}

/// @brief Adjust or clear selection coordinates after the first line is evicted.
static void outputpane_note_evicted_first_line(vg_outputpane_t *pane) {
    if (!pane || !pane->has_selection)
        return;
    if (pane->sel_start_line == 0 || pane->sel_end_line == 0) {
        outputpane_clear_selection(pane);
        return;
    }
    pane->sel_start_line--;
    pane->sel_end_line--;
}

/// @brief Append and return a zeroed segment slot to line, growing the array if needed.
static vg_styled_segment_t *add_segment(vg_output_line_t *line) {
    if (line->segment_count >= line->segment_capacity) {
        if (line->segment_capacity > SIZE_MAX / 2u)
            return NULL;
        size_t new_cap = line->segment_capacity * 2;
        if (new_cap < 4)
            new_cap = 4;
        if (new_cap > SIZE_MAX / sizeof(vg_styled_segment_t))
            return NULL;
        vg_styled_segment_t *new_segs =
            realloc(line->segments, new_cap * sizeof(vg_styled_segment_t));
        if (!new_segs)
            return NULL;
        line->segments = new_segs;
        line->segment_capacity = new_cap;
    }

    vg_styled_segment_t *seg = &line->segments[line->segment_count++];
    memset(seg, 0, sizeof(vg_styled_segment_t));
    return seg;
}

/// @brief Append a copied styled text segment atomically.
/// @details Allocates the segment text before reserving the segment slot.  If
///          either allocation fails, the line is left unchanged and no empty
///          segment is committed.
/// @param line Destination output line.
/// @param text Source text bytes to copy.
/// @param len Number of bytes to copy from @p text.
/// @param fg Foreground color for the new segment.
/// @param bg Background color for the new segment.
/// @param bold Whether the segment should be rendered bold.
/// @return true when the segment was appended; false on invalid input or OOM.
static bool outputpane_append_segment_copy(vg_output_line_t *line,
                                           const char *text,
                                           size_t len,
                                           uint32_t fg,
                                           uint32_t bg,
                                           bool bold) {
    if (!line || !text || len == 0 || len == SIZE_MAX)
        return false;
    char *copy = malloc(len + 1u);
    if (!copy)
        return false;
    memcpy(copy, text, len);
    copy[len] = '\0';

    vg_styled_segment_t *seg = add_segment(line);
    if (!seg) {
        free(copy);
        return false;
    }
    seg->text = copy;
    seg->fg_color = fg;
    seg->bg_color = bg;
    seg->bold = bold;
    return true;
}

/// @brief Append a new empty line to pane, evicting the oldest if the ring buffer is full.
static vg_output_line_t *add_line(vg_outputpane_t *pane) {
    if (!pane || pane->max_lines == 0)
        return NULL;

    // Check if we need to wrap around (ring buffer)
    if (pane->line_count >= pane->max_lines) {
        // Free oldest line
        free_output_line(&pane->lines[0]);
        outputpane_note_evicted_first_line(pane);
        // Shift all lines down
        memmove(
            &pane->lines[0], &pane->lines[1], (pane->line_count - 1) * sizeof(vg_output_line_t));
        pane->line_count--;
    }

    // Expand capacity if needed
    if (pane->line_count >= pane->line_capacity) {
        if (pane->line_capacity > SIZE_MAX / 2u)
            return NULL;
        size_t new_cap = pane->line_capacity * 2;
        if (new_cap < 64)
            new_cap = 64;
        if (new_cap > pane->max_lines)
            new_cap = pane->max_lines;
        if (new_cap == 0 || new_cap > SIZE_MAX / sizeof(vg_output_line_t))
            return NULL;
        vg_output_line_t *new_lines = realloc(pane->lines, new_cap * sizeof(vg_output_line_t));
        if (!new_lines)
            return NULL;
        pane->lines = new_lines;
        pane->line_capacity = new_cap;
    }

    vg_output_line_t *line = &pane->lines[pane->line_count++];
    memset(line, 0, sizeof(vg_output_line_t));
    return line;
}

//=============================================================================
// ANSI Parser
//=============================================================================

/// @brief Apply the buffered ANSI SGR escape sequence to pane's current color/bold state.
static void process_ansi_escape(vg_outputpane_t *pane) {
    // Parse escape sequence: ESC[<params>m
    // Common codes:
    // 0 = reset, 1 = bold, 30-37 = fg color, 40-47 = bg color
    // 90-97 = bright fg, 100-107 = bright bg

    char *buf = pane->escape_buf;
    if (buf[0] != '[') {
        pane->escape_len = 0;
        pane->in_escape = false;
        return;
    }

    // Parse parameters
    int params[16];
    int param_count = 0;
    char *p = buf + 1;

    while (*p && param_count < 16) {
        if (*p >= '0' && *p <= '9') {
            params[param_count] = (int)strtol(p, &p, 10);
            param_count++;
            if (*p == ';')
                p++;
        } else if (*p == 'm') {
            break;
        } else {
            p++;
        }
    }

    // Apply parameters
    for (int i = 0; i < param_count; i++) {
        int code = params[i];
        if (code == 0) {
            // Reset
            pane->current_fg = pane->default_fg;
            pane->current_bg = 0;
            pane->ansi_bold = false;
        } else if (code == 1) {
            pane->ansi_bold = true;
        } else if (code >= 30 && code <= 37) {
            pane->current_fg = ansi_code_to_color(code);
        } else if (code >= 40 && code <= 47) {
            pane->current_bg = ansi_code_to_color(code - 10);
        } else if (code >= 90 && code <= 97) {
            pane->current_fg = ansi_code_to_color(code);
        } else if (code >= 100 && code <= 107) {
            pane->current_bg = ansi_code_to_color(code - 10);
        }
    }

    pane->escape_len = 0;
    pane->in_escape = false;
}

//=============================================================================
// OutputPane Implementation
//=============================================================================

/// @brief Create an output pane widget with default colours and a 10 000-line ring buffer.
///
/// @details Default background is dark grey (0xFF1E1E1E) and foreground is light grey
///          (0xFFCCCCCC).  auto_scroll is enabled.  Attach a font with
///          vg_outputpane_set_font before appending any text.
///
/// @return Newly allocated vg_outputpane_t, or NULL on allocation failure.
vg_outputpane_t *vg_outputpane_create(void) {
    vg_outputpane_t *pane = calloc(1, sizeof(vg_outputpane_t));
    if (!pane)
        return NULL;

    vg_widget_init(&pane->base, VG_WIDGET_OUTPUTPANE, &g_outputpane_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    pane->max_lines = 10000;
    pane->auto_scroll = true;
    pane->line_height = 16;
    pane->font_size = theme->typography.size_normal;

    pane->bg_color = 0xFF1E1E1E;
    pane->default_fg = 0xFFCCCCCC;
    pane->current_fg = pane->default_fg;
    pane->caret_visible = true;

    return pane;
}

/// @brief Widget-vtable destroy hook: free every output line (and its
///        segment buffers) and the line array.
static void outputpane_destroy(vg_widget_t *widget) {
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;

    for (size_t i = 0; i < pane->line_count; i++) {
        free_output_line(&pane->lines[i]);
    }
    free(pane->lines);
    free(pane->cells);
    free(pane->pending_input);
}

/// @brief Destroy the output pane widget, freeing all lines and segment text buffers.
///
/// @param pane The output pane to destroy; may be NULL.
void vg_outputpane_destroy(vg_outputpane_t *pane) {
    if (!pane)
        return;
    vg_widget_destroy(&pane->base);
}

/// @brief Widget-vtable measure hook: the output pane fills the available
///        space (no intrinsic size).
static void outputpane_measure(vg_widget_t *widget, float available_width, float available_height) {
    (void)available_width;
    (void)available_height;

    // OutputPane typically fills available space
    widget->measured_width = available_width;
    widget->measured_height = available_height;
}

/// @brief Widget-vtable paint hook: draw the visible lines with per-segment
///        colors, the selection highlight, and the border.
static void outputpane_paint(vg_widget_t *widget, void *canvas) {
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;
    uint32_t selection_color =
        theme ? vg_color_blend(theme->colors.bg_selected, theme->colors.accent_primary, 0.18f)
              : 0x334C73u;
    uint32_t border_color = theme ? theme->colors.border_primary : 0x333333u;
    vg_font_metrics_t font_metrics = {0};
    uint32_t start_line = 0;
    uint32_t end_line = 0;
    uint32_t start_col = 0;
    uint32_t end_col = 0;

    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   pane->bg_color);
    vgfx_rect(win,
              (int32_t)widget->x,
              (int32_t)widget->y,
              (int32_t)widget->width,
              (int32_t)widget->height,
              border_color);

    if (!pane->font)
        return;

    vg_font_get_metrics(pane->font, pane->font_size, &font_metrics);

    // Calculate visible lines
    int first_visible = (int)(pane->scroll_y / pane->line_height);
    int visible_count = (int)(widget->height / pane->line_height) + 1;

    float y = widget->y - fmodf(pane->scroll_y, pane->line_height);
    vgfx_set_clip(win,
                  (int32_t)widget->x,
                  (int32_t)widget->y,
                  (int32_t)widget->width,
                  (int32_t)widget->height);

    if (pane->has_selection) {
        start_line = pane->sel_start_line;
        start_col = pane->sel_start_col;
        end_line = pane->sel_end_line;
        end_col = pane->sel_end_col;
        if (start_line > end_line || (start_line == end_line && start_col > end_col)) {
            uint32_t tmp = start_line;
            start_line = end_line;
            end_line = tmp;
            tmp = start_col;
            start_col = end_col;
            end_col = tmp;
        }
    }

    for (int i = 0; i < visible_count && first_visible + i < (int)pane->line_count; i++) {
        int line_idx = first_visible + i;
        if (line_idx < 0)
            continue;

        vg_output_line_t *line = &pane->lines[line_idx];
        float x = widget->x + 4; // Left padding
        float line_top = y;

        if (pane->has_selection && (uint32_t)line_idx >= start_line &&
            (uint32_t)line_idx <= end_line) {
            uint32_t line_start_col = (uint32_t)line_idx == start_line ? start_col : 0;
            uint32_t line_end_col = (uint32_t)line_idx == end_line && end_col != UINT32_MAX
                                        ? end_col
                                        : (uint32_t)outputpane_line_length(line);
            float sel_x0 = widget->x + 4.0f + outputpane_prefix_width(pane, line, line_start_col);
            float sel_x1 = widget->x + 4.0f + outputpane_prefix_width(pane, line, line_end_col);
            if (sel_x1 < sel_x0) {
                float tmp = sel_x0;
                sel_x0 = sel_x1;
                sel_x1 = tmp;
            }
            if (sel_x1 > sel_x0) {
                vgfx_fill_rect(win,
                               (int32_t)sel_x0,
                               (int32_t)line_top,
                               (int32_t)(sel_x1 - sel_x0),
                               (int32_t)pane->line_height,
                               selection_color);
            }
        }

        for (size_t s = 0; s < line->segment_count; s++) {
            vg_styled_segment_t *seg = &line->segments[s];
            vg_text_metrics_t metrics = {0};
            if (!seg->text)
                continue;

            vg_font_measure_text(pane->font, pane->font_size, seg->text, &metrics);

            // Draw segment background if any
            if (seg->bg_color != 0) {
                vgfx_fill_rect(win,
                               (int32_t)x,
                               (int32_t)line_top,
                               (int32_t)metrics.width,
                               (int32_t)pane->line_height,
                               seg->bg_color);
            }

            // Draw text
            vg_font_draw_text(canvas,
                              pane->font,
                              pane->font_size,
                              x,
                              line_top + font_metrics.ascent,
                              seg->text,
                              seg->fg_color);

            // Advance X position
            x += metrics.width;
        }

        // Terminal caret: a blinking block at the cursor column on the current
        // line. The glyph under it is redrawn in the background color (inverse
        // video) so it stays readable when editing mid-line.
        if (pane->terminal_mode && pane->has_focus && pane->caret_visible &&
            line_idx == (int)pane->line_count - 1) {
            float caret_x = widget->x + 4.0f + outputpane_prefix_width(pane, line, pane->cursor_col);
            vg_text_metrics_t cw = {0};
            vg_font_measure_text(pane->font, pane->font_size, "M", &cw);
            float caret_w = cw.width > 0.0f ? cw.width : pane->font_size * 0.6f;
            vgfx_fill_rect(win,
                           (int32_t)caret_x,
                           (int32_t)line_top,
                           (int32_t)caret_w,
                           (int32_t)pane->line_height,
                           pane->default_fg);
            if (pane->cursor_col < pane->cell_count) {
                const char *glyph = pane->cells[pane->cursor_col].utf8;
                if (glyph[0] != '\0')
                    vg_font_draw_text(canvas,
                                      pane->font,
                                      pane->font_size,
                                      caret_x,
                                      line_top + font_metrics.ascent,
                                      glyph,
                                      pane->bg_color);
            }
        }

        y += pane->line_height;
    }
    vgfx_clear_clip(win);
}

// Defined later in the Interactive Terminal Mode section; declared here for the
// event hook below.
static void term_queue_input(vg_outputpane_t *pane, const char *bytes, size_t len);
static int outputpane_encode_utf8(uint32_t cp, char *out);

/// @brief Widget-vtable can_focus: focusable only in interactive terminal mode.
static bool outputpane_can_focus(vg_widget_t *widget) {
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;
    return pane->terminal_mode && widget->enabled && widget->visible;
}

/// @brief Widget-vtable on_focus: track focus so the terminal caret can render.
static void outputpane_on_focus(vg_widget_t *widget, bool gained) {
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;
    pane->has_focus = gained;
    if (gained) {
        pane->caret_visible = true;
        pane->caret_blink_time = 0.0f;
    }
    widget->needs_paint = true;
}

/// @brief Widget-vtable event hook: terminal keyboard input + focus (terminal mode),
///        scrolling, and click/drag text selection. Returns true if consumed.
static bool outputpane_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_outputpane_t *pane = (vg_outputpane_t *)widget;

    if (pane->terminal_mode) {
        if (event->type == VG_EVENT_MOUSE_DOWN) {
            vg_widget_set_focus(widget);
            return true;
        }
        if (event->type == VG_EVENT_KEY_DOWN) {
            vg_key_t k = event->key.key;
            uint32_t mods = event->modifiers;
            // Ctrl + letter -> control byte (Ctrl-C = 0x03, Ctrl-D = 0x04, ...).
            if ((mods & VG_MOD_CTRL) && ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z'))) {
                char up = (char)((k >= 'a' && k <= 'z') ? k - 32 : k);
                char ctl = (char)(up - 'A' + 1);
                term_queue_input(pane, &ctl, 1);
                return true;
            }
            switch (k) {
                case VG_KEY_ENTER: term_queue_input(pane, "\r", 1); return true;
                case VG_KEY_BACKSPACE: term_queue_input(pane, "\x7f", 1); return true;
                case VG_KEY_TAB: term_queue_input(pane, "\t", 1); return true;
                case VG_KEY_ESCAPE: term_queue_input(pane, "\x1b", 1); return true;
                case VG_KEY_UP: term_queue_input(pane, "\x1b[A", 3); return true;
                case VG_KEY_DOWN: term_queue_input(pane, "\x1b[B", 3); return true;
                case VG_KEY_RIGHT: term_queue_input(pane, "\x1b[C", 3); return true;
                case VG_KEY_LEFT: term_queue_input(pane, "\x1b[D", 3); return true;
                case VG_KEY_HOME: term_queue_input(pane, "\x1b[H", 3); return true;
                case VG_KEY_END: term_queue_input(pane, "\x1b[F", 3); return true;
                case VG_KEY_DELETE: term_queue_input(pane, "\x1b[3~", 4); return true;
                default: break;
            }
            return false; // printable input arrives via VG_EVENT_KEY_CHAR
        }
        if (event->type == VG_EVENT_KEY_CHAR) {
            uint32_t cp = event->key.codepoint;
            if (cp >= 0x20 && cp != 0x7f) {
                char u[4];
                int l = outputpane_encode_utf8(cp, u);
                term_queue_input(pane, u, (size_t)l);
            }
            return true;
        }
    }

    if (event->type == VG_EVENT_MOUSE_WHEEL) {
        float delta = event->wheel.delta_y * 30; // 30 pixels per scroll unit
        pane->scroll_y -= delta;

        // Clamp scroll
        float max_scroll = pane->line_count * pane->line_height - widget->height;
        if (max_scroll < 0)
            max_scroll = 0;
        if (pane->scroll_y < 0)
            pane->scroll_y = 0;
        if (pane->scroll_y > max_scroll)
            pane->scroll_y = max_scroll;

        // Lock auto-scroll if user scrolled up
        pane->scroll_locked = pane->scroll_y < max_scroll - pane->line_height;

        pane->base.needs_paint = true;
        return true;
    }

    return false;
}

//=============================================================================
// Interactive Terminal Mode
//=============================================================================
// A cursor-column overwrite model layered over the same line/segment storage.
// Only active when pane->terminal_mode is set (the integrated terminal); the
// build-output pane keeps the simple append-only path below. The current line is
// held as a cell array (cells[]) so \r/\b/ESC[K/cursor-moves render correctly;
// the cells are coalesced back into styled segments after each chunk.

/// @brief UTF-8 sequence length implied by a lead byte (1..4; 1 for invalid/continuation).
static int outputpane_utf8_len(unsigned char c) {
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1;
}

/// @brief Encode a Unicode codepoint to UTF-8; returns the byte count (1..4).
static int outputpane_encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/// @brief Ensure the terminal cell buffer can hold at least @p need cells.
static bool term_ensure_cells(vg_outputpane_t *pane, size_t need) {
    if (pane->cell_capacity >= need)
        return true;
    size_t new_cap = pane->cell_capacity ? pane->cell_capacity * 2 : 128;
    if (new_cap < need)
        new_cap = need;
    if (new_cap > SIZE_MAX / sizeof(vg_term_cell_t))
        return false;
    vg_term_cell_t *nc = realloc(pane->cells, new_cap * sizeof(vg_term_cell_t));
    if (!nc)
        return false;
    pane->cells = nc;
    pane->cell_capacity = new_cap;
    return true;
}

/// @brief Reset a cell to a blank space with default colors.
static void term_blank_cell(vg_outputpane_t *pane, vg_term_cell_t *cell) {
    cell->utf8[0] = '\0';
    cell->fg = pane->default_fg;
    cell->bg = 0;
    cell->bold = false;
}

/// @brief Write a glyph at the cursor column (overwriting), extending with blanks as needed.
static void term_put_glyph(vg_outputpane_t *pane, const char *bytes, int len) {
    if (len < 1)
        len = 1;
    if (len > 4)
        len = 4;
    size_t col = pane->cursor_col;
    if (!term_ensure_cells(pane, col + 1))
        return;
    while (pane->cell_count <= col) {
        term_blank_cell(pane, &pane->cells[pane->cell_count]);
        pane->cell_count++;
    }
    vg_term_cell_t *cell = &pane->cells[col];
    memcpy(cell->utf8, bytes, (size_t)len);
    cell->utf8[len] = '\0';
    cell->fg = pane->current_fg;
    cell->bg = pane->current_bg;
    cell->bold = pane->ansi_bold;
    pane->cursor_col = (uint32_t)(col + 1);
}

/// @brief Rebuild the current (last) line's styled segments from the cell buffer.
static void term_flush_cells(vg_outputpane_t *pane) {
    if (pane->line_count == 0) {
        if (!add_line(pane))
            return;
    }
    vg_output_line_t *line = &pane->lines[pane->line_count - 1];
    for (size_t s = 0; s < line->segment_count; s++)
        free(line->segments[s].text);
    free(line->segments);
    line->segments = NULL;
    line->segment_count = 0;
    line->segment_capacity = 0;

    if (pane->cell_count == 0)
        return;
    if (pane->cell_count > (SIZE_MAX - 1) / 4)
        return;
    char *run = malloc(pane->cell_count * 4 + 1);
    if (!run)
        return;
    size_t i = 0;
    while (i < pane->cell_count) {
        uint32_t fg = pane->cells[i].fg;
        uint32_t bg = pane->cells[i].bg;
        bool bold = pane->cells[i].bold;
        size_t rl = 0;
        size_t j = i;
        for (; j < pane->cell_count; j++) {
            vg_term_cell_t *c = &pane->cells[j];
            if (c->fg != fg || c->bg != bg || c->bold != bold)
                break;
            if (c->utf8[0] == '\0') {
                run[rl++] = ' ';
            } else {
                size_t l = strlen(c->utf8);
                memcpy(run + rl, c->utf8, l);
                rl += l;
            }
        }
        (void)outputpane_append_segment_copy(line, run, rl, fg, bg, bold);
        i = j;
    }
    free(run);
}

/// @brief Finalize the current line and start a fresh one (terminal newline).
static void term_newline(vg_outputpane_t *pane) {
    term_flush_cells(pane);
    if (!add_line(pane))
        return;
    pane->cell_count = 0;
    pane->cursor_col = 0;
}

/// @brief Dispatch a completed CSI sequence (escape_buf holds the params, no '[').
static void term_dispatch_csi(vg_outputpane_t *pane, char final) {
    int params[16];
    int pc = 0;
    char *p = pane->escape_buf;
    while (*p && pc < 16) {
        if (*p >= '0' && *p <= '9') {
            params[pc++] = (int)strtol(p, &p, 10);
            if (*p == ';')
                p++;
        } else if (*p == ';') {
            params[pc++] = 0;
            p++;
        } else {
            p++;
        }
    }
    int p0 = pc > 0 ? params[0] : 0;
    switch (final) {
        case 'm':
            if (pc == 0) {
                pane->current_fg = pane->default_fg;
                pane->current_bg = 0;
                pane->ansi_bold = false;
            }
            for (int i = 0; i < pc; i++) {
                int code = params[i];
                if (code == 0) {
                    pane->current_fg = pane->default_fg;
                    pane->current_bg = 0;
                    pane->ansi_bold = false;
                } else if (code == 1) {
                    pane->ansi_bold = true;
                } else if (code == 22) {
                    pane->ansi_bold = false;
                } else if (code >= 30 && code <= 37) {
                    pane->current_fg = ansi_code_to_color(code);
                } else if (code == 39) {
                    pane->current_fg = pane->default_fg;
                } else if (code >= 40 && code <= 47) {
                    pane->current_bg = ansi_code_to_color(code - 10);
                } else if (code == 49) {
                    pane->current_bg = 0;
                } else if (code >= 90 && code <= 97) {
                    pane->current_fg = ansi_code_to_color(code);
                } else if (code >= 100 && code <= 107) {
                    pane->current_bg = ansi_code_to_color(code - 10);
                }
            }
            break;
        case 'K': // erase in line
            if (p0 == 0) {
                if (pane->cursor_col < pane->cell_count)
                    pane->cell_count = pane->cursor_col;
            } else if (p0 == 1) {
                for (size_t i = 0; i <= pane->cursor_col && i < pane->cell_count; i++)
                    term_blank_cell(pane, &pane->cells[i]);
            } else if (p0 == 2) {
                pane->cell_count = 0;
            }
            break;
        case 'C': { // cursor forward
            int n = p0 > 0 ? p0 : 1;
            pane->cursor_col += (uint32_t)n;
            break;
        }
        case 'D': { // cursor back
            int n = p0 > 0 ? p0 : 1;
            if ((uint32_t)n > pane->cursor_col)
                pane->cursor_col = 0;
            else
                pane->cursor_col -= (uint32_t)n;
            break;
        }
        case 'G': // cursor to column
            pane->cursor_col = p0 > 0 ? (uint32_t)(p0 - 1) : 0;
            break;
        case 'H':
        case 'f': { // cursor position (row ignored — single-line model)
            int col = pc >= 2 ? params[1] : 1;
            pane->cursor_col = col > 0 ? (uint32_t)(col - 1) : 0;
            break;
        }
        default:
            break; // J (erase display), others — ignored for the line model
    }
}

/// @brief Terminal-mode append: full escape state machine + cursor-column overwrite.
static void outputpane_append_terminal(vg_outputpane_t *pane, const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned char c = *p;
        switch (pane->esc_state) {
            case 0: // normal
                if (c == 0x1b) {
                    pane->esc_state = 1;
                    p++;
                } else if (c == '\n') {
                    term_newline(pane);
                    p++;
                } else if (c == '\r') {
                    pane->cursor_col = 0;
                    p++;
                } else if (c == '\b') {
                    if (pane->cursor_col > 0)
                        pane->cursor_col--;
                    p++;
                } else if (c == '\t') {
                    uint32_t next = (pane->cursor_col / 8 + 1) * 8;
                    while (pane->cursor_col < next)
                        term_put_glyph(pane, " ", 1);
                    p++;
                } else if (c < 0x20 || c == 0x7f) {
                    p++; // BEL and other C0 controls: ignore
                } else {
                    int l = outputpane_utf8_len(c);
                    int avail = 0;
                    while (avail < l && p[avail])
                        avail++;
                    if (avail < l)
                        l = avail > 0 ? avail : 1;
                    term_put_glyph(pane, (const char *)p, l);
                    p += l;
                }
                break;
            case 1: // after ESC
                if (c == '[') {
                    pane->esc_state = 2;
                    pane->escape_len = 0;
                    pane->escape_buf[0] = '\0';
                } else if (c == ']') {
                    pane->esc_state = 3; // OSC
                } else if (c == '(' || c == ')' || c == '*' || c == '+') {
                    pane->esc_state = 4; // charset designator (consume next byte)
                } else {
                    pane->esc_state = 0; // ESC 7/8/=/>/D/E/M/c/... — swallowed
                }
                p++;
                break;
            case 2: // CSI
                if (c >= 0x40 && c <= 0x7e) {
                    term_dispatch_csi(pane, (char)c);
                    pane->esc_state = 0;
                } else if (pane->escape_len < (int)sizeof(pane->escape_buf) - 1) {
                    pane->escape_buf[pane->escape_len++] = (char)c;
                    pane->escape_buf[pane->escape_len] = '\0';
                }
                p++;
                break;
            case 3: // OSC — consume until BEL or ST (ESC \)
                if (c == 0x07)
                    pane->esc_state = 0;
                else if (c == 0x1b)
                    pane->esc_state = 5;
                p++;
                break;
            case 5: // OSC saw ESC, expect '\' (ST)
                pane->esc_state = 0;
                p++;
                break;
            case 4: // charset designator — consume one byte
                pane->esc_state = 0;
                p++;
                break;
            default:
                pane->esc_state = 0;
                p++;
                break;
        }
    }

    term_flush_cells(pane);
    if (pane->auto_scroll && !pane->scroll_locked)
        vg_outputpane_scroll_to_bottom(pane);
    pane->base.needs_paint = true;
}

/// @brief Queue raw bytes for the controller to drain to the PTY (terminal keystrokes).
static void term_queue_input(vg_outputpane_t *pane, const char *bytes, size_t len) {
    if (len == 0)
        return;
    // Keep the caret solid (not mid-blink) while the user is actively typing.
    pane->caret_visible = true;
    pane->caret_blink_time = 0.0f;
    if (pane->pending_len + len + 1 > pane->pending_capacity) {
        size_t nc = pane->pending_capacity ? pane->pending_capacity * 2 : 64;
        while (nc < pane->pending_len + len + 1)
            nc *= 2;
        char *nb = realloc(pane->pending_input, nc);
        if (!nb)
            return;
        pane->pending_input = nb;
        pane->pending_capacity = nc;
    }
    memcpy(pane->pending_input + pane->pending_len, bytes, len);
    pane->pending_len += len;
}

/// @brief Enable/disable interactive terminal mode (see header).
void vg_outputpane_set_terminal_mode(vg_outputpane_t *pane, bool enabled) {
    if (!pane)
        return;
    pane->terminal_mode = enabled;
    if (enabled && pane->line_count == 0)
        (void)add_line(pane);
}

/// @brief Drain queued keystroke bytes; caller frees. NULL when empty.
char *vg_outputpane_take_input(vg_outputpane_t *pane) {
    if (!pane || pane->pending_len == 0)
        return NULL;
    char *out = malloc(pane->pending_len + 1);
    if (!out)
        return NULL;
    memcpy(out, pane->pending_input, pane->pending_len);
    out[pane->pending_len] = '\0';
    pane->pending_len = 0;
    return out;
}

/// @brief Advance the terminal caret blink (no-op unless terminal mode + focused).
void vg_outputpane_tick(vg_outputpane_t *pane, float dt) {
    if (!pane || !pane->terminal_mode || !pane->has_focus || dt <= 0.0f)
        return;
    pane->caret_blink_time += dt;
    if (pane->caret_blink_time >= 0.5f) {
        pane->caret_blink_time -= 0.5f;
        pane->caret_visible = !pane->caret_visible;
        pane->base.needs_paint = true;
    }
}

/// @brief Append a UTF-8 string to the output pane, interpreting ANSI SGR escape sequences.
///
/// @details Newlines split the current line and begin a new one.  Escape sequences update
///          the current foreground/background/bold state carried into subsequent segments.
///          ANSI state is preserved across calls, so multi-chunk appends render correctly.
///          Auto-scrolls to the bottom unless scroll_locked is set.
///
/// @param pane The output pane to append to.
/// @param text UTF-8 text to append; may be NULL (no-op).
void vg_outputpane_append(vg_outputpane_t *pane, const char *text) {
    if (!pane || !text)
        return;
    if (pane->terminal_mode) {
        outputpane_append_terminal(pane, text);
        return;
    }

    // Get or create current line
    vg_output_line_t *line = NULL;
    if (pane->line_count > 0) {
        line = &pane->lines[pane->line_count - 1];
    } else {
        line = add_line(pane);
        if (!line)
            return;
    }

    const char *p = text;
    const char *segment_start = p;

    while (*p) {
        if (*p == '\033') {
            // Flush pending text
            if (p > segment_start) {
                (void)outputpane_append_segment_copy(line,
                                                     segment_start,
                                                     (size_t)(p - segment_start),
                                                     pane->current_fg,
                                                     pane->current_bg,
                                                     pane->ansi_bold);
            }

            // Start escape sequence
            pane->in_escape = true;
            pane->escape_len = 0;
            p++;
            segment_start = p;
        } else if (pane->in_escape) {
            // Accumulate escape sequence
            if (pane->escape_len < (int)sizeof(pane->escape_buf) - 1) {
                pane->escape_buf[pane->escape_len++] = *p;
                pane->escape_buf[pane->escape_len] = '\0';
            } else {
                pane->in_escape = false;
                pane->escape_len = 0;
                segment_start = p + 1;
            }

            if (pane->in_escape && *p >= '@' && *p <= '~' && *p != '[') {
                // End of a CSI/escape sequence on its final byte (m, h, l, A-K, ...).
                // CSI parameter/intermediate bytes are all < 0x40, so the first byte
                // in 0x40-0x7E (other than the '[' introducer) is the terminator.
                // Only SGR ("...m") changes color; other finals (e.g. the shell's
                // ESC[?1034h) are consumed and ignored rather than eating later text.
                process_ansi_escape(pane);
                segment_start = p + 1;
            }
            p++;
        } else if (*p == '\r') {
            // Carriage return: flush pending text and drop the CR. The line-append
            // model has no cursor column, so CRLF collapses to one newline and a
            // bare CR (line-rewrite) is dropped rather than rendered as a stray byte.
            if (p > segment_start) {
                (void)outputpane_append_segment_copy(line,
                                                     segment_start,
                                                     (size_t)(p - segment_start),
                                                     pane->current_fg,
                                                     pane->current_bg,
                                                     pane->ansi_bold);
            }
            p++;
            segment_start = p;
        } else if (*p == '\n') {
            // Flush pending text
            if (p > segment_start) {
                (void)outputpane_append_segment_copy(line,
                                                     segment_start,
                                                     (size_t)(p - segment_start),
                                                     pane->current_fg,
                                                     pane->current_bg,
                                                     pane->ansi_bold);
            }

            // Start new line
            line = add_line(pane);
            if (!line)
                return;

            p++;
            segment_start = p;
        } else {
            p++;
        }
    }

    // Flush remaining text
    if (p > segment_start && !pane->in_escape) {
        (void)outputpane_append_segment_copy(line,
                                             segment_start,
                                             (size_t)(p - segment_start),
                                             pane->current_fg,
                                             pane->current_bg,
                                             pane->ansi_bold);
    }

    // Auto-scroll
    if (pane->auto_scroll && !pane->scroll_locked) {
        vg_outputpane_scroll_to_bottom(pane);
    }

    pane->base.needs_paint = true;
}

/// @brief Append text as a complete new line, ensuring it lands on its own row.
///
/// @details If the last line already contains content a new blank line is inserted
///          first, then text is appended via vg_outputpane_append.  A trailing
///          blank line is also added after the content so subsequent appends begin
///          on a fresh line.
///
/// @param pane The output pane to append to.
/// @param text Text to append as a new line; NULL appends an empty line.
void vg_outputpane_append_line(vg_outputpane_t *pane, const char *text) {
    if (!pane)
        return;

    if (pane->line_count == 0) {
        if (!add_line(pane))
            return;
    } else if (outputpane_line_length(&pane->lines[pane->line_count - 1]) > 0) {
        if (!add_line(pane))
            return;
    }

    if (text && *text)
        vg_outputpane_append(pane, text);

    if (pane->max_lines > 1)
        (void)add_line(pane);

    if (pane->auto_scroll && !pane->scroll_locked)
        vg_outputpane_scroll_to_bottom(pane);
    pane->base.needs_paint = true;
}

/// @brief Append a pre-styled text segment without ANSI parsing.
///
/// @details The entire text string is added as a single styled segment with the
///          given colours and bold flag.  No newline splitting or ANSI processing
///          is performed — use vg_outputpane_append for that.
///
/// @param pane The output pane to append to.
/// @param text Text to append; may be NULL (no-op).
/// @param fg   Foreground colour as AARRGGBB.
/// @param bg   Background colour as AARRGGBB (0 = transparent).
/// @param bold true to render the segment in bold.
void vg_outputpane_append_styled(
    vg_outputpane_t *pane, const char *text, uint32_t fg, uint32_t bg, bool bold) {
    if (!pane || !text)
        return;

    // Get or create current line
    vg_output_line_t *line = NULL;
    if (pane->line_count > 0) {
        line = &pane->lines[pane->line_count - 1];
    } else {
        line = add_line(pane);
        if (!line)
            return;
    }

    (void)outputpane_append_segment_copy(line, text, strlen(text), fg, bg, bold);

    // Auto-scroll
    if (pane->auto_scroll && !pane->scroll_locked) {
        vg_outputpane_scroll_to_bottom(pane);
    }

    pane->base.needs_paint = true;
}

/// @brief Remove all lines, reset ANSI state, and scroll back to the top.
///
/// @param pane The output pane to clear; may be NULL.
void vg_outputpane_clear(vg_outputpane_t *pane) {
    if (!pane)
        return;

    for (size_t i = 0; i < pane->line_count; i++) {
        free_output_line(&pane->lines[i]);
    }
    pane->line_count = 0;

    // Reset ANSI state
    pane->current_fg = pane->default_fg;
    pane->current_bg = 0;
    pane->ansi_bold = false;
    pane->in_escape = false;
    pane->escape_len = 0;

    // Reset terminal-mode cursor / escape / input state.
    pane->esc_state = 0;
    pane->cursor_col = 0;
    pane->cell_count = 0;
    pane->pending_len = 0;
    if (pane->terminal_mode)
        (void)add_line(pane);

    // Reset scroll
    pane->scroll_y = 0;
    pane->scroll_locked = false;
    outputpane_clear_selection(pane);

    pane->base.needs_paint = true;
}

/// @brief Scroll the output pane to the last line and clear scroll_locked.
///
/// @param pane The output pane to scroll; may be NULL.
void vg_outputpane_scroll_to_bottom(vg_outputpane_t *pane) {
    if (!pane)
        return;

    float content_height = pane->line_count * pane->line_height;
    float max_scroll = content_height - pane->base.height;
    if (max_scroll < 0)
        max_scroll = 0;

    pane->scroll_y = max_scroll;
    pane->scroll_locked = false;
    pane->base.needs_paint = true;
}

/// @brief Scroll the output pane to the first line and set scroll_locked.
///
/// @param pane The output pane to scroll; may be NULL.
void vg_outputpane_scroll_to_top(vg_outputpane_t *pane) {
    if (!pane)
        return;

    pane->scroll_y = 0;
    pane->scroll_locked = true;
    pane->base.needs_paint = true;
}

/// @brief Enable or disable automatic scrolling to the bottom on new content.
///
/// @param pane        The output pane to configure; may be NULL.
/// @param auto_scroll true to auto-scroll on append; false to keep the current position.
void vg_outputpane_set_auto_scroll(vg_outputpane_t *pane, bool auto_scroll) {
    if (!pane)
        return;
    pane->auto_scroll = auto_scroll;
}

/// @brief Return a heap-allocated string containing the currently selected text.
///
/// @details Lines in the selection are joined with newline characters.  The
///          caller is responsible for freeing the returned string.
///
/// @param pane The output pane to query.
/// @return Heap-allocated UTF-8 string, or NULL if there is no selection or on
///         allocation failure.
char *vg_outputpane_get_selection(vg_outputpane_t *pane) {
    if (!pane || !pane->has_selection || pane->line_count == 0)
        return NULL;

    // Normalize selection bounds
    uint32_t start_line = pane->sel_start_line;
    uint32_t start_col = pane->sel_start_col;
    uint32_t end_line = pane->sel_end_line;
    uint32_t end_col = pane->sel_end_col;
    if (start_line > end_line || (start_line == end_line && start_col > end_col)) {
        uint32_t tmp = start_line;
        start_line = end_line;
        end_line = tmp;
        tmp = start_col;
        start_col = end_col;
        end_col = tmp;
    }

    // Clamp to actual line range
    if (start_line >= (uint32_t)pane->line_count)
        return NULL;
    if (end_line >= (uint32_t)pane->line_count)
        end_line = (uint32_t)(pane->line_count - 1);

    // First pass: calculate required buffer size
    size_t total = 0;
    for (uint32_t li = start_line; li <= end_line; li++) {
        vg_output_line_t *line = &pane->lines[li];
        size_t col = 0;
        for (size_t si = 0; si < line->segment_count; si++) {
            const char *seg = line->segments[si].text;
            if (!seg)
                continue;
            for (size_t ci = 0; seg[ci] != '\0';) {
                size_t cp_len = outputpane_utf8_next_len(seg + ci);
                if (cp_len == 0)
                    break;
                if (li == start_line && col < start_col)
                    goto next_codepoint_count;
                if (li == end_line && col >= end_col && end_col != UINT32_MAX)
                    break;
                if (cp_len > SIZE_MAX - total)
                    return NULL;
                total += cp_len;
            next_codepoint_count:
                ci += cp_len;
                col++;
            }
        }
        if (li < end_line) {
            if (total == SIZE_MAX)
                return NULL;
            total++; // newline between lines
        }
    }

    if (total == 0)
        return NULL;

    char *buf = malloc(total + 1);
    if (!buf)
        return NULL;

    // Second pass: copy selected text
    size_t out = 0;
    for (uint32_t li = start_line; li <= end_line; li++) {
        vg_output_line_t *line = &pane->lines[li];
        size_t col = 0;
        for (size_t si = 0; si < line->segment_count; si++) {
            const char *seg = line->segments[si].text;
            if (!seg)
                continue;
            for (size_t ci = 0; seg[ci] != '\0';) {
                size_t cp_len = outputpane_utf8_next_len(seg + ci);
                if (cp_len == 0)
                    break;
                if (li == start_line && col < start_col)
                    goto next_codepoint_copy;
                if (li == end_line && col >= end_col && end_col != UINT32_MAX)
                    break;
                memcpy(buf + out, seg + ci, cp_len);
                out += cp_len;
            next_codepoint_copy:
                ci += cp_len;
                col++;
            }
        }
        if (li < end_line)
            buf[out++] = '\n';
    }
    buf[out] = '\0';
    return buf;
}

/// @brief Select all text in the output pane from the first to the last line.
///
/// @param pane The output pane to update; may be NULL.
void vg_outputpane_select_all(vg_outputpane_t *pane) {
    if (!pane || pane->line_count == 0)
        return;

    pane->has_selection = true;
    pane->sel_start_line = 0;
    pane->sel_start_col = 0;
    pane->sel_end_line = (uint32_t)(pane->line_count - 1);
    pane->sel_end_col = UINT32_MAX; // End of line

    pane->base.needs_paint = true;
}

/// @brief Set the maximum number of lines retained in the ring buffer.
///
/// @details If the current line count exceeds max, the oldest lines are evicted
///          immediately.  Minimum effective value is 1.
///
/// @param pane The output pane to configure; may be NULL.
/// @param max  Maximum number of lines to retain; 0 is clamped to 1.
void vg_outputpane_set_max_lines(vg_outputpane_t *pane, size_t max) {
    if (!pane)
        return;
    if (max == 0)
        max = 1;
    pane->max_lines = max;
    while (pane->line_count > pane->max_lines) {
        free_output_line(&pane->lines[0]);
        outputpane_note_evicted_first_line(pane);
        memmove(
            &pane->lines[0], &pane->lines[1], (pane->line_count - 1) * sizeof(vg_output_line_t));
        pane->line_count--;
    }
    if (pane->line_capacity > pane->max_lines)
        pane->line_capacity = pane->max_lines;
    pane->base.needs_paint = true;
}

/// @brief Set the font used to measure and render text in the output pane.
///
/// @param pane The output pane to configure; may be NULL.
/// @param font The font to use; must outlive the pane.
/// @param size Point size for text rendering.
void vg_outputpane_set_font(vg_outputpane_t *pane, vg_font_t *font, float size) {
    if (!pane)
        return;
    if (pane->font == font && pane->font_size == size)
        return;

    pane->font = font;
    pane->font_size = size;
    if (font && size > 0.0f) {
        vg_font_metrics_t metrics = {0};
        vg_font_get_metrics(font, size, &metrics);
        if (metrics.line_height > 0)
            pane->line_height = (float)metrics.line_height;
        else
            pane->line_height = size * 1.35f;
    }
    pane->base.needs_layout = true;
    pane->base.needs_paint = true;
}
