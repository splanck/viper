//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/checkbox.c
// Purpose: Checkbox widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

#define CHECKBOX_SIZE 14

//===----------------------------------------------------------------------===//
// Checkbox Paint Handler
//===----------------------------------------------------------------------===//

static void checkbox_paint(widget_t *w, gui_window_t *win) {
    checkbox_t *cb = (checkbox_t *)w;

    int x = w->x;
    int y = w->y;
    int box_y = y + (w->height - CHECKBOX_SIZE) / 2;

    // Draw checkbox box (sunken)
    draw_3d_sunken(win, x, box_y, CHECKBOX_SIZE, CHECKBOX_SIZE, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Fill background
    gui_fill_rect(win, x + 2, box_y + 2, CHECKBOX_SIZE - 4, CHECKBOX_SIZE - 4, WB_WHITE);

    // Draw checkmark if checked
    if (cb->checked) {
        uint32_t check_color = w->enabled ? WB_BLACK : WB_GRAY_MED;

        // Simple checkmark (two lines)
        int cx = x + 3;
        int cy = box_y + 3;

        // Draw checkmark as two lines forming a "V" rotated
        for (int i = 0; i < 3; i++) {
            gui_fill_rect(win, cx + i, cy + i + 3, 1, 1, check_color);
        }
        for (int i = 0; i < 5; i++) {
            gui_fill_rect(win, cx + 3 + i, cy + 5 - i, 1, 1, check_color);
        }

        // Make checkmark thicker
        for (int i = 0; i < 3; i++) {
            gui_fill_rect(win, cx + i, cy + i + 4, 1, 1, check_color);
        }
        for (int i = 0; i < 5; i++) {
            gui_fill_rect(win, cx + 3 + i, cy + 6 - i, 1, 1, check_color);
        }
    }

    // Draw label
    int text_x = x + CHECKBOX_SIZE + 6;
    int text_y = y + (w->height - 10) / 2;

    uint32_t text_color = w->enabled ? WB_BLACK : WB_GRAY_MED;
    gui_draw_text(win, text_x, text_y, cb->text, text_color);
}

//===----------------------------------------------------------------------===//
// Checkbox Event Handlers
//===----------------------------------------------------------------------===//

static void checkbox_click(widget_t *w, int x, int y, int button) {
    (void)x;
    (void)y;

    if (button != 0)
        return;

    checkbox_t *cb = (checkbox_t *)w;
    cb->checked = !cb->checked;

    if (cb->on_change) {
        cb->on_change(cb->callback_data);
    }
}

//===----------------------------------------------------------------------===//
// Checkbox API
//===----------------------------------------------------------------------===//

checkbox_t *checkbox_create(widget_t *parent, const char *text) {
    checkbox_t *cb = (checkbox_t *)malloc(sizeof(checkbox_t));
    if (!cb)
        return NULL;

    memset(cb, 0, sizeof(checkbox_t));

    // Initialize base widget
    cb->base.type = WIDGET_CHECKBOX;
    cb->base.parent = parent;
    cb->base.visible = true;
    cb->base.enabled = true;
    cb->base.bg_color = WB_GRAY_LIGHT;
    cb->base.fg_color = WB_BLACK;
    cb->base.width = 150;
    cb->base.height = 20;

    // Set handlers
    cb->base.on_paint = checkbox_paint;
    cb->base.on_click = checkbox_click;

    // Set text
    if (text) {
        strncpy(cb->text, text, sizeof(cb->text) - 1);
        cb->text[sizeof(cb->text) - 1] = '\0';
    }

    cb->checked = false;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)cb);
    }

    return cb;
}

void checkbox_set_text(checkbox_t *cb, const char *text) {
    if (cb && text) {
        strncpy(cb->text, text, sizeof(cb->text) - 1);
        cb->text[sizeof(cb->text) - 1] = '\0';
    }
}

void checkbox_set_checked(checkbox_t *cb, bool checked) {
    if (cb) {
        cb->checked = checked;
    }
}

bool checkbox_is_checked(checkbox_t *cb) {
    return cb ? cb->checked : false;
}

void checkbox_set_onchange(checkbox_t *cb, widget_callback_fn callback, void *data) {
    if (cb) {
        cb->on_change = callback;
        cb->callback_data = data;
    }
}
