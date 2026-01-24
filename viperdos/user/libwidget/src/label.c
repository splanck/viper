//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/label.c
// Purpose: Label widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Label Paint Handler
//===----------------------------------------------------------------------===//

static void label_paint(widget_t *w, gui_window_t *win) {
    label_t *lbl = (label_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;

    int text_len = (int)strlen(lbl->text);
    int text_width = text_len * 8;
    int text_x;

    switch (lbl->alignment) {
    case ALIGN_CENTER:
        text_x = x + (width - text_width) / 2;
        break;
    case ALIGN_RIGHT:
        text_x = x + width - text_width;
        break;
    case ALIGN_LEFT:
    default:
        text_x = x;
        break;
    }

    int text_y = y + (w->height - 10) / 2;

    gui_draw_text(win, text_x, text_y, lbl->text, w->fg_color);
}

//===----------------------------------------------------------------------===//
// Label API
//===----------------------------------------------------------------------===//

label_t *label_create(widget_t *parent, const char *text) {
    label_t *lbl = (label_t *)malloc(sizeof(label_t));
    if (!lbl)
        return NULL;

    memset(lbl, 0, sizeof(label_t));

    // Initialize base widget
    lbl->base.type = WIDGET_LABEL;
    lbl->base.parent = parent;
    lbl->base.visible = true;
    lbl->base.enabled = true;
    lbl->base.bg_color = WB_GRAY_LIGHT;
    lbl->base.fg_color = WB_BLACK;
    lbl->base.width = 100;
    lbl->base.height = 16;

    // Set handlers
    lbl->base.on_paint = label_paint;

    // Set text
    if (text) {
        strncpy(lbl->text, text, sizeof(lbl->text) - 1);
        lbl->text[sizeof(lbl->text) - 1] = '\0';
    }

    lbl->alignment = ALIGN_LEFT;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)lbl);
    }

    return lbl;
}

void label_set_text(label_t *lbl, const char *text) {
    if (lbl && text) {
        strncpy(lbl->text, text, sizeof(lbl->text) - 1);
        lbl->text[sizeof(lbl->text) - 1] = '\0';
    }
}

const char *label_get_text(label_t *lbl) {
    return lbl ? lbl->text : NULL;
}

void label_set_alignment(label_t *lbl, alignment_t align) {
    if (lbl) {
        lbl->alignment = align;
    }
}
