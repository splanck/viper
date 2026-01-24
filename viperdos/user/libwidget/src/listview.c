//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/listview.c
// Purpose: ListView widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_HEIGHT      18
#define INITIAL_CAPACITY 16

//===----------------------------------------------------------------------===//
// ListView Paint Handler
//===----------------------------------------------------------------------===//

static void listview_paint(widget_t *w, gui_window_t *win) {
    listview_t *lv = (listview_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Fill background
    gui_fill_rect(win, x + 2, y + 2, width - 4, height - 4, WB_WHITE);

    // Calculate visible items
    int content_height = height - 4;
    lv->visible_items = content_height / ITEM_HEIGHT;

    // Draw items
    int item_y = y + 2;
    for (int i = 0; i < lv->visible_items && (lv->scroll_offset + i) < lv->item_count; i++) {
        int item_index = lv->scroll_offset + i;

        bool is_selected = false;
        if (lv->multi_select && lv->selected) {
            is_selected = lv->selected[item_index];
        } else {
            is_selected = (item_index == lv->selected_index);
        }

        // Draw selection highlight
        if (is_selected) {
            gui_fill_rect(win, x + 2, item_y, width - 4, ITEM_HEIGHT, WB_BLUE);
        }

        // Draw item text
        uint32_t text_color = is_selected ? WB_WHITE : WB_BLACK;
        if (!w->enabled) {
            text_color = WB_GRAY_MED;
        }

        if (lv->items[item_index]) {
            gui_draw_text(win, x + 6, item_y + 4, lv->items[item_index], text_color);
        }

        item_y += ITEM_HEIGHT;
    }

    // Draw scrollbar if needed
    if (lv->item_count > lv->visible_items) {
        int sb_x = x + width - 16;
        int sb_y = y + 2;
        int sb_height = height - 4;

        // Scrollbar track
        gui_fill_rect(win, sb_x, sb_y, 14, sb_height, WB_GRAY_MED);

        // Scrollbar thumb
        int thumb_height = (lv->visible_items * sb_height) / lv->item_count;
        if (thumb_height < 20)
            thumb_height = 20;

        int thumb_y = sb_y + (lv->scroll_offset * (sb_height - thumb_height)) /
                                 (lv->item_count - lv->visible_items);

        draw_3d_raised(win, sb_x + 1, thumb_y, 12, thumb_height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);
    }
}

//===----------------------------------------------------------------------===//
// ListView Event Handlers
//===----------------------------------------------------------------------===//

static void listview_click(widget_t *w, int x, int y, int button) {
    if (button != 0)
        return;

    listview_t *lv = (listview_t *)w;

    // Check if click is on scrollbar
    if (x > w->width - 16 && lv->item_count > lv->visible_items) {
        // Scrollbar click - simplified handling
        int content_height = w->height - 4;
        int click_ratio = y * lv->item_count / content_height;
        lv->scroll_offset = click_ratio - lv->visible_items / 2;
        if (lv->scroll_offset < 0)
            lv->scroll_offset = 0;
        if (lv->scroll_offset > lv->item_count - lv->visible_items)
            lv->scroll_offset = lv->item_count - lv->visible_items;
        return;
    }

    // Calculate which item was clicked
    int item_y = y - 2;
    int clicked_item = lv->scroll_offset + item_y / ITEM_HEIGHT;

    if (clicked_item >= 0 && clicked_item < lv->item_count) {
        if (lv->multi_select && lv->selected) {
            lv->selected[clicked_item] = !lv->selected[clicked_item];
        } else {
            lv->selected_index = clicked_item;
        }

        if (lv->on_select) {
            lv->on_select(clicked_item, lv->callback_data);
        }
    }
}

static void listview_key(widget_t *w, int keycode, char ch) {
    (void)ch;
    listview_t *lv = (listview_t *)w;

    switch (keycode) {
    case 0x52: // Up arrow
        if (lv->selected_index > 0) {
            lv->selected_index--;
            listview_ensure_visible(lv, lv->selected_index);
            if (lv->on_select) {
                lv->on_select(lv->selected_index, lv->callback_data);
            }
        }
        break;

    case 0x51: // Down arrow
        if (lv->selected_index < lv->item_count - 1) {
            lv->selected_index++;
            listview_ensure_visible(lv, lv->selected_index);
            if (lv->on_select) {
                lv->on_select(lv->selected_index, lv->callback_data);
            }
        }
        break;

    case 0x4B: // Page Up
        lv->selected_index -= lv->visible_items;
        if (lv->selected_index < 0)
            lv->selected_index = 0;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x4E: // Page Down
        lv->selected_index += lv->visible_items;
        if (lv->selected_index >= lv->item_count)
            lv->selected_index = lv->item_count - 1;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x4A: // Home
        lv->selected_index = 0;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x4D: // End
        lv->selected_index = lv->item_count - 1;
        listview_ensure_visible(lv, lv->selected_index);
        if (lv->on_select) {
            lv->on_select(lv->selected_index, lv->callback_data);
        }
        break;

    case 0x28: // Enter
        if (lv->on_double_click && lv->selected_index >= 0) {
            lv->on_double_click(lv->selected_index, lv->callback_data);
        }
        break;
    }
}

//===----------------------------------------------------------------------===//
// ListView API
//===----------------------------------------------------------------------===//

listview_t *listview_create(widget_t *parent) {
    listview_t *lv = (listview_t *)malloc(sizeof(listview_t));
    if (!lv)
        return NULL;

    memset(lv, 0, sizeof(listview_t));

    // Initialize base widget
    lv->base.type = WIDGET_LISTVIEW;
    lv->base.parent = parent;
    lv->base.visible = true;
    lv->base.enabled = true;
    lv->base.bg_color = WB_WHITE;
    lv->base.fg_color = WB_BLACK;
    lv->base.width = 200;
    lv->base.height = 150;

    // Set handlers
    lv->base.on_paint = listview_paint;
    lv->base.on_click = listview_click;
    lv->base.on_key = listview_key;

    // Initialize items array
    lv->item_capacity = INITIAL_CAPACITY;
    lv->items = (char **)malloc(lv->item_capacity * sizeof(char *));
    if (!lv->items) {
        free(lv);
        return NULL;
    }
    lv->item_count = 0;
    lv->selected_index = -1;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)lv);
    }

    return lv;
}

void listview_add_item(listview_t *lv, const char *text) {
    if (!lv || !text)
        return;

    // Grow array if needed
    if (lv->item_count >= lv->item_capacity) {
        int new_cap = lv->item_capacity * 2;
        char **new_items = (char **)realloc(lv->items, new_cap * sizeof(char *));
        if (!new_items)
            return;
        lv->items = new_items;
        lv->item_capacity = new_cap;

        // Also grow selection array if multi-select
        if (lv->multi_select && lv->selected) {
            bool *new_selected = (bool *)realloc(lv->selected, new_cap * sizeof(bool));
            if (new_selected) {
                lv->selected = new_selected;
            }
        }
    }

    lv->items[lv->item_count] = strdup(text);
    if (lv->multi_select && lv->selected) {
        lv->selected[lv->item_count] = false;
    }
    lv->item_count++;
}

void listview_insert_item(listview_t *lv, int index, const char *text) {
    if (!lv || !text || index < 0)
        return;

    if (index >= lv->item_count) {
        listview_add_item(lv, text);
        return;
    }

    // Grow array if needed
    if (lv->item_count >= lv->item_capacity) {
        int new_cap = lv->item_capacity * 2;
        char **new_items = (char **)realloc(lv->items, new_cap * sizeof(char *));
        if (!new_items)
            return;
        lv->items = new_items;
        lv->item_capacity = new_cap;
    }

    // Shift items
    memmove(&lv->items[index + 1], &lv->items[index], (lv->item_count - index) * sizeof(char *));
    lv->items[index] = strdup(text);
    lv->item_count++;

    // Adjust selection
    if (lv->selected_index >= index) {
        lv->selected_index++;
    }
}

void listview_remove_item(listview_t *lv, int index) {
    if (!lv || index < 0 || index >= lv->item_count)
        return;

    free(lv->items[index]);

    // Shift items
    memmove(&lv->items[index], &lv->items[index + 1], (lv->item_count - index - 1) * sizeof(char *));
    lv->item_count--;

    // Adjust selection
    if (lv->selected_index >= lv->item_count) {
        lv->selected_index = lv->item_count - 1;
    }
}

void listview_clear(listview_t *lv) {
    if (!lv)
        return;

    for (int i = 0; i < lv->item_count; i++) {
        free(lv->items[i]);
    }
    lv->item_count = 0;
    lv->selected_index = -1;
    lv->scroll_offset = 0;
}

int listview_get_count(listview_t *lv) {
    return lv ? lv->item_count : 0;
}

const char *listview_get_item(listview_t *lv, int index) {
    if (!lv || index < 0 || index >= lv->item_count)
        return NULL;
    return lv->items[index];
}

void listview_set_item(listview_t *lv, int index, const char *text) {
    if (!lv || !text || index < 0 || index >= lv->item_count)
        return;

    free(lv->items[index]);
    lv->items[index] = strdup(text);
}

int listview_get_selected(listview_t *lv) {
    return lv ? lv->selected_index : -1;
}

void listview_set_selected(listview_t *lv, int index) {
    if (!lv)
        return;

    if (index < -1)
        index = -1;
    if (index >= lv->item_count)
        index = lv->item_count - 1;

    lv->selected_index = index;
    if (index >= 0) {
        listview_ensure_visible(lv, index);
    }
}

void listview_set_onselect(listview_t *lv, listview_select_fn callback, void *data) {
    if (lv) {
        lv->on_select = callback;
        lv->callback_data = data;
    }
}

void listview_set_ondoubleclick(listview_t *lv, listview_select_fn callback, void *data) {
    if (lv) {
        lv->on_double_click = callback;
        lv->callback_data = data;
    }
}

void listview_ensure_visible(listview_t *lv, int index) {
    if (!lv || index < 0 || index >= lv->item_count)
        return;

    if (index < lv->scroll_offset) {
        lv->scroll_offset = index;
    } else if (index >= lv->scroll_offset + lv->visible_items) {
        lv->scroll_offset = index - lv->visible_items + 1;
    }
}
