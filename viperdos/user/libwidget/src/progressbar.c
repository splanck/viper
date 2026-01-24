//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/progressbar.c
// Purpose: ProgressBar widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// ProgressBar Paint Handler
//===----------------------------------------------------------------------===//

static void progressbar_paint(widget_t *w, gui_window_t *win) {
    progressbar_t *pb = (progressbar_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

    // Calculate fill width
    int range = pb->max_val - pb->min_val;
    int fill_width = 0;
    if (range > 0) {
        fill_width = ((pb->value - pb->min_val) * (width - 4)) / range;
    }

    // Draw progress fill
    if (fill_width > 0) {
        gui_fill_rect(win, x + 2, y + 2, fill_width, height - 4, WB_BLUE);
    }

    // Draw percentage text if enabled
    if (pb->show_text) {
        char buf[16];
        int percent = 0;
        if (range > 0) {
            percent = ((pb->value - pb->min_val) * 100) / range;
        }
        snprintf(buf, sizeof(buf), "%d%%", percent);

        int text_len = (int)strlen(buf);
        int text_x = x + (width - text_len * 8) / 2;
        int text_y = y + (height - 10) / 2;

        // Draw text - white on filled part, black on unfilled
        gui_draw_text(win, text_x, text_y, buf, WB_BLACK);
    }
}

//===----------------------------------------------------------------------===//
// ProgressBar API
//===----------------------------------------------------------------------===//

progressbar_t *progressbar_create(widget_t *parent) {
    progressbar_t *pb = (progressbar_t *)malloc(sizeof(progressbar_t));
    if (!pb)
        return NULL;

    memset(pb, 0, sizeof(progressbar_t));

    // Initialize base widget
    pb->base.type = WIDGET_PROGRESSBAR;
    pb->base.parent = parent;
    pb->base.visible = true;
    pb->base.enabled = true;
    pb->base.bg_color = WB_GRAY_LIGHT;
    pb->base.fg_color = WB_BLACK;
    pb->base.width = 200;
    pb->base.height = 20;

    // Set handlers
    pb->base.on_paint = progressbar_paint;

    // Default range
    pb->min_val = 0;
    pb->max_val = 100;
    pb->value = 0;
    pb->show_text = true;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)pb);
    }

    return pb;
}

void progressbar_set_value(progressbar_t *pb, int value) {
    if (!pb)
        return;

    if (value < pb->min_val)
        value = pb->min_val;
    if (value > pb->max_val)
        value = pb->max_val;

    pb->value = value;
}

int progressbar_get_value(progressbar_t *pb) {
    return pb ? pb->value : 0;
}

void progressbar_set_range(progressbar_t *pb, int min_val, int max_val) {
    if (!pb)
        return;

    pb->min_val = min_val;
    pb->max_val = max_val;

    // Clamp current value
    if (pb->value < min_val)
        pb->value = min_val;
    if (pb->value > max_val)
        pb->value = max_val;
}

void progressbar_set_show_text(progressbar_t *pb, bool show) {
    if (pb) {
        pb->show_text = show;
    }
}
