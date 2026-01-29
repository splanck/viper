//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file graphics.cpp
 * @brief Drawing primitives for displayd.
 */

#include "graphics.hpp"
#include "font.hpp"
#include "state.hpp"

namespace displayd {

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < g_fb_width && y < g_fb_height) {
        g_draw_target[y * (g_fb_pitch / 4) + x] = color;
    }
}

uint32_t get_pixel(uint32_t x, uint32_t y) {
    if (x < g_fb_width && y < g_fb_height) {
        return g_draw_target[y * (g_fb_pitch / 4) + x];
    }
    return 0;
}

void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    // Clamp to screen
    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + static_cast<int32_t>(w);
    int32_t y2 = y + static_cast<int32_t>(h);
    if (x2 > static_cast<int32_t>(g_fb_width))
        x2 = static_cast<int32_t>(g_fb_width);
    if (y2 > static_cast<int32_t>(g_fb_height))
        y2 = static_cast<int32_t>(g_fb_height);

    for (int32_t py = y1; py < y2; py++) {
        for (int32_t px = x1; px < x2; px++) {
            g_draw_target[py * (g_fb_pitch / 4) + px] = color;
        }
    }
}

void draw_char(int32_t x, int32_t y, char c, uint32_t color) {
    if (c < 32 || c > 127)
        return;
    const uint8_t *glyph = g_font[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int32_t px = x + col;
                int32_t py = y + row;
                if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                    py < static_cast<int32_t>(g_fb_height)) {
                    put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), color);
                }
            }
        }
    }
}

void draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    while (*text) {
        draw_char(x, y, *text, color);
        x += 8;
        text++;
    }
}

} // namespace displayd
