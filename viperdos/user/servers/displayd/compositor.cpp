//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file compositor.cpp
 * @brief Compositing and buffer management for displayd.
 */

#include "compositor.hpp"
#include "cursor.hpp"
#include "graphics.hpp"
#include "menu.hpp"
#include "state.hpp"
#include "window.hpp"

namespace displayd {

void flip_buffers() {
    uint32_t pixels_per_row = g_fb_pitch / 4;
    uint32_t total_pixels = pixels_per_row * g_fb_height;

    // Fast copy using 64-bit transfers where possible
    uint64_t *dst = reinterpret_cast<uint64_t *>(g_fb);
    uint64_t *src = reinterpret_cast<uint64_t *>(g_back_buffer);
    uint32_t count64 = total_pixels / 2;

    for (uint32_t i = 0; i < count64; i++) {
        dst[i] = src[i];
    }

    // Handle odd pixel if any
    if (total_pixels & 1) {
        g_fb[total_pixels - 1] = g_back_buffer[total_pixels - 1];
    }
}

void composite() {
    // Draw to back buffer to avoid flicker
    g_draw_target = g_back_buffer;

    // Draw blue border around screen edges
    // Top border
    fill_rect(0, 0, g_fb_width, SCREEN_BORDER_WIDTH, COLOR_SCREEN_BORDER);
    // Bottom border
    fill_rect(
        0, g_fb_height - SCREEN_BORDER_WIDTH, g_fb_width, SCREEN_BORDER_WIDTH, COLOR_SCREEN_BORDER);
    // Left border
    fill_rect(0, 0, SCREEN_BORDER_WIDTH, g_fb_height, COLOR_SCREEN_BORDER);
    // Right border
    fill_rect(
        g_fb_width - SCREEN_BORDER_WIDTH, 0, SCREEN_BORDER_WIDTH, g_fb_height, COLOR_SCREEN_BORDER);

    // Clear inner desktop area
    fill_rect(SCREEN_BORDER_WIDTH,
              SCREEN_BORDER_WIDTH,
              g_fb_width - 2 * SCREEN_BORDER_WIDTH,
              g_fb_height - 2 * SCREEN_BORDER_WIDTH,
              COLOR_DESKTOP);

    // Build sorted list of visible surfaces by z-order (lowest first = drawn under)
    Surface *sorted[MAX_SURFACES];
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        Surface *surf = &g_surfaces[i];
        if (!surf->in_use || !surf->visible || !surf->pixels)
            continue;
        if (surf->minimized)
            continue; // Don't draw minimized windows
        sorted[count++] = surf;
    }

    // Simple insertion sort by z_order (small N, runs frequently)
    for (uint32_t i = 1; i < count; i++) {
        Surface *key = sorted[i];
        int32_t j = static_cast<int32_t>(i) - 1;
        while (j >= 0 && sorted[j]->z_order > key->z_order) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    // Draw surfaces back to front (lower z-order first)
    for (uint32_t i = 0; i < count; i++) {
        Surface *surf = sorted[i];

        // Draw decorations first
        draw_window_decorations(surf);

        // Blit surface content to back buffer
        for (uint32_t sy = 0; sy < surf->height; sy++) {
            int32_t dst_y = surf->y + static_cast<int32_t>(sy);
            if (dst_y < 0 || dst_y >= static_cast<int32_t>(g_fb_height))
                continue;

            for (uint32_t sx = 0; sx < surf->width; sx++) {
                int32_t dst_x = surf->x + static_cast<int32_t>(sx);
                if (dst_x < 0 || dst_x >= static_cast<int32_t>(g_fb_width))
                    continue;

                uint32_t pixel = surf->pixels[sy * (surf->stride / 4) + sx];
                g_back_buffer[dst_y * (g_fb_pitch / 4) + dst_x] = pixel;
            }
        }

        // Draw scrollbars on top of content
        draw_vscrollbar(surf);
        draw_hscrollbar(surf);
    }

    // Draw global menu bar (Amiga/Mac style - always on top)
    draw_menu_bar();
    draw_pulldown_menu();

    // Copy back buffer to front buffer in one operation
    flip_buffers();

    // Switch to front buffer for cursor operations
    g_draw_target = g_fb;

    // Save background under cursor, then draw cursor (on front buffer)
    save_cursor_background();
    draw_cursor();
}

} // namespace displayd
