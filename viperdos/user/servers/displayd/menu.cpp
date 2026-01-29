//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file menu.cpp
 * @brief Menu bar and pulldown menu handling for displayd.
 */

#include "menu.hpp"
#include "graphics.hpp"
#include "state.hpp"
#include "surface.hpp"

namespace displayd {

void calc_menu_positions(Surface *surf) {
    if (!surf || surf->menu_count == 0)
        return;

    int32_t x = MENU_PADDING; // Start at left edge of screen
    for (uint8_t i = 0; i < surf->menu_count && i < MAX_MENUS; i++) {
        g_menu_title_positions[i] = x;
        // Calculate title width (8 pixels per char)
        int32_t title_len = 0;
        for (const char *p = surf->menus[i].title; *p; p++)
            title_len++;
        x += title_len * 8 + MENU_PADDING * 2;
    }
}

int32_t find_menu_at(int32_t x, int32_t y) {
    // Menu bar is at y=0 to y=MENU_BAR_HEIGHT
    if (y < 0 || y >= static_cast<int32_t>(MENU_BAR_HEIGHT))
        return -1;

    Surface *surf = get_menu_surface();
    if (!surf || surf->menu_count == 0)
        return -1;

    for (uint8_t i = 0; i < surf->menu_count; i++) {
        int32_t title_x = g_menu_title_positions[i];
        int32_t title_len = 0;
        for (const char *p = surf->menus[i].title; *p; p++)
            title_len++;
        int32_t title_w = title_len * 8 + MENU_PADDING * 2;

        if (x >= title_x && x < title_x + title_w)
            return i;
    }
    return -1;
}

int32_t find_menu_item_at(int32_t x, int32_t y) {
    if (g_active_menu < 0)
        return -1;

    Surface *surf = get_menu_surface();
    if (!surf || g_active_menu >= surf->menu_count)
        return -1;

    const MenuDef &menu = surf->menus[g_active_menu];
    int32_t menu_x = g_menu_title_positions[g_active_menu];
    int32_t menu_y = MENU_BAR_HEIGHT; // Dropdown starts just below menu bar

    // Calculate menu width
    int32_t max_width = 0;
    for (uint8_t i = 0; i < menu.item_count; i++) {
        int32_t item_len = 0;
        for (const char *p = menu.items[i].label; *p; p++)
            item_len++;
        int32_t shortcut_len = 0;
        for (const char *p = menu.items[i].shortcut; *p; p++)
            shortcut_len++;
        int32_t w = (item_len + shortcut_len + 4) * 8;
        if (w > max_width)
            max_width = w;
    }
    int32_t menu_w = max_width + MENU_PADDING * 2;
    int32_t menu_h = menu.item_count * MENU_ITEM_HEIGHT + 4;

    if (x < menu_x || x >= menu_x + menu_w || y < menu_y || y >= menu_y + menu_h)
        return -1;

    int32_t item_idx = (y - menu_y - 2) / MENU_ITEM_HEIGHT;
    if (item_idx >= 0 && item_idx < menu.item_count)
        return item_idx;

    return -1;
}

void draw_menu_bar() {
    Surface *surf = get_menu_surface();

    // Menu bar background - full width at top of screen
    int32_t bar_x = 0;
    int32_t bar_y = 0;
    int32_t bar_w = g_fb_width;

    fill_rect(bar_x, bar_y, bar_w, MENU_BAR_HEIGHT, COLOR_MENU_BG);

    // Top highlight, bottom shadow
    for (int32_t px = bar_x; px < bar_x + static_cast<int32_t>(bar_w); px++) {
        put_pixel(px, bar_y, COLOR_MENU_BORDER_LIGHT);
        put_pixel(px, bar_y + MENU_BAR_HEIGHT - 1, COLOR_MENU_BORDER_DARK);
    }

    // Draw menu titles if focused surface has menus
    if (surf && surf->menu_count > 0) {
        calc_menu_positions(surf);

        for (uint8_t i = 0; i < surf->menu_count; i++) {
            int32_t title_x = g_menu_title_positions[i];
            int32_t title_len = 0;
            for (const char *p = surf->menus[i].title; *p; p++)
                title_len++;
            int32_t title_w = title_len * 8 + MENU_PADDING * 2;

            // Highlight active menu
            if (i == static_cast<uint8_t>(g_active_menu)) {
                fill_rect(title_x, bar_y + 1, title_w, MENU_BAR_HEIGHT - 2, COLOR_MENU_HIGHLIGHT);
                draw_text(title_x + MENU_PADDING,
                          bar_y + 4,
                          surf->menus[i].title,
                          COLOR_MENU_HIGHLIGHT_TEXT);
            } else {
                draw_text(title_x + MENU_PADDING, bar_y + 4, surf->menus[i].title, COLOR_MENU_TEXT);
            }
        }
    }

    // Right side: App title or "ViperDOS"
    const char *right_text = surf ? surf->title : "ViperDOS";
    int32_t text_len = 0;
    for (const char *p = right_text; *p; p++)
        text_len++;
    draw_text(
        bar_x + static_cast<int32_t>(bar_w) - text_len * 8 - MENU_PADDING, bar_y + 4, right_text, COLOR_MENU_DISABLED);
}

void draw_pulldown_menu() {
    if (g_active_menu < 0)
        return;

    Surface *surf = get_menu_surface();
    if (!surf || g_active_menu >= surf->menu_count)
        return;

    const MenuDef &menu = surf->menus[g_active_menu];
    int32_t menu_x = g_menu_title_positions[g_active_menu];
    int32_t menu_y = MENU_BAR_HEIGHT; // Dropdown starts just below menu bar

    // Calculate menu width
    int32_t max_width = 0;
    for (uint8_t i = 0; i < menu.item_count; i++) {
        int32_t item_len = 0;
        for (const char *p = menu.items[i].label; *p; p++)
            item_len++;
        int32_t shortcut_len = 0;
        for (const char *p = menu.items[i].shortcut; *p; p++)
            shortcut_len++;
        int32_t w = (item_len + shortcut_len + 4) * 8;
        if (w > max_width)
            max_width = w;
    }

    int32_t menu_w = max_width + MENU_PADDING * 2;
    int32_t menu_h = menu.item_count * MENU_ITEM_HEIGHT + 4;

    // Menu background
    fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_MENU_BG);

    // 3D border
    for (int32_t px = menu_x; px < menu_x + menu_w; px++) {
        put_pixel(px, menu_y, COLOR_MENU_BORDER_LIGHT);
        put_pixel(px, menu_y + menu_h - 1, COLOR_MENU_BORDER_DARK);
    }
    for (int32_t py = menu_y; py < menu_y + menu_h; py++) {
        put_pixel(menu_x, py, COLOR_MENU_BORDER_LIGHT);
        put_pixel(menu_x + menu_w - 1, py, COLOR_MENU_BORDER_DARK);
    }

    // Draw menu items
    int32_t item_y = menu_y + 2;
    for (uint8_t i = 0; i < menu.item_count; i++) {
        const MenuItem &item = menu.items[i];

        // Separator
        if (item.label[0] == '-' || item.label[0] == '\0') {
            int32_t sep_y = item_y + MENU_ITEM_HEIGHT / 2;
            for (int32_t px = menu_x + 4; px < menu_x + menu_w - 4; px++)
                put_pixel(px, sep_y, COLOR_MENU_BORDER_DARK);
            item_y += MENU_ITEM_HEIGHT;
            continue;
        }

        // Highlight hovered item
        uint32_t text_color = item.enabled ? COLOR_MENU_TEXT : COLOR_MENU_DISABLED;
        if (static_cast<int32_t>(i) == g_hovered_menu_item && item.enabled) {
            fill_rect(menu_x + 2, item_y, menu_w - 4, MENU_ITEM_HEIGHT, COLOR_MENU_HIGHLIGHT);
            text_color = COLOR_MENU_HIGHLIGHT_TEXT;
        }

        // Checkmark
        if (item.checked) {
            draw_text(menu_x + 4, item_y + 2, "*", text_color);
        }

        // Label
        draw_text(menu_x + 16, item_y + 2, item.label, text_color);

        // Shortcut (right-aligned)
        if (item.shortcut[0]) {
            int32_t sc_len = 0;
            for (const char *p = item.shortcut; *p; p++)
                sc_len++;
            draw_text(menu_x + menu_w - sc_len * 8 - 8, item_y + 2, item.shortcut, text_color);
        }

        item_y += MENU_ITEM_HEIGHT;
    }
}

void close_menu() {
    g_active_menu = -1;
    g_hovered_menu_item = -1;
}

bool handle_menu_click(int32_t x, int32_t y) {
    int32_t menu_idx = find_menu_at(x, y);
    if (menu_idx >= 0) {
        if (g_active_menu == menu_idx) {
            close_menu();
        } else {
            g_active_menu = menu_idx;
            g_hovered_menu_item = -1;
        }
        return true;
    }
    return false;
}

} // namespace displayd
