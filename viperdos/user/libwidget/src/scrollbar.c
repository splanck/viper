//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/scrollbar.c
// Purpose: Scrollbar widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

#define ARROW_SIZE   16
#define MIN_THUMB    20

//===----------------------------------------------------------------------===//
// Scrollbar Paint Handler
//===----------------------------------------------------------------------===//

static void scrollbar_paint(widget_t *w, gui_window_t *win) {
    scrollbar_t *sb = (scrollbar_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    if (sb->vertical) {
        // Vertical scrollbar
        // Draw track background
        gui_fill_rect(win, x, y, width, height, WB_GRAY_MED);

        // Draw top arrow button
        draw_3d_raised(win, x, y, width, ARROW_SIZE, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);
        gui_draw_text(win, x + width / 2 - 4, y + 3, "^", WB_BLACK);

        // Draw bottom arrow button
        draw_3d_raised(win, x, y + height - ARROW_SIZE, width, ARROW_SIZE, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
        gui_draw_text(win, x + width / 2 - 4, y + height - ARROW_SIZE + 3, "v", WB_BLACK);

        // Calculate thumb
        int track_start = y + ARROW_SIZE;
        int track_height = height - ARROW_SIZE * 2;

        int range = sb->max_val - sb->min_val;
        int thumb_height = MIN_THUMB;
        int thumb_y = track_start;

        if (range > 0 && sb->page_size > 0) {
            thumb_height = (sb->page_size * track_height) / (range + sb->page_size);
            if (thumb_height < MIN_THUMB)
                thumb_height = MIN_THUMB;
            if (thumb_height > track_height)
                thumb_height = track_height;

            thumb_y = track_start +
                      ((sb->value - sb->min_val) * (track_height - thumb_height)) / range;
        }

        // Draw thumb
        draw_3d_raised(win, x + 1, thumb_y, width - 2, thumb_height, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
    } else {
        // Horizontal scrollbar
        // Draw track background
        gui_fill_rect(win, x, y, width, height, WB_GRAY_MED);

        // Draw left arrow button
        draw_3d_raised(win, x, y, ARROW_SIZE, height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);
        gui_draw_text(win, x + 4, y + height / 2 - 5, "<", WB_BLACK);

        // Draw right arrow button
        draw_3d_raised(win, x + width - ARROW_SIZE, y, ARROW_SIZE, height, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
        gui_draw_text(win, x + width - ARROW_SIZE + 4, y + height / 2 - 5, ">", WB_BLACK);

        // Calculate thumb
        int track_start = x + ARROW_SIZE;
        int track_width = width - ARROW_SIZE * 2;

        int range = sb->max_val - sb->min_val;
        int thumb_width = MIN_THUMB;
        int thumb_x = track_start;

        if (range > 0 && sb->page_size > 0) {
            thumb_width = (sb->page_size * track_width) / (range + sb->page_size);
            if (thumb_width < MIN_THUMB)
                thumb_width = MIN_THUMB;
            if (thumb_width > track_width)
                thumb_width = track_width;

            thumb_x = track_start +
                      ((sb->value - sb->min_val) * (track_width - thumb_width)) / range;
        }

        // Draw thumb
        draw_3d_raised(win, thumb_x, y + 1, thumb_width, height - 2, WB_GRAY_LIGHT, WB_WHITE,
                       WB_GRAY_DARK);
    }
}

//===----------------------------------------------------------------------===//
// Scrollbar Event Handlers
//===----------------------------------------------------------------------===//

static void scrollbar_click(widget_t *w, int click_x, int click_y, int button) {
    if (button != 0)
        return;

    scrollbar_t *sb = (scrollbar_t *)w;

    int range = sb->max_val - sb->min_val;
    if (range <= 0)
        return;

    int new_value = sb->value;

    if (sb->vertical) {
        int height = w->height;

        if (click_y < ARROW_SIZE) {
            // Top arrow - scroll up
            new_value -= 1;
        } else if (click_y >= height - ARROW_SIZE) {
            // Bottom arrow - scroll down
            new_value += 1;
        } else {
            // Track click - page scroll or direct position
            int track_height = height - ARROW_SIZE * 2;
            int track_y = click_y - ARROW_SIZE;

            new_value = sb->min_val + (track_y * range) / track_height;
        }
    } else {
        int width = w->width;

        if (click_x < ARROW_SIZE) {
            // Left arrow - scroll left
            new_value -= 1;
        } else if (click_x >= width - ARROW_SIZE) {
            // Right arrow - scroll right
            new_value += 1;
        } else {
            // Track click - page scroll or direct position
            int track_width = width - ARROW_SIZE * 2;
            int track_x = click_x - ARROW_SIZE;

            new_value = sb->min_val + (track_x * range) / track_width;
        }
    }

    // Clamp value
    if (new_value < sb->min_val)
        new_value = sb->min_val;
    if (new_value > sb->max_val)
        new_value = sb->max_val;

    if (new_value != sb->value) {
        sb->value = new_value;
        if (sb->on_change) {
            sb->on_change(sb->callback_data);
        }
    }
}

//===----------------------------------------------------------------------===//
// Scrollbar API
//===----------------------------------------------------------------------===//

scrollbar_t *scrollbar_create(widget_t *parent, bool vertical) {
    scrollbar_t *sb = (scrollbar_t *)malloc(sizeof(scrollbar_t));
    if (!sb)
        return NULL;

    memset(sb, 0, sizeof(scrollbar_t));

    // Initialize base widget
    sb->base.type = WIDGET_SCROLLBAR;
    sb->base.parent = parent;
    sb->base.visible = true;
    sb->base.enabled = true;
    sb->base.bg_color = WB_GRAY_MED;
    sb->base.fg_color = WB_BLACK;

    if (vertical) {
        sb->base.width = 16;
        sb->base.height = 100;
    } else {
        sb->base.width = 100;
        sb->base.height = 16;
    }

    // Set handlers
    sb->base.on_paint = scrollbar_paint;
    sb->base.on_click = scrollbar_click;

    sb->vertical = vertical;
    sb->min_val = 0;
    sb->max_val = 100;
    sb->value = 0;
    sb->page_size = 10;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)sb);
    }

    return sb;
}

void scrollbar_set_value(scrollbar_t *sb, int value) {
    if (!sb)
        return;

    if (value < sb->min_val)
        value = sb->min_val;
    if (value > sb->max_val)
        value = sb->max_val;

    sb->value = value;
}

int scrollbar_get_value(scrollbar_t *sb) {
    return sb ? sb->value : 0;
}

void scrollbar_set_range(scrollbar_t *sb, int min_val, int max_val) {
    if (!sb)
        return;

    sb->min_val = min_val;
    sb->max_val = max_val;

    // Clamp current value
    if (sb->value < min_val)
        sb->value = min_val;
    if (sb->value > max_val)
        sb->value = max_val;
}

void scrollbar_set_page_size(scrollbar_t *sb, int page_size) {
    if (sb && page_size > 0) {
        sb->page_size = page_size;
    }
}

void scrollbar_set_onchange(scrollbar_t *sb, widget_callback_fn callback, void *data) {
    if (sb) {
        sb->on_change = callback;
        sb->callback_data = data;
    }
}
