//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_pixels.c
// Purpose: Implement software image buffer for Viper.Graphics.Pixels.
// Structure: width, height, uint32_t* data (RGBA, row-major)
//
//===----------------------------------------------------------------------===//

#include "rt_pixels.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// @brief Pixels implementation structure.
typedef struct rt_pixels_impl
{
    int64_t width;  ///< Width in pixels.
    int64_t height; ///< Height in pixels.
    uint32_t *data; ///< Pixel storage (RGBA, row-major).
} rt_pixels_impl;

/// @brief Allocate a new Pixels object.
static rt_pixels_impl *pixels_alloc(int64_t width, int64_t height)
{
    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;

    int64_t pixel_count = width * height;

    // Check for overflow
    if (width > 0 && height > 0 && pixel_count / width != height)
        rt_trap("Pixels: dimensions too large");

    size_t data_size = (size_t)pixel_count * sizeof(uint32_t);
    size_t total = sizeof(rt_pixels_impl) + data_size;

    if (total < sizeof(rt_pixels_impl) || total > (size_t)INT64_MAX)
        rt_trap("Pixels: memory allocation failed");

    rt_pixels_impl *pixels = (rt_pixels_impl *)rt_obj_new_i64(0, (int64_t)total);
    if (!pixels)
        rt_trap("Pixels: memory allocation failed");

    pixels->width = width;
    pixels->height = height;
    pixels->data =
        pixel_count > 0 ? (uint32_t *)((uint8_t *)pixels + sizeof(rt_pixels_impl)) : NULL;

    // Zero-fill (transparent black)
    if (pixels->data && data_size > 0)
        memset(pixels->data, 0, data_size);

    return pixels;
}

//=============================================================================
// Constructors
//=============================================================================

void *rt_pixels_new(int64_t width, int64_t height)
{
    return pixels_alloc(width, height);
}

//=============================================================================
// Property Accessors
//=============================================================================

int64_t rt_pixels_width(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Width: null pixels");
        return 0;
    }
    return ((rt_pixels_impl *)pixels)->width;
}

int64_t rt_pixels_height(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Height: null pixels");
        return 0;
    }
    return ((rt_pixels_impl *)pixels)->height;
}

//=============================================================================
// Pixel Access
//=============================================================================

int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y)
{
    if (!pixels)
    {
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

void rt_pixels_set(void *pixels, int64_t x, int64_t y, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.Set: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // Bounds check - silently ignore out of bounds
    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return;

    int64_t idx = y * p->width + x;
    p->data[idx] = (uint32_t)color;
}

//=============================================================================
// Fill Operations
//=============================================================================

void rt_pixels_fill(void *pixels, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.Fill: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    uint32_t c = (uint32_t)color;
    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++)
        p->data[i] = c;
}

void rt_pixels_clear(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Clear: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    size_t size = (size_t)(p->width * p->height) * sizeof(uint32_t);
    if (p->data && size > 0)
        memset(p->data, 0, size);
}

//=============================================================================
// Copy Operations
//=============================================================================

void rt_pixels_copy(
    void *dst, int64_t dx, int64_t dy, void *src, int64_t sx, int64_t sy, int64_t w, int64_t h)
{
    if (!dst || !src)
    {
        rt_trap("Pixels.Copy: null pixels");
        return;
    }

    rt_pixels_impl *d = (rt_pixels_impl *)dst;
    rt_pixels_impl *s = (rt_pixels_impl *)src;

    // Clip source rectangle to source bounds
    if (sx < 0)
    {
        w += sx;
        dx -= sx;
        sx = 0;
    }
    if (sy < 0)
    {
        h += sy;
        dy -= sy;
        sy = 0;
    }
    if (sx + w > s->width)
        w = s->width - sx;
    if (sy + h > s->height)
        h = s->height - sy;

    // Clip destination rectangle to destination bounds
    if (dx < 0)
    {
        w += dx;
        sx -= dx;
        dx = 0;
    }
    if (dy < 0)
    {
        h += dy;
        sy -= dy;
        dy = 0;
    }
    if (dx + w > d->width)
        w = d->width - dx;
    if (dy + h > d->height)
        h = d->height - dy;

    // Nothing to copy
    if (w <= 0 || h <= 0)
        return;

    // Copy row by row
    for (int64_t row = 0; row < h; row++)
    {
        int64_t src_idx = (sy + row) * s->width + sx;
        int64_t dst_idx = (dy + row) * d->width + dx;
        memcpy(&d->data[dst_idx], &s->data[src_idx], (size_t)w * sizeof(uint32_t));
    }
}

void *rt_pixels_clone(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Clone: null pixels");
        return NULL;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    rt_pixels_impl *clone = pixels_alloc(p->width, p->height);
    if (p->data && clone->data)
    {
        size_t size = (size_t)(p->width * p->height) * sizeof(uint32_t);
        memcpy(clone->data, p->data, size);
    }
    return clone;
}

//=============================================================================
// Byte Conversion
//=============================================================================

// Forward declarations for Bytes internal structure access
typedef struct rt_bytes_impl
{
    int64_t len;
    uint8_t *data;
} rt_bytes_impl;

void *rt_pixels_to_bytes(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.ToBytes: null pixels");
        return NULL;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    int64_t byte_count = p->width * p->height * 4; // 4 bytes per pixel (RGBA)
    void *bytes = rt_bytes_new(byte_count);

    if (byte_count > 0 && p->data)
    {
        rt_bytes_impl *b = (rt_bytes_impl *)bytes;
        memcpy(b->data, p->data, (size_t)byte_count);
    }

    return bytes;
}

void *rt_pixels_from_bytes(int64_t width, int64_t height, void *bytes)
{
    if (!bytes)
    {
        rt_trap("Pixels.FromBytes: null bytes");
        return NULL;
    }

    if (width < 0)
        width = 0;
    if (height < 0)
        height = 0;

    int64_t required_bytes = width * height * 4;
    int64_t available_bytes = rt_bytes_len(bytes);

    if (available_bytes < required_bytes)
    {
        rt_trap("Pixels.FromBytes: insufficient bytes");
        return NULL;
    }

    rt_pixels_impl *p = pixels_alloc(width, height);

    if (required_bytes > 0 && p->data)
    {
        rt_bytes_impl *b = (rt_bytes_impl *)bytes;
        memcpy(p->data, b->data, (size_t)required_bytes);
    }

    return p;
}
