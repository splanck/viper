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
    void rt_canvas_frame(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

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

    /// @brief Draw text at the specified position.
    /// @param canvas Canvas handle.
    /// @param x X coordinate (left edge).
    /// @param y Y coordinate (top edge).
    /// @param text Text string to draw.
    /// @param color Text color (0x00RRGGBB).
    void rt_canvas_text(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color);

    /// @brief Draw text with foreground and background colors.
    /// @param canvas Canvas handle.
    /// @param x X coordinate (left edge).
    /// @param y Y coordinate (top edge).
    /// @param text Text string to draw.
    /// @param fg Foreground (text) color (0x00RRGGBB).
    /// @param bg Background color (0x00RRGGBB).
    void rt_canvas_text_bg(
        void *canvas, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg);

    /// @brief Get the width of rendered text in pixels.
    /// @param text Text string to measure.
    /// @return Width in pixels (8 pixels per character).
    int64_t rt_canvas_text_width(rt_string text);

    /// @brief Get the height of rendered text in pixels.
    /// @return Height in pixels (always 8 for the built-in font).
    int64_t rt_canvas_text_height(void);

    /// @brief Blit a Pixels buffer to the canvas.
    /// @param canvas Canvas handle.
    /// @param x Destination X coordinate.
    /// @param y Destination Y coordinate.
    /// @param pixels Source Pixels object.
    void rt_canvas_blit(void *canvas, int64_t x, int64_t y, void *pixels);

    /// @brief Blit a region of a Pixels buffer to the canvas.
    /// @param canvas Canvas handle.
    /// @param dx Destination X coordinate.
    /// @param dy Destination Y coordinate.
    /// @param pixels Source Pixels object.
    /// @param sx Source X coordinate.
    /// @param sy Source Y coordinate.
    /// @param w Width of region to blit.
    /// @param h Height of region to blit.
    void rt_canvas_blit_region(void *canvas,
                               int64_t dx,
                               int64_t dy,
                               void *pixels,
                               int64_t sx,
                               int64_t sy,
                               int64_t w,
                               int64_t h);

    /// @brief Blit a Pixels buffer with alpha blending.
    /// @param canvas Canvas handle.
    /// @param x Destination X coordinate.
    /// @param y Destination Y coordinate.
    /// @param pixels Source Pixels object (RGBA format).
    void rt_canvas_blit_alpha(void *canvas, int64_t x, int64_t y, void *pixels);

    //=========================================================================
    // Extended Drawing Primitives
    //=========================================================================

    /// @brief Draw a thick line from (x1,y1) to (x2,y2).
    /// @param canvas Canvas handle.
    /// @param x1 Starting X coordinate.
    /// @param y1 Starting Y coordinate.
    /// @param x2 Ending X coordinate.
    /// @param y2 Ending Y coordinate.
    /// @param thickness Line thickness in pixels.
    /// @param color Line color (0x00RRGGBB).
    void rt_canvas_thick_line(void *canvas,
                              int64_t x1,
                              int64_t y1,
                              int64_t x2,
                              int64_t y2,
                              int64_t thickness,
                              int64_t color);

    /// @brief Draw a filled rounded rectangle.
    /// @param canvas Canvas handle.
    /// @param x Left edge X coordinate.
    /// @param y Top edge Y coordinate.
    /// @param w Rectangle width.
    /// @param h Rectangle height.
    /// @param radius Corner radius in pixels.
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_round_box(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color);

    /// @brief Draw a rounded rectangle outline.
    /// @param canvas Canvas handle.
    /// @param x Left edge X coordinate.
    /// @param y Top edge Y coordinate.
    /// @param w Rectangle width.
    /// @param h Rectangle height.
    /// @param radius Corner radius in pixels.
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_round_frame(
        void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color);

    /// @brief Flood fill an area starting from (x,y).
    /// @param canvas Canvas handle.
    /// @param x Starting X coordinate.
    /// @param y Starting Y coordinate.
    /// @param color Fill color (0x00RRGGBB).
    /// @note Fills connected pixels of the same color as the starting pixel.
    void rt_canvas_flood_fill(void *canvas, int64_t x, int64_t y, int64_t color);

    /// @brief Draw a filled triangle.
    /// @param canvas Canvas handle.
    /// @param x1 First vertex X.
    /// @param y1 First vertex Y.
    /// @param x2 Second vertex X.
    /// @param y2 Second vertex Y.
    /// @param x3 Third vertex X.
    /// @param y3 Third vertex Y.
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_triangle(void *canvas,
                            int64_t x1,
                            int64_t y1,
                            int64_t x2,
                            int64_t y2,
                            int64_t x3,
                            int64_t y3,
                            int64_t color);

    /// @brief Draw a triangle outline.
    /// @param canvas Canvas handle.
    /// @param x1 First vertex X.
    /// @param y1 First vertex Y.
    /// @param x2 Second vertex X.
    /// @param y2 Second vertex Y.
    /// @param x3 Third vertex X.
    /// @param y3 Third vertex Y.
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_triangle_frame(void *canvas,
                                  int64_t x1,
                                  int64_t y1,
                                  int64_t x2,
                                  int64_t y2,
                                  int64_t x3,
                                  int64_t y3,
                                  int64_t color);

    /// @brief Draw a filled ellipse.
    /// @param canvas Canvas handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param rx Horizontal radius.
    /// @param ry Vertical radius.
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_ellipse(
        void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color);

    /// @brief Draw an ellipse outline.
    /// @param canvas Canvas handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param rx Horizontal radius.
    /// @param ry Vertical radius.
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_ellipse_frame(
        void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color);

    //=========================================================================
    // Phase 4: Advanced Curves & Shapes
    //=========================================================================

    /// @brief Draw a filled arc (pie slice).
    /// @param canvas Canvas handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param radius Arc radius.
    /// @param start_angle Starting angle in degrees (0 = right, 90 = up).
    /// @param end_angle Ending angle in degrees.
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_arc(void *canvas,
                       int64_t cx,
                       int64_t cy,
                       int64_t radius,
                       int64_t start_angle,
                       int64_t end_angle,
                       int64_t color);

    /// @brief Draw an arc outline.
    /// @param canvas Canvas handle.
    /// @param cx Center X coordinate.
    /// @param cy Center Y coordinate.
    /// @param radius Arc radius.
    /// @param start_angle Starting angle in degrees (0 = right, 90 = up).
    /// @param end_angle Ending angle in degrees.
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_arc_frame(void *canvas,
                             int64_t cx,
                             int64_t cy,
                             int64_t radius,
                             int64_t start_angle,
                             int64_t end_angle,
                             int64_t color);

    /// @brief Draw a quadratic Bezier curve.
    /// @param canvas Canvas handle.
    /// @param x1 Start point X.
    /// @param y1 Start point Y.
    /// @param cx Control point X.
    /// @param cy Control point Y.
    /// @param x2 End point X.
    /// @param y2 End point Y.
    /// @param color Line color (0x00RRGGBB).
    void rt_canvas_bezier(void *canvas,
                          int64_t x1,
                          int64_t y1,
                          int64_t cx,
                          int64_t cy,
                          int64_t x2,
                          int64_t y2,
                          int64_t color);

    /// @brief Draw connected line segments (polyline).
    /// @param canvas Canvas handle.
    /// @param points Array of point coordinates [x1, y1, x2, y2, ...].
    /// @param count Number of points (pairs of coordinates).
    /// @param color Line color (0x00RRGGBB).
    void rt_canvas_polyline(void *canvas, void *points, int64_t count, int64_t color);

    /// @brief Draw a filled polygon.
    /// @param canvas Canvas handle.
    /// @param points Array of point coordinates [x1, y1, x2, y2, ...].
    /// @param count Number of points (pairs of coordinates).
    /// @param color Fill color (0x00RRGGBB).
    void rt_canvas_polygon(void *canvas, void *points, int64_t count, int64_t color);

    /// @brief Draw a polygon outline.
    /// @param canvas Canvas handle.
    /// @param points Array of point coordinates [x1, y1, x2, y2, ...].
    /// @param count Number of points (pairs of coordinates).
    /// @param color Outline color (0x00RRGGBB).
    void rt_canvas_polygon_frame(void *canvas, void *points, int64_t count, int64_t color);

    //=========================================================================
    // Phase 5: Canvas Utilities
    //=========================================================================

    /// @brief Get a pixel color from the canvas.
    /// @param canvas Canvas handle.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @return Pixel color (0x00RRGGBB), or 0 if out of bounds.
    int64_t rt_canvas_get_pixel(void *canvas, int64_t x, int64_t y);

    /// @brief Copy a rectangular region from canvas to a Pixels buffer.
    /// @param canvas Canvas handle.
    /// @param x Source X coordinate.
    /// @param y Source Y coordinate.
    /// @param w Width of region.
    /// @param h Height of region.
    /// @return New Pixels object containing the copied region, or NULL on failure.
    void *rt_canvas_copy_rect(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h);

    /// @brief Save the canvas contents to a BMP file.
    /// @param canvas Canvas handle.
    /// @param path File path to save to.
    /// @return 1 on success, 0 on failure.
    int64_t rt_canvas_save_bmp(void *canvas, rt_string path);

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
