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

#include <limits.h>
#include <stdint.h>

/// @brief Pixels implementation structure.
typedef struct rt_pixels_impl {
    int64_t width;           ///< Width in pixels.
    int64_t height;          ///< Height in pixels.
    uint32_t *data;          ///< Pixel storage (RGBA, row-major).
    uint64_t generation;     ///< Monotonic content version for GPU caches.
    uint64_t cache_identity; ///< Stable cache key to survive allocator address reuse.
} rt_pixels_impl;

/// @brief Convert 0x00RRGGBB canvas color to 0xRRGGBBFF (fully-opaque RGBA).
static inline uint32_t rgb_to_rgba(int64_t color) {
    uint32_t rgb = (uint32_t)((uint64_t)color & 0x00FFFFFFu);
    return (rgb << 8) | 0xFFu;
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

/// @brief Saturating int64 addition for clipping math.
static inline int64_t rt_pixels_add_sat64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Saturating subtract by a non-negative value.
static inline int64_t rt_pixels_sub_nonneg_sat64(int64_t a, int64_t b) {
    if (b <= 0)
        return a;
    if (a < INT64_MIN + b)
        return INT64_MIN;
    return a - b;
}

/// @brief Return how many leading units a negative start skips, capped by len.
static inline int64_t rt_pixels_negative_skip(int64_t start, int64_t len) {
    if (start >= 0 || len <= 0)
        return 0;
    if (start == INT64_MIN)
        return len;
    int64_t skip = -start;
    return skip >= len ? len : skip;
}

/// @brief Clip paired source/destination spans to both non-negative bounds.
static inline int8_t rt_pixels_clip_copy_axis(
    int64_t dst_limit, int64_t src_limit, int64_t *dst, int64_t *src, int64_t *len) {
    if (!dst || !src || !len || *len <= 0 || dst_limit <= 0 || src_limit <= 0) {
        if (len)
            *len = 0;
        return 0;
    }

    int64_t skip = rt_pixels_negative_skip(*src, *len);
    if (skip >= *len) {
        *len = 0;
        return 0;
    }
    if (skip > 0) {
        if (*dst > INT64_MAX - skip) {
            *len = 0;
            return 0;
        }
        *dst += skip;
        *src = 0;
        *len -= skip;
    }

    if (*src >= src_limit) {
        *len = 0;
        return 0;
    }
    int64_t src_remaining = src_limit - *src;
    if (*len > src_remaining)
        *len = src_remaining;

    skip = rt_pixels_negative_skip(*dst, *len);
    if (skip >= *len) {
        *len = 0;
        return 0;
    }
    if (skip > 0) {
        if (*src > INT64_MAX - skip) {
            *len = 0;
            return 0;
        }
        *src += skip;
        *dst = 0;
        *len -= skip;
    }

    if (*dst >= dst_limit) {
        *len = 0;
        return 0;
    }
    int64_t dst_remaining = dst_limit - *dst;
    if (*len > dst_remaining)
        *len = dst_remaining;

    return *len > 0;
}

/// @brief Clip an in-place rectangle to a pixel buffer's bounds without endpoint overflow.
static inline int8_t rt_pixels_clip_rect_to_bounds(
    rt_pixels_impl *p, int64_t *x, int64_t *y, int64_t *w, int64_t *h) {
    if (!p || !p->data || !x || !y || !w || !h || *w <= 0 || *h <= 0 || p->width <= 0 ||
        p->height <= 0)
        return 0;

    int64_t skip = rt_pixels_negative_skip(*x, *w);
    if (skip >= *w)
        return 0;
    *x = skip > 0 ? 0 : *x;
    *w -= skip;

    skip = rt_pixels_negative_skip(*y, *h);
    if (skip >= *h)
        return 0;
    *y = skip > 0 ? 0 : *y;
    *h -= skip;

    if (*x >= p->width || *y >= p->height)
        return 0;
    int64_t x_remaining = p->width - *x;
    int64_t y_remaining = p->height - *y;
    if (*w > x_remaining)
        *w = x_remaining;
    if (*h > y_remaining)
        *h = y_remaining;
    return *w > 0 && *h > 0;
}

/// @brief Compose two straight-alpha RGBA pixels using Porter-Duff source-over.
static inline uint32_t rt_pixels_alpha_over_rgba(uint32_t dst, uint32_t src) {
    uint32_t sr = (src >> 24) & 0xFFu;
    uint32_t sg = (src >> 16) & 0xFFu;
    uint32_t sb = (src >> 8) & 0xFFu;
    uint32_t sa = src & 0xFFu;
    uint32_t dr = (dst >> 24) & 0xFFu;
    uint32_t dg = (dst >> 16) & 0xFFu;
    uint32_t db = (dst >> 8) & 0xFFu;
    uint32_t da = dst & 0xFFu;

    if (sa == 0)
        return dst;
    if (sa == 255)
        return src;

    uint32_t inv = 255u - sa;
    uint32_t oa = sa + (da * inv + 127u) / 255u;
    if (oa == 0)
        return 0;

    uint32_t r_pm = sr * sa + (dr * da * inv + 127u) / 255u;
    uint32_t g_pm = sg * sa + (dg * da * inv + 127u) / 255u;
    uint32_t b_pm = sb * sa + (db * da * inv + 127u) / 255u;
    uint32_t r = (r_pm + oa / 2u) / oa;
    uint32_t g = (g_pm + oa / 2u) / oa;
    uint32_t b = (b_pm + oa / 2u) / oa;
    if (r > 255u)
        r = 255u;
    if (g > 255u)
        g = 255u;
    if (b > 255u)
        b = 255u;
    return (r << 24) | (g << 16) | (b << 8) | oa;
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
