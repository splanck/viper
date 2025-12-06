//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_graphics.h
// Purpose: Runtime bridge functions for ViperGFX graphics library.
// Key invariants: All window pointers are opaque handles from rt_gfx_window_new.
// Ownership/Lifetime: Windows must be destroyed with rt_gfx_window_destroy.
// Links: src/lib/graphics/include/vgfx.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new graphics window.
    /// @param width Window width in pixels.
    /// @param height Window height in pixels.
    /// @param title Window title (runtime string).
    /// @return Opaque window handle, or NULL on failure.
    void *rt_gfx_window_new(int64_t width, int64_t height, rt_string title);

    /// @brief Destroy a graphics window and free resources.
    /// @param window Window handle from rt_gfx_window_new.
    void rt_gfx_window_destroy(void *window);

    /// @brief Update the window display and process events.
    /// @param window Window handle.
    void rt_gfx_window_update(void *window);

    /// @brief Clear the window to a solid color.
    /// @param window Window handle.
    /// @param color RGB color (0x00RRGGBB).
    void rt_gfx_window_clear(void *window, int64_t color);

    /// @brief Draw a line from (x1,y1) to (x2,y2).
    /// @param window Window handle.
    /// @param x1 Starting X coordinate.
    /// @param y1 Starting Y coordinate.
    /// @param x2 Ending X coordinate.
    /// @param y2 Ending Y coordinate.
    /// @param color Line color (0x00RRGGBB).
    void rt_gfx_draw_line(
        void *window, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color);

    /// @brief Draw a filled rectangle.
    /// @param window Window handle.
    /// @param x Left edge X coordinate.
    /// @param y Top edge Y coordinate.
    /// @param w Rectangle width.
    /// @param h Rectangle height.
    /// @param color Fill color (0x00RRGGBB).
    void rt_gfx_draw_rect(void *window, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

    /// @brief Draw a rectangle outline.
    /// @param window Window handle.
    /// @param x Left edge X coordinate.
    /// @param y Top edge Y coordinate.
    /// @param w Rectangle width.
    /// @param h Rectangle height.
    /// @param color Outline color (0x00RRGGBB).
    void rt_gfx_draw_rect_outline(
        void *window, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

    /// @brief Draw a filled circle.
    /// @param window Window handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param radius Circle radius in pixels.
    /// @param color Fill color (0x00RRGGBB).
    void rt_gfx_draw_circle(void *window, int64_t cx, int64_t cy, int64_t radius, int64_t color);

    /// @brief Draw a circle outline.
    /// @param window Window handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param radius Circle radius in pixels.
    /// @param color Outline color (0x00RRGGBB).
    void rt_gfx_draw_circle_outline(
        void *window, int64_t cx, int64_t cy, int64_t radius, int64_t color);

    /// @brief Set a single pixel to a color.
    /// @param window Window handle.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param color Pixel color (0x00RRGGBB).
    void rt_gfx_set_pixel(void *window, int64_t x, int64_t y, int64_t color);

    /// @brief Get the window width.
    /// @param window Window handle.
    /// @return Window width in pixels.
    int64_t rt_gfx_window_width(void *window);

    /// @brief Get the window height.
    /// @param window Window handle.
    /// @return Window height in pixels.
    int64_t rt_gfx_window_height(void *window);

    /// @brief Check if window should close.
    /// @param window Window handle.
    /// @return 1 if close was requested, 0 otherwise.
    int64_t rt_gfx_window_should_close(void *window);

    /// @brief Poll the next event from the window.
    /// @param window Window handle.
    /// @return Event type code (0 = no event).
    int64_t rt_gfx_poll_event(void *window);

    /// @brief Check if a key is currently pressed.
    /// @param window Window handle.
    /// @param key Key code to check.
    /// @return 1 if pressed, 0 otherwise.
    int64_t rt_gfx_key_down(void *window, int64_t key);

    /// @brief Construct a color from RGB components.
    /// @param r Red component (0-255).
    /// @param g Green component (0-255).
    /// @param b Blue component (0-255).
    /// @return Packed color value (0x00RRGGBB).
    int64_t rt_gfx_rgb(int64_t r, int64_t g, int64_t b);

#ifdef __cplusplus
}
#endif
