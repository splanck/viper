//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/button.c
// Purpose: Button widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Button Paint Handler
//===----------------------------------------------------------------------===//

static void button_paint(widget_t *w, gui_window_t *win) {
    button_t *btn = (button_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw 3D button
    draw_3d_button(win, x, y, width, height, btn->pressed);

    // Draw text centered
    int text_len = (int)strlen(btn->text);
    int text_width = text_len * 8;
    int text_x = x + (width - text_width) / 2;
    int text_y = y + (height - 10) / 2;

    // Offset text when pressed
    if (btn->pressed) {
        text_x++;
        text_y++;
    }

    uint32_t text_color = w->enabled ? WB_BLACK : WB_GRAY_MED;
    gui_draw_text(win, text_x, text_y, btn->text, text_color);
}

//===----------------------------------------------------------------------===//
// Button Event Handlers
//===----------------------------------------------------------------------===//

static void button_click(widget_t *w, int x, int y, int button) {
    (void)x;
    (void)y;

    if (button != 0)
        return;

    button_t *btn = (button_t *)w;
    btn->pressed = true;

    // The release will trigger the callback
    // For now, trigger immediately
    btn->pressed = false;

    if (btn->on_click) {
        btn->on_click(btn->callback_data);
    }
}

//===----------------------------------------------------------------------===//
// Button API
//===----------------------------------------------------------------------===//

button_t *button_create(widget_t *parent, const char *text) {
    button_t *btn = (button_t *)malloc(sizeof(button_t));
    if (!btn)
        return NULL;

    memset(btn, 0, sizeof(button_t));

    // Initialize base widget
    btn->base.type = WIDGET_BUTTON;
    btn->base.parent = parent;
    btn->base.visible = true;
    btn->base.enabled = true;
    btn->base.bg_color = WB_GRAY_LIGHT;
    btn->base.fg_color = WB_BLACK;
    btn->base.width = 80;
    btn->base.height = 24;

    // Set handlers
    btn->base.on_paint = button_paint;
    btn->base.on_click = button_click;

    // Set text
    if (text) {
        strncpy(btn->text, text, sizeof(btn->text) - 1);
        btn->text[sizeof(btn->text) - 1] = '\0';
    }

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)btn);
    }

    return btn;
}

void button_set_text(button_t *btn, const char *text) {
    if (btn && text) {
        strncpy(btn->text, text, sizeof(btn->text) - 1);
        btn->text[sizeof(btn->text) - 1] = '\0';
    }
}

const char *button_get_text(button_t *btn) {
    return btn ? btn->text : NULL;
}

void button_set_onclick(button_t *btn, widget_callback_fn callback, void *data) {
    if (btn) {
        btn->on_click = callback;
        btn->callback_data = data;
    }
}
