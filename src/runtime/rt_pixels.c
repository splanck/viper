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
#include "rt_string.h"

#include <stdio.h>
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

//=============================================================================
// BMP Image I/O
//=============================================================================

// BMP file format structures (packed)
#pragma pack(push, 1)

typedef struct bmp_file_header
{
    uint8_t magic[2];     // 'B', 'M'
    uint32_t file_size;   // Total file size
    uint16_t reserved1;   // 0
    uint16_t reserved2;   // 0
    uint32_t data_offset; // Offset to pixel data
} bmp_file_header;

typedef struct bmp_info_header
{
    uint32_t header_size;      // 40 for BITMAPINFOHEADER
    int32_t width;             // Image width
    int32_t height;            // Image height (positive = bottom-up)
    uint16_t planes;           // 1
    uint16_t bit_count;        // Bits per pixel (24 for RGB)
    uint32_t compression;      // 0 = BI_RGB (uncompressed)
    uint32_t image_size;       // Can be 0 for uncompressed
    int32_t x_pels_per_meter;  // Horizontal resolution
    int32_t y_pels_per_meter;  // Vertical resolution
    uint32_t colors_used;      // 0 = default
    uint32_t colors_important; // 0 = all
} bmp_info_header;

#pragma pack(pop)

void *rt_pixels_load_bmp(void *path)
{
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    // Read file header
    bmp_file_header file_hdr;
    if (fread(&file_hdr, sizeof(file_hdr), 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }

    // Check magic
    if (file_hdr.magic[0] != 'B' || file_hdr.magic[1] != 'M')
    {
        fclose(f);
        return NULL;
    }

    // Read info header
    bmp_info_header info_hdr;
    if (fread(&info_hdr, sizeof(info_hdr), 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }

    // Only support 24-bit uncompressed
    if (info_hdr.bit_count != 24 || info_hdr.compression != 0)
    {
        fclose(f);
        return NULL;
    }

    int32_t width = info_hdr.width;
    int32_t height = info_hdr.height;
    int bottom_up = 1;

    // Handle negative height (top-down)
    if (height < 0)
    {
        height = -height;
        bottom_up = 0;
    }

    if (width <= 0 || height <= 0)
    {
        fclose(f);
        return NULL;
    }

    // Calculate row padding (rows must be 4-byte aligned)
    int row_size = ((width * 3 + 3) / 4) * 4;

    // Allocate row buffer
    uint8_t *row_buf = (uint8_t *)malloc((size_t)row_size);
    if (!row_buf)
    {
        fclose(f);
        return NULL;
    }

    // Create pixels
    rt_pixels_impl *pixels = pixels_alloc(width, height);
    if (!pixels)
    {
        free(row_buf);
        fclose(f);
        return NULL;
    }

    // Seek to pixel data
    if (fseek(f, (long)file_hdr.data_offset, SEEK_SET) != 0)
    {
        free(row_buf);
        fclose(f);
        return NULL;
    }

    // Read pixel data
    for (int32_t y = 0; y < height; y++)
    {
        if (fread(row_buf, 1, (size_t)row_size, f) != (size_t)row_size)
        {
            free(row_buf);
            fclose(f);
            return NULL;
        }

        // Determine destination row (bottom-up reverses row order)
        int32_t dst_y = bottom_up ? (height - 1 - y) : y;
        uint32_t *dst_row = pixels->data + dst_y * width;

        // Convert BGR to RGBA
        for (int32_t x = 0; x < width; x++)
        {
            uint8_t b = row_buf[x * 3 + 0];
            uint8_t g = row_buf[x * 3 + 1];
            uint8_t r = row_buf[x * 3 + 2];
            // Pack as 0xRRGGBBAA (alpha = 255 for opaque)
            dst_row[x] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
        }
    }

    free(row_buf);
    fclose(f);
    return pixels;
}

int64_t rt_pixels_save_bmp(void *pixels, void *path)
{
    if (!pixels || !path)
        return 0;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return 0;

    if (p->width <= 0 || p->height <= 0 || p->width > INT32_MAX || p->height > INT32_MAX)
        return 0;

    int32_t width = (int32_t)p->width;
    int32_t height = (int32_t)p->height;

    // Calculate row padding
    int row_size = ((width * 3 + 3) / 4) * 4;
    int padding = row_size - width * 3;

    // Calculate file size
    uint32_t data_size = (uint32_t)row_size * (uint32_t)height;
    uint32_t file_size = 54 + data_size; // 14 + 40 + data

    FILE *f = fopen(filepath, "wb");
    if (!f)
        return 0;

    // Write file header
    bmp_file_header file_hdr = {
        .magic = {'B', 'M'},
        .file_size = file_size,
        .reserved1 = 0,
        .reserved2 = 0,
        .data_offset = 54,
    };
    if (fwrite(&file_hdr, sizeof(file_hdr), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    // Write info header
    bmp_info_header info_hdr = {
        .header_size = 40,
        .width = width,
        .height = height, // Positive = bottom-up
        .planes = 1,
        .bit_count = 24,
        .compression = 0,
        .image_size = data_size,
        .x_pels_per_meter = 2835, // ~72 DPI
        .y_pels_per_meter = 2835,
        .colors_used = 0,
        .colors_important = 0,
    };
    if (fwrite(&info_hdr, sizeof(info_hdr), 1, f) != 1)
    {
        fclose(f);
        return 0;
    }

    // Allocate row buffer
    uint8_t *row_buf = (uint8_t *)calloc(1, (size_t)row_size);
    if (!row_buf)
    {
        fclose(f);
        return 0;
    }

    // Write pixel data (bottom-up)
    for (int32_t y = height - 1; y >= 0; y--)
    {
        uint32_t *src_row = p->data + y * width;

        // Convert RGBA to BGR
        for (int32_t x = 0; x < width; x++)
        {
            uint32_t pixel = src_row[x];
            // Pixel format is 0xRRGGBBAA
            row_buf[x * 3 + 0] = (uint8_t)((pixel >> 8) & 0xFF);  // B
            row_buf[x * 3 + 1] = (uint8_t)((pixel >> 16) & 0xFF); // G
            row_buf[x * 3 + 2] = (uint8_t)((pixel >> 24) & 0xFF); // R
        }

        // Zero padding bytes
        for (int i = 0; i < padding; i++)
            row_buf[width * 3 + i] = 0;

        if (fwrite(row_buf, 1, (size_t)row_size, f) != (size_t)row_size)
        {
            free(row_buf);
            fclose(f);
            return 0;
        }
    }

    free(row_buf);
    fclose(f);
    return 1;
}

//=============================================================================
// Image Transforms
//=============================================================================

void *rt_pixels_flip_h(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.FlipH: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    // Mirror each row: src[x] -> dst[width-1-x]
    for (int64_t y = 0; y < p->height; y++)
    {
        uint32_t *src_row = p->data + y * p->width;
        uint32_t *dst_row = result->data + y * p->width;
        for (int64_t x = 0; x < p->width; x++)
        {
            dst_row[p->width - 1 - x] = src_row[x];
        }
    }

    return result;
}

void *rt_pixels_flip_v(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.FlipV: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    // Mirror rows: src[y] -> dst[height-1-y]
    for (int64_t y = 0; y < p->height; y++)
    {
        uint32_t *src_row = p->data + y * p->width;
        uint32_t *dst_row = result->data + (p->height - 1 - y) * p->width;
        memcpy(dst_row, src_row, (size_t)p->width * sizeof(uint32_t));
    }

    return result;
}

void *rt_pixels_rotate_cw(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.RotateCW: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // New dimensions: width becomes height, height becomes width
    int64_t new_width = p->height;
    int64_t new_height = p->width;

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Rotate 90 CW: src[x,y] -> dst[height-1-y, x]
    // In terms of new coords: dst[x',y'] = src[y', width-1-x']
    for (int64_t y = 0; y < p->height; y++)
    {
        for (int64_t x = 0; x < p->width; x++)
        {
            uint32_t pixel = p->data[y * p->width + x];
            // New position: (height-1-y, x) in new coordinate system
            int64_t new_x = p->height - 1 - y;
            int64_t new_y = x;
            result->data[new_y * new_width + new_x] = pixel;
        }
    }

    return result;
}

void *rt_pixels_rotate_ccw(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.RotateCCW: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // New dimensions: width becomes height, height becomes width
    int64_t new_width = p->height;
    int64_t new_height = p->width;

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Rotate 90 CCW: src[x,y] -> dst[y, width-1-x]
    for (int64_t y = 0; y < p->height; y++)
    {
        for (int64_t x = 0; x < p->width; x++)
        {
            uint32_t pixel = p->data[y * p->width + x];
            // New position: (y, width-1-x) in new coordinate system
            int64_t new_x = y;
            int64_t new_y = p->width - 1 - x;
            result->data[new_y * new_width + new_x] = pixel;
        }
    }

    return result;
}

void *rt_pixels_rotate_180(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Rotate180: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    // Rotate 180: src[x,y] -> dst[width-1-x, height-1-y]
    int64_t total = p->width * p->height;
    for (int64_t i = 0; i < total; i++)
    {
        result->data[total - 1 - i] = p->data[i];
    }

    return result;
}

void *rt_pixels_scale(void *pixels, int64_t new_width, int64_t new_height)
{
    if (!pixels)
    {
        rt_trap("Pixels.Scale: null pixels");
        return NULL;
    }

    if (new_width <= 0)
        new_width = 1;
    if (new_height <= 0)
        new_height = 1;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // Handle empty source
    if (p->width <= 0 || p->height <= 0)
    {
        return pixels_alloc(new_width, new_height);
    }

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Nearest-neighbor scaling
    for (int64_t y = 0; y < new_height; y++)
    {
        // Map destination y to source y
        int64_t src_y = (y * p->height) / new_height;
        if (src_y >= p->height)
            src_y = p->height - 1;

        uint32_t *src_row = p->data + src_y * p->width;
        uint32_t *dst_row = result->data + y * new_width;

        for (int64_t x = 0; x < new_width; x++)
        {
            // Map destination x to source x
            int64_t src_x = (x * p->width) / new_width;
            if (src_x >= p->width)
                src_x = p->width - 1;

            dst_row[x] = src_row[src_x];
        }
    }

    return result;
}
