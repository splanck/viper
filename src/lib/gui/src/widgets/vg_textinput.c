// vg_textinput.c - Text input widget implementation
#include "../../include/vg_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include "../../../graphics/include/vgfx.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Constants
//=============================================================================

#define TEXTINPUT_INITIAL_CAPACITY 64
#define TEXTINPUT_GROWTH_FACTOR 2
#define CURSOR_BLINK_RATE 0.5f  // Seconds

//=============================================================================
// Forward Declarations
//=============================================================================

static void textinput_destroy(vg_widget_t* widget);
static void textinput_measure(vg_widget_t* widget, float available_width, float available_height);
static void textinput_paint(vg_widget_t* widget, void* canvas);
static bool textinput_handle_event(vg_widget_t* widget, vg_event_t* event);
static bool textinput_can_focus(vg_widget_t* widget);
static void textinput_on_focus(vg_widget_t* widget, bool gained);

// Forward declaration for clipboard support
char* vg_textinput_get_selection(vg_textinput_t* input);

//=============================================================================
// TextInput VTable
//=============================================================================

static vg_widget_vtable_t g_textinput_vtable = {
    .destroy = textinput_destroy,
    .measure = textinput_measure,
    .arrange = NULL,
    .paint = textinput_paint,
    .handle_event = textinput_handle_event,
    .can_focus = textinput_can_focus,
    .on_focus = textinput_on_focus
};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_capacity(vg_textinput_t* input, size_t needed) {
    if (needed <= input->text_capacity) return true;

    size_t new_capacity = input->text_capacity;
    while (new_capacity < needed) {
        new_capacity *= TEXTINPUT_GROWTH_FACTOR;
    }

    char* new_text = realloc(input->text, new_capacity);
    if (!new_text) return false;

    input->text = new_text;
    input->text_capacity = new_capacity;
    return true;
}

//=============================================================================
// TextInput Implementation
//=============================================================================

vg_textinput_t* vg_textinput_create(vg_widget_t* parent) {
    vg_textinput_t* input = calloc(1, sizeof(vg_textinput_t));
    if (!input) return NULL;

    // Initialize base widget
    vg_widget_init(&input->base, VG_WIDGET_TEXTINPUT, &g_textinput_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

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

    // Set minimum size
    input->base.constraints.min_height = theme->input.height;
    input->base.constraints.min_width = 100.0f;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &input->base);
    }

    return input;
}

static void textinput_destroy(vg_widget_t* widget) {
    vg_textinput_t* input = (vg_textinput_t*)widget;

    if (input->text) {
        free(input->text);
        input->text = NULL;
    }
    if (input->placeholder) {
        free((void*)input->placeholder);
        input->placeholder = NULL;
    }
}

static void textinput_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_textinput_t* input = (vg_textinput_t*)widget;
    vg_theme_t* theme = vg_theme_get_current();
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

static void textinput_paint(vg_widget_t* widget, void* canvas) {
    vg_textinput_t* input = (vg_textinput_t*)widget;
    vg_theme_t* theme = vg_theme_get_current();

    // Determine colors based on state
    uint32_t text_color = input->text_color;

    if (widget->state & VG_STATE_DISABLED) {
        text_color = theme->colors.fg_disabled;
    }

    // TODO: Draw background and border using vgfx primitives

    // Calculate text area
    float padding = theme->input.padding_h;
    float text_x = widget->x + padding - input->scroll_x;
    float text_y = widget->y;

    // Draw text or placeholder
    if (!input->font) return;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(input->font, input->font_size, &font_metrics);
    text_y += (widget->height + font_metrics.ascent - font_metrics.descent) / 2.0f;

    const char* display_text = input->text;
    uint32_t display_color = text_color;

    if (input->text_len == 0 && input->placeholder) {
        display_text = input->placeholder;
        display_color = input->placeholder_color;
    }

    // Draw selection highlight if focused
    if ((widget->state & VG_STATE_FOCUSED) && input->selection_start != input->selection_end) {
        // Get positions for selection
        size_t sel_start = input->selection_start < input->selection_end
            ? input->selection_start : input->selection_end;
        size_t sel_end = input->selection_start < input->selection_end
            ? input->selection_end : input->selection_start;

        float start_x = vg_font_get_cursor_x(input->font, input->font_size, input->text, (int)sel_start);
        float end_x = vg_font_get_cursor_x(input->font, input->font_size, input->text, (int)sel_end);

        // Draw selection rectangle
        // TODO: Use vgfx primitives
        (void)start_x;
        (void)end_x;
    }

    // Draw text (with password masking if needed)
    if (input->password_mode && input->text_len > 0) {
        // Draw dots instead of actual text
        // TODO: Create masked string
        vg_font_draw_text(canvas, input->font, input->font_size,
                          text_x, text_y, display_text, display_color);
    } else {
        vg_font_draw_text(canvas, input->font, input->font_size,
                          text_x, text_y, display_text, display_color);
    }

    // Draw cursor if focused and visible (blinking)
    if ((widget->state & VG_STATE_FOCUSED) && input->cursor_visible && !input->read_only) {
        float cursor_x = text_x + vg_font_get_cursor_x(input->font, input->font_size,
                                                        input->text, (int)input->cursor_pos);
        // Draw cursor line
        // TODO: Use vgfx primitives
        (void)cursor_x;
    }
}

static bool textinput_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_textinput_t* input = (vg_textinput_t*)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN:
            // Set focus and cursor position
            if (input->font) {
                float padding = vg_theme_get_current()->input.padding_h;
                float local_x = event->mouse.x - padding + input->scroll_x;
                int char_index = vg_font_hit_test(input->font, input->font_size, input->text, local_x);
                if (char_index >= 0) {
                    input->cursor_pos = (size_t)char_index;
                } else {
                    input->cursor_pos = input->text_len;
                }
                input->selection_start = input->cursor_pos;
                input->selection_end = input->cursor_pos;
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
                            char* selection = vg_textinput_get_selection(input);
                            if (selection) {
                                vgfx_clipboard_set_text(selection);
                                free(selection);
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_X: // Cut
                        if (!input->read_only && input->selection_start != input->selection_end) {
                            char* selection = vg_textinput_get_selection(input);
                            if (selection) {
                                vgfx_clipboard_set_text(selection);
                                free(selection);
                                vg_textinput_delete_selection(input);
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_V: // Paste
                        if (!input->read_only) {
                            char* text = vgfx_clipboard_get_text();
                            if (text) {
                                if (input->selection_start != input->selection_end) {
                                    vg_textinput_delete_selection(input);
                                }
                                vg_textinput_insert(input, text);
                                free(text);
                            }
                        }
                        widget->needs_paint = true;
                        return true;

                    case VG_KEY_A: // Select all
                        input->selection_start = 0;
                        input->selection_end = input->text_len;
                        input->cursor_pos = input->text_len;
                        widget->needs_paint = true;
                        return true;

                    default:
                        break;
                }
            }

            if (input->read_only) {
                // Only allow navigation in read-only mode
                switch (event->key.key) {
                    case VG_KEY_LEFT:
                        if (input->cursor_pos > 0) input->cursor_pos--;
                        break;
                    case VG_KEY_RIGHT:
                        if (input->cursor_pos < input->text_len) input->cursor_pos++;
                        break;
                    case VG_KEY_HOME:
                        input->cursor_pos = 0;
                        break;
                    case VG_KEY_END:
                        input->cursor_pos = input->text_len;
                        break;
                    default:
                        break;
                }
                widget->needs_paint = true;
                return true;
            }

            // Handle editing keys
            switch (event->key.key) {
                case VG_KEY_BACKSPACE:
                    if (input->selection_start != input->selection_end) {
                        vg_textinput_delete_selection(input);
                    } else if (input->cursor_pos > 0) {
                        // Delete character before cursor
                        memmove(input->text + input->cursor_pos - 1,
                                input->text + input->cursor_pos,
                                input->text_len - input->cursor_pos + 1);
                        input->cursor_pos--;
                        input->text_len--;
                        if (input->on_change) {
                            input->on_change(widget, input->text, input->on_change_data);
                        }
                    }
                    break;

                case VG_KEY_DELETE:
                    if (input->selection_start != input->selection_end) {
                        vg_textinput_delete_selection(input);
                    } else if (input->cursor_pos < input->text_len) {
                        // Delete character at cursor
                        memmove(input->text + input->cursor_pos,
                                input->text + input->cursor_pos + 1,
                                input->text_len - input->cursor_pos);
                        input->text_len--;
                        if (input->on_change) {
                            input->on_change(widget, input->text, input->on_change_data);
                        }
                    }
                    break;

                case VG_KEY_LEFT:
                    if (input->cursor_pos > 0) input->cursor_pos--;
                    input->selection_start = input->selection_end = input->cursor_pos;
                    break;

                case VG_KEY_RIGHT:
                    if (input->cursor_pos < input->text_len) input->cursor_pos++;
                    input->selection_start = input->selection_end = input->cursor_pos;
                    break;

                case VG_KEY_HOME:
                    input->cursor_pos = 0;
                    input->selection_start = input->selection_end = 0;
                    break;

                case VG_KEY_END:
                    input->cursor_pos = input->text_len;
                    input->selection_start = input->selection_end = input->text_len;
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
            }
            return true;

        default:
            break;
    }

    return false;
}

static bool textinput_can_focus(vg_widget_t* widget) {
    return widget->enabled && widget->visible;
}

static void textinput_on_focus(vg_widget_t* widget, bool gained) {
    vg_textinput_t* input = (vg_textinput_t*)widget;

    if (gained) {
        // Reset cursor blink
        input->cursor_blink_time = 0;
        input->cursor_visible = true;
    }
}

//=============================================================================
// TextInput API
//=============================================================================

void vg_textinput_set_text(vg_textinput_t* input, const char* text) {
    if (!input) return;

    size_t len = text ? strlen(text) : 0;

    if (!ensure_capacity(input, len + 1)) return;

    if (text) {
        memcpy(input->text, text, len + 1);
    } else {
        input->text[0] = '\0';
    }
    input->text_len = len;
    input->cursor_pos = len;
    input->selection_start = len;
    input->selection_end = len;

    input->base.needs_paint = true;

    if (input->on_change) {
        input->on_change(&input->base, input->text, input->on_change_data);
    }
}

const char* vg_textinput_get_text(vg_textinput_t* input) {
    return input ? input->text : NULL;
}

void vg_textinput_set_placeholder(vg_textinput_t* input, const char* placeholder) {
    if (!input) return;

    if (input->placeholder) {
        free((void*)input->placeholder);
    }
    input->placeholder = placeholder ? strdup(placeholder) : NULL;
    input->base.needs_paint = true;
}

void vg_textinput_set_on_change(vg_textinput_t* input, vg_text_change_callback_t callback, void* user_data) {
    if (!input) return;

    input->on_change = callback;
    input->on_change_data = user_data;
}

void vg_textinput_set_cursor(vg_textinput_t* input, size_t pos) {
    if (!input) return;

    if (pos > input->text_len) pos = input->text_len;
    input->cursor_pos = pos;
    input->selection_start = pos;
    input->selection_end = pos;
    input->base.needs_paint = true;
}

void vg_textinput_select(vg_textinput_t* input, size_t start, size_t end) {
    if (!input) return;

    if (start > input->text_len) start = input->text_len;
    if (end > input->text_len) end = input->text_len;

    input->selection_start = start;
    input->selection_end = end;
    input->cursor_pos = end;
    input->base.needs_paint = true;
}

void vg_textinput_select_all(vg_textinput_t* input) {
    if (!input) return;

    input->selection_start = 0;
    input->selection_end = input->text_len;
    input->cursor_pos = input->text_len;
    input->base.needs_paint = true;
}

void vg_textinput_insert(vg_textinput_t* input, const char* text) {
    if (!input || !text || input->read_only) return;

    // Delete selection first
    if (input->selection_start != input->selection_end) {
        vg_textinput_delete_selection(input);
    }

    size_t insert_len = strlen(text);
    size_t new_len = input->text_len + insert_len;

    // Check max length
    if (input->max_length > 0 && new_len > (size_t)input->max_length) {
        insert_len = (size_t)input->max_length - input->text_len;
        if (insert_len == 0) return;
        new_len = (size_t)input->max_length;
    }

    if (!ensure_capacity(input, new_len + 1)) return;

    // Make room for new text
    memmove(input->text + input->cursor_pos + insert_len,
            input->text + input->cursor_pos,
            input->text_len - input->cursor_pos + 1);

    // Insert text
    memcpy(input->text + input->cursor_pos, text, insert_len);
    input->text_len = new_len;
    input->cursor_pos += insert_len;
    input->selection_start = input->selection_end = input->cursor_pos;

    input->base.needs_paint = true;

    if (input->on_change) {
        input->on_change(&input->base, input->text, input->on_change_data);
    }
}

void vg_textinput_delete_selection(vg_textinput_t* input) {
    if (!input || input->read_only) return;
    if (input->selection_start == input->selection_end) return;

    size_t start = input->selection_start < input->selection_end
        ? input->selection_start : input->selection_end;
    size_t end = input->selection_start < input->selection_end
        ? input->selection_end : input->selection_start;

    memmove(input->text + start,
            input->text + end,
            input->text_len - end + 1);

    input->text_len -= (end - start);
    input->cursor_pos = start;
    input->selection_start = start;
    input->selection_end = start;

    input->base.needs_paint = true;

    if (input->on_change) {
        input->on_change(&input->base, input->text, input->on_change_data);
    }
}

char* vg_textinput_get_selection(vg_textinput_t* input) {
    if (!input) return NULL;
    if (input->selection_start == input->selection_end) return NULL;

    size_t start = input->selection_start < input->selection_end
        ? input->selection_start : input->selection_end;
    size_t end = input->selection_start < input->selection_end
        ? input->selection_end : input->selection_start;

    size_t len = end - start;
    char* result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, input->text + start, len);
    result[len] = '\0';

    return result;
}

void vg_textinput_set_font(vg_textinput_t* input, vg_font_t* font, float size) {
    if (!input) return;

    input->font = font;
    input->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    input->base.needs_layout = true;
    input->base.needs_paint = true;
}
