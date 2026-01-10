// vg_codeeditor.c - Code editor widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//=============================================================================
// Constants
//=============================================================================

#define INITIAL_LINE_CAPACITY 64
#define INITIAL_TEXT_CAPACITY 256
#define LINE_CAPACITY_GROWTH 2
#define CURSOR_BLINK_RATE 0.5f

//=============================================================================
// Forward Declarations
//=============================================================================

static void codeeditor_destroy(vg_widget_t* widget);
static void codeeditor_measure(vg_widget_t* widget, float available_width, float available_height);
static void codeeditor_paint(vg_widget_t* widget, void* canvas);
static bool codeeditor_handle_event(vg_widget_t* widget, vg_event_t* event);
static bool codeeditor_can_focus(vg_widget_t* widget);
static void codeeditor_on_focus(vg_widget_t* widget, bool gained);

static bool ensure_line_capacity(vg_codeeditor_t* editor, int needed);
static bool ensure_text_capacity(vg_code_line_t* line, size_t needed);

//=============================================================================
// CodeEditor VTable
//=============================================================================

static vg_widget_vtable_t g_codeeditor_vtable = {
    .destroy = codeeditor_destroy,
    .measure = codeeditor_measure,
    .arrange = NULL,
    .paint = codeeditor_paint,
    .handle_event = codeeditor_handle_event,
    .can_focus = codeeditor_can_focus,
    .on_focus = codeeditor_on_focus
};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_line_capacity(vg_codeeditor_t* editor, int needed) {
    if (needed <= editor->line_capacity) return true;

    int new_capacity = editor->line_capacity;
    while (new_capacity < needed) {
        new_capacity *= LINE_CAPACITY_GROWTH;
    }

    vg_code_line_t* new_lines = realloc(editor->lines, new_capacity * sizeof(vg_code_line_t));
    if (!new_lines) return false;

    // Zero new entries
    memset(new_lines + editor->line_capacity, 0,
           (new_capacity - editor->line_capacity) * sizeof(vg_code_line_t));

    editor->lines = new_lines;
    editor->line_capacity = new_capacity;
    return true;
}

static bool ensure_text_capacity(vg_code_line_t* line, size_t needed) {
    if (needed <= line->capacity) return true;

    size_t new_capacity = line->capacity;
    if (new_capacity == 0) new_capacity = INITIAL_TEXT_CAPACITY;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    char* new_text = realloc(line->text, new_capacity);
    if (!new_text) return false;

    line->text = new_text;
    line->capacity = new_capacity;
    return true;
}

static void free_line(vg_code_line_t* line) {
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
}

static void update_gutter_width(vg_codeeditor_t* editor) {
    if (!editor->show_line_numbers || !editor->font) {
        editor->gutter_width = 0;
        return;
    }

    // Calculate width needed for line numbers
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", editor->line_count);
    vg_text_metrics_t metrics;
    vg_font_measure_text(editor->font, editor->font_size, buf, &metrics);
    editor->gutter_width = metrics.width + 20.0f;  // Add padding
}

//=============================================================================
// CodeEditor Implementation
//=============================================================================

vg_codeeditor_t* vg_codeeditor_create(vg_widget_t* parent) {
    vg_codeeditor_t* editor = calloc(1, sizeof(vg_codeeditor_t));
    if (!editor) return NULL;

    // Initialize base widget
    vg_widget_init(&editor->base, VG_WIDGET_CODEEDITOR, &g_codeeditor_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

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
    editor->char_width = 8.0f;  // Default, updated when font is set
    editor->line_height = 18.0f;

    // Gutter
    editor->show_line_numbers = true;
    editor->gutter_width = 50.0f;
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

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &editor->base);
    }

    return editor;
}

static void codeeditor_destroy(vg_widget_t* widget) {
    vg_codeeditor_t* editor = (vg_codeeditor_t*)widget;

    for (int i = 0; i < editor->line_count; i++) {
        free_line(&editor->lines[i]);
    }
    free(editor->lines);
    editor->lines = NULL;
    editor->line_count = 0;
    editor->line_capacity = 0;
}

static void codeeditor_measure(vg_widget_t* widget, float available_width, float available_height) {
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

static void codeeditor_paint(vg_widget_t* widget, void* canvas) {
    vg_codeeditor_t* editor = (vg_codeeditor_t*)widget;

    if (!editor->font) return;

    // Calculate visible lines
    editor->visible_first_line = (int)(editor->scroll_y / editor->line_height);
    editor->visible_line_count = (int)(widget->height / editor->line_height) + 2;

    float content_x = widget->x + editor->gutter_width;
    float content_width = widget->width - editor->gutter_width;

    // Draw background
    // TODO: Use vgfx primitives
    (void)editor->bg_color;

    // Draw gutter background
    if (editor->show_line_numbers) {
        // TODO: Use vgfx primitives
        (void)editor->gutter_bg;
    }

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(editor->font, editor->font_size, &font_metrics);

    // Draw visible lines
    for (int i = editor->visible_first_line;
         i < editor->line_count && i < editor->visible_first_line + editor->visible_line_count;
         i++) {

        float line_y = widget->y + (i - editor->visible_first_line) * editor->line_height
                       - (editor->scroll_y - editor->visible_first_line * editor->line_height);

        // Draw current line highlight
        if (i == editor->cursor_line && (widget->state & VG_STATE_FOCUSED)) {
            // TODO: Use vgfx primitives
            (void)editor->current_line_bg;
        }

        // Draw line number
        if (editor->show_line_numbers) {
            char line_num[16];
            snprintf(line_num, sizeof(line_num), "%d", i + 1);
            vg_text_metrics_t num_metrics;
            vg_font_measure_text(editor->font, editor->font_size, line_num, &num_metrics);

            float num_x = widget->x + editor->gutter_width - num_metrics.width - 8.0f;
            float num_y = line_y + font_metrics.ascent;

            vg_font_draw_text(canvas, editor->font, editor->font_size,
                              num_x, num_y, line_num, editor->line_number_color);
        }

        // Draw selection on this line
        if (editor->has_selection && (widget->state & VG_STATE_FOCUSED)) {
            int sel_start_line = editor->selection.start_line;
            int sel_start_col = editor->selection.start_col;
            int sel_end_line = editor->selection.end_line;
            int sel_end_col = editor->selection.end_col;

            // Normalize selection
            if (sel_start_line > sel_end_line ||
                (sel_start_line == sel_end_line && sel_start_col > sel_end_col)) {
                int tmp = sel_start_line; sel_start_line = sel_end_line; sel_end_line = tmp;
                tmp = sel_start_col; sel_start_col = sel_end_col; sel_end_col = tmp;
            }

            if (i >= sel_start_line && i <= sel_end_line) {
                int col_start = (i == sel_start_line) ? sel_start_col : 0;
                int col_end = (i == sel_end_line) ? sel_end_col : (int)editor->lines[i].length;

                float sel_x = content_x + col_start * editor->char_width - editor->scroll_x;
                float sel_width = (col_end - col_start) * editor->char_width;

                // Draw selection rectangle
                // TODO: Use vgfx primitives
                (void)sel_x;
                (void)sel_width;
                (void)editor->selection_color;
            }
        }

        // Draw line text
        if (editor->lines[i].text && editor->lines[i].length > 0) {
            float text_x = content_x - editor->scroll_x;
            float text_y = line_y + font_metrics.ascent;

            // Apply syntax highlighting colors if available
            if (editor->lines[i].colors) {
                // Draw character by character with colors
                for (size_t c = 0; c < editor->lines[i].length; c++) {
                    char ch[2] = { editor->lines[i].text[c], '\0' };
                    vg_font_draw_text(canvas, editor->font, editor->font_size,
                                      text_x + c * editor->char_width, text_y,
                                      ch, editor->lines[i].colors[c]);
                }
            } else {
                // Draw entire line
                vg_font_draw_text(canvas, editor->font, editor->font_size,
                                  text_x, text_y, editor->lines[i].text, editor->text_color);
            }
        }

        (void)content_width;
    }

    // Draw cursor
    if ((widget->state & VG_STATE_FOCUSED) && editor->cursor_visible && !editor->read_only) {
        int visible_cursor_line = editor->cursor_line - editor->visible_first_line;
        if (visible_cursor_line >= 0 && visible_cursor_line < editor->visible_line_count) {
            float cursor_x = content_x + editor->cursor_col * editor->char_width - editor->scroll_x;
            float cursor_y = widget->y + visible_cursor_line * editor->line_height
                            - (editor->scroll_y - editor->visible_first_line * editor->line_height);

            // Draw cursor line
            // TODO: Use vgfx primitives
            (void)cursor_x;
            (void)cursor_y;
            (void)editor->cursor_color;
        }
    }
}

static void insert_char(vg_codeeditor_t* editor, char c) {
    vg_code_line_t* line = &editor->lines[editor->cursor_line];

    if (!ensure_text_capacity(line, line->length + 2)) return;

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

static void insert_newline(vg_codeeditor_t* editor) {
    if (!ensure_line_capacity(editor, editor->line_count + 1)) return;

    // Move lines down
    memmove(&editor->lines[editor->cursor_line + 2],
            &editor->lines[editor->cursor_line + 1],
            (editor->line_count - editor->cursor_line - 1) * sizeof(vg_code_line_t));

    // Split current line
    vg_code_line_t* current = &editor->lines[editor->cursor_line];
    vg_code_line_t* next = &editor->lines[editor->cursor_line + 1];

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

    update_gutter_width(editor);
}

static void delete_char_backward(vg_codeeditor_t* editor) {
    if (editor->cursor_col > 0) {
        vg_code_line_t* line = &editor->lines[editor->cursor_line];
        memmove(line->text + editor->cursor_col - 1,
                line->text + editor->cursor_col,
                line->length - editor->cursor_col + 1);
        line->length--;
        editor->cursor_col--;
        editor->modified = true;
        line->modified = true;
    } else if (editor->cursor_line > 0) {
        // Join with previous line
        vg_code_line_t* current = &editor->lines[editor->cursor_line];
        vg_code_line_t* prev = &editor->lines[editor->cursor_line - 1];

        size_t new_col = prev->length;

        if (!ensure_text_capacity(prev, prev->length + current->length + 1)) return;

        memcpy(prev->text + prev->length, current->text, current->length + 1);
        prev->length += current->length;

        // Free current line
        free_line(current);

        // Move lines up
        memmove(&editor->lines[editor->cursor_line],
                &editor->lines[editor->cursor_line + 1],
                (editor->line_count - editor->cursor_line - 1) * sizeof(vg_code_line_t));

        editor->line_count--;
        editor->cursor_line--;
        editor->cursor_col = (int)new_col;
        editor->modified = true;
        prev->modified = true;

        update_gutter_width(editor);
    }
}

static bool codeeditor_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_codeeditor_t* editor = (vg_codeeditor_t*)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN: {
            float content_x = editor->gutter_width;
            float local_x = event->mouse.x - content_x + editor->scroll_x;
            float local_y = event->mouse.y + editor->scroll_y;

            // Calculate clicked line and column
            int line = (int)(local_y / editor->line_height);
            if (line < 0) line = 0;
            if (line >= editor->line_count) line = editor->line_count - 1;

            int col = (int)(local_x / editor->char_width + 0.5f);
            if (col < 0) col = 0;
            if (col > (int)editor->lines[line].length) col = (int)editor->lines[line].length;

            editor->cursor_line = line;
            editor->cursor_col = col;
            editor->has_selection = false;
            editor->cursor_visible = true;
            widget->needs_paint = true;
            return true;
        }

        case VG_EVENT_KEY_DOWN:
            if (editor->read_only) {
                // Navigation only
                switch (event->key.key) {
                    case VG_KEY_UP:
                        if (editor->cursor_line > 0) editor->cursor_line--;
                        break;
                    case VG_KEY_DOWN:
                        if (editor->cursor_line < editor->line_count - 1) editor->cursor_line++;
                        break;
                    case VG_KEY_LEFT:
                        if (editor->cursor_col > 0) editor->cursor_col--;
                        break;
                    case VG_KEY_RIGHT:
                        if (editor->cursor_col < (int)editor->lines[editor->cursor_line].length) editor->cursor_col++;
                        break;
                    default:
                        break;
                }
                widget->needs_paint = true;
                return true;
            }

            switch (event->key.key) {
                case VG_KEY_UP:
                    if (editor->cursor_line > 0) {
                        editor->cursor_line--;
                        if (editor->cursor_col > (int)editor->lines[editor->cursor_line].length) {
                            editor->cursor_col = (int)editor->lines[editor->cursor_line].length;
                        }
                    }
                    break;
                case VG_KEY_DOWN:
                    if (editor->cursor_line < editor->line_count - 1) {
                        editor->cursor_line++;
                        if (editor->cursor_col > (int)editor->lines[editor->cursor_line].length) {
                            editor->cursor_col = (int)editor->lines[editor->cursor_line].length;
                        }
                    }
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
                case VG_KEY_BACKSPACE:
                    if (editor->has_selection) {
                        vg_codeeditor_delete_selection(editor);
                    } else {
                        delete_char_backward(editor);
                    }
                    break;
                case VG_KEY_ENTER:
                    insert_newline(editor);
                    break;
                case VG_KEY_TAB:
                    if (editor->use_spaces) {
                        for (int i = 0; i < editor->tab_width; i++) {
                            insert_char(editor, ' ');
                        }
                    } else {
                        insert_char(editor, '\t');
                    }
                    break;
                default:
                    break;
            }

            editor->cursor_visible = true;
            editor->has_selection = false;
            widget->needs_paint = true;
            return true;

        case VG_EVENT_KEY_CHAR:
            if (!editor->read_only && event->key.codepoint >= 32 && event->key.codepoint < 127) {
                if (editor->has_selection) {
                    vg_codeeditor_delete_selection(editor);
                }
                insert_char(editor, (char)event->key.codepoint);
                widget->needs_paint = true;
            }
            return true;

        case VG_EVENT_MOUSE_WHEEL:
            editor->scroll_y -= event->wheel.delta_y * editor->line_height * 3;
            if (editor->scroll_y < 0) editor->scroll_y = 0;
            float max_scroll = (editor->line_count - 1) * editor->line_height;
            if (editor->scroll_y > max_scroll) editor->scroll_y = max_scroll;
            widget->needs_paint = true;
            return true;

        default:
            break;
    }

    return false;
}

static bool codeeditor_can_focus(vg_widget_t* widget) {
    return widget->enabled && widget->visible;
}

static void codeeditor_on_focus(vg_widget_t* widget, bool gained) {
    vg_codeeditor_t* editor = (vg_codeeditor_t*)widget;
    if (gained) {
        editor->cursor_visible = true;
        editor->cursor_blink_time = 0;
    }
}

//=============================================================================
// CodeEditor API
//=============================================================================

void vg_codeeditor_set_text(vg_codeeditor_t* editor, const char* text) {
    if (!editor) return;

    // Clear existing lines
    for (int i = 0; i < editor->line_count; i++) {
        free_line(&editor->lines[i]);
    }
    editor->line_count = 0;

    if (!text || !text[0]) {
        // Create empty line
        if (!ensure_line_capacity(editor, 1)) return;
        editor->lines[0].text = malloc(1);
        if (editor->lines[0].text) {
            editor->lines[0].text[0] = '\0';
            editor->lines[0].length = 0;
            editor->lines[0].capacity = 1;
        }
        editor->line_count = 1;
    } else {
        // Parse text into lines
        const char* start = text;
        while (*start) {
            const char* end = strchr(start, '\n');
            size_t len = end ? (size_t)(end - start) : strlen(start);

            if (!ensure_line_capacity(editor, editor->line_count + 1)) break;

            vg_code_line_t* line = &editor->lines[editor->line_count];
            line->text = malloc(len + 1);
            if (line->text) {
                memcpy(line->text, start, len);
                line->text[len] = '\0';
                line->length = len;
                line->capacity = len + 1;
            }
            editor->line_count++;

            if (!end) break;
            start = end + 1;
        }

        // Ensure at least one line
        if (editor->line_count == 0) {
            if (ensure_line_capacity(editor, 1)) {
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
    editor->scroll_x = 0;
    editor->scroll_y = 0;
    editor->modified = false;

    update_gutter_width(editor);
    editor->base.needs_paint = true;
}

char* vg_codeeditor_get_text(vg_codeeditor_t* editor) {
    if (!editor) return NULL;

    // Calculate total size
    size_t total = 0;
    for (int i = 0; i < editor->line_count; i++) {
        total += editor->lines[i].length + 1;  // +1 for newline or null
    }

    char* result = malloc(total);
    if (!result) return NULL;

    char* p = result;
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

char* vg_codeeditor_get_selection(vg_codeeditor_t* editor) {
    if (!editor || !editor->has_selection) return NULL;

    // TODO: Implement selection extraction
    return NULL;
}

void vg_codeeditor_set_cursor(vg_codeeditor_t* editor, int line, int col) {
    if (!editor) return;

    if (line < 0) line = 0;
    if (line >= editor->line_count) line = editor->line_count - 1;
    if (col < 0) col = 0;
    if (col > (int)editor->lines[line].length) col = (int)editor->lines[line].length;

    editor->cursor_line = line;
    editor->cursor_col = col;
    editor->has_selection = false;
    editor->base.needs_paint = true;
}

void vg_codeeditor_get_cursor(vg_codeeditor_t* editor, int* out_line, int* out_col) {
    if (!editor) return;
    if (out_line) *out_line = editor->cursor_line;
    if (out_col) *out_col = editor->cursor_col;
}

void vg_codeeditor_set_selection(vg_codeeditor_t* editor,
                                  int start_line, int start_col,
                                  int end_line, int end_col) {
    if (!editor) return;

    editor->selection.start_line = start_line;
    editor->selection.start_col = start_col;
    editor->selection.end_line = end_line;
    editor->selection.end_col = end_col;
    editor->has_selection = true;
    editor->cursor_line = end_line;
    editor->cursor_col = end_col;
    editor->base.needs_paint = true;
}

void vg_codeeditor_insert_text(vg_codeeditor_t* editor, const char* text) {
    if (!editor || !text) return;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            insert_newline(editor);
        } else if (*p >= 32) {
            insert_char(editor, *p);
        }
    }
    editor->base.needs_paint = true;
}

void vg_codeeditor_delete_selection(vg_codeeditor_t* editor) {
    if (!editor || !editor->has_selection) return;

    // TODO: Implement full selection deletion
    editor->has_selection = false;
    editor->modified = true;
    editor->base.needs_paint = true;
}

void vg_codeeditor_scroll_to_line(vg_codeeditor_t* editor, int line) {
    if (!editor) return;

    float target_y = line * editor->line_height;
    float visible_height = editor->base.height;

    if (target_y < editor->scroll_y) {
        editor->scroll_y = target_y;
    } else if (target_y + editor->line_height > editor->scroll_y + visible_height) {
        editor->scroll_y = target_y + editor->line_height - visible_height;
    }

    editor->base.needs_paint = true;
}

void vg_codeeditor_set_syntax(vg_codeeditor_t* editor,
                               vg_syntax_callback_t callback, void* user_data) {
    if (!editor) return;
    editor->syntax_highlighter = callback;
    editor->syntax_data = user_data;
}

void vg_codeeditor_undo(vg_codeeditor_t* editor) {
    // TODO: Implement undo
    (void)editor;
}

void vg_codeeditor_redo(vg_codeeditor_t* editor) {
    // TODO: Implement redo
    (void)editor;
}

void vg_codeeditor_set_font(vg_codeeditor_t* editor, vg_font_t* font, float size) {
    if (!editor) return;

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

    update_gutter_width(editor);
    editor->base.needs_layout = true;
    editor->base.needs_paint = true;
}

int vg_codeeditor_get_line_count(vg_codeeditor_t* editor) {
    return editor ? editor->line_count : 0;
}

bool vg_codeeditor_is_modified(vg_codeeditor_t* editor) {
    return editor ? editor->modified : false;
}

void vg_codeeditor_clear_modified(vg_codeeditor_t* editor) {
    if (editor) {
        editor->modified = false;
        for (int i = 0; i < editor->line_count; i++) {
            editor->lines[i].modified = false;
        }
    }
}
