//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_graphics.h
// Purpose: Runtime bridge functions for ViperGFX graphics library.
// Key invariants: All canvas pointers are opaque handles from rt_canvas_new.
// Ownership/Lifetime: Canvases must be destroyed with rt_canvas_destroy.
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

    //=========================================================================
    // Canvas Functions
    //=========================================================================

    /// @brief Create a new graphics canvas.
    /// @param title Window title (runtime string).
    /// @param width Canvas width in pixels.
    /// @param height Canvas height in pixels.
    /// @return Opaque canvas handle, or NULL on failure.
    void *rt_canvas_new(rt_string title, int64_t width, int64_t height);

    /// @brief Destroy a graphics canvas and free resources.
    /// @param canvas Canvas handle from rt_canvas_new.
    void rt_canvas_destroy(void *canvas);

    /// @brief Get the canvas width.
    /// @param canvas Canvas handle.
    /// @return Canvas width in pixels.
    int64_t rt_canvas_width(void *canvas);

    /// @brief Get the canvas height.
    /// @param canvas Canvas handle.
    /// @return Canvas height in pixels.
    int64_t rt_canvas_height(void *canvas);

    /// @brief Check if canvas should close.
    /// @param canvas Canvas handle.
    /// @return 1 if close was requested, 0 otherwise.
    int64_t rt_canvas_should_close(void *canvas);

    /// @brief Swap buffers and display drawn content.
    /// @param canvas Canvas handle.
    void rt_canvas_flip(void *canvas);

    /// @brief Clear the canvas to a solid color.
    /// @param canvas Canvas handle.
    /// @param color RGB color (0x00RRGGBB).
    void rt_canvas_clear(void *canvas, int64_t color);

    /// @brief Draw a line from (x1,y1) to (x2,y2).
    /// @param canvas Canvas handle.
    /// @param x1 Starting X coordinate.
    /// @param y1 Starting Y coordinate.
    /// @param x2 Ending X coordinate.
    /// @param y2 Ending Y coordinate.
    /// @param color Line color (0x00RRGGBB).
    void rt_canvas_line(
        void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color);

    /// @brief Draw a filled rectangle.
    /// @param canvas Canvas handle.
    /// @param x Left edge X coordinate.
    /// @param y Top edge Y coordinate.
    /// @param w Rectangle width.
    /// @param h Rectangle height.
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_box(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

    /// @brief Draw a rectangle outline.
    /// @param canvas Canvas handle.
    /// @param x Left edge X coordinate.
    /// @param y Top edge Y coordinate.
    /// @param w Rectangle width.
    /// @param h Rectangle height.
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_frame(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

    /// @brief Draw a filled circle.
    /// @param canvas Canvas handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param radius Circle radius in pixels.
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_disc(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color);

    /// @brief Draw a circle outline.
    /// @param canvas Canvas handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param radius Circle radius in pixels.
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_ring(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color);

    /// @brief Set a single pixel to a color.
    /// @param canvas Canvas handle.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param color Pixel color (0x00RRGGBB).
    void rt_canvas_plot(void *canvas, int64_t x, int64_t y, int64_t color);

    /// @brief Poll the next event from the canvas.
    /// @param canvas Canvas handle.
    /// @return Event type code (0 = no event).
    int64_t rt_canvas_poll(void *canvas);

    /// @brief Check if a key is currently held down.
    /// @param canvas Canvas handle.
    /// @param key Key code to check.
    /// @return 1 if pressed, 0 otherwise.
    int64_t rt_canvas_key_held(void *canvas, int64_t key);

    //=========================================================================
    // Color Functions
    //=========================================================================

    /// @brief Construct a color from RGB components.
    /// @param r Red component (0-255).
    /// @param g Green component (0-255).
    /// @param b Blue component (0-255).
    /// @return Packed color value (0x00RRGGBB).
    int64_t rt_color_rgb(int64_t r, int64_t g, int64_t b);

    /// @brief Construct a color from RGBA components.
    /// @param r Red component (0-255).
    /// @param g Green component (0-255).
    /// @param b Blue component (0-255).
    /// @param a Alpha component (0-255).
    /// @return Packed color value (0xAARRGGBB).
    int64_t rt_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a);

#ifdef __cplusplus
}
#endif
