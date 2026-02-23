//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_pixels.h
// Purpose: Software image buffer manipulation for Viper.Graphics.Pixels, providing pixel-level read/write, drawing primitives, image loading/saving, and blitting operations.
//
// Key invariants:
//   - Pixel format is 0xRRGGBBAA (big-endian RGBA); drawing helpers use 0x00RRGGBB.
//   - Coordinates are 0-based from the top-left corner.
//   - All bounds checks trap on out-of-range pixel access.
//   - Drawing primitives (box, disc, line, etc.) render directly into the pixel buffer.
//
// Ownership/Lifetime:
//   - Pixels objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_pixels.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Pixels buffer with given dimensions.
    /// @param width Width in pixels.
    /// @param height Height in pixels.
    /// @return New Pixels object with zero-filled (transparent black) buffer.
    void *rt_pixels_new(int64_t width, int64_t height);

    /// @brief Get the width of the Pixels buffer.
    int64_t rt_pixels_width(void *pixels);

    /// @brief Get the height of the Pixels buffer.
    int64_t rt_pixels_height(void *pixels);

    /// @brief Get a pixel color at (x, y) as packed RGBA.
    /// @param pixels Pixels object.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @return Pixel color as packed RGBA (0xRRGGBBAA), or 0 if out of bounds.
    int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);

    /// @brief Set a pixel color at (x, y).
    /// @param pixels Pixels object.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param color Pixel color as packed RGBA (0xRRGGBBAA).
    void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color);

    /// @brief Fill entire buffer with a color.
    /// @param pixels Pixels object.
    /// @param color Fill color as packed RGBA.
    void rt_pixels_fill(void *pixels, int64_t color);

    /// @brief Clear buffer to transparent black (0x00000000).
    void rt_pixels_clear(void *pixels);

    /// @brief Copy a rectangle from source to destination.
    /// @param dst Destination Pixels object.
    /// @param dx Destination X coordinate.
    /// @param dy Destination Y coordinate.
    /// @param src Source Pixels object.
    /// @param sx Source X coordinate.
    /// @param sy Source Y coordinate.
    /// @param w Width of rectangle to copy.
    /// @param h Height of rectangle to copy.
    void rt_pixels_copy(
        void *dst, int64_t dx, int64_t dy, void *src, int64_t sx, int64_t sy, int64_t w, int64_t h);

    /// @brief Create a deep copy of a Pixels buffer.
    void *rt_pixels_clone(void *pixels);

    /// @brief Convert Pixels to raw bytes (RGBA, row-major).
    /// @return A Viper.Collections.Bytes object containing the pixel data.
    void *rt_pixels_to_bytes(void *pixels);

    /// @brief Create Pixels from raw bytes.
    /// @param width Width in pixels.
    /// @param height Height in pixels.
    /// @param bytes Viper.Collections.Bytes object containing RGBA data.
    /// @return New Pixels object, or NULL if bytes is insufficient.
    void *rt_pixels_from_bytes(int64_t width, int64_t height, void *bytes);

    //=========================================================================
    // BMP Image I/O
    //=========================================================================

    /// @brief Load a BMP image from file.
    /// @param path File path (runtime string).
    /// @return New Pixels object, or NULL on failure.
    /// @note Supports 24-bit uncompressed BMP files.
    void *rt_pixels_load_bmp(void *path);

    /// @brief Save a Pixels buffer to a BMP file.
    /// @param pixels Pixels object to save.
    /// @param path File path (runtime string).
    /// @return 1 on success, 0 on failure.
    int64_t rt_pixels_save_bmp(void *pixels, void *path);

    //=========================================================================
    // PNG Image I/O
    //=========================================================================

    /// @brief Load a PNG image from file.
    /// @param path File path (runtime string).
    /// @return New Pixels object, or NULL on failure.
    /// @note Supports 8-bit RGB and RGBA PNG files.
    void *rt_pixels_load_png(void *path);

    /// @brief Save a Pixels buffer to a PNG file.
    /// @param pixels Pixels object to save.
    /// @param path File path (runtime string).
    /// @return 1 on success, 0 on failure.
    int64_t rt_pixels_save_png(void *pixels, void *path);

    //=========================================================================
    // Image Transforms
    //=========================================================================

    /// @brief Flip the image horizontally (mirror left-right).
    /// @param pixels Pixels object.
    /// @return New flipped Pixels object.
    void *rt_pixels_flip_h(void *pixels);

    /// @brief Flip the image vertically (mirror top-bottom).
    /// @param pixels Pixels object.
    /// @return New flipped Pixels object.
    void *rt_pixels_flip_v(void *pixels);

    /// @brief Rotate the image 90 degrees clockwise.
    /// @param pixels Pixels object.
    /// @return New rotated Pixels object (width and height swapped).
    void *rt_pixels_rotate_cw(void *pixels);

    /// @brief Rotate the image 90 degrees counter-clockwise.
    /// @param pixels Pixels object.
    /// @return New rotated Pixels object (width and height swapped).
    void *rt_pixels_rotate_ccw(void *pixels);

    /// @brief Rotate the image 180 degrees.
    /// @param pixels Pixels object.
    /// @return New rotated Pixels object.
    void *rt_pixels_rotate_180(void *pixels);

    /// @brief Rotate the image by an arbitrary angle.
    /// @param pixels Pixels object.
    /// @param angle_degrees Rotation angle in degrees (positive = clockwise).
    /// @return New rotated Pixels object with expanded dimensions to fit.
    /// @note Uses bilinear interpolation for smooth results.
    void *rt_pixels_rotate(void *pixels, double angle_degrees);

    /// @brief Scale the image using nearest-neighbor interpolation.
    /// @param pixels Pixels object.
    /// @param new_width Target width.
    /// @param new_height Target height.
    /// @return New scaled Pixels object.
    void *rt_pixels_scale(void *pixels, int64_t new_width, int64_t new_height);

    //=========================================================================
    // Image Processing
    //=========================================================================

    /// @brief Invert all colors in the image.
    /// @param pixels Pixels object.
    /// @return New inverted Pixels object.
    void *rt_pixels_invert(void *pixels);

    /// @brief Convert image to grayscale.
    /// @param pixels Pixels object.
    /// @return New grayscale Pixels object.
    void *rt_pixels_grayscale(void *pixels);

    /// @brief Apply a color tint to the image.
    /// @param pixels Pixels object.
    /// @param color Tint color (0x00RRGGBB).
    /// @return New tinted Pixels object.
    void *rt_pixels_tint(void *pixels, int64_t color);

    /// @brief Apply a box blur to the image.
    /// @param pixels Pixels object.
    /// @param radius Blur radius (1-10).
    /// @return New blurred Pixels object.
    void *rt_pixels_blur(void *pixels, int64_t radius);

    /// @brief Scale the image using bilinear interpolation.
    /// @param pixels Pixels object.
    /// @param new_width Target width.
    /// @param new_height Target height.
    /// @return New scaled Pixels object.
    void *rt_pixels_resize(void *pixels, int64_t new_width, int64_t new_height);

    //=========================================================================
    // Drawing Primitives  (color format: 0x00RRGGBB — Canvas-compatible)
    //=========================================================================
    // All drawing primitives accept colors in the same 0x00RRGGBB format used
    // by Canvas drawing calls and Color.RGB(). Alpha is always 255 (fully
    // opaque). Coordinates outside the buffer are silently clipped.

    /// @brief Set a pixel using 0x00RRGGBB color format (alpha = 255).
    void rt_pixels_set_rgb(void *pixels, int64_t x, int64_t y, int64_t color);

    /// @brief Get a pixel as 0x00RRGGBB (alpha channel discarded).
    int64_t rt_pixels_get_rgb(void *pixels, int64_t x, int64_t y);

    /// @brief Draw a line between two points (Bresenham algorithm).
    void rt_pixels_draw_line(void *pixels,
                             int64_t x1, int64_t y1,
                             int64_t x2, int64_t y2,
                             int64_t color);

    /// @brief Draw a filled rectangle.
    void rt_pixels_draw_box(void *pixels,
                            int64_t x, int64_t y,
                            int64_t w, int64_t h,
                            int64_t color);

    /// @brief Draw a rectangle outline.
    void rt_pixels_draw_frame(void *pixels,
                              int64_t x, int64_t y,
                              int64_t w, int64_t h,
                              int64_t color);

    /// @brief Draw a filled circle.
    void rt_pixels_draw_disc(void *pixels,
                             int64_t cx, int64_t cy,
                             int64_t r, int64_t color);

    /// @brief Draw a circle outline.
    void rt_pixels_draw_ring(void *pixels,
                             int64_t cx, int64_t cy,
                             int64_t r, int64_t color);

    /// @brief Draw a filled ellipse.
    void rt_pixels_draw_ellipse(void *pixels,
                                int64_t cx, int64_t cy,
                                int64_t rx, int64_t ry,
                                int64_t color);

    /// @brief Draw an ellipse outline.
    void rt_pixels_draw_ellipse_frame(void *pixels,
                                      int64_t cx, int64_t cy,
                                      int64_t rx, int64_t ry,
                                      int64_t color);

    /// @brief Flood fill from a seed point (iterative scanline, any canvas size).
    void rt_pixels_flood_fill(void *pixels,
                              int64_t x, int64_t y,
                              int64_t color);

    /// @brief Draw a thick line (pen-radius approach).
    /// @param thickness Stroke width in pixels (pen diameter).
    void rt_pixels_draw_thick_line(void *pixels,
                                   int64_t x1, int64_t y1,
                                   int64_t x2, int64_t y2,
                                   int64_t thickness, int64_t color);

    /// @brief Draw a filled triangle (scanline fill).
    void rt_pixels_draw_triangle(void *pixels,
                                 int64_t x1, int64_t y1,
                                 int64_t x2, int64_t y2,
                                 int64_t x3, int64_t y3,
                                 int64_t color);

    /// @brief Draw a quadratic Bézier curve.
    /// @param x1,y1 Start point.
    /// @param cx,cy Control point.
    /// @param x2,y2 End point.
    void rt_pixels_draw_bezier(void *pixels,
                               int64_t x1, int64_t y1,
                               int64_t cx, int64_t cy,
                               int64_t x2, int64_t y2,
                               int64_t color);

    /// @brief Alpha-composite a color onto a pixel (Porter-Duff over).
    /// @param pixels Pixels object.
    /// @param x X coordinate.
    /// @param y Y coordinate.
    /// @param color Source color in 0x00RRGGBB format (Canvas-compatible).
    /// @param alpha Source alpha 0–255 (0 = transparent, 255 = fully opaque).
    /// @note Coordinates outside the buffer are silently clipped.
    void rt_pixels_blend_pixel(void *pixels,
                               int64_t x, int64_t y,
                               int64_t color, int64_t alpha);

#ifdef __cplusplus
}
#endif
