//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file window.cpp
 * @brief Window decorations and scrollbars for displayd.
 */

#include "window.hpp"
#include "graphics.hpp"
#include "state.hpp"

namespace displayd {

void draw_window_decorations(Surface *surf) {
    if (!surf || !surf->in_use || !surf->visible)
        return;
    if (surf->flags & SURFACE_FLAG_NO_DECORATIONS)
        return;

    int32_t win_x = surf->x - static_cast<int32_t>(BORDER_WIDTH);
    int32_t win_y = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
    uint32_t win_w = surf->width + BORDER_WIDTH * 2;
    uint32_t win_h = surf->height + TITLE_BAR_HEIGHT + BORDER_WIDTH * 2;

    bool focused = (surf->id == g_focused_surface);

    // Border
    fill_rect(win_x, win_y, win_w, win_h, COLOR_BORDER);

    // Title bar
    uint32_t title_color = focused ? COLOR_TITLE_FOCUSED : COLOR_TITLE_UNFOCUSED;
    fill_rect(win_x + BORDER_WIDTH,
              win_y + BORDER_WIDTH,
              win_w - BORDER_WIDTH * 2,
              TITLE_BAR_HEIGHT,
              title_color);

    // Title text
    draw_text(win_x + BORDER_WIDTH + 8, win_y + BORDER_WIDTH + 8, surf->title, COLOR_WHITE);

    int32_t btn_y = win_y + BORDER_WIDTH + 4;
    int32_t btn_spacing = CLOSE_BUTTON_SIZE + 4;

    // Close button (rightmost)
    int32_t close_x = win_x + static_cast<int32_t>(win_w) - BORDER_WIDTH - CLOSE_BUTTON_SIZE - 4;
    fill_rect(close_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_CLOSE_BTN);
    draw_char(close_x + 4, btn_y + 4, 'X', COLOR_WHITE);

    // Maximize button (second from right)
    int32_t max_x = close_x - btn_spacing;
    fill_rect(max_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_MAX_BTN);
    if (surf->maximized) {
        draw_char(max_x + 4, btn_y + 4, 'R', COLOR_WHITE);
    } else {
        draw_char(max_x + 4, btn_y + 4, 'M', COLOR_WHITE);
    }

    // Minimize button (third from right)
    int32_t min_x = max_x - btn_spacing;
    fill_rect(min_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_MIN_BTN);
    draw_char(min_x + 4, btn_y + 4, '_', COLOR_WHITE);
}

void draw_vscrollbar(Surface *surf) {
    if (!surf || !surf->vscroll.enabled)
        return;
    if (surf->vscroll.content_size <= surf->vscroll.viewport_size)
        return;

    // Scrollbar is drawn INSIDE the client area on the right edge
    int32_t sb_x =
        surf->x + static_cast<int32_t>(surf->width) - static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_y = surf->y;
    int32_t sb_h = static_cast<int32_t>(surf->height);

    // Clamp to screen bounds
    if (sb_x < 0 || sb_x >= static_cast<int32_t>(g_fb_width))
        return;

    // Draw track background
    fill_rect(sb_x, sb_y, SCROLLBAR_WIDTH, static_cast<uint32_t>(sb_h), COLOR_SCROLLBAR_TRACK);

    // Calculate thumb position and size
    int32_t content = surf->vscroll.content_size;
    int32_t viewport = surf->vscroll.viewport_size;
    int32_t scroll_pos = surf->vscroll.scroll_pos;

    // Thumb height proportional to viewport/content ratio
    int32_t thumb_h = (viewport * sb_h) / content;
    if (thumb_h < static_cast<int32_t>(SCROLLBAR_MIN_THUMB))
        thumb_h = static_cast<int32_t>(SCROLLBAR_MIN_THUMB);

    // Thumb position proportional to scroll position
    int32_t scroll_range = content - viewport;
    int32_t track_range = sb_h - thumb_h;
    int32_t thumb_y = sb_y;
    if (scroll_range > 0)
        thumb_y = sb_y + (scroll_pos * track_range) / scroll_range;

    // Draw thumb with 3D appearance
    fill_rect(sb_x + 2,
              thumb_y + 2,
              SCROLLBAR_WIDTH - 4,
              static_cast<uint32_t>(thumb_h - 4),
              COLOR_SCROLLBAR_THUMB);

    // Top highlight
    fill_rect(sb_x + 2, thumb_y + 2, SCROLLBAR_WIDTH - 4, 1, COLOR_WHITE);
    // Left highlight
    fill_rect(sb_x + 2, thumb_y + 2, 1, static_cast<uint32_t>(thumb_h - 4), COLOR_WHITE);
    // Bottom shadow
    fill_rect(sb_x + 2, thumb_y + thumb_h - 3, SCROLLBAR_WIDTH - 4, 1, COLOR_SCROLLBAR_ARROW);
    // Right shadow
    fill_rect(sb_x + SCROLLBAR_WIDTH - 3,
              thumb_y + 2,
              1,
              static_cast<uint32_t>(thumb_h - 4),
              COLOR_SCROLLBAR_ARROW);
}

void draw_hscrollbar(Surface *surf) {
    if (!surf || !surf->hscroll.enabled)
        return;
    if (surf->hscroll.content_size <= surf->hscroll.viewport_size)
        return;

    // Scrollbar is drawn INSIDE the client area on the bottom edge
    int32_t sb_x = surf->x;
    int32_t sb_y =
        surf->y + static_cast<int32_t>(surf->height) - static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_w = static_cast<int32_t>(surf->width);

    // Draw track
    fill_rect(sb_x, sb_y, static_cast<uint32_t>(sb_w), SCROLLBAR_WIDTH, COLOR_SCROLLBAR_TRACK);

    // Calculate thumb position and size
    int32_t content = surf->hscroll.content_size;
    int32_t viewport = surf->hscroll.viewport_size;
    int32_t scroll_pos = surf->hscroll.scroll_pos;

    // Thumb width proportional to viewport/content ratio
    int32_t thumb_w = (viewport * sb_w) / content;
    if (thumb_w < static_cast<int32_t>(SCROLLBAR_MIN_THUMB))
        thumb_w = static_cast<int32_t>(SCROLLBAR_MIN_THUMB);

    // Thumb position proportional to scroll position
    int32_t scroll_range = content - viewport;
    int32_t track_range = sb_w - thumb_w;
    int32_t thumb_x = sb_x;
    if (scroll_range > 0)
        thumb_x = sb_x + (scroll_pos * track_range) / scroll_range;

    // Draw thumb
    fill_rect(thumb_x + 2,
              sb_y + 2,
              static_cast<uint32_t>(thumb_w - 4),
              SCROLLBAR_WIDTH - 4,
              COLOR_SCROLLBAR_THUMB);
}

uint8_t get_resize_edge(Surface *surf, int32_t x, int32_t y) {
    if (!surf)
        return 0;
    if (surf->maximized)
        return 0; // Can't resize maximized windows
    if (surf->flags & SURFACE_FLAG_SYSTEM)
        return 0; // SYSTEM surfaces (desktop) are not resizable

    int32_t win_x1 = surf->x - static_cast<int32_t>(BORDER_WIDTH);
    int32_t win_y1 = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
    int32_t win_x2 = surf->x + static_cast<int32_t>(surf->width + BORDER_WIDTH);
    int32_t win_y2 = surf->y + static_cast<int32_t>(surf->height + BORDER_WIDTH);

    // Check if inside window at all
    if (x < win_x1 || x >= win_x2 || y < win_y1 || y >= win_y2)
        return 0;

    // Check if in title bar (not resizable)
    int32_t title_y2 = surf->y - static_cast<int32_t>(BORDER_WIDTH);
    if (y >= win_y1 && y < title_y2)
        return 0;

    uint8_t edge = 0;

    // Check edges (only if in border area)
    if (x < win_x1 + RESIZE_BORDER)
        edge |= 1; // Left
    if (x >= win_x2 - RESIZE_BORDER)
        edge |= 2; // Right
    if (y >= win_y2 - RESIZE_BORDER)
        edge |= 8; // Bottom

    return edge;
}

int32_t check_vscrollbar_click(Surface *surf, int32_t x, int32_t y) {
    if (!surf || !surf->vscroll.enabled)
        return -1;
    if (surf->vscroll.content_size <= surf->vscroll.viewport_size)
        return -1;

    // Scrollbar bounds (inside client area on right edge)
    int32_t sb_x =
        surf->x + static_cast<int32_t>(surf->width) - static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_y = surf->y;
    int32_t sb_w = static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_h = static_cast<int32_t>(surf->height);

    // Check if click is in scrollbar area
    if (x < sb_x || x >= sb_x + sb_w)
        return -1;
    if (y < sb_y || y >= sb_y + sb_h)
        return -1;

    // Calculate new scroll position based on click position
    int32_t content = surf->vscroll.content_size;
    int32_t viewport = surf->vscroll.viewport_size;
    int32_t scroll_range = content - viewport;

    // Calculate thumb size
    int32_t thumb_h = (viewport * sb_h) / content;
    if (thumb_h < static_cast<int32_t>(SCROLLBAR_MIN_THUMB))
        thumb_h = static_cast<int32_t>(SCROLLBAR_MIN_THUMB);

    int32_t track_range = sb_h - thumb_h;

    // Map click position to scroll position
    int32_t click_offset = y - sb_y - thumb_h / 2;
    if (click_offset < 0)
        click_offset = 0;
    if (click_offset > track_range)
        click_offset = track_range;

    int32_t new_pos = 0;
    if (track_range > 0)
        new_pos = (click_offset * scroll_range) / track_range;

    return new_pos;
}

} // namespace displayd
