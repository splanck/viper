//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_pixels.h
// Purpose: Software image buffer manipulation for Viper.Graphics.Pixels, providing pixel-level
// read/write, drawing primitives, image loading/saving, and blitting operations.
//
// Key invariants:
//   - Pixel format is 0xRRGGBBAA (big-endian RGBA); drawing helpers use 0x00RRGGBB.
//   - Coordinates are 0-based from the top-left corner.
//   - Out-of-bounds reads return 0; out-of-bounds writes and drawing spans are clipped/no-op.
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

#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

#define RT_PIXELS_CLASS_ID INT64_C(-0x600201)

#ifdef __cplusplus
extern "C" {
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

/// @brief Get a pixel color at (x, y) as raw packed RGBA.
/// @param pixels Pixels object.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @return Pixel color as packed RGBA (0xRRGGBBAA), or 0 if out of bounds.
int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);

/// @brief Get a pixel color at (x, y) as raw packed RGBA. Explicit alias for Pixels.GetRGBA.
int64_t rt_pixels_get_rgba(void *pixels, int64_t x, int64_t y);

/// @brief Get a pixel color at (x, y) as a Viper.Graphics.Color-compatible value.
int64_t rt_pixels_get_color(void *pixels, int64_t x, int64_t y);

/// @brief Set a raw RGBA pixel at (x, y).
/// @param pixels Pixels object.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param color Pixel color as packed RGBA (0xRRGGBBAA).
void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color);

/// @brief Set a raw RGBA pixel at (x, y). Explicit alias for Pixels.SetRGBA.
void rt_pixels_set_rgba(void *pixels, int64_t x, int64_t y, int64_t rgba);

/// @brief Set a pixel from a Canvas RGB or Color.RGBA value, converting to raw RGBA.
void rt_pixels_set_color(void *pixels, int64_t x, int64_t y, int64_t color);

/// @brief Get direct read-only access to the underlying RGBA pixel buffer.
/// @param pixels Pixels object.
/// @return Pointer to width*height uint32_t values (0xRRGGBBAA), or NULL.
/// @warning The buffer length is width*height — query rt_pixels_width()/height()
///   for bounds; no length is returned here. The pointer is only valid until the
///   next operation that resizes or reallocates this Pixels object (scale, resize,
///   transform) or a GC cycle that could move it. Do NOT cache it across such calls
///   or across frames; re-fetch it (and the dimensions) each time you need it.
const uint32_t *rt_pixels_raw_buffer(void *pixels);

/// @brief Return the mutation generation for cache invalidation.
uint64_t rt_pixels_generation(void *pixels);

/// @brief Fill entire buffer with a raw RGBA color.
/// @param pixels Pixels object.
/// @param color Fill color as packed RGBA (0xRRGGBBAA).
void rt_pixels_fill(void *pixels, int64_t color);

/// @brief Fill entire buffer with a raw RGBA color. Explicit alias for Pixels.Fill.
void rt_pixels_fill_rgba(void *pixels, int64_t rgba);

/// @brief Fill entire buffer from a Canvas RGB or Color.RGBA value, converting to raw RGBA.
void rt_pixels_fill_color(void *pixels, int64_t color);

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
/// @note Supports all PNG color types and bit depths.
void *rt_pixels_load_png(void *path);

/// @brief Decode a PNG memory buffer into malloc-owned raw RGBA32 pixels.
/// @details Internal worker-safe helper. The returned buffer stores 0xRRGGBBAA
///          pixels in row-major order; caller frees it with free().
/// @return 1 on success, 0 on failure.
int rt_png_decode_buffer_rgba32(const uint8_t *data,
                                size_t len,
                                uint32_t **out_pixels,
                                int64_t *out_width,
                                int64_t *out_height);

/// @brief Load a JPEG image from a file path.
/// @param path File path (runtime string).
/// @return New Pixels object, or NULL on failure.
/// @note Supports baseline DCT JPEG: 8-bit, YCbCr/grayscale, 4:4:4/4:2:0/4:2:2.
void *rt_pixels_load_jpeg(void *path);

/// @brief Decode a JPEG image from a memory buffer.
/// @param data Pointer to JPEG data (must start with 0xFFD8 SOI marker).
/// @param len Length of data in bytes.
/// @return New Pixels object, or NULL on failure. Does NOT take ownership of data.
void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);

/// @brief Decode a JPEG memory buffer into malloc-owned raw RGBA32 pixels.
/// @details Internal worker-safe helper. The returned buffer stores 0xRRGGBBAA
///          pixels in row-major order; caller frees it with free().
/// @return 1 on success, 0 on failure.
int rt_jpeg_decode_buffer_rgba32(const uint8_t *data,
                                 size_t len,
                                 uint32_t **out_pixels,
                                 int64_t *out_width,
                                 int64_t *out_height);

/// @brief Load a GIF image from a file path (first frame only for static use).
/// @param path File path (runtime string).
/// @return New Pixels object (first frame), or NULL on failure.
/// @note For animated GIFs, use Sprite.FromFile() which loads all frames.
void *rt_pixels_load_gif(void *path);

/// @brief Load an image from a file path, auto-detecting format from magic bytes.
/// @details Supports PNG, JPEG, BMP, and GIF. Returns first frame for animated GIFs.
/// @param path File path (runtime string).
/// @return New Pixels object, or NULL on failure.
void *rt_pixels_load(void *path);

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
/// @param radius Blur radius (0 returns an exact copy; positive values are clamped to 10).
/// @return New blurred Pixels object.
void *rt_pixels_blur(void *pixels, int64_t radius);

/// @brief Scale the image using bilinear interpolation.
/// @param pixels Pixels object.
/// @param new_width Target width.
/// @param new_height Target height.
/// @return New scaled Pixels object.
void *rt_pixels_resize(void *pixels, int64_t new_width, int64_t new_height);

//=========================================================================
// Drawing Primitives
//=========================================================================
// Drawing primitives accept Canvas 0x00RRGGBB, Color.RGB(), and tagged
// Color.RGBA() values. RGB-only inputs draw with alpha 255; tagged RGBA
// values preserve alpha. Coordinates outside the buffer are silently clipped.

/// @brief Set a pixel using 0x00RRGGBB color format (alpha = 255).
void rt_pixels_set_rgb(void *pixels, int64_t x, int64_t y, int64_t color);

/// @brief Get a pixel as 0x00RRGGBB (alpha channel discarded).
int64_t rt_pixels_get_rgb(void *pixels, int64_t x, int64_t y);

/// @brief Draw a line between two points (Bresenham algorithm).
void rt_pixels_draw_line(
    void *pixels, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color);

/// @brief Draw a filled rectangle.
void rt_pixels_draw_box(void *pixels, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

/// @brief Draw a rectangle outline.
void rt_pixels_draw_frame(void *pixels, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color);

/// @brief Draw a filled circle.
void rt_pixels_draw_disc(void *pixels, int64_t cx, int64_t cy, int64_t r, int64_t color);

/// @brief Draw a circle outline.
void rt_pixels_draw_ring(void *pixels, int64_t cx, int64_t cy, int64_t r, int64_t color);

/// @brief Draw a filled ellipse.
void rt_pixels_draw_ellipse(
    void *pixels, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color);

/// @brief Draw an ellipse outline.
void rt_pixels_draw_ellipse_frame(
    void *pixels, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color);

/// @brief Flood fill from a seed point (iterative scanline, any canvas size).
void rt_pixels_flood_fill(void *pixels, int64_t x, int64_t y, int64_t color);

/// @brief Draw a thick line (pen-radius approach).
/// @param thickness Stroke width in pixels (pen diameter).
void rt_pixels_draw_thick_line(
    void *pixels, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t thickness, int64_t color);

/// @brief Draw a filled triangle (scanline fill).
void rt_pixels_draw_triangle(void *pixels,
                             int64_t x1,
                             int64_t y1,
                             int64_t x2,
                             int64_t y2,
                             int64_t x3,
                             int64_t y3,
                             int64_t color);

/// @brief Draw a quadratic Bézier curve.
/// @param x1,y1 Start point.
/// @param cx,cy Control point.
/// @param x2,y2 End point.
void rt_pixels_draw_bezier(void *pixels,
                           int64_t x1,
                           int64_t y1,
                           int64_t cx,
                           int64_t cy,
                           int64_t x2,
                           int64_t y2,
                           int64_t color);

/// @brief Draw built-in 8x8 bitmap-font text at (x, y).
void rt_pixels_draw_text(void *pixels, int64_t x, int64_t y, rt_string text, int64_t color);

/// @brief Draw built-in 8x8 bitmap-font text with a filled background cell per glyph pixel.
void rt_pixels_draw_text_bg(
    void *pixels, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg);

/// @brief Return rendered text width in pixels at 1x scale.
int64_t rt_pixels_text_width(rt_string text);

/// @brief Return built-in font line height in pixels.
int64_t rt_pixels_text_height(void);

/// @brief Draw built-in text scaled by an integer factor.
void rt_pixels_draw_text_scaled(
    void *pixels, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color);

/// @brief Draw scaled built-in text with a filled background cell per glyph pixel.
void rt_pixels_draw_text_scaled_bg(
    void *pixels, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t fg, int64_t bg);

/// @brief Return rendered text width in pixels at the given integer scale.
int64_t rt_pixels_text_scaled_width(rt_string text, int64_t scale);

/// @brief Draw text horizontally centered in the Pixels buffer at row y.
void rt_pixels_draw_text_centered(void *pixels, int64_t y, rt_string text, int64_t color);

/// @brief Draw text right-aligned to the Pixels buffer with the given margin.
void rt_pixels_draw_text_right(
    void *pixels, int64_t margin, int64_t y, rt_string text, int64_t color);

/// @brief Draw scaled text horizontally centered in the Pixels buffer at row y.
void rt_pixels_draw_text_centered_scaled(
    void *pixels, int64_t y, rt_string text, int64_t color, int64_t scale);

/// @brief Alpha-composite a color onto a pixel (Porter-Duff over).
/// @param pixels Pixels object.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param color Source color in 0x00RRGGBB format (Canvas-compatible).
/// @param alpha Source alpha 0–255 (0 = transparent, 255 = fully opaque).
/// @note Coordinates outside the buffer are silently clipped.
void rt_pixels_blend_pixel(void *pixels, int64_t x, int64_t y, int64_t color, int64_t alpha);

#ifdef __cplusplus
}
#endif
