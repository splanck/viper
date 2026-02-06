//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file cursor.cpp
 * @brief Mouse cursor management for displayd.
 */

#include "cursor.hpp"
#include "graphics.hpp"
#include "state.hpp"

namespace displayd {

// 24x24 arrow cursor (1 = orange fill, 2 = black outline)
// clang-format off
const uint8_t g_cursor_data[CURSOR_SIZE * CURSOR_SIZE] = {
    2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,2,2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
// clang-format on

void save_cursor_background() {
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                py < static_cast<int32_t>(g_fb_height)) {
                g_cursor_saved[dy * CURSOR_SIZE + dx] =
                    get_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
            }
        }
    }
}

void restore_cursor_background() {
    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                py < static_cast<int32_t>(g_fb_height)) {
                put_pixel(static_cast<uint32_t>(px),
                          static_cast<uint32_t>(py),
                          g_cursor_saved[dy * CURSOR_SIZE + dx]);
            }
        }
    }
}

void draw_cursor() {
    if (!g_cursor_visible)
        return;

    for (int dy = 0; dy < CURSOR_SIZE; dy++) {
        for (int dx = 0; dx < CURSOR_SIZE; dx++) {
            uint8_t pixel = g_cursor_data[dy * CURSOR_SIZE + dx];
            if (pixel == 0)
                continue;

            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                py < static_cast<int32_t>(g_fb_height)) {
                uint32_t color = (pixel == 1) ? COLOR_CURSOR : 0xFF000000;
                put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), color);
            }
        }
    }
}

void setup_hardware_cursor() {
    // Convert 24x24 cursor bitmap to 24x24 BGRA pixels
    static uint32_t cursor_pixels[CURSOR_SIZE * CURSOR_SIZE];
    for (int i = 0; i < CURSOR_SIZE * CURSOR_SIZE; i++) {
        uint8_t v = g_cursor_data[i];
        if (v == 0)
            cursor_pixels[i] = 0x00000000; // Transparent
        else if (v == 1)
            cursor_pixels[i] = COLOR_CURSOR; // Orange fill
        else
            cursor_pixels[i] = 0xFF000000; // Black outline
    }
    if (sys::set_cursor_image(cursor_pixels, CURSOR_SIZE, CURSOR_SIZE, 0, 0) == 0) {
        g_cursor_visible = false; // Disable software cursor
        sys::move_hw_cursor(static_cast<uint32_t>(g_cursor_x), static_cast<uint32_t>(g_cursor_y));
        debug_print("[displayd] Hardware cursor enabled\n");
    }
}

} // namespace displayd
