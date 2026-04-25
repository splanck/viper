//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_pixels.c
// Purpose: Core operations for Viper.Graphics.Pixels — the CPU-side software
//   image buffer. Contains allocation, pixel access, fill/clear, copy/clone,
//   and byte conversion. Image I/O lives in rt_pixels_io.c, transforms in
//   rt_pixels_transform.c, and drawing primitives in rt_pixels_draw.c.
//
// Key invariants:
//   - Internal pixel format is 32-bit RGBA: R in bits [31:24], G in [23:16],
//     B in [15:8], A in [7:0] — i.e. 0xRRGGBBAA stored as uint32_t in
//     row-major order (row y starts at data + y * width).
//   - Canvas drawing methods (Box, Disc, etc.) take colors as 0x00RRGGBB
//     (alpha implicitly 0xFF). Pixels Set/Get use 0xRRGGBBAA. Helper
//     rgb_to_rgba(color) = (color << 8) | 0xFF converts between the two.
//   - Blit operations write pre-multiplied alpha or simple alpha-blend
//     depending on the function (BlitAlpha blends; Blit overwrites).
//   - Drawing primitives use integer coordinates. Sub-pixel rendering is not
//     supported; callers should pre-scale to the physical (HiDPI) resolution.
//   - Flood fill uses an iterative scanline algorithm with a malloc'd stack to
//     avoid recursion depth limits on large images.
//
// Ownership/Lifetime:
//   - Pixels objects are GC-managed (rt_obj_new_i64). The pixel data array is
//     malloc'd separately and freed by the GC finalizer (pixels_finalizer).
//
// Links: src/runtime/graphics/rt_pixels_internal.h (shared struct definition),
//        src/runtime/graphics/rt_pixels_io.c (BMP/PNG/JPEG I/O),
//        src/runtime/graphics/rt_pixels_transform.c (geometric and effect ops),
//        src/runtime/graphics/rt_pixels_draw.c (drawing primitives),
//        src/runtime/graphics/rt_pixels.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_pixels.h"
#include "rt_pixels_internal.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static volatile int64_t g_next_pixels_cache_identity = 1;

static int32_t pixels_checked_layout(int64_t width,
                                     int64_t height,
                                     int64_t *pixel_count_out,
                                     size_t *data_size_out,
                                     size_t *total_size_out) {
    if (width < 0 || height < 0)
        return 0;

    int64_t pixel_count = 0;
    if (width != 0 && height != 0) {
        if (width > INT64_MAX / height)
            return 0;
        pixel_count = width * height;
    }

    if (pixel_count > (INT64_MAX - (int64_t)sizeof(rt_pixels_impl)) / (int64_t)sizeof(uint32_t))
        return 0;

    size_t data_size = (size_t)pixel_count * sizeof(uint32_t);
    size_t total = sizeof(rt_pixels_impl) + data_size;
    if (total < sizeof(rt_pixels_impl) || total > (size_t)INT64_MAX)
        return 0;

    if (pixel_count_out)
        *pixel_count_out = pixel_count;
    if (data_size_out)
        *data_size_out = data_size;
    if (total_size_out)
        *total_size_out = total;
    return 1;
}

static int32_t pixels_checked_raw_bytes(int64_t width, int64_t height, int64_t *byte_count_out) {
    int64_t pixel_count = 0;
    if (!pixels_checked_layout(width, height, &pixel_count, NULL, NULL))
        return 0;
    if (pixel_count > INT64_MAX / 4)
        return 0;
    if (byte_count_out)
        *byte_count_out = pixel_count * 4;
    return 1;
}

/// @brief Allocate a new Pixels object with embedded pixel data.
rt_pixels_impl *pixels_alloc(int64_t width, int64_t height) {
    if (width < 0 || height < 0)
        return NULL;

    int64_t pixel_count = 0;
    size_t data_size = 0;
    size_t total = 0;
    if (!pixels_checked_layout(width, height, &pixel_count, &data_size, &total)) {
        rt_trap("Pixels: dimensions too large");
        return NULL;
    }

    rt_pixels_impl *pixels = (rt_pixels_impl *)rt_obj_new_i64(RT_PIXELS_CLASS_ID, (int64_t)total);
    if (!pixels) {
        rt_trap("Pixels: memory allocation failed");
        return NULL;
    }

    pixels->width = width;
    pixels->height = height;
    pixels->data =
        pixel_count > 0 ? (uint32_t *)((uint8_t *)pixels + sizeof(rt_pixels_impl)) : NULL;
    pixels->generation = 0;
    pixels->cache_identity =
        (uint64_t)__atomic_fetch_add(&g_next_pixels_cache_identity, (int64_t)1, __ATOMIC_RELAXED);
    if (pixels->cache_identity == 0)
        pixels->cache_identity = (uint64_t)__atomic_fetch_add(
            &g_next_pixels_cache_identity, (int64_t)1, __ATOMIC_RELAXED);

    // Zero-fill (transparent black)
    if (pixels->data && data_size > 0)
        memset(pixels->data, 0, data_size);

    return pixels;
}

//=============================================================================
// Constructors
//=============================================================================

/// @brief Construct a Pixels buffer of `width × height` (zero-initialized, transparent).
/// Each pixel is a 32-bit RGBA value (0xRRGGBBAA).
void *rt_pixels_new(int64_t width, int64_t height) {
    return pixels_alloc(width, height);
}

//=============================================================================
// Property Accessors
//=============================================================================

/// @brief Pixel buffer width. Traps on null.
int64_t rt_pixels_width(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Width: null pixels");
        return 0;
    }
    return ((rt_pixels_impl *)pixels)->width;
}

/// @brief Pixel buffer height. Traps on null.
int64_t rt_pixels_height(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Height: null pixels");
        return 0;
    }
    return ((rt_pixels_impl *)pixels)->height;
}

uint64_t rt_pixels_generation(void *pixels) {
    if (!pixels)
        return 0;
    return ((rt_pixels_impl *)pixels)->generation;
}

//=============================================================================
// Pixel Access
//=============================================================================

/// @brief Read the pixel at (x, y) as 0xRRGGBBAA. Out-of-bounds returns 0 (transparent).
int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y) {
    if (!pixels) {
        rt_trap("Pixels.Get: null pixels");
        return 0;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // Bounds check - return 0 for out of bounds
    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return 0;

    int64_t idx = y * p->width + x;
    return (int64_t)p->data[idx];
}

/// @brief Write `color` (0xRRGGBBAA) at (x, y). Out-of-bounds is a silent no-op.
void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.Set: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // Bounds check - silently ignore out of bounds
    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return;

    int64_t idx = y * p->width + x;
    p->data[idx] = (uint32_t)color;
    pixels_touch(p);
}

/// @brief Internal: borrow the raw uint32_t pixel array (row-major). NULL-safe. Use sparingly —
/// caller must respect width × height bounds and not free the pointer.
const uint32_t *rt_pixels_raw_buffer(void *pixels) {
    if (!pixels)
        return NULL;
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    return p->data;
}

//=============================================================================
// Fill Operations
//=============================================================================

/// @brief Fill the entire buffer with `color`. Optimized for color=0 (memset).
void rt_pixels_fill(void *pixels, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.Fill: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    uint32_t c = (uint32_t)color;
    int64_t count = p->width * p->height;
    if (count <= 0 || !p->data)
        return;
    if (c == 0) {
        memset(p->data, 0, (size_t)count * sizeof(uint32_t));
        pixels_touch(p);
        return;
    }
    for (int64_t i = 0; i < count; i++)
        p->data[i] = c;
    pixels_touch(p);
}

/// @brief Reset every pixel to 0 (transparent black). Equivalent to `_fill(pixels, 0)`.
void rt_pixels_clear(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Clear: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    size_t size = (size_t)(p->width * p->height) * sizeof(uint32_t);
    if (p->data && size > 0) {
        memset(p->data, 0, size);
        pixels_touch(p);
    }
}

//=============================================================================
// Copy Operations
//=============================================================================

/// @brief Blit the `w × h` source rectangle (`src` at sx, sy) into `dst` at (dx, dy). Auto-clips
/// to both source and dest bounds; out-of-range pixels are skipped silently.
void rt_pixels_copy(
    void *dst, int64_t dx, int64_t dy, void *src, int64_t sx, int64_t sy, int64_t w, int64_t h) {
    if (!dst || !src) {
        rt_trap("Pixels.Copy: null pixels");
        return;
    }

    rt_pixels_impl *d = (rt_pixels_impl *)dst;
    rt_pixels_impl *s = (rt_pixels_impl *)src;

    if (!rt_pixels_clip_copy_axis(d->width, s->width, &dx, &sx, &w) ||
        !rt_pixels_clip_copy_axis(d->height, s->height, &dy, &sy, &h))
        return;

    int same_buffer = (d == s);
    int overlap = same_buffer && !(dx + w <= sx || sx + w <= dx || dy + h <= sy || sy + h <= dy);
    int copy_backwards = overlap && dy > sy;

    // Copy row by row. memmove is required for overlapping self-copies.
    for (int64_t row_idx = 0; row_idx < h; row_idx++) {
        int64_t row = copy_backwards ? (h - 1 - row_idx) : row_idx;
        int64_t src_idx = (sy + row) * s->width + sx;
        int64_t dst_idx = (dy + row) * d->width + dx;
        if (overlap)
            memmove(&d->data[dst_idx], &s->data[src_idx], (size_t)w * sizeof(uint32_t));
        else
            memcpy(&d->data[dst_idx], &s->data[src_idx], (size_t)w * sizeof(uint32_t));
    }
    pixels_touch(d);
}

/// @brief Return a deep copy of the buffer (independent storage). Useful before applying
/// destructive transforms or sharing a snapshot across threads.
void *rt_pixels_clone(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Clone: null pixels");
        return NULL;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    rt_pixels_impl *clone = pixels_alloc(p->width, p->height);
    if (!clone)
        return NULL;
    if (p->data && clone->data) {
        size_t size = (size_t)(p->width * p->height) * sizeof(uint32_t);
        memcpy(clone->data, p->data, size);
    }
    return clone;
}

//=============================================================================
// Byte Conversion
//=============================================================================

// Forward declarations for Bytes internal structure access
typedef struct rt_bytes_impl {
    int64_t len;
    uint8_t *data;
} rt_bytes_impl;

/// @brief Serialize the buffer to a fresh Bytes blob (raw 4 bytes/pixel, row-major). Useful for
/// hashing, persistence, or transmission. Inverse: `_from_bytes`.
void *rt_pixels_to_bytes(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.ToBytes: null pixels");
        return NULL;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    int64_t byte_count = 0;
    if (!pixels_checked_raw_bytes(p->width, p->height, &byte_count)) {
        rt_trap("Pixels.ToBytes: dimensions too large");
        return NULL;
    }
    void *bytes = rt_bytes_new(byte_count);
    if (!bytes)
        return NULL;

    if (byte_count > 0 && p->data) {
        rt_bytes_impl *b = (rt_bytes_impl *)bytes;
        memcpy(b->data, p->data, (size_t)byte_count);
    }

    return bytes;
}

/// @brief Deserialize a `width × height` pixel buffer from a Bytes blob (raw RGBA payload).
/// Bytes length must be at least `width * height * 4`; surplus bytes are ignored.
void *rt_pixels_from_bytes(int64_t width, int64_t height, void *bytes) {
    if (!bytes) {
        rt_trap("Pixels.FromBytes: null bytes");
        return NULL;
    }

    if (width < 0 || height < 0)
        return NULL;

    int64_t required_bytes = 0;
    if (!pixels_checked_raw_bytes(width, height, &required_bytes)) {
        rt_trap("Pixels.FromBytes: dimensions too large");
        return NULL;
    }
    int64_t available_bytes = rt_bytes_len(bytes);

    if (available_bytes < required_bytes) {
        rt_trap("Pixels.FromBytes: insufficient bytes");
        return NULL;
    }

    rt_pixels_impl *p = pixels_alloc(width, height);
    if (!p)
        return NULL;

    if (required_bytes > 0 && p->data) {
        rt_bytes_impl *b = (rt_bytes_impl *)bytes;
        memcpy(p->data, b->data, (size_t)required_bytes);
        pixels_touch(p);
    }

    return p;
}
