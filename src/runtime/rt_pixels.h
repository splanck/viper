//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_pixels.h
// Purpose: Software image buffer manipulation for Viper.Graphics.Pixels.
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
    void rt_pixels_copy(void *dst, int64_t dx, int64_t dy,
                        void *src, int64_t sx, int64_t sy,
                        int64_t w, int64_t h);

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

#ifdef __cplusplus
}
#endif
