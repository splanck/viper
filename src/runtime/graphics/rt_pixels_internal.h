//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_pixels_internal.h
// Purpose: Shared internal definitions for the Pixels (software image buffer)
//   runtime implementation. Exposes the rt_pixels_impl struct and common
//   helpers so that the core, I/O, transform, and draw modules can all access
//   pixel data directly.
//
// Key invariants:
//   - This header is INTERNAL to the runtime — never include from public APIs.
//   - Only rt_pixels*.c files should include this header.
//   - The public API header (rt_pixels.h) remains the sole interface for callers.
//   - Pixel format is 32-bit RGBA: 0xRRGGBBAA in row-major order.
//
// Ownership/Lifetime:
//   - The rt_pixels_impl struct is GC-managed via rt_obj_new_i64.
//   - Pixel data is embedded in the GC allocation (not separately malloc'd).
//
// Links: src/runtime/graphics/rt_pixels.h (public API),
//        src/runtime/graphics/rt_pixels.c (core operations),
//        src/runtime/graphics/rt_pixels_io.c (BMP/PNG/JPEG I/O),
//        src/runtime/graphics/rt_pixels_transform.c (geometric and effect ops),
//        src/runtime/graphics/rt_pixels_draw.c (drawing primitives)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_pixels.h"

#include <stdint.h>

/// @brief Pixels implementation structure.
typedef struct rt_pixels_impl {
    int64_t width;       ///< Width in pixels.
    int64_t height;      ///< Height in pixels.
    uint32_t *data;      ///< Pixel storage (RGBA, row-major).
    uint64_t generation; ///< Monotonic content version for GPU caches.
    uint64_t cache_identity; ///< Stable cache key to survive allocator address reuse.
} rt_pixels_impl;

/// @brief Convert 0x00RRGGBB canvas color to 0xRRGGBBFF (fully-opaque RGBA).
static inline uint32_t rgb_to_rgba(int64_t color) {
    return (uint32_t)((color << 8) | 0xFF);
}

/// @brief Write one pixel with bounds check (no null check — caller ensures p is valid).
static inline void set_pixel_raw(rt_pixels_impl *p, int64_t x, int64_t y, uint32_t c) {
    if (x >= 0 && x < p->width && y >= 0 && y < p->height)
        p->data[y * p->width + x] = c;
}

/// @brief Bump the image content generation after an in-place mutation.
static inline void pixels_touch(rt_pixels_impl *p) {
    if (p)
        p->generation++;
}

/// @brief Integer square root (Newton's method, exact for perfect squares).
static inline int64_t isqrt64(int64_t n) {
    if (n <= 0)
        return 0;
    int64_t x = n;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/// @brief Allocate a new Pixels object with embedded pixel data.
/// @details Defined in rt_pixels.c. Creates a GC-managed Pixels object with
///          zero-filled pixel data embedded in the allocation.
rt_pixels_impl *pixels_alloc(int64_t width, int64_t height);
