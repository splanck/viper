//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_codeeditor.c
// Purpose: Code editor widget — a full-featured multi-line text editor with
//          syntax highlighting, undo/redo, multi-cursor editing, line folding,
//          word wrap, and scrollbars.
// Key invariants:
//   - Text is stored as an array of vg_code_line_t, each with a heap-allocated
//     char buffer; there is always at least one line.
//   - cursor_line/cursor_col are always clamped to valid positions before use.
//   - editor->modified is set true by any insert or delete operation and must
//     be cleared explicitly via vg_codeeditor_clear_modified.
//   - syncing_children / syncing guards are used in the color picker but not
//     here; instead, multi-cursor edits apply all targets in a single sorted
//     pass to avoid position invalidation.
//   - scroll_x and scroll_y are logical pixel offsets; codeeditor_clamp_scroll
//     keeps them within valid bounds after every content change.
//   - Lines hidden by a fold region are excluded from visual row calculations
//     and paint; fold regions are stored in fold_regions[].
// Ownership/Lifetime:
//   - Each vg_code_line_t owns its text and colors buffers; freed by free_line.
//   - editor->history is heap-allocated and freed in codeeditor_destroy.
//   - editor->fold_regions is heap-allocated and freed in codeeditor_destroy.
// Links: lib/gui/include/vg_ide_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../../graphics/src/vgfx_internal.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <limits.h>
#include <stdint.h>
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
// Lines scrolled per mouse-wheel delta unit. Was 1.0f, but on macOS trackpads
// (which emit high-frequency, high-magnitude delta_y values) that produced an
// uncomfortably fast scroll. Half-line per delta unit feels closer to the
// platform-native cadence of Safari, VS Code, and Sublime.
#define CODEEDITOR_MOUSE_WHEEL_LINES 0.3f
// Prevent malicious or accidental all-document highlight spans from allocating
// unbounded line-index storage. Normal diagnostics/find highlights are far below
// this because they usually touch one source line each.
#define CODEEDITOR_HIGHLIGHT_LINE_INDEX_MAX_ENTRIES 2000000
#define CODEEDITOR_PAIR_HIGHLIGHT_FILL 0x6638BDF8u
#define CODEEDITOR_PAIR_HIGHLIGHT_BORDER 0xFF38BDF8u

//=============================================================================
// Forward Declarations
//=============================================================================

static void codeeditor_destroy(vg_widget_t *widget);
static void codeeditor_measure(vg_widget_t *widget, float available_width, float available_height);
static void codeeditor_paint(vg_widget_t *widget, void *canvas);
static bool codeeditor_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool codeeditor_can_focus(vg_widget_t *widget);
static void codeeditor_on_focus(vg_widget_t *widget, bool gained);
static void codeeditor_draw_highlight_underline(
    void *canvas, float x, float y, float width, float line_height, uint32_t color);
static void codeeditor_paint_pair_highlight_for_segment(vg_codeeditor_t *editor,
                                                        void *canvas,
                                                        int line,
                                                        int segment_start,
                                                        int segment_len,
                                                        float content_x,
                                                        float row_y,
                                                        float scroll_x,
                                                        bool wrapped);

static bool ensure_line_capacity(vg_codeeditor_t *editor, int needed);
static bool ensure_text_capacity(vg_code_line_t *line, size_t needed);
static void insert_text_at_internal(vg_codeeditor_t *editor, int line, int col, const char *text);
static void delete_text_range_internal(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col);
static void codeeditor_adjust_hidden_cursors(vg_codeeditor_t *editor);
static int codeeditor_prev_byte_boundary(const vg_codeeditor_t *editor, int line, int byte_col);

static void codeeditor_perf_add(uint64_t *counter, uint64_t amount) {
    if (!counter)
        return;
    if (*counter > UINT64_MAX - amount) {
        *counter = UINT64_MAX;
    } else {
        *counter += amount;
    }
}

static void codeeditor_bump_layout_generation(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    editor->layout_generation =
        editor->layout_generation == UINT64_MAX ? 1 : editor->layout_generation + 1;
    editor->layout_cache_valid = false;
}

static void codeeditor_refresh_fold_summary(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    editor->has_folded_lines = false;
    for (int i = 0; i < editor->fold_region_count; i++) {
        if (editor->fold_regions[i].folded) {
            editor->has_folded_lines = true;
            return;
        }
    }
}

static bool codeeditor_has_hidden_lines(const vg_codeeditor_t *editor) {
    return editor ? editor->has_folded_lines : false;
}

static int codeeditor_clamp_line_index(const vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return 0;
    if (line < 0)
        return 0;
    if (line >= editor->line_count)
        return editor->line_count - 1;
    return line;
}

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

/// @brief Grows @p *lines to hold at least @p needed entries, doubling capacity; zeros new slots.
static bool ensure_line_array_capacity(vg_code_line_t **lines, int *capacity, int needed) {
    if (!lines || !capacity || needed < 0)
        return false;
    if (needed <= *capacity)
        return true;

    int old_capacity = *capacity;
    int new_capacity = old_capacity;
    if (new_capacity <= 0)
        new_capacity = INITIAL_LINE_CAPACITY;
    while (new_capacity < needed) {
        if (new_capacity > INT_MAX / LINE_CAPACITY_GROWTH)
            return false;
        new_capacity *= LINE_CAPACITY_GROWTH;
    }

    if ((size_t)new_capacity > SIZE_MAX / sizeof(vg_code_line_t))
        return false;
    vg_code_line_t *new_lines = realloc(*lines, (size_t)new_capacity * sizeof(vg_code_line_t));
    if (!new_lines)
        return false;

    memset(new_lines + old_capacity,
           0,
           (size_t)(new_capacity - old_capacity) * sizeof(vg_code_line_t));

    *lines = new_lines;
    *capacity = new_capacity;
    return true;
}

/// @brief Ensures editor->lines can hold at least @p needed entries, delegating to
/// ensure_line_array_capacity.
static bool ensure_line_capacity(vg_codeeditor_t *editor, int needed) {
    if (!editor)
        return false;
    return ensure_line_array_capacity(&editor->lines, &editor->line_capacity, needed);
}

/// @brief Advance the editor content revision, keeping zero reserved for invalid handles.
static void codeeditor_bump_revision(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    editor->revision = editor->revision == UINT64_MAX ? 1 : editor->revision + 1;
}

/// @brief Advance the syntax-cache generation after a language/theme/global-state change.
static void codeeditor_bump_highlight_generation(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    editor->highlight_generation =
        editor->highlight_generation == UINT64_MAX ? 1 : editor->highlight_generation + 1;
}

/// @brief Mark one line's cached syntax colors and language state stale.
static void codeeditor_invalidate_line_highlight(vg_codeeditor_t *editor, int line) {
    if (!editor || line < 0 || line >= editor->line_count)
        return;
    editor->lines[line].highlight_generation = 0;
    editor->lines[line].syntax_state_generation = 0;
}

/// @brief Mark all syntax colors and cached language states stale without touching text revision.
static void codeeditor_invalidate_all_highlights(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_bump_highlight_generation(editor);
    for (int i = 0; i < editor->line_count; i++) {
        editor->lines[i].highlight_generation = 0;
        editor->lines[i].syntax_state_generation = 0;
    }
}

/// @brief Heuristic for edits that can change downstream lexical state.
static bool codeeditor_text_may_affect_multiline_syntax(const char *text) {
    if (!text)
        return false;
    for (const char *p = text; *p; p++) {
        if (*p == '\n' || *p == '/' || *p == '*')
            return true;
    }
    return false;
}

static int highlight_span_compare(const void *lhs, const void *rhs) {
    const struct vg_highlight_span *a = (const struct vg_highlight_span *)lhs;
    const struct vg_highlight_span *b = (const struct vg_highlight_span *)rhs;
    if (a->start_line != b->start_line)
        return a->start_line < b->start_line ? -1 : 1;
    if (a->start_col != b->start_col)
        return a->start_col < b->start_col ? -1 : 1;
    if (a->end_line != b->end_line)
        return a->end_line < b->end_line ? -1 : 1;
    if (a->end_col != b->end_col)
        return a->end_col < b->end_col ? -1 : 1;
    return 0;
}

static void codeeditor_invalidate_highlight_line_index(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    editor->highlight_line_index_valid = false;
}

static void codeeditor_free_highlight_line_index(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    free(editor->highlight_line_offsets);
    free(editor->highlight_line_span_indices);
    editor->highlight_line_offsets = NULL;
    editor->highlight_line_span_indices = NULL;
    editor->highlight_line_offsets_cap = 0;
    editor->highlight_line_span_indices_cap = 0;
    editor->highlight_line_index_line_count = 0;
    editor->highlight_line_index_span_count = 0;
    editor->highlight_line_index_valid = false;
}

static void codeeditor_sort_highlight_spans(vg_codeeditor_t *editor) {
    if (!editor || editor->highlight_spans_sorted)
        return;
    if (editor->highlight_span_count <= 1) {
        editor->highlight_spans_sorted = true;
        codeeditor_invalidate_highlight_line_index(editor);
        return;
    }
    qsort(editor->highlight_spans,
          (size_t)editor->highlight_span_count,
          sizeof(*editor->highlight_spans),
          highlight_span_compare);
    editor->highlight_spans_sorted = true;
    codeeditor_invalidate_highlight_line_index(editor);
}

static bool codeeditor_highlight_line_range_for_span(const vg_codeeditor_t *editor,
                                                     const struct vg_highlight_span *span,
                                                     int *out_start,
                                                     int *out_end) {
    if (!editor || !span || editor->line_count <= 0)
        return false;

    int start = span->start_line;
    int end = span->end_line;
    if (end < 0 || start >= editor->line_count)
        return false;
    if (start < 0)
        start = 0;
    if (end >= editor->line_count)
        end = editor->line_count - 1;
    if (end < start)
        return false;

    if (out_start)
        *out_start = start;
    if (out_end)
        *out_end = end;
    return true;
}

static bool codeeditor_ensure_highlight_line_index(vg_codeeditor_t *editor) {
    if (!editor)
        return false;

    codeeditor_sort_highlight_spans(editor);

    if (editor->highlight_line_index_valid &&
        editor->highlight_line_index_line_count == editor->line_count &&
        editor->highlight_line_index_span_count == editor->highlight_span_count)
        return true;

    editor->highlight_line_index_valid = false;
    editor->highlight_line_index_line_count = 0;
    editor->highlight_line_index_span_count = 0;

    if (editor->line_count < 0)
        return false;
    int offset_count = editor->line_count + 1;
    if (offset_count <= 0)
        return false;

    if (offset_count > editor->highlight_line_offsets_cap) {
        if ((size_t)offset_count > SIZE_MAX / sizeof(int))
            return false;
        int *offsets =
            (int *)realloc(editor->highlight_line_offsets, (size_t)offset_count * sizeof(int));
        if (!offsets)
            return false;
        editor->highlight_line_offsets = offsets;
        editor->highlight_line_offsets_cap = offset_count;
    }

    memset(editor->highlight_line_offsets, 0, (size_t)offset_count * sizeof(int));

    size_t total_entries = 0;
    for (int s = 0; s < editor->highlight_span_count; s++) {
        int start = 0;
        int end = 0;
        if (!codeeditor_highlight_line_range_for_span(
                editor, &editor->highlight_spans[s], &start, &end))
            continue;
        size_t coverage = (size_t)(end - start + 1);
        if (coverage > CODEEDITOR_HIGHLIGHT_LINE_INDEX_MAX_ENTRIES - total_entries)
            return false;
        total_entries += coverage;
        for (int line = start; line <= end; line++)
            editor->highlight_line_offsets[line + 1]++;
    }

    for (int line = 1; line < offset_count; line++)
        editor->highlight_line_offsets[line] += editor->highlight_line_offsets[line - 1];

    if ((int)total_entries > editor->highlight_line_span_indices_cap) {
        if (total_entries > SIZE_MAX / sizeof(int))
            return false;
        int *indices =
            (int *)realloc(editor->highlight_line_span_indices, total_entries * sizeof(int));
        if (!indices && total_entries > 0)
            return false;
        editor->highlight_line_span_indices = indices;
        editor->highlight_line_span_indices_cap = (int)total_entries;
    }

    int *write_offsets = NULL;
    if (offset_count > 0) {
        if ((size_t)offset_count > SIZE_MAX / sizeof(int))
            return false;
        write_offsets = (int *)malloc((size_t)offset_count * sizeof(int));
        if (!write_offsets)
            return false;
        memcpy(write_offsets, editor->highlight_line_offsets, (size_t)offset_count * sizeof(int));
    }

    for (int s = 0; s < editor->highlight_span_count; s++) {
        int start = 0;
        int end = 0;
        if (!codeeditor_highlight_line_range_for_span(
                editor, &editor->highlight_spans[s], &start, &end))
            continue;
        for (int line = start; line <= end; line++)
            editor->highlight_line_span_indices[write_offsets[line]++] = s;
    }

    free(write_offsets);
    editor->highlight_line_index_line_count = editor->line_count;
    editor->highlight_line_index_span_count = editor->highlight_span_count;
    editor->highlight_line_index_valid = true;
    return true;
}

static void codeeditor_paint_highlights_for_line(vg_codeeditor_t *editor,
                                                 void *canvas,
                                                 int line,
                                                 int segment_start,
                                                 int segment_len,
                                                 float content_x,
                                                 float row_y,
                                                 float scroll_x,
                                                 bool wrapped) {
    if (!editor || line < 0 || line >= editor->line_count || editor->highlight_span_count <= 0)
        return;

    bool indexed = codeeditor_ensure_highlight_line_index(editor);
    int begin = 0;
    int end = editor->highlight_span_count;
    if (indexed && editor->highlight_line_offsets &&
        line + 1 <= editor->highlight_line_index_line_count) {
        begin = editor->highlight_line_offsets[line];
        end = editor->highlight_line_offsets[line + 1];
    }

    size_t line_len = editor->lines[line].length;
    for (int entry = begin; entry < end; entry++) {
        codeeditor_perf_add(&editor->perf_stats.highlight_span_checks, 1);
        int span_index = indexed ? editor->highlight_line_span_indices[entry] : entry;
        struct vg_highlight_span *span = &editor->highlight_spans[span_index];
        if (!indexed) {
            if (span->start_line > line)
                break;
            if (line < span->start_line || line > span->end_line)
                continue;
        }

        int span_start = (line == span->start_line) ? span->start_col : 0;
        int span_end = (line == span->end_line) ? span->end_col : (int)line_len;
        if (span_end <= span_start)
            span_end = span_start + 1;

        if (!wrapped) {
            float span_x = content_x + (float)span_start * editor->char_width - scroll_x;
            float span_w = (float)(span_end - span_start) * editor->char_width;
            codeeditor_draw_highlight_underline(
                canvas, span_x, row_y, span_w, editor->line_height, span->color);
            continue;
        }

        int overlap_start = span_start > segment_start ? span_start : segment_start;
        int overlap_end =
            span_end < segment_start + segment_len ? span_end : segment_start + segment_len;
        if (overlap_end <= overlap_start)
            continue;
        float span_x = content_x + (float)(overlap_start - segment_start) * editor->char_width;
        float span_w = (float)(overlap_end - overlap_start) * editor->char_width;
        codeeditor_draw_highlight_underline(
            canvas, span_x, row_y, span_w, editor->line_height, span->color);
    }
}

static bool codeeditor_pair_info(char ch, char *out_match, int *out_direction) {
    char match = '\0';
    int direction = 0;
    switch (ch) {
        case '(':
            match = ')';
            direction = 1;
            break;
        case '[':
            match = ']';
            direction = 1;
            break;
        case '{':
            match = '}';
            direction = 1;
            break;
        case ')':
            match = '(';
            direction = -1;
            break;
        case ']':
            match = '[';
            direction = -1;
            break;
        case '}':
            match = '{';
            direction = -1;
            break;
        default:
            return false;
    }
    if (out_match)
        *out_match = match;
    if (out_direction)
        *out_direction = direction;
    return true;
}

static bool codeeditor_byte_at(const vg_codeeditor_t *editor, int line, int col, char *out_ch) {
    if (out_ch)
        *out_ch = '\0';
    if (!editor || line < 0 || line >= editor->line_count)
        return false;
    const vg_code_line_t *code_line = &editor->lines[line];
    if (!code_line->text || col < 0 || col >= (int)code_line->length)
        return false;
    if (out_ch)
        *out_ch = code_line->text[col];
    return true;
}

static bool codeeditor_find_matching_pair_forward(const vg_codeeditor_t *editor,
                                                  int line,
                                                  int col,
                                                  char opener,
                                                  char closer,
                                                  int *out_line,
                                                  int *out_col) {
    int depth = 1;
    for (int l = line; l < editor->line_count; l++) {
        const vg_code_line_t *code_line = &editor->lines[l];
        int c = l == line ? col + 1 : 0;
        while (code_line->text && c < (int)code_line->length) {
            char ch = code_line->text[c];
            if (ch == opener) {
                depth++;
            } else if (ch == closer) {
                depth--;
                if (depth == 0) {
                    if (out_line)
                        *out_line = l;
                    if (out_col)
                        *out_col = c;
                    return true;
                }
            }
            c++;
        }
    }
    return false;
}

static bool codeeditor_find_matching_pair_backward(const vg_codeeditor_t *editor,
                                                   int line,
                                                   int col,
                                                   char closer,
                                                   char opener,
                                                   int *out_line,
                                                   int *out_col) {
    int depth = 1;
    for (int l = line; l >= 0; l--) {
        const vg_code_line_t *code_line = &editor->lines[l];
        int c = l == line ? col - 1 : (int)code_line->length - 1;
        while (code_line->text && c >= 0) {
            char ch = code_line->text[c];
            if (ch == closer) {
                depth++;
            } else if (ch == opener) {
                depth--;
                if (depth == 0) {
                    if (out_line)
                        *out_line = l;
                    if (out_col)
                        *out_col = c;
                    return true;
                }
            }
            c--;
        }
    }
    return false;
}

static void codeeditor_update_pair_match_cache(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    if (editor->pair_match_cache_valid && editor->pair_match_revision == editor->revision &&
        editor->pair_match_cursor_line == editor->cursor_line &&
        editor->pair_match_cursor_col == editor->cursor_col)
        return;

    editor->pair_match_cache_valid = true;
    editor->pair_match_revision = editor->revision;
    editor->pair_match_cursor_line = editor->cursor_line;
    editor->pair_match_cursor_col = editor->cursor_col;
    editor->pair_match_active = false;
    editor->pair_anchor_line = -1;
    editor->pair_anchor_col = -1;
    editor->pair_peer_line = -1;
    editor->pair_peer_col = -1;

    int line = editor->cursor_line;
    int col = editor->cursor_col;
    char ch = '\0';
    if (!codeeditor_byte_at(editor, line, col, &ch) || !codeeditor_pair_info(ch, NULL, NULL)) {
        int prev_col = codeeditor_prev_byte_boundary(editor, line, col);
        if (prev_col == col || !codeeditor_byte_at(editor, line, prev_col, &ch) ||
            !codeeditor_pair_info(ch, NULL, NULL)) {
            return;
        }
        col = prev_col;
    }

    char match = '\0';
    int direction = 0;
    if (!codeeditor_pair_info(ch, &match, &direction))
        return;

    int peer_line = -1;
    int peer_col = -1;
    bool found = direction > 0 ? codeeditor_find_matching_pair_forward(
                                     editor, line, col, ch, match, &peer_line, &peer_col)
                               : codeeditor_find_matching_pair_backward(
                                     editor, line, col, ch, match, &peer_line, &peer_col);
    if (!found)
        return;

    editor->pair_match_active = true;
    editor->pair_anchor_line = line;
    editor->pair_anchor_col = col;
    editor->pair_peer_line = peer_line;
    editor->pair_peer_col = peer_col;
}

static void codeeditor_draw_pair_cell(vg_codeeditor_t *editor,
                                      void *canvas,
                                      int col,
                                      int segment_start,
                                      int segment_len,
                                      float content_x,
                                      float row_y,
                                      float scroll_x,
                                      bool wrapped) {
    if (!editor)
        return;
    if (wrapped && (col < segment_start || col >= segment_start + segment_len))
        return;
    float x = wrapped ? content_x + (float)(col - segment_start) * editor->char_width
                      : content_x + (float)col * editor->char_width - scroll_x;
    float y = row_y + 1.0f;
    float h = editor->line_height - 2.0f;
    if (h < 1.0f)
        h = editor->line_height;
    vgfx_fill_rect((vgfx_window_t)canvas,
                   (int32_t)x,
                   (int32_t)y,
                   (int32_t)editor->char_width,
                   (int32_t)h,
                   CODEEDITOR_PAIR_HIGHLIGHT_FILL);
    vgfx_rect((vgfx_window_t)canvas,
              (int32_t)x,
              (int32_t)y,
              (int32_t)editor->char_width,
              (int32_t)h,
              CODEEDITOR_PAIR_HIGHLIGHT_BORDER);
}

static void codeeditor_paint_pair_highlight_for_segment(vg_codeeditor_t *editor,
                                                        void *canvas,
                                                        int line,
                                                        int segment_start,
                                                        int segment_len,
                                                        float content_x,
                                                        float row_y,
                                                        float scroll_x,
                                                        bool wrapped) {
    if (!editor || !(editor->base.state & VG_STATE_FOCUSED))
        return;
    codeeditor_update_pair_match_cache(editor);
    if (!editor->pair_match_active)
        return;
    if (line == editor->pair_anchor_line) {
        codeeditor_draw_pair_cell(editor,
                                  canvas,
                                  editor->pair_anchor_col,
                                  segment_start,
                                  segment_len,
                                  content_x,
                                  row_y,
                                  scroll_x,
                                  wrapped);
    }
    if (line == editor->pair_peer_line) {
        codeeditor_draw_pair_cell(editor,
                                  canvas,
                                  editor->pair_peer_col,
                                  segment_start,
                                  segment_len,
                                  content_x,
                                  row_y,
                                  scroll_x,
                                  wrapped);
    }
}

/// @brief Grows @p line->text to hold at least @p needed bytes, doubling from
/// INITIAL_TEXT_CAPACITY.
static bool ensure_text_capacity(vg_code_line_t *line, size_t needed) {
    if (needed <= line->capacity)
        return true;

    size_t new_capacity = line->capacity;
    if (new_capacity == 0)
        new_capacity = INITIAL_TEXT_CAPACITY;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2)
            return false;
        new_capacity *= 2;
    }

    char *new_text = realloc(line->text, new_capacity);
    if (!new_text)
        return false;

    line->text = new_text;
    line->capacity = new_capacity;
    return true;
}

static int codeeditor_line_length_i32(const vg_codeeditor_t *editor, int line) {
    if (!editor || line < 0 || line >= editor->line_count)
        return 0;
    return editor->lines[line].length > (size_t)INT_MAX ? INT_MAX : (int)editor->lines[line].length;
}

/// @brief Allocates and copies @p len bytes from @p text into @p line, zero-initializing all other
/// fields.
static bool init_line_text(vg_code_line_t *line, const char *text, size_t len) {
    if (!line || len > SIZE_MAX - 1 || len > (size_t)INT_MAX)
        return false;
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return false;
    if (len > 0 && text)
        memcpy(copy, text, len);
    copy[len] = '\0';
    memset(line, 0, sizeof(*line));
    line->text = copy;
    line->length = len;
    line->capacity = len + 1;
    return true;
}

/// @brief Allocates a minimal empty text buffer for @p line if it has none; no-op if text already
/// exists.
static bool ensure_line_text_exists(vg_code_line_t *line) {
    if (!line)
        return false;
    if (line->text)
        return true;
    char *empty = (char *)malloc(1);
    if (!empty)
        return false;
    empty[0] = '\0';
    line->text = empty;
    line->length = 0;
    line->capacity = 1;
    return true;
}

/// @brief Frees the text and colors buffers of @p line and zeroes its length/capacity fields.
static void free_line(vg_code_line_t *line) {
    if (!line)
        return;
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

/// @brief Calls free_line on each of the @p count entries in @p lines, then frees the array itself.
static void free_line_array(vg_code_line_t *lines, int count) {
    if (!lines)
        return;
    for (int i = 0; i < count; i++)
        free_line(&lines[i]);
    free(lines);
}

/// @brief Returns the byte length of the well-formed UTF-8 sequence starting at @p p (1–4), or 0 on
/// error.
static size_t codeeditor_utf8_span(const char *p) {
    if (!p || *p == '\0')
        return 0;

    const unsigned char *s = (const unsigned char *)p;
    if ((s[0] & 0x80u) == 0)
        return 1;
    if ((s[0] & 0xE0u) == 0xC0u) {
        if (s[1] == '\0')
            return 0;
        if ((s[1] & 0xC0u) != 0x80u)
            return 0;
        uint32_t cp = ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
        return cp >= 0x80u ? 2u : 0u;
    }
    if ((s[0] & 0xF0u) == 0xE0u) {
        if (s[1] == '\0' || s[2] == '\0')
            return 0;
        if ((s[1] & 0xC0u) != 0x80u || (s[2] & 0xC0u) != 0x80u)
            return 0;
        uint32_t cp = ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) |
                      (uint32_t)(s[2] & 0x3Fu);
        if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu))
            return 0;
        return 3;
    }
    if ((s[0] & 0xF8u) == 0xF0u) {
        if (s[1] == '\0' || s[2] == '\0' || s[3] == '\0')
            return 0;
        if ((s[1] & 0xC0u) != 0x80u || (s[2] & 0xC0u) != 0x80u || (s[3] & 0xC0u) != 0x80u)
            return 0;
        uint32_t cp = ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
                      ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
        return (cp >= 0x10000u && cp <= 0x10FFFFu) ? 4u : 0u;
    }
    return 0;
}

/// @brief Convert an internal byte column to a UTF-8 codepoint column.
static int codeeditor_byte_col_to_char_col(const vg_codeeditor_t *editor, int line, int byte_col) {
    if (!editor || line < 0 || line >= editor->line_count)
        return 0;
    const vg_code_line_t *code_line = &editor->lines[line];
    if (byte_col <= 0)
        return 0;
    if (byte_col > (int)code_line->length)
        byte_col = (int)code_line->length;

    int chars = 0;
    size_t pos = 0;
    while (pos < (size_t)byte_col) {
        size_t span = codeeditor_utf8_span(code_line->text + pos);
        if (span == 0 || pos + span > code_line->length)
            span = 1;
        if (pos + span > (size_t)byte_col)
            break;
        pos += span;
        chars++;
    }
    return chars;
}

/// @brief Convert a public UTF-8 codepoint column to the corresponding internal byte column.
static int codeeditor_char_col_to_byte_col(const vg_codeeditor_t *editor, int line, int char_col) {
    if (!editor || line < 0 || line >= editor->line_count || char_col <= 0)
        return 0;
    const vg_code_line_t *code_line = &editor->lines[line];
    size_t pos = 0;
    int chars = 0;
    while (pos < code_line->length && chars < char_col) {
        size_t span = codeeditor_utf8_span(code_line->text + pos);
        if (span == 0 || pos + span > code_line->length)
            span = 1;
        pos += span;
        chars++;
    }
    return pos > (size_t)INT_MAX ? INT_MAX : (int)pos;
}

/// @brief Return the start byte of the UTF-8 codepoint immediately before @p byte_col.
static int codeeditor_prev_byte_boundary(const vg_codeeditor_t *editor, int line, int byte_col) {
    if (!editor || line < 0 || line >= editor->line_count || byte_col <= 0)
        return 0;
    const vg_code_line_t *code_line = &editor->lines[line];
    if (byte_col > (int)code_line->length)
        byte_col = (int)code_line->length;

    size_t pos = 0;
    while (pos < (size_t)byte_col) {
        size_t span = codeeditor_utf8_span(code_line->text + pos);
        if (span == 0 || pos + span > code_line->length)
            span = 1;
        if (pos + span >= (size_t)byte_col)
            return (int)pos;
        pos += span;
    }
    return (int)pos;
}

/// @brief Return the byte offset immediately after the UTF-8 codepoint at or after @p byte_col.
static int codeeditor_next_byte_boundary(const vg_codeeditor_t *editor, int line, int byte_col) {
    if (!editor || line < 0 || line >= editor->line_count)
        return 0;
    const vg_code_line_t *code_line = &editor->lines[line];
    if (byte_col < 0)
        byte_col = 0;
    if (byte_col >= (int)code_line->length)
        return (int)code_line->length;

    size_t pos = 0;
    while (pos < code_line->length) {
        size_t span = codeeditor_utf8_span(code_line->text + pos);
        if (span == 0 || pos + span > code_line->length)
            span = 1;
        if ((size_t)byte_col <= pos || (size_t)byte_col < pos + span)
            return (int)(pos + span);
        pos += span;
    }
    return (int)code_line->length;
}

/// @brief Frees the highlight_spans array and resets its count and capacity to zero.
static void codeeditor_clear_highlight_spans(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    free(editor->highlight_spans);
    editor->highlight_spans = NULL;
    editor->highlight_span_count = 0;
    editor->highlight_span_cap = 0;
    editor->highlight_spans_sorted = true;
    codeeditor_free_highlight_line_index(editor);
}

static void codeeditor_free_inlay_hint_storage(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    for (int i = 0; i < editor->inlay_hint_count; i++)
        free(editor->inlay_hints[i].text);
    free(editor->inlay_hints);
    editor->inlay_hints = NULL;
    editor->inlay_hint_count = 0;
    editor->inlay_hint_cap = 0;
    editor->inlay_hints_sorted = true;
}

static int codeeditor_compare_inlay_hints(const void *a, const void *b) {
    const struct vg_inlay_hint *lhs = (const struct vg_inlay_hint *)a;
    const struct vg_inlay_hint *rhs = (const struct vg_inlay_hint *)b;
    if (lhs->line != rhs->line)
        return lhs->line < rhs->line ? -1 : 1;
    if (lhs->col != rhs->col)
        return lhs->col < rhs->col ? -1 : 1;
    return 0;
}

static void codeeditor_sort_inlay_hints(vg_codeeditor_t *editor) {
    if (!editor || editor->inlay_hints_sorted)
        return;
    if (editor->inlay_hint_count > 1) {
        qsort(editor->inlay_hints,
              (size_t)editor->inlay_hint_count,
              sizeof(*editor->inlay_hints),
              codeeditor_compare_inlay_hints);
    }
    editor->inlay_hints_sorted = true;
}

static int codeeditor_first_inlay_hint_for_line(const vg_codeeditor_t *editor, int line) {
    if (!editor || !editor->inlay_hints || editor->inlay_hint_count <= 0)
        return -1;

    int lo = 0;
    int hi = editor->inlay_hint_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (editor->inlay_hints[mid].line < line)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= editor->inlay_hint_count || editor->inlay_hints[lo].line != line)
        return -1;
    return lo;
}

/// @brief Ensures the colors buffer for @p line_idx is sized, then invokes the syntax highlighter
/// callback.
static void highlight_line(vg_codeeditor_t *editor, size_t line_idx) {
    if (!editor->syntax_highlighter || line_idx >= (size_t)editor->line_count)
        return;

    vg_code_line_t *line = &editor->lines[line_idx];
    if (line->length == 0)
        return;
    if (line->colors && line->colors_capacity >= line->length &&
        line->highlight_generation == editor->highlight_generation)
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

    codeeditor_perf_add(&editor->perf_stats.line_highlight_calls, 1);
    editor->syntax_highlighter(
        (vg_widget_t *)editor, (int)line_idx, line->text, line->colors, editor->syntax_data);
    line->highlight_generation = editor->highlight_generation;
}

/// @brief Draws the substring text[start..start+len] at (x,y) using a stack buffer for short
/// slices.
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

/// @brief Draws text[start..start+len] in color runs derived from @p colors[], falling back to @p
/// fallback_color.
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

/// @brief Computes the minimum gutter width needed to display the widest line-number string plus 20
/// px padding.
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

/// @brief Returns the fold-marker gutter width (one line_height wide, clamped to
/// CODEEDITOR_FOLD_GUTTER_MIN_WIDTH).
static float codeeditor_fold_gutter_width(const vg_codeeditor_t *editor) {
    if (!editor || !editor->show_fold_gutter)
        return 0.0f;
    float width =
        editor->line_height > 0.0f ? editor->line_height : CODEEDITOR_FOLD_GUTTER_MIN_WIDTH;
    return width < CODEEDITOR_FOLD_GUTTER_MIN_WIDTH ? CODEEDITOR_FOLD_GUTTER_MIN_WIDTH : width;
}

/// @brief Returns the effective line-number gutter width, honouring line_number_width_override when
/// set.
static float codeeditor_line_number_gutter_width(const vg_codeeditor_t *editor) {
    if (!editor || !editor->show_line_numbers)
        return 0.0f;
    if (editor->line_number_width_override > 0.0f)
        return editor->line_number_width_override * editor->char_width;
    return codeeditor_auto_line_number_gutter_width(editor);
}

/// @brief Returns a const pointer to the fold region whose start_line equals @p line, or NULL if
/// none.
static const struct vg_fold_region *codeeditor_fold_region_starting_at(
    const vg_codeeditor_t *editor, int line) {
    if (!editor)
        return NULL;
    for (int i = 0; i < editor->fold_region_count; i++) {
        if (editor->fold_regions[i].start_line == line)
            return &editor->fold_regions[i];
    }
    return NULL;
}

/// @brief Returns a mutable pointer to the fold region whose start_line equals @p line, or NULL if
/// none.
static struct vg_fold_region *codeeditor_fold_region_starting_at_mut(vg_codeeditor_t *editor,
                                                                     int line) {
    if (!editor)
        return NULL;
    for (int i = 0; i < editor->fold_region_count; i++) {
        if (editor->fold_regions[i].start_line == line)
            return &editor->fold_regions[i];
    }
    return NULL;
}

/// @brief Returns true if @p line falls inside a currently folded region (i.e., it is not visible).
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

/// @brief Returns the nearest visible line at or above @p line, snapping hidden lines to their
/// fold's start_line.
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

/// @brief Returns the index of the next visible (non-hidden) line after @p line, or line_count if
/// none.
static int codeeditor_next_visible_line(const vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return -1;

    int next = line + 1;
    bool changed = true;
    while (changed && next < editor->line_count) {
        changed = false;
        for (int i = 0; i < editor->fold_region_count; i++) {
            const struct vg_fold_region *region = &editor->fold_regions[i];
            if (!region->folded)
                continue;
            if (line == region->start_line && next <= region->end_line) {
                next = region->end_line + 1;
                changed = true;
            } else if (next > region->start_line && next <= region->end_line) {
                next = region->end_line + 1;
                changed = true;
            }
        }
    }
    return next < editor->line_count ? next : editor->line_count;
}

/// @brief Returns the index of the previous visible line before @p line, or -1 if none exists.
VG_UNUSED static int codeeditor_prev_visible_line(const vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return -1;
    int prev = line - 1;
    bool changed = true;
    while (changed && prev >= 0) {
        changed = false;
        for (int i = 0; i < editor->fold_region_count; i++) {
            const struct vg_fold_region *region = &editor->fold_regions[i];
            if (!region->folded)
                continue;
            if (prev > region->start_line && prev <= region->end_line) {
                prev = region->start_line;
                changed = true;
            }
        }
    }
    return prev >= 0 ? prev : -1;
}

/// @brief Recomputes editor->gutter_width as the sum of the line-number and fold-gutter widths.
static void update_gutter_width(vg_codeeditor_t *editor) {
    if (!editor) {
        return;
    }
    editor->gutter_width =
        codeeditor_line_number_gutter_width(editor) + codeeditor_fold_gutter_width(editor);
}

/// @brief Returns the number of characters that fit per wrap row given @p content_width; 0 if
/// word_wrap is off.
static int codeeditor_chars_per_row(const vg_codeeditor_t *editor, float content_width) {
    if (!editor || !editor->word_wrap || editor->char_width <= 0.0f)
        return 0;
    if (content_width <= 0.0f)
        return 1;
    int chars = (int)(content_width / editor->char_width);
    return chars > 0 ? chars : 1;
}

/// @brief Returns the number of wrapped rows needed to display @p line given @p content_width
/// (minimum 1).
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
    size_t rows = (len + (size_t)chars_per_row - 1) / (size_t)chars_per_row;
    return rows > (size_t)INT_MAX ? INT_MAX : (int)rows;
}

/// @brief Returns the visual row count for @p line (0 if hidden, 1 if no word-wrap, else wrapped
/// row count).
static int codeeditor_visual_rows_for_line(const vg_codeeditor_t *editor,
                                           int line,
                                           float content_width) {
    if (!editor || line < 0 || line >= editor->line_count ||
        codeeditor_line_is_hidden(editor, line))
        return 0;
    if (!editor->word_wrap)
        return 1;
    return codeeditor_wrapped_rows_for_line(editor, line, content_width);
}

static bool codeeditor_layout_cache_ensure(const vg_codeeditor_t *editor, float content_width) {
    if (!editor)
        return false;

    vg_codeeditor_t *mutable_editor = (vg_codeeditor_t *)editor;
    if (mutable_editor->layout_cache_valid &&
        mutable_editor->layout_cache_generation == mutable_editor->layout_generation &&
        mutable_editor->layout_cache_content_width == content_width &&
        mutable_editor->layout_cache_word_wrap == mutable_editor->word_wrap &&
        mutable_editor->layout_cache_line_count == mutable_editor->line_count)
        return true;

    int needed = mutable_editor->line_count + 1;
    if (needed < 1)
        needed = 1;
    if (mutable_editor->layout_cache_capacity < needed) {
        int *rows = realloc(mutable_editor->layout_cache_prefix_rows, (size_t)needed * sizeof(int));
        if (!rows)
            return false;
        mutable_editor->layout_cache_prefix_rows = rows;
        mutable_editor->layout_cache_capacity = needed;
    }

    int total_rows = 0;
    mutable_editor->layout_cache_prefix_rows[0] = 0;
    for (int i = 0; i < mutable_editor->line_count; i++) {
        int line_rows = codeeditor_visual_rows_for_line(mutable_editor, i, content_width);
        if (line_rows < 0)
            line_rows = 0;
        if (line_rows > INT_MAX - total_rows)
            total_rows = INT_MAX;
        else
            total_rows += line_rows;
        mutable_editor->layout_cache_prefix_rows[i + 1] = total_rows;
    }

    mutable_editor->layout_cache_valid = true;
    mutable_editor->layout_cache_generation = mutable_editor->layout_generation;
    mutable_editor->layout_cache_content_width = content_width;
    mutable_editor->layout_cache_word_wrap = mutable_editor->word_wrap;
    mutable_editor->layout_cache_line_count = mutable_editor->line_count;
    mutable_editor->layout_cache_total_visual_rows = total_rows;
    mutable_editor->layout_cache_total_height = (float)total_rows * mutable_editor->line_height;
    return true;
}

static int codeeditor_cached_row_count_for_line(const vg_codeeditor_t *editor,
                                                float content_width,
                                                int line) {
    if (!editor || line < 0 || line >= editor->line_count)
        return 0;
    if (!codeeditor_layout_cache_ensure(editor, content_width))
        return codeeditor_visual_rows_for_line(editor, line, content_width);
    return editor->layout_cache_prefix_rows[line + 1] - editor->layout_cache_prefix_rows[line];
}

static int codeeditor_last_visible_line_from_cache(const vg_codeeditor_t *editor,
                                                   float content_width) {
    if (!editor || editor->line_count <= 0)
        return 0;
    if (!codeeditor_layout_cache_ensure(editor, content_width))
        return codeeditor_visible_anchor_line(editor, editor->line_count - 1);
    for (int line = editor->line_count - 1; line >= 0; line--) {
        if (editor->layout_cache_prefix_rows[line + 1] > editor->layout_cache_prefix_rows[line])
            return line;
    }
    return 0;
}

/// @brief Returns the total document height in pixels for the given @p content_width (sums all
/// visual rows).
static float codeeditor_total_content_height_for_width(const vg_codeeditor_t *editor,
                                                       float content_width) {
    if (!editor)
        return 0.0f;

    if (!editor->word_wrap && !codeeditor_has_hidden_lines(editor))
        return (float)editor->line_count * editor->line_height;

    if (codeeditor_layout_cache_ensure(editor, content_width))
        return editor->layout_cache_total_height;

    float total_rows = 0.0f;
    for (int i = 0; i < editor->line_count; i++) {
        codeeditor_perf_add(&((vg_codeeditor_t *)editor)->perf_stats.total_height_linear_scans, 1);
        total_rows += (float)codeeditor_visual_rows_for_line(editor, i, content_width);
    }
    return total_rows * editor->line_height;
}

/// @brief Computes the drawable content width without triggering document-height scans.
static float codeeditor_content_draw_width(const vg_codeeditor_t *editor,
                                           const vg_widget_t *widget) {
    if (!editor || !widget)
        return 0.0f;

    float base_width = widget->width - editor->gutter_width;
    if (base_width < 0.0f)
        base_width = 0.0f;
    if (!editor->word_wrap)
        return base_width;

    float minimum_height = (float)editor->line_count * editor->line_height;
    float content_width =
        base_width - ((minimum_height > widget->height) ? CODEEDITOR_SCROLLBAR_WIDTH : 0.0f);
    return content_width > 0.0f ? content_width : 0.0f;
}

/// @brief Returns total document height using the resolved content draw width for the given widget
/// bounds.
static float codeeditor_total_content_height(const vg_codeeditor_t *editor,
                                             const vg_widget_t *widget) {
    return codeeditor_total_content_height_for_width(editor,
                                                     codeeditor_content_draw_width(editor, widget));
}

/// @brief Returns the total count of visual rows across all lines for @p content_width (used for
/// scroll math).
static int codeeditor_total_visual_rows_for_width(const vg_codeeditor_t *editor,
                                                  float content_width) {
    if (!editor)
        return 0;

    if (!editor->word_wrap && !codeeditor_has_hidden_lines(editor))
        return editor->line_count;

    if (codeeditor_layout_cache_ensure(editor, content_width))
        return editor->layout_cache_total_visual_rows;

    int total_rows = 0;
    for (int i = 0; i < editor->line_count; i++) {
        codeeditor_perf_add(&((vg_codeeditor_t *)editor)->perf_stats.total_visual_row_linear_scans,
                            1);
        total_rows += codeeditor_visual_rows_for_line(editor, i, content_width);
    }
    return total_rows;
}

/// @brief Returns the maximum valid scroll_y value, with substantial empty
///        space below the last line so the user can scroll the document end up
///        to roughly the middle of the viewport (the scrollBeyondLastLine UX
///        used by VS Code, Sublime, Atom). Without this, the last few lines
///        are uncomfortable to read because they're pinned at the viewport's
///        bottom edge.
///
///        Padding is half the viewport, with a minimum of one line so single-
///        line viewports don't lose any scroll allowance.
static float codeeditor_max_scroll_y(const vg_codeeditor_t *editor, const vg_widget_t *widget) {
    if (!editor || !widget)
        return 0.0f;
    float padding = widget->height * 0.5f;
    if (padding < editor->line_height)
        padding = editor->line_height;
    float max_scroll = codeeditor_total_content_height(editor, widget) - widget->height + padding;
    return max_scroll > 0.0f ? max_scroll : 0.0f;
}

/// @brief Decomposes (line, col) into a wrap-row index within the line and a column within that
/// row.
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

/// @brief Returns the absolute visual row index (across all lines) for document position (line,
/// col).
static int codeeditor_visual_row_for_position(const vg_codeeditor_t *editor,
                                              float content_width,
                                              int line,
                                              int col) {
    if (!editor)
        return 0;
    if (!editor->word_wrap && !codeeditor_has_hidden_lines(editor))
        return codeeditor_clamp_line_index(editor, line);
    if (line < 0)
        line = 0;
    if (line >= editor->line_count)
        line = editor->line_count - 1;
    line = codeeditor_visible_anchor_line(editor, line);

    if (codeeditor_layout_cache_ensure(editor, content_width)) {
        int visual_row = editor->layout_cache_prefix_rows[line];
        int wrapped_row = 0;
        codeeditor_visual_offset_for_position(editor, content_width, line, col, &wrapped_row, NULL);
        return visual_row + wrapped_row;
    }

    int visual_row = 0;
    for (int i = 0; i < line; i++) {
        codeeditor_perf_add(&((vg_codeeditor_t *)editor)->perf_stats.visual_row_linear_scans, 1);
        visual_row += codeeditor_visual_rows_for_line(editor, i, content_width);
    }

    int wrapped_row = 0;
    codeeditor_visual_offset_for_position(editor, content_width, line, col, &wrapped_row, NULL);
    visual_row += wrapped_row;
    return visual_row;
}

/// @brief Resolves an absolute @p visual_row index to the source line and wrap-row offset within
/// that line.
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

    if (!editor->word_wrap && !codeeditor_has_hidden_lines(editor)) {
        if (out_line)
            *out_line = codeeditor_clamp_line_index(editor, visual_row);
        if (out_row_in_line)
            *out_row_in_line = 0;
        return;
    }

    if (codeeditor_layout_cache_ensure(editor, content_width)) {
        int total_rows = editor->layout_cache_total_visual_rows;
        if (total_rows <= 0) {
            if (out_line)
                *out_line = 0;
            if (out_row_in_line)
                *out_row_in_line = 0;
            return;
        }
        if (visual_row >= total_rows)
            visual_row = total_rows - 1;

        int lo = 0;
        int hi = editor->line_count - 1;
        int found = codeeditor_last_visible_line_from_cache(editor, content_width);
        while (lo <= hi) {
            int mid = lo + (hi - lo) / 2;
            int start = editor->layout_cache_prefix_rows[mid];
            int end = editor->layout_cache_prefix_rows[mid + 1];
            if (visual_row < start) {
                hi = mid - 1;
            } else if (visual_row >= end || end == start) {
                lo = mid + 1;
            } else {
                found = mid;
                break;
            }
        }
        if (out_line)
            *out_line = found;
        if (out_row_in_line)
            *out_row_in_line = visual_row - editor->layout_cache_prefix_rows[found];
        return;
    }

    int accumulated = 0;
    for (int line = 0; line < editor->line_count; line++) {
        codeeditor_perf_add(&((vg_codeeditor_t *)editor)->perf_stats.locate_visual_row_linear_scans,
                            1);
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
        *out_line = codeeditor_last_visible_line_from_cache(editor, content_width);
    if (out_row_in_line) {
        int last_rows = codeeditor_visual_rows_for_line(
            editor, codeeditor_last_visible_line_from_cache(editor, content_width), content_width);
        *out_row_in_line = last_rows > 0 ? last_rows - 1 : 0;
    }
}

/// @brief Clamps scroll_x to 0 in word-wrap mode and clamps scroll_y to [0, max_scroll_y].
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

/// @brief Computes scrollbar thumb geometry; returns false if content fits without scrolling.
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

/// @brief Converts a local-space click point (local_x, local_y) to a document (line, col) position.
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
        int visual_row = editor->line_height > 0.0f ? (int)(doc_y / editor->line_height) : 0;
        int line = 0;
        int row_in_line = 0;
        codeeditor_locate_visual_row(editor, content_width, visual_row, &line, &row_in_line);

        float content_local_x = local_x - editor->gutter_width;
        if (content_local_x < 0.0f)
            content_local_x = 0.0f;
        int col_in_row =
            editor->char_width > 0.0f ? (int)(content_local_x / editor->char_width + 0.5f) : 0;
        if (col_in_row < 0)
            col_in_row = 0;
        int col = row_in_line * chars_per_row + col_in_row;
        col = codeeditor_char_col_to_byte_col(editor, line, col);
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
    codeeditor_locate_visual_row(
        editor, codeeditor_content_draw_width(editor, widget), visual_row, &line, NULL);

    int col = editor->char_width > 0.0f ? (int)(content_local_x / editor->char_width + 0.5f) : 0;
    if (col < 0)
        col = 0;
    col = codeeditor_char_col_to_byte_col(editor, line, col);
    if (out_line)
        *out_line = line;
    if (out_col)
        *out_col = col;
}

/// @brief Moves the cursor up or down by @p visual_rows visual rows, preserving the column position
/// within a wrap row.
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

    int visual_row = codeeditor_visual_row_for_position(
        editor, content_width, editor->cursor_line, editor->cursor_col);
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
    int target_line_len = codeeditor_line_length_i32(editor, target_line);
    if (target_col > target_line_len)
        target_col = target_line_len;

    editor->cursor_line = target_line;
    editor->cursor_col = target_col;
}

/// @brief Adjusts scroll_y so the cursor's visual row is within the visible viewport.
static void ensure_cursor_visible(vg_codeeditor_t *editor) {
    if (!editor)
        return;

    float visible_height = editor->base.height;
    float content_width = codeeditor_content_draw_width(editor, &editor->base);
    int cursor_visual_row = codeeditor_visual_row_for_position(
        editor, content_width, editor->cursor_line, editor->cursor_col);
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

/// @brief Recalculate gutter width, clamp cursors to visible lines, clamp scroll, and
///        mark the widget for re-layout and repaint.
///
/// @details Call this after any structural change (fold toggle, text replacement,
///          font change) that may affect layout metrics.
///
/// @param editor The code editor to refresh.
void vg_codeeditor_refresh_layout_state(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_bump_layout_generation(editor);
    codeeditor_refresh_fold_summary(editor);
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

/// @brief Allocates and zero-initializes a new edit history with HISTORY_INITIAL_CAPACITY operation
/// slots.
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

/// @brief Frees the old_text and new_text strings owned by @p op, then frees the op itself.
static void edit_op_destroy(vg_edit_op_t *op) {
    if (!op)
        return;
    free(op->old_text);
    free(op->new_text);
    free(op);
}

/// @brief Destroys all operations in @p history and frees the history struct itself.
static void edit_history_destroy(vg_edit_history_t *history) {
    if (!history)
        return;

    for (size_t i = 0; i < history->count; i++) {
        edit_op_destroy(history->operations[i]);
    }
    free(history->operations);
    free(history);
}

/// @brief Destroys all operations in @p history and resets count and current_index to 0 without
/// freeing the struct.
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
    history->is_grouping = false;
    history->current_group = 0;
}

/// @brief Discards any redo tail, grows the operation array if needed, and appends @p op; sets its
/// group_id if grouping.
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

/// @brief Decrements current_index and returns the operation to be undone, or NULL if at the
/// beginning.
static vg_edit_op_t *edit_history_pop_undo(vg_edit_history_t *history) {
    if (!history || history->current_index == 0)
        return NULL;
    history->current_index--;
    return history->operations[history->current_index];
}

/// @brief Returns the operation at current_index-1 without advancing the index, or NULL if empty.
static vg_edit_op_t *edit_history_peek_undo(vg_edit_history_t *history) {
    if (!history || history->current_index == 0)
        return NULL;
    return history->operations[history->current_index - 1];
}

/// @brief Returns the operation at current_index and advances it, enabling a redo step; NULL if at
/// end.
static vg_edit_op_t *edit_history_pop_redo(vg_edit_history_t *history) {
    if (!history || history->current_index >= history->count)
        return NULL;
    vg_edit_op_t *op = history->operations[history->current_index];
    history->current_index++;
    return op;
}

/// @brief Begins an edit group so that subsequent pushed operations share a group_id for atomic
/// undo.
VG_UNUSED
static void edit_history_begin_group(vg_edit_history_t *history) {
    if (!history)
        return;
    history->is_grouping = true;
    history->current_group = history->next_group_id++;
}

/// @brief Ends the current edit group, clearing is_grouping and current_group on @p history.
VG_UNUSED
static void edit_history_end_group(vg_edit_history_t *history) {
    if (!history)
        return;
    history->is_grouping = false;
    history->current_group = 0;
}

/// @brief Rewrite the cursor-after position of the latest undo operation.
/// @details Used by paired-character insertion: the edit still owns the full
///          inserted range, but redo should leave the cursor between the pair.
static void edit_history_update_latest_cursor_after(vg_edit_history_t *history, int line, int col) {
    if (!history || history->current_index == 0 || history->current_index != history->count)
        return;
    vg_edit_op_t *op = history->operations[history->current_index - 1];
    if (!op || op->group_id != 0)
        return;
    op->cursor_line_after = line;
    op->cursor_col_after = col;
}

/// @brief Allocates a new vg_edit_op_t, strdup'ing old_text and new_text, with group_id initialised
/// to 0.
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
    if ((old_text && !op->old_text) || (new_text && !op->new_text)) {
        edit_op_destroy(op);
        return NULL;
    }
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

/// @brief Reads editor->selection and writes a canonically ordered start/end into the four
/// out-params.
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

/// @brief Swaps the start/end pair in-place so that start always precedes end in document order.
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

/// @brief Extracts and returns a heap-allocated copy of the text in [start_line:start_col,
/// end_line:end_col).
static char *copy_text_range_len(vg_codeeditor_t *editor,
                                 int start_line,
                                 int start_col,
                                 int end_line,
                                 int end_col,
                                 size_t *out_len) {
    if (out_len)
        *out_len = 0;
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
    if (out_len)
        *out_len = total_len;
    return result;
}

/// @brief Extracts and returns a heap-allocated copy of the text in [start_line:start_col,
/// end_line:end_col).
static char *copy_text_range(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    return copy_text_range_len(editor, start_line, start_col, end_line, end_col, NULL);
}

/// @brief Clears the has_selection flag on all extra (non-primary) cursors.
static void clear_extra_cursor_selections(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    for (int i = 0; i < editor->extra_cursor_count; i++)
        editor->extra_cursors[i].has_selection = false;
}

/// @brief Clamps @p *line to [0, line_count-1] without touching the column (used for selection
/// anchors).
static void clamp_editor_line(vg_codeeditor_t *editor, int *line) {
    if (!editor || !line || editor->line_count <= 0)
        return;
    if (*line < 0)
        *line = 0;
    if (*line >= editor->line_count)
        *line = editor->line_count - 1;
}

/// @brief Clamps @p *col to [0, lines[line].length] for an already-validated @p line index.
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

/// @brief Clamps both line and col atomically; note that clamping the line may implicitly constrain
/// the col.
static void clamp_editor_position(vg_codeeditor_t *editor, int *line, int *col) {
    if (!editor || !line || !col || editor->line_count <= 0)
        return;
    clamp_editor_line(editor, line);
    clamp_editor_col(editor, *line, col);
}

/// @brief Add a secondary cursor unless one already exists at the same position.
static void codeeditor_add_extra_cursor_at(vg_codeeditor_t *editor, int line, int col) {
    if (!editor || editor->line_count <= 0)
        return;
    clamp_editor_position(editor, &line, &col);
    if (editor->cursor_line == line && editor->cursor_col == col)
        return;
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        if (editor->extra_cursors[i].line == line && editor->extra_cursors[i].col == col)
            return;
    }
    if (editor->extra_cursor_count >= editor->extra_cursor_cap) {
        if (editor->extra_cursor_cap > INT_MAX / 2)
            return;
        int new_cap = editor->extra_cursor_cap ? editor->extra_cursor_cap * 2 : 4;
        void *p = realloc(editor->extra_cursors, (size_t)new_cap * sizeof(*editor->extra_cursors));
        if (!p)
            return;
        editor->extra_cursors = p;
        editor->extra_cursor_cap = new_cap;
    }
    struct vg_extra_cursor *cursor = &editor->extra_cursors[editor->extra_cursor_count++];
    cursor->line = line;
    cursor->col = col;
    memset(&cursor->selection, 0, sizeof(cursor->selection));
    cursor->has_selection = false;
}

/// @brief Clamps (line, col) to valid bounds and then snaps the line to its fold's visible anchor.
static void codeeditor_clamp_cursor_to_visible(vg_codeeditor_t *editor, int *line, int *col) {
    if (!editor || !line || !col || editor->line_count <= 0)
        return;
    clamp_editor_position(editor, line, col);
    *line = codeeditor_visible_anchor_line(editor, *line);
    clamp_editor_col(editor, *line, col);
}

/// @brief Snaps all cursors (primary and extra) off hidden lines after a fold toggle or line
/// deletion.
static void codeeditor_adjust_hidden_cursors(vg_codeeditor_t *editor) {
    if (!editor || editor->line_count <= 0)
        return;

    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        codeeditor_clamp_cursor_to_visible(
            editor, &editor->extra_cursors[i].line, &editor->extra_cursors[i].col);
    }
}

/// @brief Returns -1, 0, or 1 for lhs < rhs, lhs == rhs, or lhs > rhs in document order.
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

/// @brief qsort comparator that orders edit targets in descending document position (last target
/// first).
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

/// @brief qsort comparator that orders edit targets in ascending document position (first target
/// first).
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

/// @brief Updates the cursor at @p cursor_id (0 = primary, 1+ = extra) to (line, col).
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

/// @brief Clears the selection flag for the cursor identified by @p cursor_id.
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

/// @brief Appends a normalized, de-duplicated edit target to @p targets[]; no-op if an identical
/// range exists.
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

/// @brief Creates an edit operation and pushes it onto the undo history; no-op if history is NULL.
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

/// @brief Applies @p replacement_text at each edit target in descending order, recording undo ops,
/// then updates cursor positions.
static void apply_edit_targets(vg_codeeditor_t *editor,
                               vg_edit_target_t *targets,
                               int target_count,
                               const char *replacement_text) {
    if (!editor || target_count <= 0)
        return;

    const int cursor_count = 1 + editor->extra_cursor_count;
    int *new_lines = malloc((size_t)cursor_count * sizeof(int));
    int *new_cols = malloc((size_t)cursor_count * sizeof(int));
    vg_edit_target_t *clamped_targets = calloc((size_t)target_count, sizeof(vg_edit_target_t));
    char **old_texts = calloc((size_t)target_count, sizeof(char *));
    bool *has_old_texts = calloc((size_t)target_count, sizeof(bool));
    if (!new_lines || !new_cols || !clamped_targets || !old_texts || !has_old_texts) {
        free(new_lines);
        free(new_cols);
        free(clamped_targets);
        free(old_texts);
        free(has_old_texts);
        return;
    }

    new_lines[0] = editor->cursor_line;
    new_cols[0] = editor->cursor_col;
    for (int i = 0; i < editor->extra_cursor_count; i++) {
        new_lines[i + 1] = editor->extra_cursors[i].line;
        new_cols[i + 1] = editor->extra_cursors[i].col;
    }

    for (int i = 0; i < target_count; i++) {
        clamped_targets[i] = targets[i];
        clamp_editor_position(
            editor, &clamped_targets[i].start_line, &clamped_targets[i].start_col);
        clamp_editor_position(editor, &clamped_targets[i].end_line, &clamped_targets[i].end_col);
        normalize_selection_range(&clamped_targets[i].start_line,
                                  &clamped_targets[i].start_col,
                                  &clamped_targets[i].end_line,
                                  &clamped_targets[i].end_col);
        has_old_texts[i] = compare_positions(clamped_targets[i].start_line,
                                             clamped_targets[i].start_col,
                                             clamped_targets[i].end_line,
                                             clamped_targets[i].end_col) != 0;
        if (has_old_texts[i]) {
            old_texts[i] = copy_text_range(editor,
                                           clamped_targets[i].start_line,
                                           clamped_targets[i].start_col,
                                           clamped_targets[i].end_line,
                                           clamped_targets[i].end_col);
            if (!old_texts[i])
                goto cleanup;
        }
    }

    if (editor->history && target_count > 1)
        edit_history_begin_group(editor->history);

    bool changed = false;
    bool syntax_global_dirty = false;
    for (int i = 0; i < target_count; i++) {
        const vg_edit_target_t *target = &clamped_targets[i];
        char *old_text = old_texts[i];
        int history_end_line = target->end_line;
        int history_end_col = target->end_col;
        const bool has_old_text = has_old_texts[i];
        syntax_global_dirty = syntax_global_dirty ||
                              codeeditor_text_may_affect_multiline_syntax(old_text) ||
                              codeeditor_text_may_affect_multiline_syntax(replacement_text);

        if (has_old_text) {
            delete_text_range_internal(
                editor, target->start_line, target->start_col, target->end_line, target->end_col);
            changed = true;
        } else {
            editor->cursor_line = target->start_line;
            editor->cursor_col = target->start_col;
        }

        if (replacement_text && replacement_text[0] != '\0') {
            insert_text_at_internal(
                editor, target->start_line, target->start_col, replacement_text);
            changed = true;
        }

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
    }

cleanup:
    if (editor->history && target_count > 1)
        edit_history_end_group(editor->history);

    for (int i = 0; i < cursor_count; i++)
        set_cursor_position_by_id(editor, i, new_lines[i], new_cols[i]);

    free(new_lines);
    free(new_cols);
    for (int i = 0; i < target_count; i++)
        free(old_texts[i]);
    free(clamped_targets);
    free(old_texts);
    free(has_old_texts);
    if (!changed)
        return;
    codeeditor_bump_revision(editor);
    if (syntax_global_dirty)
        codeeditor_invalidate_all_highlights(editor);
    editor->modified = true;
    vg_codeeditor_refresh_layout_state(editor);
    ensure_cursor_visible(editor);
    editor->base.needs_paint = true;
}

//=============================================================================
// CodeEditor Implementation
//=============================================================================

/// @brief Create a code editor widget with a single empty line and default theme colours.
///
/// @details The editor supports syntax highlighting (set via vg_codeeditor_set_syntax),
///          word wrap, line numbers, folding gutter, and a scrollbar.  All features
///          are disabled or hidden by default; enable them via the corresponding setters.
///
/// @param parent Widget to attach to as a child (may be NULL).
/// @return Newly allocated vg_codeeditor_t, or NULL on allocation failure.
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
        vg_widget_destroy(&editor->base);
        return NULL;
    }
    editor->line_capacity = INITIAL_LINE_CAPACITY;

    // Create first empty line
    editor->line_count = 1;
    editor->lines[0].text = malloc(INITIAL_TEXT_CAPACITY);
    if (!editor->lines[0].text) {
        vg_widget_destroy(&editor->base);
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
    editor->gutter_clicked_line = -1;
    editor->gutter_clicked_slot = -1;

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
    editor->revision = 1;
    editor->highlight_generation = 1;
    editor->layout_generation = 1;
    editor->has_folded_lines = false;
    editor->layout_cache_valid = false;
    editor->layout_cache_generation = 0;
    editor->layout_cache_content_width = 0.0f;
    editor->layout_cache_word_wrap = false;
    editor->layout_cache_line_count = 0;
    editor->layout_cache_total_visual_rows = 0;
    editor->layout_cache_total_height = 0.0f;
    editor->layout_cache_prefix_rows = NULL;
    editor->layout_cache_capacity = 0;
    editor->highlight_spans_sorted = true;
    editor->highlight_line_index_valid = false;
    editor->highlight_line_index_line_count = 0;
    editor->highlight_line_index_span_count = 0;
    editor->highlight_line_offsets = NULL;
    editor->highlight_line_offsets_cap = 0;
    editor->highlight_line_span_indices = NULL;
    editor->highlight_line_span_indices_cap = 0;
    editor->inlay_hints_sorted = true;

    // Create undo/redo history
    editor->history = edit_history_create();
    if (!editor->history) {
        vg_widget_destroy(&editor->base);
        return NULL;
    }

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &editor->base);
    }

    return editor;
}

/// @brief VTable destroy: frees all lines, history, custom keywords, highlight spans, gutter icons,
/// and fold regions.
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

    codeeditor_clear_highlight_spans(editor);
    codeeditor_free_inlay_hint_storage(editor);

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
    editor->has_folded_lines = false;

    free(editor->layout_cache_prefix_rows);
    editor->layout_cache_prefix_rows = NULL;
    editor->layout_cache_capacity = 0;
    editor->layout_cache_valid = false;

    free(editor->extra_cursors);
    editor->extra_cursors = NULL;
    editor->extra_cursor_count = 0;
    editor->extra_cursor_cap = 0;
}

/// @brief VTable measure: fills all available space, clamped to min_width/min_height constraints.
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

/// @brief Blits a gutter icon's RGBA pixel data into the framebuffer at (dst_x, dst_y) with
/// clipping applied.
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

/// @brief Draws a '+' or '-' fold marker in the fold gutter column for the region starting at @p
/// line.
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
    vg_font_draw_text(canvas,
                      editor->font,
                      editor->font_size,
                      marker_x,
                      marker_y,
                      marker,
                      editor->line_number_color);
}

/// @brief Draws a "..." continuation marker after the last visible character of a folded region.
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
    vg_font_draw_text(canvas,
                      editor->font,
                      editor->font_size,
                      ellipsis_x,
                      baseline_y,
                      "...",
                      editor->line_number_color);
}

/// @brief Draws an editor highlight span as an underline so diagnostics do not obscure the text.
static void codeeditor_draw_highlight_underline(
    void *canvas, float x, float y, float width, float line_height, uint32_t color) {
    if (!canvas || width <= 0.0f || line_height <= 0.0f)
        return;

    float underline_height = line_height >= 8.0f ? 2.0f : 1.0f;
    float underline_y = y + line_height - underline_height - 1.0f;
    if (underline_y < y)
        underline_y = y;

    int32_t draw_width = (int32_t)(width + 0.5f);
    int32_t draw_height = (int32_t)(underline_height + 0.5f);
    if (draw_width < 1)
        draw_width = 1;
    if (draw_height < 1)
        draw_height = 1;

    vgfx_fill_rect((vgfx_window_t)canvas,
                   (int32_t)x,
                   (int32_t)underline_y,
                   draw_width,
                   draw_height,
                   (vgfx_color_t)color);
}

static void codeeditor_paint_inlay_hints_for_segment(vg_codeeditor_t *editor,
                                                     void *canvas,
                                                     int line,
                                                     int segment_start,
                                                     int segment_len,
                                                     float content_x,
                                                     float row_y,
                                                     float scroll_x,
                                                     float baseline_y,
                                                     bool wrapped) {
    if (!editor || !canvas || !editor->font || editor->inlay_hint_count <= 0)
        return;

    int line_len = 0;
    if (line >= 0 && line < editor->line_count)
        line_len = (int)editor->lines[line].length;
    int segment_end = segment_start + segment_len;
    float hint_size = editor->font_size * 0.9f;
    if (hint_size <= 0.0f)
        hint_size = editor->font_size;

    int first_hint = codeeditor_first_inlay_hint_for_line(editor, line);
    if (first_hint < 0)
        return;

    for (int i = first_hint; i < editor->inlay_hint_count; i++) {
        const struct vg_inlay_hint *hint = &editor->inlay_hints[i];
        if (hint->line != line)
            break;
        if (!hint->text || hint->text[0] == '\0')
            continue;

        int col = hint->col;
        if (col < 0)
            col = 0;
        if (col > line_len)
            col = line_len;

        if (wrapped) {
            bool at_segment_end = col == line_len && line_len <= segment_end;
            if ((col < segment_start || col > segment_end) && !at_segment_end)
                continue;
        }

        int visual_col = wrapped ? col - segment_start : col;
        if (visual_col < 0)
            visual_col = 0;
        float x = content_x + (float)visual_col * editor->char_width - scroll_x + 4.0f;
        vg_text_metrics_t metrics = {0};
        vg_font_measure_text(editor->font, hint_size, hint->text, &metrics);
        if (metrics.width > 0.0f) {
            uint32_t bg = (editor->current_line_bg & 0x00FFFFFFu) | 0x66000000u;
            vgfx_fill_rect((vgfx_window_t)canvas,
                           (int32_t)(x - 2.0f),
                           (int32_t)(row_y + 2.0f),
                           (int32_t)(metrics.width + 4.0f),
                           (int32_t)(editor->line_height - 4.0f),
                           bg);
        }
        vg_font_draw_text(canvas, editor->font, hint_size, x, baseline_y, hint->text, hint->color);
    }
}

/// @brief VTable paint: renders background, gutter, current-line highlight, selection,
/// syntax-coloured text, cursor, and scrollbar.
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
    codeeditor_locate_visual_row(
        editor, content_width, first_visual_row, &editor->visible_first_line, NULL);
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
    codeeditor_sort_highlight_spans(editor);
    codeeditor_sort_inlay_hints(editor);
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

            codeeditor_paint_highlights_for_line(editor,
                                                 canvas,
                                                 i,
                                                 0,
                                                 (int)editor->lines[i].length,
                                                 content_x,
                                                 line_y,
                                                 editor->scroll_x,
                                                 false);
            codeeditor_paint_pair_highlight_for_segment(editor,
                                                        canvas,
                                                        i,
                                                        0,
                                                        (int)editor->lines[i].length,
                                                        content_x,
                                                        line_y,
                                                        editor->scroll_x,
                                                        false);

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
                codeeditor_draw_fold_ellipsis(editor,
                                              canvas,
                                              content_x - editor->scroll_x,
                                              text_y,
                                              i,
                                              (int)editor->lines[i].length);
            }
            codeeditor_paint_inlay_hints_for_segment(editor,
                                                     canvas,
                                                     i,
                                                     0,
                                                     (int)editor->lines[i].length,
                                                     content_x,
                                                     line_y,
                                                     editor->scroll_x,
                                                     line_y + font_metrics.ascent,
                                                     false);

            line_y += editor->line_height;
        }

        if ((widget->state & VG_STATE_FOCUSED) && editor->cursor_visible && !editor->read_only) {
            int visible_cursor_line = codeeditor_visual_row_for_position(
                editor, content_width, editor->cursor_line, editor->cursor_col);
            float cursor_y =
                widget->y + (float)visible_cursor_line * editor->line_height - editor->scroll_y;
            if (cursor_y + editor->line_height > widget->y &&
                cursor_y < widget->y + widget->height) {
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
                int visible_extra_line =
                    codeeditor_visual_row_for_position(editor,
                                                       content_width,
                                                       editor->extra_cursors[c].line,
                                                       editor->extra_cursors[c].col);
                float cursor_x = content_x + editor->extra_cursors[c].col * editor->char_width -
                                 editor->scroll_x;
                float cursor_y =
                    widget->y + (float)visible_extra_line * editor->line_height - editor->scroll_y;
                if (cursor_y + editor->line_height <= widget->y ||
                    cursor_y >= widget->y + widget->height)
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
                int row_count = codeeditor_cached_row_count_for_line(editor, content_width, line);
                if (row_count == 0) {
                    line = codeeditor_next_visible_line(editor, line);
                    row_in_line = 0;
                    continue;
                }
                int seg_start = chars_per_row > 0 ? row_in_line * chars_per_row : 0;
                size_t remaining = line_len > (size_t)seg_start ? line_len - (size_t)seg_start : 0;
                int seg_len = chars_per_row > 0 ? (int)((remaining < (size_t)chars_per_row)
                                                            ? remaining
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

                codeeditor_paint_highlights_for_line(
                    editor, canvas, line, seg_start, seg_len, content_x, row_y, 0.0f, true);
                codeeditor_paint_pair_highlight_for_segment(
                    editor, canvas, line, seg_start, seg_len, content_x, row_y, 0.0f, true);

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
                        int overlap_end =
                            sel_end < seg_start + seg_len ? sel_end : seg_start + seg_len;
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
                        int overlap_end =
                            sel_end < seg_start + seg_len ? sel_end : seg_start + seg_len;
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
                codeeditor_paint_inlay_hints_for_segment(editor,
                                                         canvas,
                                                         line,
                                                         seg_start,
                                                         seg_len,
                                                         content_x,
                                                         row_y,
                                                         0.0f,
                                                         row_y + font_metrics.ascent,
                                                         true);

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
            int cursor_visual_row = codeeditor_visual_row_for_position(
                editor, content_width, editor->cursor_line, editor->cursor_col);
            float cursor_y =
                widget->y + (float)cursor_visual_row * editor->line_height - editor->scroll_y;
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
                int extra_visual_row =
                    codeeditor_visual_row_for_position(editor,
                                                       content_width,
                                                       editor->extra_cursors[c].line,
                                                       editor->extra_cursors[c].col);
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
                    int32_t icon_y =
                        (int32_t)line_y + ((int32_t)editor->line_height - icon_box) / 2;
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
                    codeeditor_draw_fold_marker(
                        editor, canvas, fold_gutter_x, fold_gutter_width, line_y, &font_metrics, i);
                }

                line_y += editor->line_height;
            }
        } else {
            float row_offset = editor->scroll_y - (float)first_visual_row * editor->line_height;
            int line = 0;
            int row_in_line = 0;
            codeeditor_locate_visual_row(
                editor, content_width, first_visual_row, &line, &row_in_line);
            float row_y = widget->y - row_offset;
            while (line < editor->line_count && row_y < widget->y + widget->height) {
                int row_count = codeeditor_cached_row_count_for_line(editor, content_width, line);
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
                        vg_font_measure_text(
                            editor->font, editor->font_size, line_num, &num_metrics);
                        float num_x =
                            widget->x + line_number_gutter_width - num_metrics.width - 8.0f;
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
                        int32_t icon_y =
                            (int32_t)row_y + ((int32_t)editor->line_height - icon_box) / 2;
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

/// @brief Encodes @p cp as UTF-8 into @p buf (≥4 bytes); returns bytes written, or 0 for invalid
/// codepoints.
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

/// @brief True if a character event should insert literal text instead of being treated as a
/// command shortcut.
static bool codeeditor_key_char_allows_text(const vg_event_t *event) {
    if (!event)
        return false;

    uint32_t mods = event->modifiers;
    bool has_super = (mods & VG_MOD_SUPER) != 0;
    bool has_ctrl = (mods & VG_MOD_CTRL) != 0;
    bool has_alt = (mods & VG_MOD_ALT) != 0;

    if (has_super)
        return false;
    if (has_ctrl && !has_alt)
        return false;
    if (has_alt && !has_ctrl)
        return false;
    return true;
}

static bool codeeditor_codepoint_is_text(uint32_t cp) {
    if (cp < 0x20 || cp == 0x7F || cp > 0x10FFFF)
        return false;
    if (cp >= 0x80 && cp <= 0x9F)
        return false;
    if (cp >= 0xD800 && cp <= 0xDFFF)
        return false;
    if ((cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xF0000 && cp <= 0xFFFFD) ||
        (cp >= 0x100000 && cp <= 0x10FFFD))
        return false;
    return true;
}

static int codeeditor_leading_indent_len(const char *text, size_t len) {
    int i = 0;
    while ((size_t)i < len && (text[i] == ' ' || text[i] == '\t'))
        i++;
    return i;
}

static int codeeditor_last_nonspace_before(const char *text, int col) {
    int i = col - 1;
    while (i >= 0 && (text[i] == ' ' || text[i] == '\t'))
        i--;
    return i;
}

static bool codeeditor_opener_adds_indent(char ch) {
    return ch == '{' || ch == '(' || ch == '[';
}

static void codeeditor_insert_newline_with_indent(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    if (!editor->auto_indent || editor->has_selection || editor->extra_cursor_count > 0 ||
        editor->cursor_line < 0 || editor->cursor_line >= editor->line_count) {
        vg_codeeditor_insert_text(editor, "\n");
        return;
    }

    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    int line_len = line->length > (size_t)INT_MAX ? INT_MAX : (int)line->length;
    int col = editor->cursor_col;
    if (col < 0)
        col = 0;
    if (col > line_len)
        col = line_len;

    int indent_len = codeeditor_leading_indent_len(line->text, line->length);
    int extra_indent_len = 0;
    int last_nonspace = codeeditor_last_nonspace_before(line->text, col);
    if (last_nonspace >= 0 && codeeditor_opener_adds_indent(line->text[last_nonspace])) {
        extra_indent_len = editor->use_spaces ? editor->tab_width : 1;
        if (extra_indent_len < 1)
            extra_indent_len = 1;
        if (extra_indent_len > 16)
            extra_indent_len = 16;
    }

    size_t insert_len = 1u + (size_t)indent_len + (size_t)extra_indent_len;
    char *insert = malloc(insert_len + 1u);
    if (!insert) {
        vg_codeeditor_insert_text(editor, "\n");
        return;
    }

    char *p = insert;
    *p++ = '\n';
    if (indent_len > 0) {
        memcpy(p, line->text, (size_t)indent_len);
        p += indent_len;
    }
    if (extra_indent_len > 0) {
        if (editor->use_spaces) {
            memset(p, ' ', (size_t)extra_indent_len);
            p += extra_indent_len;
        } else {
            *p++ = '\t';
        }
    }
    *p = '\0';

    vg_codeeditor_insert_text(editor, insert);
    free(insert);
}

static char codeeditor_closing_for_opener(char opener) {
    switch (opener) {
        case '(':
            return ')';
        case '[':
            return ']';
        case '{':
            return '}';
        case '"':
            return '"';
        case '\'':
            return '\'';
        default:
            return '\0';
    }
}

static bool codeeditor_is_pair_closer(char ch) {
    return ch == ')' || ch == ']' || ch == '}' || ch == '"' || ch == '\'';
}

static bool codeeditor_is_whitespace_span(const char *text, int start, int end) {
    if (!text || start < 0 || end < start)
        return false;
    for (int i = start; i < end; i++) {
        if (text[i] != ' ' && text[i] != '\t')
            return false;
    }
    return true;
}

static bool codeeditor_try_format_closing_brace(vg_codeeditor_t *editor) {
    if (!editor || !editor->auto_indent || editor->has_selection || editor->extra_cursor_count > 0)
        return false;
    if (editor->cursor_line < 0 || editor->cursor_line >= editor->line_count)
        return false;

    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    int line_len = line->length > (size_t)INT_MAX ? INT_MAX : (int)line->length;
    int col = editor->cursor_col;
    if (col < 0 || col > line_len)
        return false;
    if (col == 0)
        return false;
    if (!codeeditor_is_whitespace_span(line->text, 0, col))
        return false;

    int remove_start = col;
    if (remove_start > 0 && line->text[remove_start - 1] == '\t') {
        remove_start--;
    } else {
        int spaces_to_remove = editor->tab_width;
        if (spaces_to_remove < 1)
            spaces_to_remove = 1;
        if (spaces_to_remove > 16)
            spaces_to_remove = 16;
        int removed = 0;
        while (remove_start > 0 && removed < spaces_to_remove &&
               line->text[remove_start - 1] == ' ') {
            remove_start--;
            removed++;
        }
    }
    if (remove_start == col)
        return false;

    int replace_end = col;
    if (replace_end < line_len && line->text[replace_end] == '}')
        replace_end++;
    if (!codeeditor_is_whitespace_span(line->text, replace_end, line_len))
        return false;
    replace_end = line_len;

    vg_edit_target_t target = {0};
    int target_count = 0;
    add_edit_target(&target,
                    &target_count,
                    0,
                    editor->cursor_line,
                    editor->cursor_col,
                    editor->cursor_line,
                    remove_start,
                    editor->cursor_line,
                    replace_end);
    apply_edit_targets(editor, &target, target_count, "}");
    return true;
}

static bool codeeditor_try_skip_pair_closer(vg_codeeditor_t *editor, char ch) {
    if (!editor || editor->has_selection || editor->extra_cursor_count > 0)
        return false;
    if (!codeeditor_is_pair_closer(ch))
        return false;
    if (editor->cursor_line < 0 || editor->cursor_line >= editor->line_count)
        return false;
    vg_code_line_t *line = &editor->lines[editor->cursor_line];
    if (editor->cursor_col < 0 || editor->cursor_col >= (int)line->length)
        return false;
    if (line->text[editor->cursor_col] != ch)
        return false;
    editor->cursor_col++;
    editor->cursor_visible = true;
    editor->base.needs_paint = true;
    return true;
}

static bool codeeditor_try_insert_pair(vg_codeeditor_t *editor, char opener) {
    if (!editor || editor->has_selection || editor->extra_cursor_count > 0)
        return false;
    char closer = codeeditor_closing_for_opener(opener);
    if (closer == '\0')
        return false;

    char pair[3] = {opener, closer, '\0'};
    int line_before = editor->cursor_line;
    int col_before = editor->cursor_col;
    vg_codeeditor_insert_text(editor, pair);

    editor->cursor_line = line_before;
    editor->cursor_col = col_before + 1;
    edit_history_update_latest_cursor_after(
        editor->history, editor->cursor_line, editor->cursor_col);
    ensure_cursor_visible(editor);
    editor->cursor_visible = true;
    editor->base.needs_paint = true;
    return true;
}

// Insert n raw bytes at the current cursor position and advance cursor_col by n.
VG_UNUSED static void insert_bytes(vg_codeeditor_t *editor, const char *bytes, size_t n) {
    if (!editor || !bytes || !n)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    vg_code_line_t *line = &editor->lines[editor->cursor_line];

    if (line->length > SIZE_MAX - 1 || n > SIZE_MAX - line->length - 1 ||
        line->length > (size_t)INT_MAX || n > (size_t)INT_MAX - line->length)
        return;
    if (!ensure_text_capacity(line, line->length + n + 1))
        return;

    memmove(line->text + editor->cursor_col + n,
            line->text + editor->cursor_col,
            line->length - editor->cursor_col + 1);

    memcpy(line->text + editor->cursor_col, bytes, n);
    line->length += n;
    editor->cursor_col += (int)n;
    editor->modified = true;
    line->modified = true;
    codeeditor_invalidate_line_highlight(editor, editor->cursor_line);
}

VG_UNUSED static void insert_char(vg_codeeditor_t *editor, char c) {
    if (!editor)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    vg_code_line_t *line = &editor->lines[editor->cursor_line];

    if (line->length > SIZE_MAX - 2 || line->length >= (size_t)INT_MAX)
        return;
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
    codeeditor_invalidate_line_highlight(editor, editor->cursor_line);
}

/// @brief Inserts @p n bytes from @p bytes into @p line_idx at *col, advancing *col past the
/// inserted content.
static bool codeeditor_insert_bytes_at(
    vg_codeeditor_t *editor, int line_idx, int *col, const char *bytes, size_t n) {
    if (!editor || !bytes || !col || n == 0 || line_idx < 0 || line_idx >= editor->line_count)
        return false;

    vg_code_line_t *line = &editor->lines[line_idx];
    if (!ensure_line_text_exists(line))
        return false;
    if (*col < 0)
        *col = 0;
    int line_len = line->length > (size_t)INT_MAX ? INT_MAX : (int)line->length;
    if (*col > line_len)
        *col = line_len;

    if (line->length > SIZE_MAX - 1 || n > SIZE_MAX - line->length - 1 ||
        line->length > (size_t)INT_MAX || n > (size_t)INT_MAX - line->length)
        return false;
    if (n > (size_t)(INT_MAX - *col))
        return false;
    if (!ensure_text_capacity(line, line->length + n + 1))
        return false;

    memmove(line->text + *col + n, line->text + *col, line->length - (size_t)*col + 1);
    memcpy(line->text + *col, bytes, n);
    line->length += n;
    *col += (int)n;
    line->modified = true;
    codeeditor_invalidate_line_highlight(editor, line_idx);
    return true;
}

/// @brief Splits @p line_idx at @p col by inserting a new empty line and moving trailing text to
/// it.
static bool codeeditor_split_line_at(vg_codeeditor_t *editor, int line_idx, int col) {
    if (!editor || line_idx < 0 || line_idx >= editor->line_count)
        return false;
    if (!ensure_line_capacity(editor, editor->line_count + 1))
        return false;

    vg_code_line_t *current = &editor->lines[line_idx];
    if (!ensure_line_text_exists(current))
        return false;
    if (col < 0)
        col = 0;
    int current_len = current->length > (size_t)INT_MAX ? INT_MAX : (int)current->length;
    if (col > current_len)
        col = current_len;

    size_t split_col = (size_t)col;
    size_t remaining = current->length - split_col;
    vg_code_line_t new_line;
    if (!init_line_text(&new_line, current->text + split_col, remaining))
        return false;

    if (line_idx + 1 < editor->line_count) {
        memmove(&editor->lines[line_idx + 2],
                &editor->lines[line_idx + 1],
                (size_t)(editor->line_count - line_idx - 1) * sizeof(vg_code_line_t));
    }
    editor->lines[line_idx + 1] = new_line;
    current = &editor->lines[line_idx];
    current->text[split_col] = '\0';
    current->length = split_col;
    current->modified = true;
    editor->lines[line_idx + 1].modified = true;
    codeeditor_invalidate_line_highlight(editor, line_idx);
    codeeditor_invalidate_line_highlight(editor, line_idx + 1);
    editor->line_count++;
    return true;
}

VG_UNUSED static void insert_newline(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    if (!codeeditor_split_line_at(editor, editor->cursor_line, editor->cursor_col))
        return;

    editor->cursor_line++;
    editor->cursor_col = 0;
    editor->modified = true;

    vg_codeeditor_refresh_layout_state(editor);
}

VG_UNUSED static void delete_char_backward(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_clamp_cursor_to_visible(editor, &editor->cursor_line, &editor->cursor_col);
    if (editor->cursor_col > 0) {
        vg_code_line_t *line = &editor->lines[editor->cursor_line];
        memmove(line->text + editor->cursor_col - 1,
                line->text + editor->cursor_col,
                line->length - editor->cursor_col + 1);
        line->length--;
        editor->cursor_col--;
        editor->modified = true;
        line->modified = true;
        codeeditor_invalidate_line_highlight(editor, editor->cursor_line);
    } else if (editor->cursor_line > 0) {
        // Join with previous line
        vg_code_line_t *current = &editor->lines[editor->cursor_line];
        vg_code_line_t *prev = &editor->lines[editor->cursor_line - 1];
        int old_line_count = editor->line_count;

        size_t new_col = prev->length;

        if (prev->length > (size_t)INT_MAX || current->length > (size_t)INT_MAX - prev->length ||
            !ensure_text_capacity(prev, prev->length + current->length + 1))
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
        codeeditor_invalidate_line_highlight(editor, editor->cursor_line);

        vg_codeeditor_refresh_layout_state(editor);
    }
}

/// @brief Performs a Backspace deletion across all cursors: deletes selections or removes the
/// preceding character/newline.
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
        int prev_col =
            codeeditor_prev_byte_boundary(editor, editor->cursor_line, editor->cursor_col);
        add_edit_target(targets,
                        &target_count,
                        0,
                        editor->cursor_line,
                        editor->cursor_col,
                        editor->cursor_line,
                        prev_col,
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
            int prev_col = codeeditor_prev_byte_boundary(editor, cursor->line, cursor->col);
            add_edit_target(targets,
                            &target_count,
                            i + 1,
                            cursor->line,
                            cursor->col,
                            cursor->line,
                            prev_col,
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

/// @brief VTable handle_event: routes mouse (click/drag/scroll/scrollbar) and keyboard events to
/// the appropriate editor action.
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
            if (editor->selection_dragging) {
                editor->selection_dragging = false;
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
                if (codeeditor_get_scrollbar_metrics(editor,
                                                     widget,
                                                     NULL,
                                                     &thumb_y,
                                                     &thumb_height,
                                                     &max_scroll,
                                                     &thumb_travel) &&
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
            if (editor->selection_dragging) {
                int line = 0;
                int col = 0;
                codeeditor_local_point_to_position(
                    editor, widget, event->mouse.x, event->mouse.y, &line, &col);
                editor->selection.start_line = editor->selection_anchor_line;
                editor->selection.start_col = editor->selection_anchor_col;
                editor->selection.end_line = line;
                editor->selection.end_col = col;
                editor->cursor_line = line;
                editor->cursor_col = col;
                editor->has_selection = compare_positions(editor->selection.start_line,
                                                          editor->selection.start_col,
                                                          editor->selection.end_line,
                                                          editor->selection.end_col) != 0;
                editor->cursor_visible = true;
                ensure_cursor_visible(editor);
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
            if (codeeditor_get_scrollbar_metrics(editor,
                                                 widget,
                                                 &track_x,
                                                 &thumb_y,
                                                 &thumb_height,
                                                 &max_scroll,
                                                 &thumb_travel) &&
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
                codeeditor_local_point_to_position(
                    editor, widget, event->mouse.x, event->mouse.y, &line, NULL);
                float line_number_gutter_width = codeeditor_line_number_gutter_width(editor);
                float fold_gutter_width = codeeditor_fold_gutter_width(editor);

                if (editor->show_fold_gutter && fold_gutter_width > 0.0f &&
                    event->mouse.x >= line_number_gutter_width) {
                    struct vg_fold_region *region =
                        codeeditor_fold_region_starting_at_mut(editor, line);
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
            codeeditor_local_point_to_position(
                editor, widget, event->mouse.x, event->mouse.y, &line, &col);
            uint32_t mods = event->modifiers;
            bool additive_click = (mods & (VG_MOD_CTRL | VG_MOD_SUPER)) != 0;
            bool range_click = (mods & VG_MOD_SHIFT) != 0;
            if (additive_click) {
                codeeditor_add_extra_cursor_at(editor, line, col);
                editor->cursor_visible = true;
                widget->needs_paint = true;
                return true;
            }
            if (range_click) {
                editor->selection.start_line = editor->cursor_line;
                editor->selection.start_col = editor->cursor_col;
                editor->selection.end_line = line;
                editor->selection.end_col = col;
                editor->cursor_line = line;
                editor->cursor_col = col;
                editor->has_selection = compare_positions(editor->selection.start_line,
                                                          editor->selection.start_col,
                                                          editor->selection.end_line,
                                                          editor->selection.end_col) != 0;
                clear_extra_cursor_selections(editor);
                editor->cursor_visible = true;
                widget->needs_paint = true;
                return true;
            }
            editor->cursor_line = line;
            editor->cursor_col = col;
            editor->has_selection = false;
            clear_extra_cursor_selections(editor);
            if (event->mouse.button == VG_MOUSE_LEFT) {
                editor->selection_dragging = true;
                editor->selection_anchor_line = line;
                editor->selection_anchor_col = col;
                vg_widget_set_input_capture(widget);
            }
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
                bool handled_key = true;
                switch (event->key.key) {
                    case VG_KEY_UP:
                        codeeditor_move_cursor_vertical(editor, widget, -1);
                        break;
                    case VG_KEY_DOWN:
                        codeeditor_move_cursor_vertical(editor, widget, 1);
                        break;
                    case VG_KEY_LEFT:
                        if (editor->cursor_col > 0)
                            editor->cursor_col = codeeditor_prev_byte_boundary(
                                editor, editor->cursor_line, editor->cursor_col);
                        break;
                    case VG_KEY_RIGHT:
                        if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length)
                            editor->cursor_col = codeeditor_next_byte_boundary(
                                editor, editor->cursor_line, editor->cursor_col);
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
                        handled_key = false;
                        break;
                }
                if (!handled_key)
                    return false;
                ensure_cursor_visible(editor);
                widget->needs_paint = true;
                return true;
            }

            bool handled_key = true;
            switch (event->key.key) {
                case VG_KEY_UP:
                    codeeditor_move_cursor_vertical(editor, widget, -1);
                    break;
                case VG_KEY_DOWN:
                    codeeditor_move_cursor_vertical(editor, widget, 1);
                    break;
                case VG_KEY_LEFT:
                    if (editor->cursor_col > 0) {
                        editor->cursor_col = codeeditor_prev_byte_boundary(
                            editor, editor->cursor_line, editor->cursor_col);
                    } else if (editor->cursor_line > 0) {
                        editor->cursor_line--;
                        editor->cursor_col = (int)editor->lines[editor->cursor_line].length;
                    }
                    break;
                case VG_KEY_RIGHT:
                    if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length) {
                        editor->cursor_col = codeeditor_next_byte_boundary(
                            editor, editor->cursor_line, editor->cursor_col);
                    } else if (editor->cursor_line < editor->line_count - 1) {
                        editor->cursor_line++;
                        editor->cursor_col = 0;
                    }
                    break;
                case VG_KEY_HOME:
                    if (editor->cursor_line >= 0 && editor->cursor_line < editor->line_count) {
                        vg_code_line_t *line = &editor->lines[editor->cursor_line];
                        int first_text_col =
                            codeeditor_leading_indent_len(line->text, line->length);
                        editor->cursor_col =
                            editor->cursor_col == first_text_col ? 0 : first_text_col;
                    } else {
                        editor->cursor_col = 0;
                    }
                    break;
                case VG_KEY_END:
                    if (editor->cursor_line >= 0 && editor->cursor_line < editor->line_count) {
                        vg_code_line_t *line = &editor->lines[editor->cursor_line];
                        int end_col = (int)line->length;
                        int last_text_col = end_col;
                        while (last_text_col > 0 && (line->text[last_text_col - 1] == ' ' ||
                                                     line->text[last_text_col - 1] == '\t'))
                            last_text_col--;
                        editor->cursor_col =
                            editor->cursor_col == last_text_col ? end_col : last_text_col;
                    }
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
                    codeeditor_insert_newline_with_indent(editor);
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
                    handled_key = false;
                    break;
            }

            if (!handled_key)
                return false;

            // Ensure cursor stays visible after movement
            ensure_cursor_visible(editor);

            editor->cursor_visible = true;
            editor->has_selection = false;
            clear_extra_cursor_selections(editor);
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_CHAR: {
            if (!codeeditor_key_char_allows_text(event))
                return false;
            uint32_t cp = event->key.codepoint;
            bool printable = codeeditor_codepoint_is_text(cp);
            if (!editor->read_only && printable) {
                if (cp < 0x80) {
                    char text[2] = {(char)cp, '\0'};
                    if (text[0] == '}' && codeeditor_try_format_closing_brace(editor)) {
                        ensure_cursor_visible(editor);
                        widget->needs_paint = true;
                        return true;
                    }
                    if (codeeditor_try_skip_pair_closer(editor, text[0])) {
                        ensure_cursor_visible(editor);
                        widget->needs_paint = true;
                        return true;
                    }
                    if (codeeditor_try_insert_pair(editor, text[0])) {
                        ensure_cursor_visible(editor);
                        widget->needs_paint = true;
                        return true;
                    }
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
            editor->scroll_y -=
                event->wheel.delta_y * editor->line_height * CODEEDITOR_MOUSE_WHEEL_LINES;
            codeeditor_clamp_scroll(editor, widget);
            widget->needs_paint = true;
            return true;

        default:
            break;
    }

    return false;
}

/// @brief VTable can_focus: returns true when the editor is both enabled and visible.
static bool codeeditor_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

/// @brief VTable on_focus: on focus gain, resets the cursor blink state and triggers a repaint.
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

/// @brief Advance the cursor blink timer; call once per frame while the editor is visible.
///
/// @details Only advances the timer when the editor has focus; no-op otherwise.
///
/// @param editor The code editor to tick.
/// @param dt     Elapsed time in seconds since the last frame.
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

/// @brief Replace all editor content with the given byte span.
///
/// @details Splits text on newlines to populate the line array.  The cursor is
///          moved to (0,0), all selections and extra cursors are cleared, scroll
///          is reset to 0, and the modified flag is cleared.  NULL or empty text
///          results in a single empty line. Embedded NUL bytes are preserved in
///          the line buffers and returned by vg_codeeditor_get_text.
///
/// @param editor The code editor to update.
/// @param text   UTF-8 source bytes; may be NULL to clear.
/// @param len    Number of bytes to read from @p text.
void vg_codeeditor_set_text_bytes(vg_codeeditor_t *editor, const char *text, size_t len) {
    if (!editor)
        return;

    vg_code_line_t *new_lines = NULL;
    int new_capacity = 0;
    int new_count = 0;

    if (!text || len == 0) {
        if (!ensure_line_array_capacity(&new_lines, &new_capacity, 1) ||
            !init_line_text(&new_lines[0], "", 0)) {
            free_line_array(new_lines, new_count);
            return;
        }
        new_count = 1;
    } else {
        const char *start = text;
        const char *limit = text + len;
        while (start <= limit) {
            const void *found = memchr(start, '\n', (size_t)(limit - start));
            const char *end = found ? (const char *)found : limit;
            size_t line_len = (size_t)(end - start);

            if (!ensure_line_array_capacity(&new_lines, &new_capacity, new_count + 1) ||
                !init_line_text(&new_lines[new_count], start, line_len)) {
                free_line_array(new_lines, new_count);
                return;
            }
            new_count++;

            if (!found)
                break;
            start = end + 1;
        }

        if (new_count == 0) {
            if (!ensure_line_array_capacity(&new_lines, &new_capacity, 1) ||
                !init_line_text(&new_lines[0], "", 0)) {
                free_line_array(new_lines, new_count);
                return;
            }
            new_count = 1;
        }
    }

    codeeditor_clear_highlight_spans(editor);
    codeeditor_free_inlay_hint_storage(editor);
    free_line_array(editor->lines, editor->line_count);
    editor->lines = new_lines;
    editor->line_capacity = new_capacity;
    editor->line_count = new_count;
    editor->cursor_line = 0;
    editor->cursor_col = 0;
    editor->has_selection = false;
    clear_extra_cursor_selections(editor);
    editor->extra_cursor_count = 0;
    editor->scroll_x = 0;
    editor->scroll_y = 0;
    editor->modified = false;
    editor->zia_block_comment_depth = 0;
    free(editor->fold_regions);
    editor->fold_regions = NULL;
    editor->fold_region_count = 0;
    editor->fold_region_cap = 0;
    editor->has_folded_lines = false;
    edit_history_clear(editor->history);

    codeeditor_bump_revision(editor);
    vg_codeeditor_refresh_layout_state(editor);
    editor->base.needs_paint = true;
}

void vg_codeeditor_set_text(vg_codeeditor_t *editor, const char *text) {
    vg_codeeditor_set_text_bytes(editor, text, text ? strlen(text) : 0);
}

void vg_codeeditor_clear_inlay_hints(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    codeeditor_free_inlay_hint_storage(editor);
    editor->base.needs_paint = true;
}

void vg_codeeditor_add_inlay_hint(
    vg_codeeditor_t *editor, int line, int col, const char *text, uint32_t color) {
    if (!editor || !text || text[0] == '\0')
        return;
    if (line < 0 || line >= editor->line_count)
        return;
    if (col < 0)
        col = 0;
    if (col > (int)editor->lines[line].length)
        col = (int)editor->lines[line].length;
    if (editor->inlay_hint_count >= editor->inlay_hint_cap) {
        if (editor->inlay_hint_cap > INT_MAX / 2)
            return;
        int new_cap = editor->inlay_hint_cap ? editor->inlay_hint_cap * 2 : 8;
        void *p = realloc(editor->inlay_hints, (size_t)new_cap * sizeof(*editor->inlay_hints));
        if (!p)
            return;
        editor->inlay_hints = p;
        editor->inlay_hint_cap = new_cap;
    }
    char *copy = strdup(text);
    if (!copy)
        return;
    if (editor->inlay_hint_count > 0) {
        const struct vg_inlay_hint *prev = &editor->inlay_hints[editor->inlay_hint_count - 1];
        if (prev->line > line || (prev->line == line && prev->col > col))
            editor->inlay_hints_sorted = false;
    }
    struct vg_inlay_hint *hint = &editor->inlay_hints[editor->inlay_hint_count++];
    hint->line = line;
    hint->col = col;
    hint->text = copy;
    hint->color = color;
    editor->base.needs_paint = true;
}

int vg_codeeditor_get_inlay_hint_count(const vg_codeeditor_t *editor) {
    return editor ? editor->inlay_hint_count : 0;
}

/// @brief Return the complete editor content as a heap-allocated newline-joined string.
///
/// @details The caller owns the returned buffer and must free it.
///
/// @param editor The code editor to query.
/// @return Heap-allocated null-terminated UTF-8 string, or NULL on allocation failure
///         or if editor is NULL.
char *vg_codeeditor_get_text(vg_codeeditor_t *editor) {
    if (!editor)
        return NULL;

    // Calculate total size, including one final NUL byte.
    size_t total = 1;
    for (int i = 0; i < editor->line_count; i++) {
        size_t add = editor->lines[i].length;
        if (i < editor->line_count - 1) {
            if (add == SIZE_MAX)
                return NULL;
            add++;
        }
        if (add > SIZE_MAX - total)
            return NULL;
        total += add;
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

    codeeditor_perf_add(&editor->perf_stats.full_text_copies, 1);
    codeeditor_perf_add(&editor->perf_stats.full_text_copy_bytes, total > 0 ? total - 1 : 0);
    return result;
}

uint64_t vg_codeeditor_get_revision(vg_codeeditor_t *editor) {
    return editor ? editor->revision : 0;
}

/// @brief Return the selected text as a heap-allocated string.
///
/// @details The caller owns the returned buffer and must free it.  Returns NULL
///          if there is no active selection or if editor is NULL.
///
/// @param editor The code editor to query.
/// @return Heap-allocated null-terminated UTF-8 string of selected text, or NULL.
char *vg_codeeditor_get_selection(vg_codeeditor_t *editor) {
    if (!editor || !editor->has_selection)
        return NULL;

    int start_line, start_col, end_line, end_col;
    normalize_selection(editor, &start_line, &start_col, &end_line, &end_col);
    return copy_text_range(editor, start_line, start_col, end_line, end_col);
}

/// @brief Move the cursor to the specified line and column, clearing any selection.
///
/// @details line and col are clamped to valid positions.  Extra cursors and
///          selections are also cleared.
///
/// @param editor The code editor to update.
/// @param line   Zero-based target line; clamped to [0, line_count-1].
/// @param col    Zero-based target column; clamped to the line's length.
void vg_codeeditor_set_cursor(vg_codeeditor_t *editor, int line, int col) {
    if (!editor)
        return;

    clamp_editor_line(editor, &line);
    col = codeeditor_char_col_to_byte_col(editor, line, col);
    codeeditor_clamp_cursor_to_visible(editor, &line, &col);

    editor->cursor_line = line;
    editor->cursor_col = col;
    editor->has_selection = false;
    clear_extra_cursor_selections(editor);
    editor->base.needs_paint = true;
}

/// @brief Retrieve the current cursor line and column.
///
/// @param editor   The code editor to query.
/// @param out_line Output for the zero-based cursor line; may be NULL.
/// @param out_col  Output for the zero-based cursor column; may be NULL.
void vg_codeeditor_get_cursor(vg_codeeditor_t *editor, int *out_line, int *out_col) {
    if (!editor)
        return;
    if (out_line)
        *out_line = editor->cursor_line;
    if (out_col)
        *out_col = codeeditor_byte_col_to_char_col(editor, editor->cursor_line, editor->cursor_col);
}

/// @brief Programmatically set the selection range and position the cursor at the end.
///
/// @details All coordinates are clamped to valid positions.  The cursor is placed
///          at (end_line, end_col) after the call.
///
/// @param editor     The code editor to update.
/// @param start_line Zero-based selection start line.
/// @param start_col  Zero-based selection start column.
/// @param end_line   Zero-based selection end line; cursor lands here.
/// @param end_col    Zero-based selection end column; cursor lands here.
void vg_codeeditor_set_selection(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    if (!editor)
        return;

    clamp_editor_line(editor, &start_line);
    clamp_editor_line(editor, &end_line);
    start_col = codeeditor_char_col_to_byte_col(editor, start_line, start_col);
    end_col = codeeditor_char_col_to_byte_col(editor, end_line, end_col);
    clamp_editor_position(editor, &start_line, &start_col);
    clamp_editor_position(editor, &end_line, &end_col);
    editor->selection.start_line = start_line;
    editor->selection.start_col = start_col;
    editor->selection.end_line = end_line;
    editor->selection.end_col = end_col;
    editor->cursor_line = end_line;
    editor->cursor_col = end_col;
    editor->has_selection = compare_positions(start_line, start_col, end_line, end_col) != 0;
    editor->base.needs_paint = true;
}

/// @brief Insert text at each cursor, replacing any active selections.
///
/// @details In multi-cursor mode each cursor receives the same text, applied in
///          reverse document order to preserve position validity.  The operation
///          is recorded in the undo history.  No-op if editor is in read-only mode.
///
/// @param editor The code editor to update.
/// @param text   Null-terminated UTF-8 text to insert; may contain newlines.
void vg_codeeditor_insert_text(vg_codeeditor_t *editor, const char *text) {
    if (!editor || editor->read_only || !text || text[0] == '\0')
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

/// @brief Delete the selected text at all active cursors, recording the operation.
///
/// @details If no selection exists the call is a no-op.  In multi-cursor mode,
///          all cursor selections are deleted in a single pass.
///
/// @param editor The code editor to update.
void vg_codeeditor_delete_selection(vg_codeeditor_t *editor) {
    if (!editor || editor->read_only)
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

/// @brief Scroll the viewport so that the specified line is visible.
///
/// @details Scrolls the minimum distance needed: if the line is already visible
///          the scroll position is unchanged.  Line is clamped to [0, line_count-1].
///          Hidden (folded) lines are remapped to their visible anchor.
///
/// @param editor The code editor to scroll.
/// @param line   Zero-based line number to scroll into view.
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

int vg_codeeditor_get_scroll_top_line(vg_codeeditor_t *editor) {
    if (!editor || editor->line_count <= 0 || editor->line_height <= 0.0f)
        return 0;

    int visual_row = (int)(editor->scroll_y / editor->line_height);
    int line = 0;
    codeeditor_locate_visual_row(
        editor, codeeditor_content_draw_width(editor, &editor->base), visual_row, &line, NULL);
    if (line < 0)
        return 0;
    if (line >= editor->line_count)
        return editor->line_count - 1;
    return line;
}

void vg_codeeditor_set_scroll_top_line(vg_codeeditor_t *editor, int line) {
    if (!editor || editor->line_count <= 0)
        return;

    if (line < 0)
        line = 0;
    if (line >= editor->line_count)
        line = editor->line_count - 1;
    line = codeeditor_visible_anchor_line(editor, line);

    float content_width = codeeditor_content_draw_width(editor, &editor->base);
    int visual_row = codeeditor_visual_row_for_position(editor, content_width, line, 0);
    editor->scroll_y = (float)visual_row * editor->line_height;
    codeeditor_clamp_scroll(editor, &editor->base);
    editor->base.needs_paint = true;
}

/// @brief Register a syntax highlighting callback invoked once per visible line during paint.
///
/// @details Setting a new callback invalidates all cached per-character colour data,
///          forcing a full highlight refresh on the next repaint.  Set callback to NULL
///          to disable syntax highlighting.
///
/// @param editor    The code editor to configure.
/// @param callback  Syntax highlighter called with (editor, line_idx, user_data).
/// @param user_data Opaque pointer forwarded unchanged to the callback.
void vg_codeeditor_set_syntax(vg_codeeditor_t *editor,
                              vg_syntax_callback_t callback,
                              void *user_data) {
    if (!editor)
        return;
    editor->syntax_highlighter = callback;
    editor->syntax_data = user_data;
    // Reset language-specific scanner state — block-comment depth from a prior
    // Zia document must not leak into a freshly-installed highlighter.
    editor->zia_block_comment_depth = 0;
    codeeditor_bump_highlight_generation(editor);
    // Invalidate cached colors so the new highlighter runs on the next paint.
    for (int i = 0; i < editor->line_count; i++) {
        if (editor->lines[i].colors) {
            free(editor->lines[i].colors);
            editor->lines[i].colors = NULL;
            editor->lines[i].colors_capacity = 0;
        }
        editor->lines[i].highlight_generation = 0;
        editor->lines[i].syntax_state_generation = 0;
    }
    editor->base.needs_paint = true;
}

/// @brief Low-level text insert at (line, col) without recording to history; handles embedded
/// newlines via line splits.
static void insert_text_at_internal(vg_codeeditor_t *editor, int line, int col, const char *text) {
    if (!editor || !text || line < 0 || line >= editor->line_count)
        return;

    clamp_editor_position(editor, &line, &col);

    int cur_line = line;
    int cur_col = col;
    bool changed = false;

    for (const char *p = text; *p;) {
        if (*p == '\n') {
            if (!codeeditor_split_line_at(editor, cur_line, cur_col))
                return;
            cur_line++;
            cur_col = 0;
            changed = true;
            p++;
            continue;
        }

        size_t span = codeeditor_utf8_span(p);
        if (span == 0) {
            unsigned char byte = (unsigned char)*p;
            if (byte < 32 && byte != '\t') {
                p++;
                continue;
            }
            span = 1;
        }

        if (!codeeditor_insert_bytes_at(editor, cur_line, &cur_col, p, span))
            return;
        changed = true;
        p += span;
    }

    editor->cursor_line = cur_line;
    editor->cursor_col = cur_col;
    if (changed) {
        editor->modified = true;
        vg_codeeditor_refresh_layout_state(editor);
    }
}

/// @brief Low-level range deletion without history recording; merges lines when the range spans
/// newlines.
static void delete_text_range_internal(
    vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col) {
    if (!editor || start_line < 0 || start_line >= editor->line_count)
        return;
    if (end_line < 0 || end_line >= editor->line_count)
        return;

    normalize_selection_range(&start_line, &start_col, &end_line, &end_col);
    clamp_editor_position(editor, &start_line, &start_col);
    clamp_editor_position(editor, &end_line, &end_col);
    normalize_selection_range(&start_line, &start_col, &end_line, &end_col);
    if (compare_positions(start_line, start_col, end_line, end_col) >= 0)
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
        codeeditor_invalidate_line_highlight(editor, start_line);
    } else {
        // Multi-line deletion
        vg_code_line_t *first = &editor->lines[start_line];
        vg_code_line_t *last = &editor->lines[end_line];
        int old_line_count = editor->line_count;

        if (end_col > (int)last->length)
            end_col = (int)last->length;
        if (start_col > (int)first->length)
            start_col = (int)first->length;
        if ((size_t)start_col > SIZE_MAX - (last->length - (size_t)end_col))
            return;
        size_t new_len = start_col + (last->length - end_col);
        if (!ensure_text_capacity(first, new_len + 1))
            return;

        memcpy(first->text + start_col, last->text + end_col, last->length - end_col + 1);
        first->length = new_len;
        codeeditor_invalidate_line_highlight(editor, start_line);

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
    editor->lines[start_line].modified = true;
}

/// @brief Computes the document position reached after inserting @p text starting at (start_line,
/// start_col).
static void compute_text_end_position(
    int start_line, int start_col, const char *text, int *out_line, int *out_col) {
    int line = start_line;
    int col = start_col;
    if (col < 0)
        col = 0;

    if (text) {
        for (const char *p = text; *p;) {
            if (*p == '\n') {
                line++;
                col = 0;
                p++;
                continue;
            }

            size_t span = codeeditor_utf8_span(p);
            if (span == 0) {
                unsigned char byte = (unsigned char)*p;
                if (byte < 32 && byte != '\t') {
                    p++;
                    continue;
                }
                span = 1;
            }

            if (span > (size_t)(INT_MAX - col))
                col = INT_MAX;
            else
                col += (int)span;
            p += span;
        }
    }

    if (out_line)
        *out_line = line;
    if (out_col)
        *out_col = col;
}

/// @brief Undo the most recent edit operation (or group of grouped operations).
///
/// @details Grouped operations sharing the same group_id are undone atomically.
///          Cursor position is restored to the pre-edit location.  No-op if the
///          history stack is empty.
///
/// @param editor The code editor to apply undo to.
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
    codeeditor_bump_revision(editor);
    codeeditor_invalidate_all_highlights(editor);
    editor->base.needs_paint = true;
}

/// @brief Redo the most recently undone edit operation (or group).
///
/// @details Grouped operations are replayed atomically.  Cursor position is
///          restored to the post-edit location.  No-op if the redo stack is empty.
///
/// @param editor The code editor to apply redo to.
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
    codeeditor_bump_revision(editor);
    codeeditor_invalidate_all_highlights(editor);
    editor->base.needs_paint = true;
}

/// @brief Copy the selected text to the system clipboard.
///
/// @details In multi-cursor mode, all cursor selections are concatenated with
///          newlines between each segment.  Returns false if there is no selection
///          or if clipboard access fails.
///
/// @param editor The code editor to copy from.
/// @return true if text was placed on the clipboard, false otherwise.
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
        parts[i] = copy_text_range_len(editor,
                                       targets[i].start_line,
                                       targets[i].start_col,
                                       targets[i].end_line,
                                       targets[i].end_col,
                                       &part_lengths[i]);
        if (!parts[i])
            goto cleanup;
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

/// @brief Copy the selected text to the clipboard and delete it from the editor.
///
/// @details No-op if the editor is read-only or there is no selection.
///
/// @param editor The code editor to cut from.
/// @return true if text was cut, false if read-only or nothing was selected.
bool vg_codeeditor_cut(vg_codeeditor_t *editor) {
    if (!editor || editor->read_only)
        return false;

    if (!vg_codeeditor_copy(editor))
        return false;

    vg_codeeditor_delete_selection(editor);
    editor->base.needs_paint = true;
    return true;
}

/// @brief Insert the current clipboard text at the cursor, replacing any selection.
///
/// @details No-op if the editor is read-only or the clipboard is empty.
///
/// @param editor The code editor to paste into.
/// @return true if text was pasted, false if read-only or clipboard empty.
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

/// @brief Select all text from the first character of line 0 to the end of the last line.
///
/// @param editor The code editor whose content will be fully selected.
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

/// @brief Set the monospace font and size used to render all editor text.
///
/// @details Updates char_width (measured from 'M') and line_height from the font
///          metrics.  Triggers a full layout refresh.
///
/// @param editor The code editor to configure.
/// @param font   Monospace font to use; NULL leaves the font unchanged.
/// @param size   Font size in logical pixels; values ≤ 0 default to the theme's
///               normal text size.
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

/// @brief Return the number of lines in the editor (always ≥ 1).
///
/// @param editor The code editor to query.
/// @return Total line count, or 0 if editor is NULL.
int vg_codeeditor_get_line_count(vg_codeeditor_t *editor) {
    return editor ? editor->line_count : 0;
}

/// @brief Return true if the editor content has been modified since the last
///        vg_codeeditor_clear_modified call or vg_codeeditor_set_text call.
///
/// @param editor The code editor to query.
/// @return true if modified, false if clean or editor is NULL.
bool vg_codeeditor_is_modified(vg_codeeditor_t *editor) {
    return editor ? editor->modified : false;
}

/// @brief Clear the modified flag on the editor and all individual lines.
///
/// @details Typically called after saving the file to reset the dirty indicator.
///
/// @param editor The code editor to mark as clean.
void vg_codeeditor_clear_modified(vg_codeeditor_t *editor) {
    if (editor) {
        editor->modified = false;
        for (int i = 0; i < editor->line_count; i++) {
            editor->lines[i].modified = false;
        }
    }
}

void vg_codeeditor_reset_perf_stats(vg_codeeditor_t *editor) {
    if (!editor)
        return;
    memset(&editor->perf_stats, 0, sizeof(editor->perf_stats));
}

vg_codeeditor_perf_stats_t vg_codeeditor_get_perf_stats(const vg_codeeditor_t *editor) {
    vg_codeeditor_perf_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    if (editor)
        stats = editor->perf_stats;
    return stats;
}
