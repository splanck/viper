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

    // Ensure framebuffer writes reach memory before display scanout
    __asm__ volatile("dsb sy" ::: "memory");
}

void composite() {
    // Ensure we see the latest pixel writes from client applications.
    // On ARM64, cache coherency isn't automatic between processes sharing memory.
    __asm__ volatile("dmb sy" ::: "memory");

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

        // Blit surface content to back buffer (row-based for performance)
        for (uint32_t sy = 0; sy < surf->height; sy++) {
            int32_t dst_y = surf->y + static_cast<int32_t>(sy);
            if (dst_y < 0 || dst_y >= static_cast<int32_t>(g_fb_height))
                continue;

            // Calculate visible horizontal span (clamp once per row)
            int32_t src_start = 0;
            int32_t dst_x_start = surf->x;
            if (dst_x_start < 0) {
                src_start = -dst_x_start;
                dst_x_start = 0;
            }
            int32_t dst_x_end = surf->x + static_cast<int32_t>(surf->width);
            if (dst_x_end > static_cast<int32_t>(g_fb_width))
                dst_x_end = static_cast<int32_t>(g_fb_width);

            if (dst_x_start >= dst_x_end)
                continue;

            uint32_t copy_width = static_cast<uint32_t>(dst_x_end - dst_x_start);
            uint32_t *src_row = &surf->pixels[sy * (surf->stride / 4) + src_start];
            uint32_t *dst_row =
                &g_back_buffer[dst_y * (g_fb_pitch / 4) + dst_x_start];

            for (uint32_t i = 0; i < copy_width; i++) {
                dst_row[i] = src_row[i];
            }
        }

        // Draw scrollbars on top of content
        draw_vscrollbar(surf);
        draw_hscrollbar(surf);
    }

    // Draw global menu bar (Amiga/Mac style - always on top)
    draw_menu_bar();
    draw_pulldown_menu();

    // Draw cursor to back buffer (included in the atomic flip, eliminates flicker)
    draw_cursor();

    // Copy back buffer (with cursor) to front buffer in one operation
    flip_buffers();

    // Restore draw target to front buffer for any direct operations
    g_draw_target = g_fb;
}

} // namespace displayd
