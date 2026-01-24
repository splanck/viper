//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/textbox.c
// Purpose: TextBox widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

#define TEXTBOX_INITIAL_CAPACITY 256
#define CHAR_WIDTH               8
#define CHAR_HEIGHT              10

//===----------------------------------------------------------------------===//
// TextBox Paint Handler
//===----------------------------------------------------------------------===//

static void textbox_paint(widget_t *w, gui_window_t *win) {
    textbox_t *tb = (textbox_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Background
    gui_fill_rect(win, x + 2, y + 2, width - 4, height - 4, WB_WHITE);

    // Calculate visible text area
    int text_x = x + 4;
    int text_y = y + (height - CHAR_HEIGHT) / 2;
    int visible_chars = (width - 8) / CHAR_WIDTH;

    // Get text to display
    const char *display_text = tb->text ? tb->text : "";
    int text_len = tb->text_length;

    // Handle scroll offset
    int start = tb->scroll_offset;
    if (start > text_len)
        start = text_len;

    // Create display buffer
    char display_buf[256];
    int copy_len = text_len - start;
    if (copy_len > visible_chars)
        copy_len = visible_chars;
    if (copy_len > (int)sizeof(display_buf) - 1)
        copy_len = sizeof(display_buf) - 1;

    if (tb->password_mode) {
        // Show asterisks
        for (int i = 0; i < copy_len; i++) {
            display_buf[i] = '*';
        }
    } else {
        memcpy(display_buf, display_text + start, copy_len);
    }
    display_buf[copy_len] = '\0';

    // Draw text
    uint32_t text_color = w->enabled ? WB_BLACK : WB_GRAY_MED;
    gui_draw_text(win, text_x, text_y, display_buf, text_color);

    // Draw cursor if focused
    if (w->focused && w->enabled && !tb->readonly) {
        int cursor_screen_pos = tb->cursor_pos - tb->scroll_offset;
        if (cursor_screen_pos >= 0 && cursor_screen_pos <= visible_chars) {
            int cursor_x = text_x + cursor_screen_pos * CHAR_WIDTH;
            gui_draw_vline(win, cursor_x, text_y, text_y + CHAR_HEIGHT, WB_BLACK);
        }
    }

    // Draw selection highlight
    if (tb->selection_start != tb->selection_end) {
        int sel_start = tb->selection_start < tb->selection_end ? tb->selection_start : tb->selection_end;
        int sel_end = tb->selection_start < tb->selection_end ? tb->selection_end : tb->selection_start;

        sel_start -= tb->scroll_offset;
        sel_end -= tb->scroll_offset;

        if (sel_start < 0)
            sel_start = 0;
        if (sel_end > visible_chars)
            sel_end = visible_chars;

        if (sel_start < sel_end) {
            int sel_x = text_x + sel_start * CHAR_WIDTH;
            int sel_width = (sel_end - sel_start) * CHAR_WIDTH;
            gui_fill_rect(win, sel_x, text_y, sel_width, CHAR_HEIGHT, WB_BLUE);

            // Redraw selected text in white
            char sel_buf[256];
            int sel_len = sel_end - sel_start;
            if (sel_len > (int)sizeof(sel_buf) - 1)
                sel_len = sizeof(sel_buf) - 1;
            memcpy(sel_buf, display_buf + sel_start, sel_len);
            sel_buf[sel_len] = '\0';
            gui_draw_text(win, sel_x, text_y, sel_buf, WB_WHITE);
        }
    }
}

//===----------------------------------------------------------------------===//
// TextBox Event Handlers
//===----------------------------------------------------------------------===//

static void textbox_click(widget_t *w, int x, int y, int button) {
    (void)y;

    if (button != 0)
        return;

    textbox_t *tb = (textbox_t *)w;

    // Set focus
    widget_set_focus(w);

    // Calculate cursor position from click
    int text_x = 4; // Offset from widget edge
    int click_char = (x - text_x) / CHAR_WIDTH + tb->scroll_offset;

    if (click_char < 0)
        click_char = 0;
    if (click_char > tb->text_length)
        click_char = tb->text_length;

    tb->cursor_pos = click_char;
    tb->selection_start = tb->selection_end = click_char;
}

static void textbox_ensure_cursor_visible(textbox_t *tb) {
    int visible_chars = (tb->base.width - 8) / CHAR_WIDTH;

    if (tb->cursor_pos < tb->scroll_offset) {
        tb->scroll_offset = tb->cursor_pos;
    } else if (tb->cursor_pos > tb->scroll_offset + visible_chars) {
        tb->scroll_offset = tb->cursor_pos - visible_chars;
    }
}

static void textbox_delete_selection(textbox_t *tb) {
    if (tb->selection_start == tb->selection_end)
        return;

    int sel_start = tb->selection_start < tb->selection_end ? tb->selection_start : tb->selection_end;
    int sel_end = tb->selection_start < tb->selection_end ? tb->selection_end : tb->selection_start;

    // Remove selected text
    int remove_len = sel_end - sel_start;
    memmove(tb->text + sel_start, tb->text + sel_end, tb->text_length - sel_end + 1);
    tb->text_length -= remove_len;

    tb->cursor_pos = sel_start;
    tb->selection_start = tb->selection_end = sel_start;
}

static void textbox_insert_char(textbox_t *tb, char ch) {
    // Delete selection first if any
    textbox_delete_selection(tb);

    // Grow buffer if needed
    if (tb->text_length + 1 >= tb->text_capacity) {
        int new_cap = tb->text_capacity * 2;
        char *new_text = (char *)realloc(tb->text, new_cap);
        if (!new_text)
            return;
        tb->text = new_text;
        tb->text_capacity = new_cap;
    }

    // Insert character
    memmove(tb->text + tb->cursor_pos + 1, tb->text + tb->cursor_pos,
            tb->text_length - tb->cursor_pos + 1);
    tb->text[tb->cursor_pos] = ch;
    tb->text_length++;
    tb->cursor_pos++;

    textbox_ensure_cursor_visible(tb);

    if (tb->on_change) {
        tb->on_change(tb->callback_data);
    }
}

static void textbox_key(widget_t *w, int keycode, char ch) {
    textbox_t *tb = (textbox_t *)w;

    if (tb->readonly)
        return;

    switch (keycode) {
    case 0x50: // Left arrow
        if (tb->cursor_pos > 0) {
            tb->cursor_pos--;
            tb->selection_start = tb->selection_end = tb->cursor_pos;
            textbox_ensure_cursor_visible(tb);
        }
        break;

    case 0x4F: // Right arrow
        if (tb->cursor_pos < tb->text_length) {
            tb->cursor_pos++;
            tb->selection_start = tb->selection_end = tb->cursor_pos;
            textbox_ensure_cursor_visible(tb);
        }
        break;

    case 0x4A: // Home
        tb->cursor_pos = 0;
        tb->selection_start = tb->selection_end = 0;
        textbox_ensure_cursor_visible(tb);
        break;

    case 0x4D: // End
        tb->cursor_pos = tb->text_length;
        tb->selection_start = tb->selection_end = tb->text_length;
        textbox_ensure_cursor_visible(tb);
        break;

    case 0x2A: // Backspace
        if (tb->selection_start != tb->selection_end) {
            textbox_delete_selection(tb);
        } else if (tb->cursor_pos > 0) {
            memmove(tb->text + tb->cursor_pos - 1, tb->text + tb->cursor_pos,
                    tb->text_length - tb->cursor_pos + 1);
            tb->text_length--;
            tb->cursor_pos--;
            textbox_ensure_cursor_visible(tb);
        }
        if (tb->on_change) {
            tb->on_change(tb->callback_data);
        }
        break;

    case 0x4C: // Delete
        if (tb->selection_start != tb->selection_end) {
            textbox_delete_selection(tb);
        } else if (tb->cursor_pos < tb->text_length) {
            memmove(tb->text + tb->cursor_pos, tb->text + tb->cursor_pos + 1,
                    tb->text_length - tb->cursor_pos);
            tb->text_length--;
        }
        if (tb->on_change) {
            tb->on_change(tb->callback_data);
        }
        break;

    case 0x28: // Enter
        if (tb->on_enter) {
            tb->on_enter(tb->callback_data);
        }
        break;

    default:
        // Printable character
        if (ch >= 32 && ch < 127) {
            textbox_insert_char(tb, ch);
        }
        break;
    }
}

//===----------------------------------------------------------------------===//
// TextBox API
//===----------------------------------------------------------------------===//

textbox_t *textbox_create(widget_t *parent) {
    textbox_t *tb = (textbox_t *)malloc(sizeof(textbox_t));
    if (!tb)
        return NULL;

    memset(tb, 0, sizeof(textbox_t));

    // Initialize base widget
    tb->base.type = WIDGET_TEXTBOX;
    tb->base.parent = parent;
    tb->base.visible = true;
    tb->base.enabled = true;
    tb->base.bg_color = WB_WHITE;
    tb->base.fg_color = WB_BLACK;
    tb->base.width = 150;
    tb->base.height = 20;

    // Set handlers
    tb->base.on_paint = textbox_paint;
    tb->base.on_click = textbox_click;
    tb->base.on_key = textbox_key;

    // Allocate text buffer
    tb->text_capacity = TEXTBOX_INITIAL_CAPACITY;
    tb->text = (char *)malloc(tb->text_capacity);
    if (!tb->text) {
        free(tb);
        return NULL;
    }
    tb->text[0] = '\0';
    tb->text_length = 0;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)tb);
    }

    return tb;
}

void textbox_set_text(textbox_t *tb, const char *text) {
    if (!tb)
        return;

    int len = text ? (int)strlen(text) : 0;

    // Grow buffer if needed
    if (len + 1 > tb->text_capacity) {
        int new_cap = len + 1;
        char *new_text = (char *)realloc(tb->text, new_cap);
        if (!new_text)
            return;
        tb->text = new_text;
        tb->text_capacity = new_cap;
    }

    if (text) {
        strcpy(tb->text, text);
        tb->text_length = len;
    } else {
        tb->text[0] = '\0';
        tb->text_length = 0;
    }

    tb->cursor_pos = 0;
    tb->scroll_offset = 0;
    tb->selection_start = tb->selection_end = 0;
}

const char *textbox_get_text(textbox_t *tb) {
    return tb ? tb->text : NULL;
}

void textbox_set_password_mode(textbox_t *tb, bool enabled) {
    if (tb) {
        tb->password_mode = enabled;
    }
}

void textbox_set_multiline(textbox_t *tb, bool enabled) {
    if (tb) {
        tb->multiline = enabled;
    }
}

void textbox_set_readonly(textbox_t *tb, bool readonly) {
    if (tb) {
        tb->readonly = readonly;
    }
}

void textbox_set_onchange(textbox_t *tb, widget_callback_fn callback, void *data) {
    if (tb) {
        tb->on_change = callback;
        tb->callback_data = data;
    }
}

void textbox_set_onenter(textbox_t *tb, widget_callback_fn callback, void *data) {
    if (tb) {
        tb->on_enter = callback;
        tb->callback_data = data;
    }
}

int textbox_get_cursor_pos(textbox_t *tb) {
    return tb ? tb->cursor_pos : 0;
}

void textbox_set_cursor_pos(textbox_t *tb, int pos) {
    if (!tb)
        return;

    if (pos < 0)
        pos = 0;
    if (pos > tb->text_length)
        pos = tb->text_length;

    tb->cursor_pos = pos;
    tb->selection_start = tb->selection_end = pos;
    textbox_ensure_cursor_visible(tb);
}

void textbox_select_all(textbox_t *tb) {
    if (tb) {
        tb->selection_start = 0;
        tb->selection_end = tb->text_length;
        tb->cursor_pos = tb->text_length;
    }
}

void textbox_clear_selection(textbox_t *tb) {
    if (tb) {
        tb->selection_start = tb->selection_end = tb->cursor_pos;
    }
}
