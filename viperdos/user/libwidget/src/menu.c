//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/menu.c
// Purpose: Menu system implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

#define MENU_ITEM_HEIGHT   20
#define MENU_PADDING       4
#define MENU_MIN_WIDTH     100
#define SEPARATOR_HEIGHT   8
#define INITIAL_CAPACITY   8

//===----------------------------------------------------------------------===//
// Menu API
//===----------------------------------------------------------------------===//

menu_t *menu_create(void) {
    menu_t *m = (menu_t *)malloc(sizeof(menu_t));
    if (!m)
        return NULL;

    memset(m, 0, sizeof(menu_t));

    m->item_capacity = INITIAL_CAPACITY;
    m->items = (menu_item_t *)malloc(m->item_capacity * sizeof(menu_item_t));
    if (!m->items) {
        free(m);
        return NULL;
    }

    m->hovered_index = -1;
    return m;
}

void menu_destroy(menu_t *m) {
    if (!m)
        return;

    // Destroy submenus
    for (int i = 0; i < m->item_count; i++) {
        if (m->items[i].submenu) {
            menu_destroy(m->items[i].submenu);
        }
    }

    free(m->items);
    free(m);
}

static void menu_grow_if_needed(menu_t *m) {
    if (m->item_count >= m->item_capacity) {
        int new_cap = m->item_capacity * 2;
        menu_item_t *new_items = (menu_item_t *)realloc(m->items, new_cap * sizeof(menu_item_t));
        if (!new_items)
            return;
        m->items = new_items;
        m->item_capacity = new_cap;
    }
}

void menu_add_item(menu_t *m, const char *text, widget_callback_fn callback, void *data) {
    menu_add_item_with_shortcut(m, text, NULL, callback, data);
}

void menu_add_item_with_shortcut(menu_t *m, const char *text, const char *shortcut,
                                  widget_callback_fn callback, void *data) {
    if (!m)
        return;

    menu_grow_if_needed(m);

    menu_item_t *item = &m->items[m->item_count++];
    memset(item, 0, sizeof(menu_item_t));

    if (text) {
        strncpy(item->text, text, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    }

    if (shortcut) {
        strncpy(item->shortcut, shortcut, sizeof(item->shortcut) - 1);
        item->shortcut[sizeof(item->shortcut) - 1] = '\0';
    }

    item->enabled = true;
    item->on_click = callback;
    item->callback_data = data;
}

void menu_add_separator(menu_t *m) {
    if (!m)
        return;

    menu_grow_if_needed(m);

    menu_item_t *item = &m->items[m->item_count++];
    memset(item, 0, sizeof(menu_item_t));
    item->separator = true;
    item->enabled = false;
}

void menu_add_submenu(menu_t *m, const char *text, menu_t *submenu) {
    if (!m)
        return;

    menu_grow_if_needed(m);

    menu_item_t *item = &m->items[m->item_count++];
    memset(item, 0, sizeof(menu_item_t));

    if (text) {
        strncpy(item->text, text, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    }

    item->enabled = true;
    item->submenu = submenu;
}

void menu_set_item_enabled(menu_t *m, int index, bool enabled) {
    if (!m || index < 0 || index >= m->item_count)
        return;
    m->items[index].enabled = enabled;
}

void menu_set_item_checked(menu_t *m, int index, bool checked) {
    if (!m || index < 0 || index >= m->item_count)
        return;
    m->items[index].checked = checked;
}

static void menu_calculate_size(menu_t *m) {
    int max_text_width = 0;
    int max_shortcut_width = 0;

    for (int i = 0; i < m->item_count; i++) {
        menu_item_t *item = &m->items[i];

        if (!item->separator) {
            int text_width = (int)strlen(item->text) * 8;
            if (text_width > max_text_width) {
                max_text_width = text_width;
            }

            if (item->shortcut[0]) {
                int shortcut_width = (int)strlen(item->shortcut) * 8;
                if (shortcut_width > max_shortcut_width) {
                    max_shortcut_width = shortcut_width;
                }
            }
        }
    }

    m->width = max_text_width + max_shortcut_width + MENU_PADDING * 4;
    if (max_shortcut_width > 0) {
        m->width += 20; // Gap between text and shortcut
    }
    if (m->width < MENU_MIN_WIDTH) {
        m->width = MENU_MIN_WIDTH;
    }

    m->height = MENU_PADDING * 2;
    for (int i = 0; i < m->item_count; i++) {
        m->height += m->items[i].separator ? SEPARATOR_HEIGHT : MENU_ITEM_HEIGHT;
    }
}

void menu_show(menu_t *m, gui_window_t *win, int x, int y) {
    (void)win;
    if (!m)
        return;

    menu_calculate_size(m);

    m->x = x;
    m->y = y;
    m->visible = true;
    m->hovered_index = -1;
}

void menu_hide(menu_t *m) {
    if (m) {
        m->visible = false;
        m->hovered_index = -1;

        // Hide submenus
        for (int i = 0; i < m->item_count; i++) {
            if (m->items[i].submenu) {
                menu_hide(m->items[i].submenu);
            }
        }
    }
}

bool menu_is_visible(menu_t *m) {
    return m ? m->visible : false;
}

bool menu_handle_mouse(menu_t *m, int x, int y, int button, int event_type) {
    if (!m || !m->visible)
        return false;

    // Check if inside menu
    bool inside = (x >= m->x && x < m->x + m->width && y >= m->y && y < m->y + m->height);

    if (!inside) {
        if (event_type == 1 && button == 0) { // Click outside
            menu_hide(m);
        }
        return false;
    }

    // Find hovered item
    int item_y = m->y + MENU_PADDING;
    int hovered = -1;

    for (int i = 0; i < m->item_count; i++) {
        menu_item_t *item = &m->items[i];
        int item_height = item->separator ? SEPARATOR_HEIGHT : MENU_ITEM_HEIGHT;

        if (y >= item_y && y < item_y + item_height) {
            if (!item->separator && item->enabled) {
                hovered = i;
            }
            break;
        }

        item_y += item_height;
    }

    m->hovered_index = hovered;

    // Handle click
    if (event_type == 1 && button == 0 && hovered >= 0) {
        menu_item_t *item = &m->items[hovered];

        if (item->submenu) {
            // Show submenu
            menu_show(item->submenu, NULL, m->x + m->width - 4, item_y);
        } else if (item->on_click) {
            // Execute callback
            item->on_click(item->callback_data);
            menu_hide(m);
        }

        return true;
    }

    return true;
}

void menu_paint(menu_t *m, gui_window_t *win) {
    if (!m || !m->visible)
        return;

    int x = m->x;
    int y = m->y;

    // Draw menu background with 3D border
    draw_3d_raised(win, x, y, m->width, m->height, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

    // Draw items
    int item_y = y + MENU_PADDING;

    for (int i = 0; i < m->item_count; i++) {
        menu_item_t *item = &m->items[i];

        if (item->separator) {
            // Draw separator line
            int sep_y = item_y + SEPARATOR_HEIGHT / 2;
            gui_draw_hline(win, x + MENU_PADDING, x + m->width - MENU_PADDING, sep_y, WB_GRAY_DARK);
            gui_draw_hline(win, x + MENU_PADDING, x + m->width - MENU_PADDING, sep_y + 1, WB_WHITE);
            item_y += SEPARATOR_HEIGHT;
        } else {
            // Highlight hovered item
            if (i == m->hovered_index) {
                gui_fill_rect(win, x + 2, item_y, m->width - 4, MENU_ITEM_HEIGHT, WB_BLUE);
            }

            // Draw checkmark if checked
            if (item->checked) {
                uint32_t check_color = (i == m->hovered_index) ? WB_WHITE : WB_BLACK;
                gui_draw_text(win, x + MENU_PADDING, item_y + 5, "*", check_color);
            }

            // Draw text
            uint32_t text_color;
            if (!item->enabled) {
                text_color = WB_GRAY_MED;
            } else if (i == m->hovered_index) {
                text_color = WB_WHITE;
            } else {
                text_color = WB_BLACK;
            }

            gui_draw_text(win, x + MENU_PADDING + 16, item_y + 5, item->text, text_color);

            // Draw shortcut
            if (item->shortcut[0]) {
                int shortcut_x = x + m->width - MENU_PADDING - (int)strlen(item->shortcut) * 8;
                gui_draw_text(win, shortcut_x, item_y + 5, item->shortcut, text_color);
            }

            // Draw submenu arrow
            if (item->submenu) {
                gui_draw_text(win, x + m->width - MENU_PADDING - 8, item_y + 5, ">", text_color);
            }

            item_y += MENU_ITEM_HEIGHT;
        }
    }

    // Paint visible submenus
    for (int i = 0; i < m->item_count; i++) {
        if (m->items[i].submenu && menu_is_visible(m->items[i].submenu)) {
            menu_paint(m->items[i].submenu, win);
        }
    }
}
