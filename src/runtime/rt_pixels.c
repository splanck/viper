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

#include <math.h>
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
    fflush(f);
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

void *rt_pixels_rotate(void *pixels, double angle_degrees)
{
    if (!pixels)
    {
        rt_trap("Pixels.Rotate: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    if (p->width <= 0 || p->height <= 0)
        return pixels_alloc(0, 0);

    // Normalize angle to 0-360
    while (angle_degrees < 0)
        angle_degrees += 360.0;
    while (angle_degrees >= 360.0)
        angle_degrees -= 360.0;

    // Fast paths for common angles
    if (fabs(angle_degrees) < 0.001 || fabs(angle_degrees - 360.0) < 0.001)
    {
        // No rotation - return a copy
        rt_pixels_impl *result = pixels_alloc(p->width, p->height);
        if (!result)
            return NULL;
        memcpy(result->data, p->data, (size_t)(p->width * p->height) * sizeof(uint32_t));
        return result;
    }
    if (fabs(angle_degrees - 90.0) < 0.001)
        return rt_pixels_rotate_cw(pixels);
    if (fabs(angle_degrees - 180.0) < 0.001)
        return rt_pixels_rotate_180(pixels);
    if (fabs(angle_degrees - 270.0) < 0.001)
        return rt_pixels_rotate_ccw(pixels);

    // Convert to radians
    double rad = angle_degrees * (3.14159265358979323846 / 180.0);
    double cos_a = cos(rad);
    double sin_a = sin(rad);

    // Calculate new bounding box dimensions
    // The four corners of the original image rotated about center
    double hw = p->width / 2.0;
    double hh = p->height / 2.0;

    // Rotated corner positions (relative to center)
    double corners[4][2] = {
        {-hw * cos_a + hh * sin_a, -hw * sin_a - hh * cos_a}, // top-left
        {hw * cos_a + hh * sin_a, hw * sin_a - hh * cos_a},   // top-right
        {hw * cos_a - hh * sin_a, hw * sin_a + hh * cos_a},   // bottom-right
        {-hw * cos_a - hh * sin_a, -hw * sin_a + hh * cos_a}  // bottom-left
    };

    double min_x = corners[0][0], max_x = corners[0][0];
    double min_y = corners[0][1], max_y = corners[0][1];
    for (int i = 1; i < 4; i++)
    {
        if (corners[i][0] < min_x)
            min_x = corners[i][0];
        if (corners[i][0] > max_x)
            max_x = corners[i][0];
        if (corners[i][1] < min_y)
            min_y = corners[i][1];
        if (corners[i][1] > max_y)
            max_y = corners[i][1];
    }

    int64_t new_width = (int64_t)ceil(max_x - min_x);
    int64_t new_height = (int64_t)ceil(max_y - min_y);
    if (new_width < 1)
        new_width = 1;
    if (new_height < 1)
        new_height = 1;

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Clear to transparent
    memset(result->data, 0, (size_t)(new_width * new_height) * sizeof(uint32_t));

    // New center
    double new_hw = new_width / 2.0;
    double new_hh = new_height / 2.0;

    // For each destination pixel, find source pixel using inverse rotation
    for (int64_t dy = 0; dy < new_height; dy++)
    {
        for (int64_t dx = 0; dx < new_width; dx++)
        {
            // Destination position relative to new center
            double dx_c = dx - new_hw;
            double dy_c = dy - new_hh;

            // Inverse rotation to find source position
            double sx_c = dx_c * cos_a + dy_c * sin_a;
            double sy_c = -dx_c * sin_a + dy_c * cos_a;

            // Source position in original image coordinates
            double sx = sx_c + hw;
            double sy = sy_c + hh;

            // Bilinear interpolation
            int64_t x0 = (int64_t)floor(sx);
            int64_t y0 = (int64_t)floor(sy);
            int64_t x1 = x0 + 1;
            int64_t y1 = y0 + 1;

            // Skip if completely outside source
            if (x1 < 0 || x0 >= p->width || y1 < 0 || y0 >= p->height)
                continue;

            // Fractional parts
            double fx = sx - x0;
            double fy = sy - y0;

            // Get the four surrounding pixels (with bounds checking)
            uint32_t c00 = 0, c10 = 0, c01 = 0, c11 = 0;

            if (x0 >= 0 && x0 < p->width && y0 >= 0 && y0 < p->height)
                c00 = p->data[y0 * p->width + x0];
            if (x1 >= 0 && x1 < p->width && y0 >= 0 && y0 < p->height)
                c10 = p->data[y0 * p->width + x1];
            if (x0 >= 0 && x0 < p->width && y1 >= 0 && y1 < p->height)
                c01 = p->data[y1 * p->width + x0];
            if (x1 >= 0 && x1 < p->width && y1 >= 0 && y1 < p->height)
                c11 = p->data[y1 * p->width + x1];

            // Bilinear interpolation for each channel
            uint8_t r00 = (c00 >> 0) & 0xFF, g00 = (c00 >> 8) & 0xFF;
            uint8_t b00 = (c00 >> 16) & 0xFF, a00 = (c00 >> 24) & 0xFF;
            uint8_t r10 = (c10 >> 0) & 0xFF, g10 = (c10 >> 8) & 0xFF;
            uint8_t b10 = (c10 >> 16) & 0xFF, a10 = (c10 >> 24) & 0xFF;
            uint8_t r01 = (c01 >> 0) & 0xFF, g01 = (c01 >> 8) & 0xFF;
            uint8_t b01 = (c01 >> 16) & 0xFF, a01 = (c01 >> 24) & 0xFF;
            uint8_t r11 = (c11 >> 0) & 0xFF, g11 = (c11 >> 8) & 0xFF;
            uint8_t b11 = (c11 >> 16) & 0xFF, a11 = (c11 >> 24) & 0xFF;

            double r = r00 * (1 - fx) * (1 - fy) + r10 * fx * (1 - fy) + r01 * (1 - fx) * fy + r11 * fx * fy;
            double g = g00 * (1 - fx) * (1 - fy) + g10 * fx * (1 - fy) + g01 * (1 - fx) * fy + g11 * fx * fy;
            double b = b00 * (1 - fx) * (1 - fy) + b10 * fx * (1 - fy) + b01 * (1 - fx) * fy + b11 * fx * fy;
            double a = a00 * (1 - fx) * (1 - fy) + a10 * fx * (1 - fy) + a01 * (1 - fx) * fy + a11 * fx * fy;

            uint8_t ri = (uint8_t)(r > 255 ? 255 : (r < 0 ? 0 : r));
            uint8_t gi = (uint8_t)(g > 255 ? 255 : (g < 0 ? 0 : g));
            uint8_t bi = (uint8_t)(b > 255 ? 255 : (b < 0 ? 0 : b));
            uint8_t ai = (uint8_t)(a > 255 ? 255 : (a < 0 ? 0 : a));

            result->data[dy * new_width + dx] = ri | (gi << 8) | (bi << 16) | (ai << 24);
        }
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

//=============================================================================
// Image Processing
//=============================================================================

void *rt_pixels_invert(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Invert: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++)
    {
        uint32_t px = p->data[i];
        // Format is 0xAARRGGBB - invert RGB, keep alpha
        uint8_t a = (px >> 24) & 0xFF;
        uint8_t r = 255 - ((px >> 16) & 0xFF);
        uint8_t g = 255 - ((px >> 8) & 0xFF);
        uint8_t b = 255 - (px & 0xFF);
        result->data[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    return result;
}

void *rt_pixels_grayscale(void *pixels)
{
    if (!pixels)
    {
        rt_trap("Pixels.Grayscale: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++)
    {
        uint32_t px = p->data[i];
        // Format is 0xAARRGGBB
        uint8_t a = (px >> 24) & 0xFF;
        uint8_t r = (px >> 16) & 0xFF;
        uint8_t g = (px >> 8) & 0xFF;
        uint8_t b = px & 0xFF;

        // Standard grayscale formula: 0.299*R + 0.587*G + 0.114*B
        uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
        result->data[i] = ((uint32_t)a << 24) | ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | gray;
    }

    return result;
}

void *rt_pixels_tint(void *pixels, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.Tint: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    // Extract tint color (0xAARRGGBB format)
    int64_t tr = (color >> 16) & 0xFF;
    int64_t tg = (color >> 8) & 0xFF;
    int64_t tb = color & 0xFF;

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++)
    {
        uint32_t px = p->data[i];
        // Format is 0xAARRGGBB
        uint8_t a = (px >> 24) & 0xFF;
        int64_t r = (px >> 16) & 0xFF;
        int64_t g = (px >> 8) & 0xFF;
        int64_t b = px & 0xFF;

        // Multiply blend: result = (pixel * tint) / 255
        r = (r * tr) / 255;
        g = (g * tg) / 255;
        b = (b * tb) / 255;

        result->data[i] = ((uint32_t)a << 24) | ((uint32_t)(r & 0xFF) << 16) |
                          ((uint32_t)(g & 0xFF) << 8) | (b & 0xFF);
    }

    return result;
}

void *rt_pixels_blur(void *pixels, int64_t radius)
{
    if (!pixels)
    {
        rt_trap("Pixels.Blur: null pixels");
        return NULL;
    }

    if (radius < 1)
        radius = 1;
    if (radius > 10)
        radius = 10;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t w = p->width;
    int64_t h = p->height;

    // Simple box blur - format is 0xAARRGGBB
    for (int64_t y = 0; y < h; y++)
    {
        for (int64_t x = 0; x < w; x++)
        {
            int64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int64_t count = 0;

            for (int64_t dy = -radius; dy <= radius; dy++)
            {
                for (int64_t dx = -radius; dx <= radius; dx++)
                {
                    int64_t sx = x + dx;
                    int64_t sy = y + dy;

                    if (sx >= 0 && sx < w && sy >= 0 && sy < h)
                    {
                        uint32_t px = p->data[sy * w + sx];
                        sum_a += (px >> 24) & 0xFF;
                        sum_r += (px >> 16) & 0xFF;
                        sum_g += (px >> 8) & 0xFF;
                        sum_b += px & 0xFF;
                        count++;
                    }
                }
            }

            if (count > 0)
            {
                uint8_t a = (uint8_t)(sum_a / count);
                uint8_t r = (uint8_t)(sum_r / count);
                uint8_t g = (uint8_t)(sum_g / count);
                uint8_t b = (uint8_t)(sum_b / count);
                result->data[y * w + x] =
                    ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    return result;
}

void *rt_pixels_resize(void *pixels, int64_t new_width, int64_t new_height)
{
    if (!pixels)
    {
        rt_trap("Pixels.Resize: null pixels");
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

    // Bilinear interpolation scaling
    for (int64_t y = 0; y < new_height; y++)
    {
        // Map destination y to source y (with fractional part)
        int64_t src_y_256 = (y * p->height * 256) / new_height;
        int64_t src_y = src_y_256 >> 8;
        int64_t frac_y = src_y_256 & 0xFF;

        if (src_y >= p->height - 1)
        {
            src_y = p->height - 2;
            if (src_y < 0)
                src_y = 0;
            frac_y = 255;
        }

        for (int64_t x = 0; x < new_width; x++)
        {
            // Map destination x to source x (with fractional part)
            int64_t src_x_256 = (x * p->width * 256) / new_width;
            int64_t src_x = src_x_256 >> 8;
            int64_t frac_x = src_x_256 & 0xFF;

            if (src_x >= p->width - 1)
            {
                src_x = p->width - 2;
                if (src_x < 0)
                    src_x = 0;
                frac_x = 255;
            }

            // Get four neighboring pixels
            uint32_t p00 = p->data[src_y * p->width + src_x];
            uint32_t p10 = p->data[src_y * p->width + src_x + 1];
            uint32_t p01 = p->data[(src_y + 1) * p->width + src_x];
            uint32_t p11 = p->data[(src_y + 1) * p->width + src_x + 1];

            // Extract components - format is 0xAARRGGBB
            int64_t a00 = (p00 >> 24) & 0xFF, r00 = (p00 >> 16) & 0xFF, g00 = (p00 >> 8) & 0xFF,
                    b00 = p00 & 0xFF;
            int64_t a10 = (p10 >> 24) & 0xFF, r10 = (p10 >> 16) & 0xFF, g10 = (p10 >> 8) & 0xFF,
                    b10 = p10 & 0xFF;
            int64_t a01 = (p01 >> 24) & 0xFF, r01 = (p01 >> 16) & 0xFF, g01 = (p01 >> 8) & 0xFF,
                    b01 = p01 & 0xFF;
            int64_t a11 = (p11 >> 24) & 0xFF, r11 = (p11 >> 16) & 0xFF, g11 = (p11 >> 8) & 0xFF,
                    b11 = p11 & 0xFF;

            // Bilinear interpolation
            int64_t inv_frac_x = 256 - frac_x;
            int64_t inv_frac_y = 256 - frac_y;

            int64_t a =
                (a00 * inv_frac_x * inv_frac_y + a10 * frac_x * inv_frac_y +
                 a01 * inv_frac_x * frac_y + a11 * frac_x * frac_y) >>
                16;
            int64_t r =
                (r00 * inv_frac_x * inv_frac_y + r10 * frac_x * inv_frac_y +
                 r01 * inv_frac_x * frac_y + r11 * frac_x * frac_y) >>
                16;
            int64_t g =
                (g00 * inv_frac_x * inv_frac_y + g10 * frac_x * inv_frac_y +
                 g01 * inv_frac_x * frac_y + g11 * frac_x * frac_y) >>
                16;
            int64_t b =
                (b00 * inv_frac_x * inv_frac_y + b10 * frac_x * inv_frac_y +
                 b01 * inv_frac_x * frac_y + b11 * frac_x * frac_y) >>
                16;

            result->data[y * new_width + x] = ((uint32_t)(a & 0xFF) << 24) |
                                              ((uint32_t)(r & 0xFF) << 16) |
                                              ((uint32_t)(g & 0xFF) << 8) | (b & 0xFF);
        }
    }

    return result;
}
