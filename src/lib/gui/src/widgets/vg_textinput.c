//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_textinput.c
//
//===----------------------------------------------------------------------===//
// vg_textinput.c - Text input widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Constants
//=============================================================================

#define TEXTINPUT_INITIAL_CAPACITY 64
#define TEXTINPUT_GROWTH_FACTOR 2
#define CURSOR_BLINK_RATE 0.5f // Seconds
#define TEXTINPUT_UNDO_CAPACITY 32

//=============================================================================
// Forward Declarations
//=============================================================================

static void textinput_destroy(vg_widget_t *widget);
static void textinput_measure(vg_widget_t *widget, float available_width, float available_height);
static void textinput_paint(vg_widget_t *widget, void *canvas);
static bool textinput_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool textinput_can_focus(vg_widget_t *widget);
static void textinput_on_focus(vg_widget_t *widget, bool gained);

// Forward declaration for clipboard support
char *vg_textinput_get_selection(vg_textinput_t *input);

//=============================================================================
// TextInput VTable
//=============================================================================

static vg_widget_vtable_t g_textinput_vtable = {.destroy = textinput_destroy,
                                                .measure = textinput_measure,
                                                .arrange = NULL,
                                                .paint = textinput_paint,
                                                .handle_event = textinput_handle_event,
                                                .can_focus = textinput_can_focus,
                                                .on_focus = textinput_on_focus};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_capacity(vg_textinput_t *input, size_t needed) {
    if (needed <= input->text_capacity)
        return true;

    size_t new_capacity = input->text_capacity;
    while (new_capacity < needed) {
        new_capacity *= TEXTINPUT_GROWTH_FACTOR;
    }

    char *new_text = realloc(input->text, new_capacity);
    if (!new_text)
        return false;

    input->text = new_text;
    input->text_capacity = new_capacity;
    return true;
}

static size_t textinput_char_count(const vg_textinput_t *input) {
    return input && input->text ? (size_t)vg_utf8_strlen(input->text) : 0;
}

static size_t textinput_clamp_char_pos(const vg_textinput_t *input, size_t pos) {
    size_t chars = textinput_char_count(input);
    return pos > chars ? chars : pos;
}

static size_t textinput_byte_offset(const vg_textinput_t *input, size_t char_pos) {
    return (size_t)vg_utf8_offset(input->text, (int)textinput_clamp_char_pos(input, char_pos));
}

static size_t textinput_char_index_from_byte_offset(const char *text, size_t byte_offset) {
    if (!text)
        return 0;

    const char *cursor = text;
    const char *target = text + byte_offset;
    size_t chars = 0;
    while (*cursor && cursor < target) {
        vg_utf8_decode(&cursor);
        chars++;
    }
    return chars;
}

static size_t textinput_codepoint_count_in_prefix(const char *text, size_t byte_len) {
    if (!text)
        return 0;

    const char *cursor = text;
    const char *end = text + byte_len;
    size_t chars = 0;
    while (*cursor && cursor < end) {
        const char *prev = cursor;
        vg_utf8_decode(&cursor);
        if (cursor > end)
            break;
        if (cursor == prev)
            break;
        chars++;
    }
    return chars;
}

static size_t textinput_valid_utf8_prefix(const char *text, size_t max_bytes) {
    if (!text)
        return 0;

    const char *cursor = text;
    const char *limit = text + max_bytes;
    while (*cursor && cursor < limit) {
        const char *prev = cursor;
        vg_utf8_decode(&cursor);
        if (cursor > limit)
            return (size_t)(prev - text);
        if (cursor == prev)
            break;
    }
    return (size_t)(cursor - text);
}

static void textinput_set_cursor_internal(vg_textinput_t *input, size_t pos) {
    size_t clamped = textinput_clamp_char_pos(input, pos);
    input->cursor_pos = clamped;
    input->selection_start = clamped;
    input->selection_end = clamped;
}

static void textinput_ensure_cursor_visible(vg_textinput_t *input) {
    if (!input || !input->font || input->multiline)
        return;

    float padding = vg_theme_get_current()->input.padding_h;
    float viewport = input->base.width - padding * 2.0f;
    if (viewport <= 0.0f) {
        input->scroll_x = 0.0f;
        return;
    }

    float cursor_x =
        vg_font_get_cursor_x(input->font, input->font_size, input->text, (int)input->cursor_pos);
    if (cursor_x < input->scroll_x) {
        input->scroll_x = cursor_x;
    } else if (cursor_x > input->scroll_x + viewport) {
        input->scroll_x = cursor_x - viewport;
    }

    float text_width = 0.0f;
    if (input->text_len > 0) {
        vg_text_metrics_t metrics;
        vg_font_measure_text(input->font, input->font_size, input->text, &metrics);
        text_width = metrics.width;
    }
    float max_scroll = text_width - viewport;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (input->scroll_x < 0.0f)
        input->scroll_x = 0.0f;
    if (input->scroll_x > max_scroll)
        input->scroll_x = max_scroll;
}

//=============================================================================
// TextInput Implementation
//=============================================================================

vg_textinput_t *vg_textinput_create(vg_widget_t *parent) {
    vg_textinput_t *input = calloc(1, sizeof(vg_textinput_t));
    if (!input)
        return NULL;

    // Initialize base widget
    vg_widget_init(&input->base, VG_WIDGET_TEXTINPUT, &g_textinput_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Allocate initial text buffer
    input->text = malloc(TEXTINPUT_INITIAL_CAPACITY);
    if (!input->text) {
        free(input);
        return NULL;
    }
    input->text[0] = '\0';
    input->text_len = 0;
    input->text_capacity = TEXTINPUT_INITIAL_CAPACITY;

    // Initialize text input fields
    input->cursor_pos = 0;
    input->selection_start = 0;
    input->selection_end = 0;
    input->placeholder = NULL;
    input->font = NULL;
    input->font_size = theme->typography.size_normal;
    input->max_length = 0;
    input->password_mode = false;
    input->read_only = false;
    input->multiline = false;

    // Appearance
    input->text_color = theme->colors.fg_primary;
    input->placeholder_color = theme->colors.fg_placeholder;
    input->selection_color = theme->colors.bg_selected;
    input->cursor_color = theme->colors.fg_primary;
    input->bg_color = theme->colors.bg_primary;
    input->border_color = theme->colors.border_primary;

    // Scrolling
    input->scroll_x = 0;
    input->scroll_y = 0;

    // Callbacks
    input->on_change = NULL;
    input->on_change_data = NULL;

    // Internal state
    input->cursor_blink_time = 0;
    input->cursor_visible = true;

    // Initialize undo history with the initial empty-string snapshot at position 0.
    // push_undo is called AFTER each edit, so undo_stack[0] is the "before any typing"
    // baseline that Ctrl+Z can restore to.
    input->undo_stack[0] = strdup("");
    input->undo_cursors[0] = 0;
    input->undo_count = 1;
    input->undo_pos = 0;

    // Set minimum size
    input->base.constraints.min_height = theme->input.height;
    input->base.constraints.min_width = 100.0f;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &input->base);
    }

    return input;
}

static void textinput_destroy(vg_widget_t *widget) {
    vg_textinput_t *input = (vg_textinput_t *)widget;

    if (input->text) {
        free(input->text);
        input->text = NULL;
    }
    if (input->placeholder) {
        free((void *)input->placeholder);
        input->placeholder = NULL;
    }

    // Free undo ring-buffer snapshots
    for (int i = 0; i < TEXTINPUT_UNDO_CAPACITY; i++) {
        free(input->undo_stack[i]);
        input->undo_stack[i] = NULL;
    }
}

static void textinput_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_textinput_t *input = (vg_textinput_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    (void)available_height;

    // Default height from theme
    float height = theme->input.height;

    // Width uses available or minimum
    float width = available_width > 0 ? available_width : widget->constraints.min_width;

    if (widget->constraints.preferred_width > 0) {
        width = widget->constraints.preferred_width;
    }

    // Apply multiline height
    if (input->multiline && input->font) {
        vg_font_metrics_t font_metrics;
        vg_font_get_metrics(input->font, input->font_size, &font_metrics);
        // Use at least 3 lines for multiline
        height = font_metrics.line_height * 3;
    }

    widget->measured_width = width;
    widget->measured_height = height;

    // Apply constraints
    if (widget->measured_width < widget->constraints.min_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height) {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void textinput_paint(vg_widget_t *widget, void *canvas) {
    vg_textinput_t *input = (vg_textinput_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    // Determine colors based on state
    uint32_t text_color = input->text_color;

    if (widget->state & VG_STATE_DISABLED) {
        text_color = theme->colors.fg_disabled;
    }

    // Draw background and border
    uint32_t border_color = (widget->state & VG_STATE_FOCUSED) ? theme->colors.border_focus
                                                               : theme->colors.border_primary;
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   input->bg_color);
    vgfx_rect(win,
              (int32_t)widget->x,
              (int32_t)widget->y,
              (int32_t)widget->width,
              (int32_t)widget->height,
              border_color);

    // Calculate text area
    float padding = theme->input.padding_h;
    float text_x = widget->x + padding - input->scroll_x;
    float text_y = widget->y;
    int32_t clip_x = (int32_t)(widget->x + 1.0f);
    int32_t clip_y = (int32_t)(widget->y + 1.0f);
    int32_t clip_w = (int32_t)(widget->width - 2.0f);
    int32_t clip_h = (int32_t)(widget->height - 2.0f);

    // Draw text or placeholder
    if (!input->font)
        return;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(input->font, input->font_size, &font_metrics);
    text_y += (widget->height + font_metrics.ascent - font_metrics.descent) / 2.0f;

    const char *display_text = input->text;
    uint32_t display_color = text_color;

    if (input->text_len == 0 && input->placeholder) {
        display_text = input->placeholder;
        display_color = input->placeholder_color;
    }

    if (clip_w > 0 && clip_h > 0)
        vgfx_set_clip(win, clip_x, clip_y, clip_w, clip_h);

    // Draw selection highlight if focused
    if ((widget->state & VG_STATE_FOCUSED) && input->selection_start != input->selection_end) {
        // Get positions for selection
        size_t sel_start = input->selection_start < input->selection_end ? input->selection_start
                                                                         : input->selection_end;
        size_t sel_end = input->selection_start < input->selection_end ? input->selection_end
                                                                       : input->selection_start;

        float start_x =
            vg_font_get_cursor_x(input->font, input->font_size, input->text, (int)sel_start);
        float end_x =
            vg_font_get_cursor_x(input->font, input->font_size, input->text, (int)sel_end);

        // Draw selection rectangle
        float padding = theme->input.padding_h;
        float sel_abs_x = widget->x + padding + start_x - input->scroll_x;
        float sel_w = end_x - start_x;
        vgfx_fill_rect(win,
                       (int32_t)sel_abs_x,
                       (int32_t)widget->y,
                       (int32_t)sel_w,
                       (int32_t)widget->height,
                       input->selection_color);
    }

    // Draw text (with password masking if needed)
    if (input->password_mode && input->text_len > 0) {
        // Mask each codepoint with one asterisk. The previous implementation
        // used a fixed-size 1024-byte buffer that silently truncated long
        // password content — users had no way to tell their password was
        // longer than the visible asterisks. Allocate exactly what is needed.
        size_t char_count = textinput_char_count(input);
        char stack_buf[256];
        char *masked = (char_count + 1 <= sizeof(stack_buf)) ? stack_buf
                                                              : (char *)malloc(char_count + 1);
        if (masked) {
            for (size_t m = 0; m < char_count; m++)
                masked[m] = '*';
            masked[char_count] = '\0';
            vg_font_draw_text(
                canvas, input->font, input->font_size, text_x, text_y, masked, display_color);
            if (masked != stack_buf)
                free(masked);
        }
    } else {
        vg_font_draw_text(
            canvas, input->font, input->font_size, text_x, text_y, display_text, display_color);
    }

    // Draw cursor if focused and visible (blinking)
    if ((widget->state & VG_STATE_FOCUSED) && input->cursor_visible && !input->read_only) {
        float cursor_x =
            text_x + vg_font_get_cursor_x(
                         input->font, input->font_size, input->text, (int)input->cursor_pos);
        // Draw cursor line
        vgfx_line(win,
                  (int32_t)cursor_x,
                  (int32_t)widget->y + 2,
                  (int32_t)cursor_x,
                  (int32_t)(widget->y + widget->height - 2),
                  text_color);
    }

    if (clip_w > 0 && clip_h > 0)
        vgfx_clear_clip(win);
}

//=============================================================================
// Undo / Redo — ring buffer of text snapshots
//=============================================================================

// textinput_push_undo — call AFTER each edit to record the new state.
//
// Design (linear array, push-after semantics):
//   undo_stack[0]           = initial empty string (set at creation)
//   undo_stack[1..undo_pos] = states after successive edits
//   undo_pos                = index of the currently-displayed state (0-based)
//   undo_count              = total valid entries (undo_pos + 1 after any push)
//
// Undo: undo_pos-- and restore undo_stack[undo_pos].
// Redo: undo_pos++ and restore undo_stack[undo_pos].
// New edit while redos are available: truncate entries above undo_pos, then push.
static void textinput_push_undo(vg_textinput_t *input) {
    if (!input)
        return;

    // Truncate redo future: free entries above the current position
    while (input->undo_count > input->undo_pos + 1) {
        input->undo_count--;
        free(input->undo_stack[input->undo_count]);
        input->undo_stack[input->undo_count] = NULL;
    }

    // Deduplicate: skip if current text already matches the top snapshot
    if (input->undo_stack[input->undo_pos] &&
        strcmp(input->undo_stack[input->undo_pos], input->text) == 0)
        return;

    // Advance the write position
    input->undo_pos++;

    // Ring overflow: evict the oldest entry by shifting everything down by one
    if (input->undo_pos >= TEXTINPUT_UNDO_CAPACITY) {
        free(input->undo_stack[0]);
        memmove(input->undo_stack,
                input->undo_stack + 1,
                (TEXTINPUT_UNDO_CAPACITY - 1) * sizeof(char *));
        memmove(input->undo_cursors,
                input->undo_cursors + 1,
                (TEXTINPUT_UNDO_CAPACITY - 1) * sizeof(size_t));
        input->undo_stack[TEXTINPUT_UNDO_CAPACITY - 1] = NULL;
        input->undo_pos = TEXTINPUT_UNDO_CAPACITY - 1;
        input->undo_count = TEXTINPUT_UNDO_CAPACITY;
    } else {
        input->undo_count = input->undo_pos + 1;
    }

    input->undo_stack[input->undo_pos] = strdup(input->text);
    input->undo_cursors[input->undo_pos] = input->cursor_pos;
}

static void textinput_undo(vg_textinput_t *input) {
    if (!input || input->undo_pos <= 0)
        return; // Already at the oldest snapshot

    input->undo_pos--;

    const char *snap = input->undo_stack[input->undo_pos];
    if (!snap)
        return;

    size_t len = strlen(snap);
    if (!ensure_capacity(input, len + 1))
        return;
    memcpy(input->text, snap, len + 1);
    input->text_len = len;

    size_t cur = input->undo_cursors[input->undo_pos];
    textinput_set_cursor_internal(input, cur);
    textinput_ensure_cursor_visible(input);
    input->base.needs_paint = true;
}

static void textinput_redo(vg_textinput_t *input) {
    if (!input || input->undo_pos >= input->undo_count - 1)
        return; // Already at the newest snapshot

    input->undo_pos++;

    const char *snap = input->undo_stack[input->undo_pos];
    if (!snap)
        return;

    size_t len = strlen(snap);
    if (!ensure_capacity(input, len + 1))
        return;
    memcpy(input->text, snap, len + 1);
    input->text_len = len;

    size_t cur = input->undo_cursors[input->undo_pos];
    textinput_set_cursor_internal(input, cur);
    textinput_ensure_cursor_visible(input);
    input->base.needs_paint = true;
}

static bool textinput_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_textinput_t *input = (vg_textinput_t *)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN:
            // Set focus and cursor position
            if (input->font) {
                float padding = vg_theme_get_current()->input.padding_h;
                float local_x = event->mouse.x - padding + input->scroll_x;
                int char_index =
                    vg_font_hit_test(input->font, input->font_size, input->text, local_x);
                if (char_index >= 0) {
                    input->cursor_pos = (size_t)char_index;
                } else {
                    input->cursor_pos = textinput_char_count(input);
                }
                input->selection_start = input->cursor_pos;
                input->selection_end = input->cursor_pos;
                textinput_ensure_cursor_visible(input);
            }
            return true;

        case VG_EVENT_KEY_DOWN: {
            // Check for modifier key shortcuts
            uint32_t mods = event->modifiers;
            bool has_ctrl = (mods & VG_MOD_CTRL) != 0 || (mods & VG_MOD_SUPER) != 0;

            // Clipboard shortcuts (work in read-only mode for copy)
            if (has_ctrl) {
                switch (event->key.key) {
                    case VG_KEY_C: // Copy
                        if (input->selection_start != input->selection_end) {
                            char *selection = vg_textinput_get_selection(input);
                            if (selection) {
                                vgfx_clipboard_set_text(selection);
                                free(selection);
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_X: // Cut
                        if (!input->read_only && input->selection_start != input->selection_end) {
                            char *selection = vg_textinput_get_selection(input);
                            if (selection) {
                                vgfx_clipboard_set_text(selection);
                                free(selection);
                                vg_textinput_delete_selection(input);
                                textinput_push_undo(input);
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_V: // Paste
                        if (!input->read_only) {
                            char *text = vgfx_clipboard_get_text();
                            if (text) {
                                if (input->selection_start != input->selection_end) {
                                    vg_textinput_delete_selection(input);
                                }
                                vg_textinput_insert(input, text);
                                textinput_push_undo(input);
                                free(text);
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_A: // Select all
                        input->selection_start = 0;
                        input->selection_end = textinput_char_count(input);
                        input->cursor_pos = input->selection_end;
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_Z: // Undo
                        if (!input->read_only)
                            textinput_undo(input);
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_Y: // Redo
                        if (!input->read_only)
                            textinput_redo(input);
                        widget->needs_paint = true;
                        return true;

                    default:
                        break;
                }
            }

            bool has_shift = (mods & VG_MOD_SHIFT) != 0;

            if (input->read_only) {
                size_t char_count = textinput_char_count(input);
                // Only allow navigation in read-only mode
                switch (event->key.key) {
                    case VG_KEY_LEFT:
                        if (input->cursor_pos > 0)
                            input->cursor_pos--;
                        break;
                    case VG_KEY_RIGHT:
                        if (input->cursor_pos < char_count)
                            input->cursor_pos++;
                        break;
                    case VG_KEY_HOME:
                        input->cursor_pos = 0;
                        break;
                    case VG_KEY_END:
                        input->cursor_pos = char_count;
                        break;
                    default:
                        break;
                }
                textinput_ensure_cursor_visible(input);
                widget->needs_paint = true;
                return true;
            }

            /* Ctrl+Left/Right: jump to next word boundary */
            if (has_ctrl) {
                switch (event->key.key) {
                    case VG_KEY_LEFT: {
                        size_t pos = input->cursor_pos;
                        size_t byte_pos = textinput_byte_offset(input, pos);
                        /* Skip leading spaces */
                        while (byte_pos > 0 && input->text[byte_pos - 1] == ' ')
                            byte_pos--;
                        /* Skip word characters */
                        while (byte_pos > 0 && input->text[byte_pos - 1] != ' ')
                            byte_pos--;
                        pos = textinput_char_index_from_byte_offset(input->text, byte_pos);
                        if (has_shift) {
                            /* Extend / shrink selection toward cursor */
                            input->selection_end = pos;
                        } else {
                            input->selection_start = input->selection_end = pos;
                        }
                        input->cursor_pos = pos;
                        textinput_ensure_cursor_visible(input);
                        widget->needs_paint = true;
                        return true;
                    }
                    case VG_KEY_RIGHT: {
                        size_t pos = input->cursor_pos;
                        size_t byte_pos = textinput_byte_offset(input, pos);
                        /* Skip word characters */
                        while (byte_pos < input->text_len && input->text[byte_pos] != ' ')
                            byte_pos++;
                        /* Skip trailing spaces */
                        while (byte_pos < input->text_len && input->text[byte_pos] == ' ')
                            byte_pos++;
                        pos = textinput_char_index_from_byte_offset(input->text, byte_pos);
                        if (has_shift) {
                            input->selection_end = pos;
                        } else {
                            input->selection_start = input->selection_end = pos;
                        }
                        input->cursor_pos = pos;
                        textinput_ensure_cursor_visible(input);
                        widget->needs_paint = true;
                        return true;
                    }
                    default:
                        break;
                }
            }

            // Handle editing keys
            switch (event->key.key) {
                case VG_KEY_BACKSPACE:
                    if (input->selection_start != input->selection_end) {
                        vg_textinput_delete_selection(input);
                        textinput_push_undo(input);
                    } else if (input->cursor_pos > 0) {
                        size_t end = textinput_byte_offset(input, input->cursor_pos);
                        size_t start = textinput_byte_offset(input, input->cursor_pos - 1);
                        memmove(input->text + start, input->text + end, input->text_len - end + 1);
                        input->cursor_pos--;
                        input->text_len -= (end - start);
                        textinput_ensure_cursor_visible(input);
                        textinput_push_undo(input);
                        if (input->on_change) {
                            input->on_change(widget, input->text, input->on_change_data);
                        }
                    }
                    break;

                case VG_KEY_DELETE:
                    if (input->selection_start != input->selection_end) {
                        vg_textinput_delete_selection(input);
                        textinput_push_undo(input);
                    } else if (input->cursor_pos < textinput_char_count(input)) {
                        size_t start = textinput_byte_offset(input, input->cursor_pos);
                        size_t end = textinput_byte_offset(input, input->cursor_pos + 1);
                        memmove(input->text + start, input->text + end, input->text_len - end + 1);
                        input->text_len -= (end - start);
                        textinput_ensure_cursor_visible(input);
                        textinput_push_undo(input);
                        if (input->on_change) {
                            input->on_change(widget, input->text, input->on_change_data);
                        }
                    }
                    textinput_ensure_cursor_visible(input);
                    break;

                case VG_KEY_LEFT:
                    if (has_shift) {
                        /* Extend selection: anchor is kept, end moves */
                        if (input->cursor_pos > 0)
                            input->cursor_pos--;
                        input->selection_end = input->cursor_pos;
                    } else {
                        /* Plain Left: collapse selection or move */
                        if (input->selection_start != input->selection_end)
                            input->cursor_pos = input->selection_start;
                        else if (input->cursor_pos > 0)
                            input->cursor_pos--;
                        input->selection_start = input->selection_end = input->cursor_pos;
                    }
                    textinput_ensure_cursor_visible(input);
                    break;

                case VG_KEY_RIGHT:
                    if (has_shift) {
                        if (input->cursor_pos < textinput_char_count(input))
                            input->cursor_pos++;
                        input->selection_end = input->cursor_pos;
                    } else {
                        if (input->selection_start != input->selection_end)
                            input->cursor_pos = input->selection_end;
                        else if (input->cursor_pos < textinput_char_count(input))
                            input->cursor_pos++;
                        input->selection_start = input->selection_end = input->cursor_pos;
                    }
                    textinput_ensure_cursor_visible(input);
                    break;

                case VG_KEY_HOME:
                    if (has_shift) {
                        input->cursor_pos = 0;
                        input->selection_end = 0;
                    } else {
                        input->cursor_pos = 0;
                        input->selection_start = input->selection_end = 0;
                    }
                    textinput_ensure_cursor_visible(input);
                    break;

                case VG_KEY_END:
                    if (has_shift) {
                        input->cursor_pos = textinput_char_count(input);
                        input->selection_end = input->cursor_pos;
                    } else {
                        input->cursor_pos = textinput_char_count(input);
                        input->selection_start = input->selection_end = input->cursor_pos;
                    }
                    break;

                case VG_KEY_ENTER:
                    if (!input->multiline && input->on_commit)
                        input->on_commit(widget, input->text, input->on_commit_data);
                    break;

                default:
                    break;
            }
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_CHAR:
            if (!input->read_only) {
                // Insert typed character
                char utf8[5] = {0};
                // Convert codepoint to UTF-8
                uint32_t cp = event->key.codepoint;
                if (cp < 0x80) {
                    utf8[0] = (char)cp;
                } else if (cp < 0x800) {
                    utf8[0] = (char)(0xC0 | (cp >> 6));
                    utf8[1] = (char)(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    utf8[0] = (char)(0xE0 | (cp >> 12));
                    utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    utf8[2] = (char)(0x80 | (cp & 0x3F));
                } else {
                    utf8[0] = (char)(0xF0 | (cp >> 18));
                    utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    utf8[3] = (char)(0x80 | (cp & 0x3F));
                }
                vg_textinput_insert(input, utf8);
                textinput_push_undo(input);
            }
            return true;

        default:
            break;
    }

    return false;
}

static bool textinput_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

static void textinput_on_focus(vg_widget_t *widget, bool gained) {
    vg_textinput_t *input = (vg_textinput_t *)widget;

    if (gained) {
        // Reset cursor blink
        input->cursor_blink_time = 0;
        input->cursor_visible = true;
    }
}

//=============================================================================
// TextInput API
//=============================================================================

void vg_textinput_set_text(vg_textinput_t *input, const char *text) {
    if (!input)
        return;

    size_t len = text ? strlen(text) : 0;

    if (!ensure_capacity(input, len + 1))
        return;

    if (text) {
        memcpy(input->text, text, len + 1);
    } else {
        input->text[0] = '\0';
    }
    input->text_len = len;
    textinput_set_cursor_internal(input, textinput_char_count(input));
    input->scroll_x = 0.0f;
    input->scroll_y = 0.0f;
    textinput_ensure_cursor_visible(input);

    input->base.needs_paint = true;
}

const char *vg_textinput_get_text(vg_textinput_t *input) {
    return input ? input->text : NULL;
}

/// @brief Textinput set placeholder.
void vg_textinput_set_placeholder(vg_textinput_t *input, const char *placeholder) {
    if (!input)
        return;

    if (input->placeholder) {
        free((void *)input->placeholder);
    }
    input->placeholder = placeholder ? strdup(placeholder) : NULL;
    input->base.needs_paint = true;
}

/// @brief Textinput set on change.
void vg_textinput_set_on_change(vg_textinput_t *input,
                                vg_text_change_callback_t callback,
                                void *user_data) {
    if (!input)
        return;

    input->on_change = callback;
    input->on_change_data = user_data;
}

/// @brief Textinput set on commit.
void vg_textinput_set_on_commit(vg_textinput_t *input,
                                vg_text_change_callback_t callback,
                                void *user_data) {
    if (!input)
        return;

    input->on_commit = callback;
    input->on_commit_data = user_data;
}

/// @brief Textinput set cursor.
void vg_textinput_set_cursor(vg_textinput_t *input, size_t pos) {
    if (!input)
        return;

    textinput_set_cursor_internal(input, pos);
    textinput_ensure_cursor_visible(input);
    input->base.needs_paint = true;
}

/// @brief Textinput select.
void vg_textinput_select(vg_textinput_t *input, size_t start, size_t end) {
    if (!input)
        return;

    input->selection_start = textinput_clamp_char_pos(input, start);
    input->selection_end = textinput_clamp_char_pos(input, end);
    input->cursor_pos = input->selection_end;
    textinput_ensure_cursor_visible(input);
    input->base.needs_paint = true;
}

/// @brief Textinput select all.
void vg_textinput_select_all(vg_textinput_t *input) {
    if (!input)
        return;

    input->selection_start = 0;
    input->selection_end = textinput_char_count(input);
    input->cursor_pos = input->selection_end;
    textinput_ensure_cursor_visible(input);
    input->base.needs_paint = true;
}

/// @brief Textinput insert.
void vg_textinput_insert(vg_textinput_t *input, const char *text) {
    if (!input || !text || input->read_only)
        return;

    // Delete selection first
    if (input->selection_start != input->selection_end) {
        vg_textinput_delete_selection(input);
    }

    size_t insert_len = strlen(text);
    size_t new_len = input->text_len + insert_len;

    // Check max length
    if (input->max_length > 0 && new_len > (size_t)input->max_length) {
        insert_len = (size_t)input->max_length - input->text_len;
        insert_len = textinput_valid_utf8_prefix(text, insert_len);
        if (insert_len == 0)
            return;
        new_len = input->text_len + insert_len;
    }

    if (!ensure_capacity(input, new_len + 1))
        return;

    size_t byte_pos = textinput_byte_offset(input, input->cursor_pos);

    // Make room for new text
    memmove(input->text + byte_pos + insert_len,
            input->text + byte_pos,
            input->text_len - byte_pos + 1);

    // Insert text
    memcpy(input->text + byte_pos, text, insert_len);
    input->text_len = new_len;
    input->cursor_pos += textinput_codepoint_count_in_prefix(text, insert_len);
    input->selection_start = input->selection_end = input->cursor_pos;
    textinput_ensure_cursor_visible(input);

    input->base.needs_paint = true;

    if (input->on_change) {
        input->on_change(&input->base, input->text, input->on_change_data);
    }
}

/// @brief Textinput delete selection.
void vg_textinput_delete_selection(vg_textinput_t *input) {
    if (!input || input->read_only)
        return;
    if (input->selection_start == input->selection_end)
        return;

    size_t start = input->selection_start < input->selection_end ? input->selection_start
                                                                 : input->selection_end;
    size_t end = input->selection_start < input->selection_end ? input->selection_end
                                                               : input->selection_start;
    size_t start_byte = textinput_byte_offset(input, start);
    size_t end_byte = textinput_byte_offset(input, end);

    memmove(input->text + start_byte, input->text + end_byte, input->text_len - end_byte + 1);

    input->text_len -= (end_byte - start_byte);
    input->cursor_pos = start;
    input->selection_start = start;
    input->selection_end = start;
    textinput_ensure_cursor_visible(input);

    input->base.needs_paint = true;

    if (input->on_change) {
        input->on_change(&input->base, input->text, input->on_change_data);
    }
}

/// @brief Textinput get selection.
char *vg_textinput_get_selection(vg_textinput_t *input) {
    if (!input)
        return NULL;
    if (input->selection_start == input->selection_end)
        return NULL;

    size_t start = input->selection_start < input->selection_end ? input->selection_start
                                                                 : input->selection_end;
    size_t end = input->selection_start < input->selection_end ? input->selection_end
                                                               : input->selection_start;
    size_t start_byte = textinput_byte_offset(input, start);
    size_t end_byte = textinput_byte_offset(input, end);

    size_t len = end_byte - start_byte;
    char *result = malloc(len + 1);
    if (!result)
        return NULL;

    memcpy(result, input->text + start_byte, len);
    result[len] = '\0';

    return result;
}

/// @brief Textinput set font.
void vg_textinput_set_font(vg_textinput_t *input, vg_font_t *font, float size) {
    if (!input)
        return;

    input->font = font;
    input->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    textinput_ensure_cursor_visible(input);
    input->base.needs_layout = true;
    input->base.needs_paint = true;
}

/// @brief Textinput tick.
void vg_textinput_tick(vg_textinput_t *input, float dt) {
    if (!input || !(input->base.state & VG_STATE_FOCUSED))
        return;

    input->cursor_blink_time += dt;
    if (input->cursor_blink_time >= CURSOR_BLINK_RATE) {
        input->cursor_blink_time -= CURSOR_BLINK_RATE;
        input->cursor_visible = !input->cursor_visible;
        input->base.needs_paint = true;
    }
}
