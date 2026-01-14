// vg_findreplacebar.c - Find/Replace bar widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

//=============================================================================
// Constants
//=============================================================================

#define FINDREPLACEBAR_HEIGHT 36
#define FINDREPLACEBAR_HEIGHT_REPLACE 72
#define INPUT_WIDTH 200
#define BUTTON_WIDTH 24
#define PADDING 4
#define INITIAL_MATCH_CAPACITY 64

//=============================================================================
// Forward Declarations
//=============================================================================

static void findreplacebar_destroy(vg_widget_t* widget);
static void findreplacebar_measure(vg_widget_t* widget, float available_width, float available_height);
static void findreplacebar_arrange(vg_widget_t* widget, float x, float y, float width, float height);
static void findreplacebar_paint(vg_widget_t* widget, void* canvas);
static bool findreplacebar_handle_event(vg_widget_t* widget, vg_event_t* event);

static void perform_search(vg_findreplacebar_t* bar);
static void clear_matches(vg_findreplacebar_t* bar);
static void add_match(vg_findreplacebar_t* bar, uint32_t line, uint32_t start, uint32_t end);
static const char* find_in_line(const char* text, const char* query,
    vg_search_options_t* options, size_t* match_len);
static void highlight_current_match(vg_findreplacebar_t* bar);
static void update_result_text(vg_findreplacebar_t* bar);

// Button callbacks
static void on_find_prev_click(vg_widget_t* btn, void* user_data);
static void on_find_next_click(vg_widget_t* btn, void* user_data);
static void on_replace_click(vg_widget_t* btn, void* user_data);
static void on_replace_all_click(vg_widget_t* btn, void* user_data);
static void on_close_click(vg_widget_t* btn, void* user_data);
static void on_option_change(vg_widget_t* cb, bool checked, void* user_data);
static void on_find_text_change(vg_widget_t* input, const char* text, void* user_data);

//=============================================================================
// FindReplaceBar VTable
//=============================================================================

static vg_widget_vtable_t g_findreplacebar_vtable = {
    .destroy = findreplacebar_destroy,
    .measure = findreplacebar_measure,
    .arrange = findreplacebar_arrange,
    .paint = findreplacebar_paint,
    .handle_event = findreplacebar_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Helper Functions - Case Insensitive String Search
//=============================================================================

static const char* strcasestr_custom(const char* haystack, const char* needle) {
    if (!*needle) return haystack;

    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;

        while (*n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }

        if (!*n) return haystack;
    }
    return NULL;
}

static bool is_word_boundary(char c) {
    return !isalnum((unsigned char)c) && c != '_';
}

static bool check_whole_word(const char* text, const char* match, size_t match_len) {
    // Check character before match
    if (match > text && !is_word_boundary(match[-1])) {
        return false;
    }
    // Check character after match
    if (match[match_len] && !is_word_boundary(match[match_len])) {
        return false;
    }
    return true;
}

//=============================================================================
// Search Implementation
//=============================================================================

static const char* find_in_line(const char* text, const char* query,
    vg_search_options_t* options, size_t* match_len) {

    if (!text || !query || !*query) return NULL;

    *match_len = strlen(query);
    const char* pos = text;

    while (*pos) {
        const char* found;

        if (options->case_sensitive) {
            found = strstr(pos, query);
        } else {
            found = strcasestr_custom(pos, query);
        }

        if (!found) return NULL;

        // Check whole word if required
        if (options->whole_word) {
            if (!check_whole_word(text, found, *match_len)) {
                pos = found + 1;
                continue;
            }
        }

        return found;
    }

    return NULL;
}

static void add_match(vg_findreplacebar_t* bar, uint32_t line, uint32_t start, uint32_t end) {
    // Grow array if needed
    if (bar->match_count >= bar->match_capacity) {
        size_t new_cap = bar->match_capacity * 2;
        if (new_cap < INITIAL_MATCH_CAPACITY) new_cap = INITIAL_MATCH_CAPACITY;

        vg_search_match_t* new_matches = realloc(bar->matches,
            new_cap * sizeof(vg_search_match_t));
        if (!new_matches) return;

        bar->matches = new_matches;
        bar->match_capacity = new_cap;
    }

    bar->matches[bar->match_count].line = line;
    bar->matches[bar->match_count].start_col = start;
    bar->matches[bar->match_count].end_col = end;
    bar->match_count++;
}

static void clear_matches(vg_findreplacebar_t* bar) {
    bar->match_count = 0;
    bar->current_match = 0;
    snprintf(bar->result_text, sizeof(bar->result_text), "");
}

static void perform_search(vg_findreplacebar_t* bar) {
    clear_matches(bar);

    if (!bar->target_editor) return;

    vg_textinput_t* find_input = (vg_textinput_t*)bar->find_input;
    if (!find_input) return;

    const char* query = vg_textinput_get_text(find_input);
    if (!query || !*query) {
        update_result_text(bar);
        return;
    }

    vg_codeeditor_t* ed = bar->target_editor;

    // Search through editor lines
    for (int line = 0; line < ed->line_count; line++) {
        const char* text = ed->lines[line].text;
        if (!text) continue;

        const char* pos = text;
        size_t match_len;

        while ((pos = find_in_line(pos, query, &bar->options, &match_len)) != NULL) {
            uint32_t start_col = (uint32_t)(pos - text);
            uint32_t end_col = start_col + (uint32_t)match_len;
            add_match(bar, (uint32_t)line, start_col, end_col);
            pos++;
        }
    }

    // Update result text and highlight
    if (bar->match_count > 0) {
        bar->current_match = 0;
        highlight_current_match(bar);
    }
    update_result_text(bar);

    // Call callback if set
    if (bar->on_find) {
        bar->on_find(bar, query, &bar->options, bar->user_data);
    }
}

static void update_result_text(vg_findreplacebar_t* bar) {
    if (bar->match_count == 0) {
        vg_textinput_t* find_input = (vg_textinput_t*)bar->find_input;
        const char* query = find_input ? vg_textinput_get_text(find_input) : NULL;
        if (query && *query) {
            snprintf(bar->result_text, sizeof(bar->result_text), "No results");
        } else {
            bar->result_text[0] = '\0';
        }
    } else {
        snprintf(bar->result_text, sizeof(bar->result_text),
            "%zu of %zu", bar->current_match + 1, bar->match_count);
    }
}

static void highlight_current_match(vg_findreplacebar_t* bar) {
    if (bar->match_count == 0 || !bar->target_editor) return;

    vg_search_match_t* match = &bar->matches[bar->current_match];
    vg_codeeditor_t* ed = bar->target_editor;

    // Set selection to current match
    vg_codeeditor_set_selection(ed,
        (int)match->line, (int)match->start_col,
        (int)match->line, (int)match->end_col);

    // Scroll to make match visible
    vg_codeeditor_scroll_to_line(ed, (int)match->line);
}

//=============================================================================
// Button Callbacks
//=============================================================================

static void on_find_prev_click(vg_widget_t* btn, void* user_data) {
    (void)btn;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;
    vg_findreplacebar_find_prev(bar);
}

static void on_find_next_click(vg_widget_t* btn, void* user_data) {
    (void)btn;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;
    vg_findreplacebar_find_next(bar);
}

static void on_replace_click(vg_widget_t* btn, void* user_data) {
    (void)btn;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;
    vg_findreplacebar_replace_current(bar);
}

static void on_replace_all_click(vg_widget_t* btn, void* user_data) {
    (void)btn;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;
    vg_findreplacebar_replace_all(bar);
}

static void on_close_click(vg_widget_t* btn, void* user_data) {
    (void)btn;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;
    bar->base.visible = false;

    if (bar->on_close) {
        bar->on_close(bar, bar->user_data);
    }
}

static void on_option_change(vg_widget_t* cb, bool checked, void* user_data) {
    (void)checked;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;

    // Update options from checkboxes
    vg_checkbox_t* case_cb = (vg_checkbox_t*)bar->case_sensitive_cb;
    vg_checkbox_t* word_cb = (vg_checkbox_t*)bar->whole_word_cb;
    vg_checkbox_t* regex_cb = (vg_checkbox_t*)bar->regex_cb;

    if (case_cb && cb == (vg_widget_t*)case_cb) {
        bar->options.case_sensitive = vg_checkbox_is_checked(case_cb);
    }
    if (word_cb && cb == (vg_widget_t*)word_cb) {
        bar->options.whole_word = vg_checkbox_is_checked(word_cb);
    }
    if (regex_cb && cb == (vg_widget_t*)regex_cb) {
        bar->options.use_regex = vg_checkbox_is_checked(regex_cb);
    }

    // Re-run search with new options
    perform_search(bar);
}

static void on_find_text_change(vg_widget_t* input, const char* text, void* user_data) {
    (void)input;
    (void)text;
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)user_data;
    perform_search(bar);
}

//=============================================================================
// Widget Implementation
//=============================================================================

vg_findreplacebar_t* vg_findreplacebar_create(void) {
    vg_findreplacebar_t* bar = calloc(1, sizeof(vg_findreplacebar_t));
    if (!bar) return NULL;

    // Initialize base widget
    vg_widget_init(&bar->base, VG_WIDGET_CUSTOM, &g_findreplacebar_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Set default colors
    bar->bg_color = theme->colors.bg_secondary;
    bar->border_color = theme->colors.border_primary;
    bar->match_highlight = 0x40FFFF00;  // Yellow, semi-transparent
    bar->current_highlight = 0x80FF9900;  // Orange, semi-transparent
    bar->font_size = theme->typography.size_normal;

    // Default options
    bar->options.wrap_around = true;

    // Create child widgets
    bar->find_input = vg_textinput_create(&bar->base);
    if (bar->find_input) {
        vg_textinput_set_placeholder((vg_textinput_t*)bar->find_input, "Find");
        vg_textinput_set_on_change((vg_textinput_t*)bar->find_input,
            on_find_text_change, bar);
    }

    bar->replace_input = vg_textinput_create(&bar->base);
    if (bar->replace_input) {
        vg_textinput_set_placeholder((vg_textinput_t*)bar->replace_input, "Replace");
    }

    // Create buttons with simple text labels
    bar->find_prev_btn = vg_button_create(&bar->base, "<");
    if (bar->find_prev_btn) {
        vg_button_set_on_click((vg_button_t*)bar->find_prev_btn,
            on_find_prev_click, bar);
    }

    bar->find_next_btn = vg_button_create(&bar->base, ">");
    if (bar->find_next_btn) {
        vg_button_set_on_click((vg_button_t*)bar->find_next_btn,
            on_find_next_click, bar);
    }

    bar->replace_btn = vg_button_create(&bar->base, "Replace");
    if (bar->replace_btn) {
        vg_button_set_on_click((vg_button_t*)bar->replace_btn,
            on_replace_click, bar);
    }

    bar->replace_all_btn = vg_button_create(&bar->base, "All");
    if (bar->replace_all_btn) {
        vg_button_set_on_click((vg_button_t*)bar->replace_all_btn,
            on_replace_all_click, bar);
    }

    bar->close_btn = vg_button_create(&bar->base, "X");
    if (bar->close_btn) {
        vg_button_set_on_click((vg_button_t*)bar->close_btn,
            on_close_click, bar);
    }

    // Create option checkboxes
    bar->case_sensitive_cb = vg_checkbox_create(&bar->base, "Aa");
    if (bar->case_sensitive_cb) {
        vg_checkbox_set_on_change((vg_checkbox_t*)bar->case_sensitive_cb,
            on_option_change, bar);
    }

    bar->whole_word_cb = vg_checkbox_create(&bar->base, "W");
    if (bar->whole_word_cb) {
        vg_checkbox_set_on_change((vg_checkbox_t*)bar->whole_word_cb,
            on_option_change, bar);
    }

    bar->regex_cb = vg_checkbox_create(&bar->base, ".*");
    if (bar->regex_cb) {
        vg_checkbox_set_on_change((vg_checkbox_t*)bar->regex_cb,
            on_option_change, bar);
    }

    return bar;
}

static void findreplacebar_destroy(vg_widget_t* widget) {
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)widget;

    // Free matches array
    free(bar->matches);

    // Child widgets are destroyed by parent destruction
}

static void findreplacebar_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)widget;
    (void)available_height;

    widget->measured_width = available_width;
    widget->measured_height = bar->show_replace ?
        FINDREPLACEBAR_HEIGHT_REPLACE : FINDREPLACEBAR_HEIGHT;
}

static void findreplacebar_arrange(vg_widget_t* widget, float x, float y, float width, float height) {
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    // Layout first row: Find input, prev/next buttons, options, close
    float row_y = y + PADDING;
    float cur_x = x + PADDING;
    float row_height = FINDREPLACEBAR_HEIGHT - PADDING * 2;

    // Find input
    if (bar->find_input) {
        vg_widget_t* w = (vg_widget_t*)bar->find_input;
        vg_widget_arrange(w, cur_x, row_y + 4, INPUT_WIDTH, row_height - 8);
        cur_x += INPUT_WIDTH + PADDING;
    }

    // Prev button
    if (bar->find_prev_btn) {
        vg_widget_t* w = (vg_widget_t*)bar->find_prev_btn;
        vg_widget_arrange(w, cur_x, row_y + 4, BUTTON_WIDTH, row_height - 8);
        cur_x += BUTTON_WIDTH + PADDING;
    }

    // Next button
    if (bar->find_next_btn) {
        vg_widget_t* w = (vg_widget_t*)bar->find_next_btn;
        vg_widget_arrange(w, cur_x, row_y + 4, BUTTON_WIDTH, row_height - 8);
        cur_x += BUTTON_WIDTH + PADDING * 2;
    }

    // Case sensitive checkbox
    if (bar->case_sensitive_cb) {
        vg_widget_t* w = (vg_widget_t*)bar->case_sensitive_cb;
        vg_widget_arrange(w, cur_x, row_y + 4, 40, row_height - 8);
        cur_x += 40 + PADDING;
    }

    // Whole word checkbox
    if (bar->whole_word_cb) {
        vg_widget_t* w = (vg_widget_t*)bar->whole_word_cb;
        vg_widget_arrange(w, cur_x, row_y + 4, 32, row_height - 8);
        cur_x += 32 + PADDING;
    }

    // Regex checkbox
    if (bar->regex_cb) {
        vg_widget_t* w = (vg_widget_t*)bar->regex_cb;
        vg_widget_arrange(w, cur_x, row_y + 4, 36, row_height - 8);
        cur_x += 36 + PADDING;
    }

    // Close button at right
    if (bar->close_btn) {
        vg_widget_t* w = (vg_widget_t*)bar->close_btn;
        float close_x = x + width - BUTTON_WIDTH - PADDING;
        vg_widget_arrange(w, close_x, row_y + 4, BUTTON_WIDTH, row_height - 8);
    }

    // Second row (replace mode): Replace input, replace/all buttons
    if (bar->show_replace) {
        row_y = y + FINDREPLACEBAR_HEIGHT;
        cur_x = x + PADDING;

        // Replace input
        if (bar->replace_input) {
            vg_widget_t* w = (vg_widget_t*)bar->replace_input;
            vg_widget_arrange(w, cur_x, row_y + 4, INPUT_WIDTH, row_height - 8);
            cur_x += INPUT_WIDTH + PADDING;
        }

        // Replace button
        if (bar->replace_btn) {
            vg_widget_t* w = (vg_widget_t*)bar->replace_btn;
            vg_widget_arrange(w, cur_x, row_y + 4, 60, row_height - 8);
            cur_x += 60 + PADDING;
        }

        // Replace all button
        if (bar->replace_all_btn) {
            vg_widget_t* w = (vg_widget_t*)bar->replace_all_btn;
            vg_widget_arrange(w, cur_x, row_y + 4, 40, row_height - 8);
        }
    }

    // Hide replace widgets when not in replace mode
    if (bar->replace_input) {
        ((vg_widget_t*)bar->replace_input)->visible = bar->show_replace;
    }
    if (bar->replace_btn) {
        ((vg_widget_t*)bar->replace_btn)->visible = bar->show_replace;
    }
    if (bar->replace_all_btn) {
        ((vg_widget_t*)bar->replace_all_btn)->visible = bar->show_replace;
    }
}

static void findreplacebar_paint(vg_widget_t* widget, void* canvas) {
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)widget;

    // Draw background (placeholder - actual drawing via vgfx)
    (void)bar->bg_color;

    // Draw border (placeholder - actual drawing via vgfx)
    (void)bar->border_color;

    // Draw result text
    if (bar->result_text[0] && bar->font) {
        float text_x = widget->x + INPUT_WIDTH + BUTTON_WIDTH * 2 + PADDING * 5 + 108;
        float text_y = widget->y + (float)FINDREPLACEBAR_HEIGHT / 2.0f - bar->font_size / 2.0f;

        uint32_t text_color = bar->match_count > 0 ? 0xFF00FF00 : 0xFFFF6666;
        vg_font_draw_text(canvas, bar->font, bar->font_size,
            text_x, text_y, bar->result_text, text_color);
    }

    // Paint child widgets
    vg_widget_t* child = widget->first_child;
    while (child) {
        if (child->visible && child->vtable && child->vtable->paint) {
            child->vtable->paint(child, canvas);
        }
        child = child->next_sibling;
    }
}

static bool findreplacebar_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_findreplacebar_t* bar = (vg_findreplacebar_t*)widget;

    // Handle keyboard shortcuts
    if (event->type == VG_EVENT_KEY_DOWN) {
        uint32_t mods = event->modifiers;

        // Escape: close
        if (event->key.key == VG_KEY_ESCAPE) {
            bar->base.visible = false;
            if (bar->on_close) {
                bar->on_close(bar, bar->user_data);
            }
            return true;
        }

        // Enter: find next
        if (event->key.key == VG_KEY_ENTER) {
            if (mods & VG_MOD_SHIFT) {
                vg_findreplacebar_find_prev(bar);
            } else {
                vg_findreplacebar_find_next(bar);
            }
            return true;
        }

        // Ctrl+H: toggle replace mode
        bool has_ctrl = (mods & VG_MOD_CTRL) != 0 || (mods & VG_MOD_SUPER) != 0;
        if (has_ctrl && event->key.key == VG_KEY_H) {
            bar->show_replace = !bar->show_replace;
            vg_widget_invalidate(widget);
            return true;
        }
    }

    // Pass events to children
    vg_widget_t* child = widget->first_child;
    while (child) {
        if (child->visible && child->vtable && child->vtable->handle_event) {
            if (child->vtable->handle_event(child, event)) {
                return true;
            }
        }
        child = child->next_sibling;
    }

    return false;
}

//=============================================================================
// Public API
//=============================================================================

void vg_findreplacebar_destroy(vg_findreplacebar_t* bar) {
    if (!bar) return;
    vg_widget_destroy(&bar->base);
}

void vg_findreplacebar_set_target(vg_findreplacebar_t* bar, struct vg_codeeditor* editor) {
    if (!bar) return;
    bar->target_editor = editor;

    // Re-run search if there's text
    perform_search(bar);
}

void vg_findreplacebar_set_show_replace(vg_findreplacebar_t* bar, bool show) {
    if (!bar) return;
    bar->show_replace = show;
    vg_widget_invalidate(&bar->base);
}

void vg_findreplacebar_set_options(vg_findreplacebar_t* bar, vg_search_options_t* options) {
    if (!bar || !options) return;
    bar->options = *options;

    // Update checkboxes
    if (bar->case_sensitive_cb) {
        vg_checkbox_set_checked((vg_checkbox_t*)bar->case_sensitive_cb,
            options->case_sensitive);
    }
    if (bar->whole_word_cb) {
        vg_checkbox_set_checked((vg_checkbox_t*)bar->whole_word_cb,
            options->whole_word);
    }
    if (bar->regex_cb) {
        vg_checkbox_set_checked((vg_checkbox_t*)bar->regex_cb,
            options->use_regex);
    }

    perform_search(bar);
}

void vg_findreplacebar_find(vg_findreplacebar_t* bar, const char* query) {
    if (!bar) return;

    if (bar->find_input && query) {
        vg_textinput_set_text((vg_textinput_t*)bar->find_input, query);
    }

    perform_search(bar);
}

void vg_findreplacebar_find_next(vg_findreplacebar_t* bar) {
    if (!bar || bar->match_count == 0) return;

    bar->current_match++;
    if (bar->current_match >= bar->match_count) {
        if (bar->options.wrap_around) {
            bar->current_match = 0;
        } else {
            bar->current_match = bar->match_count - 1;
        }
    }

    highlight_current_match(bar);
    update_result_text(bar);
    vg_widget_invalidate(&bar->base);
}

void vg_findreplacebar_find_prev(vg_findreplacebar_t* bar) {
    if (!bar || bar->match_count == 0) return;

    if (bar->current_match == 0) {
        if (bar->options.wrap_around) {
            bar->current_match = bar->match_count - 1;
        }
    } else {
        bar->current_match--;
    }

    highlight_current_match(bar);
    update_result_text(bar);
    vg_widget_invalidate(&bar->base);
}

void vg_findreplacebar_replace_current(vg_findreplacebar_t* bar) {
    if (!bar || bar->match_count == 0 || !bar->target_editor) return;

    vg_textinput_t* replace_input = (vg_textinput_t*)bar->replace_input;
    vg_textinput_t* find_input = (vg_textinput_t*)bar->find_input;
    if (!replace_input || !find_input) return;

    const char* replace_text = vg_textinput_get_text(replace_input);
    const char* find_text = vg_textinput_get_text(find_input);
    if (!replace_text) replace_text = "";

    vg_codeeditor_t* ed = bar->target_editor;

    // Delete selection (current match) and insert replacement
    vg_codeeditor_delete_selection(ed);
    vg_codeeditor_insert_text(ed, replace_text);

    // Callback
    if (bar->on_replace) {
        bar->on_replace(bar, find_text, replace_text, bar->user_data);
    }

    // Re-search
    perform_search(bar);
}

void vg_findreplacebar_replace_all(vg_findreplacebar_t* bar) {
    if (!bar || bar->match_count == 0 || !bar->target_editor) return;

    vg_textinput_t* replace_input = (vg_textinput_t*)bar->replace_input;
    vg_textinput_t* find_input = (vg_textinput_t*)bar->find_input;
    if (!replace_input || !find_input) return;

    const char* replace_text = vg_textinput_get_text(replace_input);
    const char* find_text = vg_textinput_get_text(find_input);
    if (!replace_text) replace_text = "";

    vg_codeeditor_t* ed = bar->target_editor;

    // Replace from end to start to preserve positions
    for (size_t i = bar->match_count; i > 0; i--) {
        vg_search_match_t* match = &bar->matches[i - 1];

        // Select match
        vg_codeeditor_set_selection(ed,
            (int)match->line, (int)match->start_col,
            (int)match->line, (int)match->end_col);

        // Replace
        vg_codeeditor_delete_selection(ed);
        vg_codeeditor_insert_text(ed, replace_text);
    }

    // Callback
    if (bar->on_replace_all) {
        bar->on_replace_all(bar, find_text, replace_text, bar->user_data);
    }

    // Re-search (should find nothing)
    perform_search(bar);
}

size_t vg_findreplacebar_get_match_count(vg_findreplacebar_t* bar) {
    return bar ? bar->match_count : 0;
}

size_t vg_findreplacebar_get_current_match(vg_findreplacebar_t* bar) {
    return bar ? bar->current_match : 0;
}

void vg_findreplacebar_focus(vg_findreplacebar_t* bar) {
    if (!bar || !bar->find_input) return;
    vg_widget_set_focus((vg_widget_t*)bar->find_input);
}

void vg_findreplacebar_set_find_text(vg_findreplacebar_t* bar, const char* text) {
    if (!bar || !bar->find_input) return;
    vg_textinput_set_text((vg_textinput_t*)bar->find_input, text);
    perform_search(bar);
}

void vg_findreplacebar_set_on_close(vg_findreplacebar_t* bar,
    void (*callback)(vg_findreplacebar_t*, void*), void* user_data) {
    if (!bar) return;
    bar->on_close = callback;
    bar->user_data = user_data;
}

void vg_findreplacebar_set_font(vg_findreplacebar_t* bar, vg_font_t* font, float size) {
    if (!bar) return;
    bar->font = font;
    bar->font_size = size;

    // Set font on child widgets
    if (bar->find_input) {
        vg_textinput_set_font((vg_textinput_t*)bar->find_input, font, size);
    }
    if (bar->replace_input) {
        vg_textinput_set_font((vg_textinput_t*)bar->replace_input, font, size);
    }
}
