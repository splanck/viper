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
#include "rt_compress.h"
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

/// @brief Convert 0x00RRGGBB canvas color to 0xRRGGBBFF (fully-opaque RGBA).
static inline uint32_t rgb_to_rgba(int64_t color)
{
    return (uint32_t)((color << 8) | 0xFF);
}

/// @brief Write one pixel with bounds check (no null check — caller ensures p is valid).
static inline void set_pixel_raw(rt_pixels_impl *p, int64_t x, int64_t y, uint32_t c)
{
    if (x >= 0 && x < p->width && y >= 0 && y < p->height)
        p->data[y * p->width + x] = c;
}

/// @brief Integer square root (Newton's method, exact for perfect squares).
static int64_t isqrt64(int64_t n)
{
    if (n <= 0)
        return 0;
    int64_t x = n;
    int64_t y = (x + 1) / 2;
    while (y < x)
    {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

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

    // Calculate file size (guard against uint32 overflow for very large images)
    uint64_t data_size_u64 = (uint64_t)row_size * (uint64_t)height;
    if (data_size_u64 > (uint64_t)0xFFFFFFC9u) // UINT32_MAX - 54
        return 0;
    uint32_t data_size = (uint32_t)data_size_u64;
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
// PNG Image I/O
//=============================================================================

// PNG uses big-endian integers
static uint32_t png_read_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Paeth predictor as defined by the PNG spec
static uint8_t paeth_predict(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

void *rt_pixels_load_png(void *path)
{
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    // Read entire file into memory
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_len < 8)
    {
        fclose(f);
        return NULL;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_len);
    if (!file_data)
    {
        fclose(f);
        return NULL;
    }
    if (fread(file_data, 1, (size_t)file_len, f) != (size_t)file_len)
    {
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    // Verify PNG signature
    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(file_data, png_sig, 8) != 0)
    {
        free(file_data);
        return NULL;
    }

    // Parse IHDR and collect IDAT chunks
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    uint8_t *idat_buf = NULL;
    size_t idat_len = 0;
    size_t idat_cap = 0;
    size_t pos = 8;

    while (pos + 12 <= (size_t)file_len)
    {
        uint32_t chunk_len = png_read_u32(file_data + pos);
        const uint8_t *chunk_type = file_data + pos + 4;
        const uint8_t *chunk_data = file_data + pos + 8;

        if (pos + 12 + chunk_len > (size_t)file_len)
            break;

        if (memcmp(chunk_type, "IHDR", 4) == 0 && chunk_len >= 13)
        {
            width = png_read_u32(chunk_data);
            height = png_read_u32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            // Only support 8-bit RGB (2) and RGBA (6)
            if (bit_depth != 8 || (color_type != 2 && color_type != 6))
            {
                free(file_data);
                if (idat_buf)
                    free(idat_buf);
                return NULL;
            }
        }
        else if (memcmp(chunk_type, "IDAT", 4) == 0)
        {
            // Accumulate IDAT data
            if (chunk_len > SIZE_MAX - idat_len) // overflow guard
            {
                free(file_data);
                if (idat_buf)
                    free(idat_buf);
                return NULL;
            }
            if (idat_len + chunk_len > idat_cap)
            {
                idat_cap = (idat_len + chunk_len) * 2;
                uint8_t *new_buf = (uint8_t *)realloc(idat_buf, idat_cap);
                if (!new_buf)
                {
                    free(file_data);
                    if (idat_buf)
                        free(idat_buf);
                    return NULL;
                }
                idat_buf = new_buf;
            }
            memcpy(idat_buf + idat_len, chunk_data, chunk_len);
            idat_len += chunk_len;
        }
        else if (memcmp(chunk_type, "IEND", 4) == 0)
        {
            break;
        }

        pos += 12 + chunk_len; // length + type + data + crc
    }

    free(file_data);

    if (width == 0 || height == 0 || !idat_buf || idat_len < 2)
    {
        if (idat_buf)
            free(idat_buf);
        return NULL;
    }

    // IDAT data is a zlib stream: 2-byte header + DEFLATE data + 4-byte Adler32
    // Skip the 2-byte zlib header and use our DEFLATE decompressor
    size_t deflate_len = idat_len - 2; // skip zlib header, ignore trailing adler32
    if (deflate_len <= 4)
    {
        free(idat_buf);
        return NULL;
    }
    deflate_len -= 4; // strip adler32 checksum

    // Create a Bytes object with the raw DEFLATE data for rt_compress_inflate
    void *comp_bytes = rt_bytes_new((int64_t)deflate_len);
    if (!comp_bytes)
    {
        free(idat_buf);
        return NULL;
    }
    // Copy deflate data (skip 2-byte zlib header)
    {
        // Access internal bytes data
        typedef struct
        {
            int64_t len;
            uint8_t *data;
        } bytes_t;

        bytes_t *b = (bytes_t *)comp_bytes;
        memcpy(b->data, idat_buf + 2, deflate_len);
    }
    free(idat_buf);

    // Decompress
    void *raw_bytes = rt_compress_inflate(comp_bytes);
    if (!raw_bytes)
        return NULL;

    // Access decompressed data
    typedef struct
    {
        int64_t len;
        uint8_t *data;
    } bytes_t;

    bytes_t *raw = (bytes_t *)raw_bytes;

    int channels = (color_type == 6) ? 4 : 3; // RGBA vs RGB
    if ((size_t)width > SIZE_MAX / (size_t)channels) // overflow guard
        return NULL;
    size_t stride = (size_t)width * (size_t)channels;
    if (height > 0 && (stride + 1) > SIZE_MAX / (size_t)height) // overflow guard
        return NULL;
    size_t expected = (stride + 1) * (size_t)height; // +1 for filter byte per row

    if ((size_t)raw->len < expected)
        return NULL;

    // Reconstruct filtered scanlines
    uint8_t *scanlines = raw->data;
    // Allocate temp buffer for reconstructed image data
    uint8_t *img = (uint8_t *)malloc(stride * height);
    if (!img)
        return NULL;

    for (uint32_t y = 0; y < height; y++)
    {
        uint8_t filter = scanlines[y * (stride + 1)];
        const uint8_t *src = scanlines + y * (stride + 1) + 1;
        uint8_t *dst = img + y * stride;
        const uint8_t *prev = (y > 0) ? img + (y - 1) * stride : NULL;

        for (size_t i = 0; i < stride; i++)
        {
            uint8_t raw_byte = src[i];
            uint8_t a = (i >= (size_t)channels) ? dst[i - channels] : 0;
            uint8_t b_val = prev ? prev[i] : 0;
            uint8_t c = (prev && i >= (size_t)channels) ? prev[i - channels] : 0;

            switch (filter)
            {
                case 0: // None
                    dst[i] = raw_byte;
                    break;
                case 1: // Sub
                    dst[i] = raw_byte + a;
                    break;
                case 2: // Up
                    dst[i] = raw_byte + b_val;
                    break;
                case 3: // Average
                    dst[i] = raw_byte + (uint8_t)(((int)a + (int)b_val) / 2);
                    break;
                case 4: // Paeth
                    dst[i] = raw_byte + paeth_predict(a, b_val, c);
                    break;
                default: // Unknown filter
                    free(img);
                    return NULL;
            }
        }
    }

    // Create Pixels object and convert to our RGBA format (0xRRGGBBAA)
    rt_pixels_impl *pixels = pixels_alloc((int64_t)width, (int64_t)height);
    if (!pixels)
    {
        free(img);
        return NULL;
    }

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            const uint8_t *px = img + (y * stride) + x * channels;
            uint8_t r = px[0];
            uint8_t g = px[1];
            uint8_t b_ch = px[2];
            uint8_t alpha = (channels == 4) ? px[3] : 0xFF;
            pixels->data[y * width + x] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b_ch << 8) | alpha;
        }
    }

    free(img);
    return pixels;
}

int64_t rt_pixels_save_png(void *pixels_ptr, void *path)
{
    if (!pixels_ptr || !path)
        return 0;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels_ptr;
    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath || p->width <= 0 || p->height <= 0)
        return 0;

    uint32_t w = (uint32_t)p->width;
    uint32_t h = (uint32_t)p->height;
    size_t stride = (size_t)w * 4; // RGBA

    // Build raw PNG scanline data with filter byte (filter=0 = None)
    size_t raw_len = (stride + 1) * h;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw)
        return 0;

    for (uint32_t y = 0; y < h; y++)
    {
        raw[y * (stride + 1)] = 0; // Filter: None
        uint8_t *dst = raw + y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++)
        {
            uint32_t pixel = p->data[y * w + x];
            dst[x * 4 + 0] = (uint8_t)((pixel >> 24) & 0xFF); // R
            dst[x * 4 + 1] = (uint8_t)((pixel >> 16) & 0xFF); // G
            dst[x * 4 + 2] = (uint8_t)((pixel >> 8) & 0xFF);  // B
            dst[x * 4 + 3] = (uint8_t)(pixel & 0xFF);         // A
        }
    }

    // Compress the raw data using DEFLATE
    void *raw_bytes = rt_bytes_new((int64_t)raw_len);
    if (!raw_bytes)
    {
        free(raw);
        return 0;
    }
    {
        typedef struct
        {
            int64_t len;
            uint8_t *data;
        } bytes_t;

        bytes_t *b = (bytes_t *)raw_bytes;
        memcpy(b->data, raw, raw_len);
    }
    free(raw);

    void *comp_bytes = rt_compress_deflate(raw_bytes);
    if (!comp_bytes)
        return 0;

    typedef struct
    {
        int64_t len;
        uint8_t *data;
    } bytes_t;

    bytes_t *comp = (bytes_t *)comp_bytes;

    // Build zlib stream: 2-byte header + deflate data + 4-byte adler32
    // Zlib header: CMF=0x78 (deflate, window=32K), FLG=0x01 (no dict, check=1)
    size_t zlib_len = 2 + (size_t)comp->len + 4;
    uint8_t *zlib_data = (uint8_t *)malloc(zlib_len);
    if (!zlib_data)
        return 0;

    zlib_data[0] = 0x78; // CMF
    zlib_data[1] = 0x01; // FLG
    memcpy(zlib_data + 2, comp->data, (size_t)comp->len);

    // Compute Adler-32 of the raw (uncompressed) data
    {
        bytes_t *raw_b = (bytes_t *)raw_bytes;
        uint32_t a = 1, b_v = 0;
        for (int64_t i = 0; i < raw_b->len; i++)
        {
            a = (a + raw_b->data[i]) % 65521;
            b_v = (b_v + a) % 65521;
        }
        uint32_t adler = (b_v << 16) | a;
        zlib_data[2 + comp->len + 0] = (uint8_t)((adler >> 24) & 0xFF);
        zlib_data[2 + comp->len + 1] = (uint8_t)((adler >> 16) & 0xFF);
        zlib_data[2 + comp->len + 2] = (uint8_t)((adler >> 8) & 0xFF);
        zlib_data[2 + comp->len + 3] = (uint8_t)(adler & 0xFF);
    }

    // CRC-32 table
    static uint32_t crc_table[256];
    static int crc_table_init = 0;
    if (!crc_table_init)
    {
        for (uint32_t n = 0; n < 256; n++)
        {
            uint32_t c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            crc_table[n] = c;
        }
        crc_table_init = 1;
    }

// Helper: compute CRC over type + data
#define PNG_CRC(type_data, len)                                                                    \
    do                                                                                             \
    {                                                                                              \
        uint32_t crc = 0xFFFFFFFF;                                                                 \
        for (size_t _i = 0; _i < (size_t)(len); _i++)                                              \
            crc = crc_table[(crc ^ (type_data)[_i]) & 0xFF] ^ (crc >> 8);                          \
        chunk_crc = crc ^ 0xFFFFFFFF;                                                              \
    } while (0)

    FILE *out = fopen(filepath, "wb");
    if (!out)
    {
        free(zlib_data);
        return 0;
    }

    // Write PNG signature
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, out);

    // Write IHDR chunk
    {
        uint8_t ihdr[13];
        ihdr[0] = (uint8_t)(w >> 24);
        ihdr[1] = (uint8_t)(w >> 16);
        ihdr[2] = (uint8_t)(w >> 8);
        ihdr[3] = (uint8_t)w;
        ihdr[4] = (uint8_t)(h >> 24);
        ihdr[5] = (uint8_t)(h >> 16);
        ihdr[6] = (uint8_t)(h >> 8);
        ihdr[7] = (uint8_t)h;
        ihdr[8] = 8;  // bit depth
        ihdr[9] = 6;  // color type = RGBA
        ihdr[10] = 0; // compression
        ihdr[11] = 0; // filter
        ihdr[12] = 0; // interlace

        uint8_t len_buf[4] = {0, 0, 0, 13};
        fwrite(len_buf, 1, 4, out);

        uint8_t type_data[4 + 13];
        memcpy(type_data, "IHDR", 4);
        memcpy(type_data + 4, ihdr, 13);
        fwrite(type_data, 1, 17, out);

        uint32_t chunk_crc;
        PNG_CRC(type_data, 17);
        uint8_t crc_buf[4] = {(uint8_t)(chunk_crc >> 24),
                              (uint8_t)(chunk_crc >> 16),
                              (uint8_t)(chunk_crc >> 8),
                              (uint8_t)chunk_crc};
        fwrite(crc_buf, 1, 4, out);
    }

    // Write IDAT chunk
    {
        uint8_t len_buf[4] = {(uint8_t)(zlib_len >> 24),
                              (uint8_t)(zlib_len >> 16),
                              (uint8_t)(zlib_len >> 8),
                              (uint8_t)zlib_len};
        fwrite(len_buf, 1, 4, out);

        size_t td_len = 4 + zlib_len;
        uint8_t *type_data = (uint8_t *)malloc(td_len);
        if (!type_data)
        {
            free(zlib_data);
            fclose(out);
            return 0;
        }
        memcpy(type_data, "IDAT", 4);
        memcpy(type_data + 4, zlib_data, zlib_len);
        fwrite(type_data, 1, td_len, out);

        uint32_t chunk_crc;
        PNG_CRC(type_data, td_len);
        uint8_t crc_buf[4] = {(uint8_t)(chunk_crc >> 24),
                              (uint8_t)(chunk_crc >> 16),
                              (uint8_t)(chunk_crc >> 8),
                              (uint8_t)chunk_crc};
        fwrite(crc_buf, 1, 4, out);
        free(type_data);
    }

    free(zlib_data);

    // Write IEND chunk
    {
        uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
        fwrite(iend, 1, 12, out);
    }

#undef PNG_CRC

    fflush(out);
    fclose(out);
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

            double r = r00 * (1 - fx) * (1 - fy) + r10 * fx * (1 - fy) + r01 * (1 - fx) * fy +
                       r11 * fx * fy;
            double g = g00 * (1 - fx) * (1 - fy) + g10 * fx * (1 - fy) + g01 * (1 - fx) * fy +
                       g11 * fx * fy;
            double b = b00 * (1 - fx) * (1 - fy) + b10 * fx * (1 - fy) + b01 * (1 - fx) * fy +
                       b11 * fx * fy;
            double a = a00 * (1 - fx) * (1 - fy) + a10 * fx * (1 - fy) + a01 * (1 - fx) * fy +
                       a11 * fx * fy;

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
        result->data[i] =
            ((uint32_t)a << 24) | ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | gray;
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

    // Separable box blur: horizontal pass → temp, then vertical pass → result.
    // Reduces O(w×h×(2r+1)²) to O(w×h×(2r+1)×2).  Format: 0xRRGGBBAA.
    uint32_t *tmp = (uint32_t *)malloc((size_t)(w * h) * sizeof(uint32_t));
    if (!tmp)
        return result; // return zero-filled result on OOM

    // Horizontal pass: blur each row independently into tmp
    for (int64_t y = 0; y < h; y++)
    {
        for (int64_t x = 0; x < w; x++)
        {
            int64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int64_t count = 0;
            for (int64_t kdx = -radius; kdx <= radius; kdx++)
            {
                int64_t sx = x + kdx;
                if (sx >= 0 && sx < w)
                {
                    uint32_t pixel = p->data[y * w + sx];
                    sum_a += (pixel >> 24) & 0xFF;
                    sum_r += (pixel >> 16) & 0xFF;
                    sum_g += (pixel >> 8) & 0xFF;
                    sum_b += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0)
                tmp[y * w + x] = ((uint32_t)(sum_a / count) << 24)
                               | ((uint32_t)(sum_r / count) << 16)
                               | ((uint32_t)(sum_g / count) << 8)
                               |  (uint32_t)(sum_b / count);
        }
    }

    // Vertical pass: blur each column from tmp into result
    for (int64_t x = 0; x < w; x++)
    {
        for (int64_t y = 0; y < h; y++)
        {
            int64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int64_t count = 0;
            for (int64_t kdy = -radius; kdy <= radius; kdy++)
            {
                int64_t sy = y + kdy;
                if (sy >= 0 && sy < h)
                {
                    uint32_t pixel = tmp[sy * w + x];
                    sum_a += (pixel >> 24) & 0xFF;
                    sum_r += (pixel >> 16) & 0xFF;
                    sum_g += (pixel >> 8) & 0xFF;
                    sum_b += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0)
                result->data[y * w + x] = ((uint32_t)(sum_a / count) << 24)
                                        | ((uint32_t)(sum_r / count) << 16)
                                        | ((uint32_t)(sum_g / count) << 8)
                                        |  (uint32_t)(sum_b / count);
        }
    }

    free(tmp);
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

        if (src_y >= p->height)
            src_y = p->height - 1;
        if (src_y < 0)
            src_y = 0;
        int64_t sy1 = (src_y + 1 < p->height) ? src_y + 1 : src_y;
        if (src_y >= p->height - 1)
            frac_y = 255;

        for (int64_t x = 0; x < new_width; x++)
        {
            // Map destination x to source x (with fractional part)
            int64_t src_x_256 = (x * p->width * 256) / new_width;
            int64_t src_x = src_x_256 >> 8;
            int64_t frac_x = src_x_256 & 0xFF;

            if (src_x >= p->width)
                src_x = p->width - 1;
            if (src_x < 0)
                src_x = 0;
            int64_t sx1 = (src_x + 1 < p->width) ? src_x + 1 : src_x;
            if (src_x >= p->width - 1)
                frac_x = 255;

            // Get four neighboring pixels
            uint32_t p00 = p->data[src_y * p->width + src_x];
            uint32_t p10 = p->data[src_y * p->width + sx1];
            uint32_t p01 = p->data[sy1 * p->width + src_x];
            uint32_t p11 = p->data[sy1 * p->width + sx1];

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

            int64_t a = (a00 * inv_frac_x * inv_frac_y + a10 * frac_x * inv_frac_y +
                         a01 * inv_frac_x * frac_y + a11 * frac_x * frac_y) >>
                        16;
            int64_t r = (r00 * inv_frac_x * inv_frac_y + r10 * frac_x * inv_frac_y +
                         r01 * inv_frac_x * frac_y + r11 * frac_x * frac_y) >>
                        16;
            int64_t g = (g00 * inv_frac_x * inv_frac_y + g10 * frac_x * inv_frac_y +
                         g01 * inv_frac_x * frac_y + g11 * frac_x * frac_y) >>
                        16;
            int64_t b = (b00 * inv_frac_x * inv_frac_y + b10 * frac_x * inv_frac_y +
                         b01 * inv_frac_x * frac_y + b11 * frac_x * frac_y) >>
                        16;

            result->data[y * new_width + x] = ((uint32_t)(a & 0xFF) << 24) |
                                              ((uint32_t)(r & 0xFF) << 16) |
                                              ((uint32_t)(g & 0xFF) << 8) | (b & 0xFF);
        }
    }

    return result;
}

//=============================================================================
// Drawing Primitives  (color format: 0x00RRGGBB — Canvas-compatible)
//=============================================================================

void rt_pixels_set_rgb(void *pixels, int64_t x, int64_t y, int64_t color)
{
    rt_pixels_set(pixels, x, y, (color << 8) | 0xFF);
}

int64_t rt_pixels_get_rgb(void *pixels, int64_t x, int64_t y)
{
    return rt_pixels_get(pixels, x, y) >> 8;
}

void rt_pixels_draw_line(
    void *pixels, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawLine: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    int64_t adx = dx < 0 ? -dx : dx;
    int64_t ady = dy < 0 ? -dy : dy;
    int64_t sx = dx >= 0 ? 1 : -1;
    int64_t sy = dy >= 0 ? 1 : -1;

    int64_t err = adx - ady;
    int64_t x = x1;
    int64_t y = y1;

    for (;;)
    {
        set_pixel_raw(p, x, y, rgba);
        if (x == x2 && y == y2)
            break;
        int64_t e2 = err * 2;
        if (e2 > -ady)
        {
            err -= ady;
            x += sx;
        }
        if (e2 < adx)
        {
            err += adx;
            y += sy;
        }
    }
}

void rt_pixels_draw_box(
    void *pixels, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawBox: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    // Clip to buffer bounds
    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < 0 ? 0 : y;
    int64_t x1 = x + w;
    int64_t y1 = y + h;
    if (x1 > p->width)
        x1 = p->width;
    if (y1 > p->height)
        y1 = p->height;

    for (int64_t row = y0; row < y1; row++)
        for (int64_t col = x0; col < x1; col++)
            p->data[row * p->width + col] = rgba;
}

void rt_pixels_draw_frame(
    void *pixels, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawFrame: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (w <= 0 || h <= 0)
        return;

    // Top and bottom rows
    for (int64_t col = x; col < x + w; col++)
    {
        set_pixel_raw(p, col, y, rgba);
        set_pixel_raw(p, col, y + h - 1, rgba);
    }
    // Left and right columns (skip corners already drawn)
    for (int64_t row = y + 1; row < y + h - 1; row++)
    {
        set_pixel_raw(p, x, row, rgba);
        set_pixel_raw(p, x + w - 1, row, rgba);
    }
}

void rt_pixels_draw_disc(void *pixels, int64_t cx, int64_t cy, int64_t r, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawDisc: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (r < 0)
        r = 0;

    for (int64_t dy = -r; dy <= r; dy++)
    {
        int64_t dx = isqrt64(r * r - dy * dy);
        for (int64_t fx = cx - dx; fx <= cx + dx; fx++)
            set_pixel_raw(p, fx, cy + dy, rgba);
    }
}

void rt_pixels_draw_ring(void *pixels, int64_t cx, int64_t cy, int64_t r, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawRing: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (r < 0)
        return;
    if (r == 0)
    {
        set_pixel_raw(p, cx, cy, rgba);
        return;
    }

    // Midpoint circle: 8-way symmetry
    int64_t mx = r;
    int64_t my = 0;
    int64_t err = 0;

    while (mx >= my)
    {
        set_pixel_raw(p, cx + mx, cy + my, rgba);
        set_pixel_raw(p, cx + my, cy + mx, rgba);
        set_pixel_raw(p, cx - my, cy + mx, rgba);
        set_pixel_raw(p, cx - mx, cy + my, rgba);
        set_pixel_raw(p, cx - mx, cy - my, rgba);
        set_pixel_raw(p, cx - my, cy - mx, rgba);
        set_pixel_raw(p, cx + my, cy - mx, rgba);
        set_pixel_raw(p, cx + mx, cy - my, rgba);

        my++;
        if (err <= 0)
        {
            err += 2 * my + 1;
        }
        else
        {
            mx--;
            err += 2 * (my - mx) + 1;
        }
    }
}

void rt_pixels_draw_ellipse(
    void *pixels, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawEllipse: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (rx <= 0 || ry <= 0)
    {
        set_pixel_raw(p, cx, cy, rgba);
        return;
    }

    // Scanline fill: for each row dy, fill span [cx-dx .. cx+dx]
    // dx = rx * isqrt(ry^2 - dy^2) / ry  (integer arithmetic, no float)
    int64_t ry2 = ry * ry;
    for (int64_t dy = -ry; dy <= ry; dy++)
    {
        int64_t rem = ry2 - dy * dy;
        if (rem < 0)
            rem = 0;
        int64_t dx = rx * isqrt64(rem) / ry;
        for (int64_t fx = cx - dx; fx <= cx + dx; fx++)
            set_pixel_raw(p, fx, cy + dy, rgba);
    }
}

void rt_pixels_draw_ellipse_frame(
    void *pixels, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawEllipseFrame: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (rx <= 0 || ry <= 0)
    {
        set_pixel_raw(p, cx, cy, rgba);
        return;
    }

    // Midpoint ellipse algorithm — 4-quadrant symmetry
    int64_t rx2 = rx * rx;
    int64_t ry2 = ry * ry;
    int64_t two_rx2 = 2 * rx2;
    int64_t two_ry2 = 2 * ry2;
    int64_t ex = 0;
    int64_t ey = ry;
    int64_t px_val = 0;
    int64_t py_val = two_rx2 * ey;

    // Region 1 (slope magnitude < 1)
    int64_t d1 = ry2 - rx2 * ry + rx2 / 4;
    while (px_val < py_val)
    {
        set_pixel_raw(p, cx + ex, cy + ey, rgba);
        set_pixel_raw(p, cx - ex, cy + ey, rgba);
        set_pixel_raw(p, cx + ex, cy - ey, rgba);
        set_pixel_raw(p, cx - ex, cy - ey, rgba);
        ex++;
        px_val += two_ry2;
        if (d1 < 0)
        {
            d1 += ry2 + px_val;
        }
        else
        {
            ey--;
            py_val -= two_rx2;
            d1 += ry2 + px_val - py_val;
        }
    }

    // Region 2 (slope magnitude >= 1)
    int64_t d2 = ry2 * ex * ex + rx2 * (ey - 1) * (ey - 1) - rx2 * ry2;
    while (ey >= 0)
    {
        set_pixel_raw(p, cx + ex, cy + ey, rgba);
        set_pixel_raw(p, cx - ex, cy + ey, rgba);
        set_pixel_raw(p, cx + ex, cy - ey, rgba);
        set_pixel_raw(p, cx - ex, cy - ey, rgba);
        ey--;
        py_val -= two_rx2;
        if (d2 > 0)
        {
            d2 += rx2 - py_val;
        }
        else
        {
            ex++;
            px_val += two_ry2;
            d2 += rx2 - py_val + px_val;
        }
    }
}

void rt_pixels_flood_fill(void *pixels, int64_t x, int64_t y, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.FloodFill: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return;

    uint32_t target = p->data[y * p->width + x];
    uint32_t fill_c = rgb_to_rgba(color);

    if (target == fill_c)
        return;

    // Iterative scanline flood fill — no recursion, no stack overflow risk
    typedef struct
    {
        int64_t x;
        int64_t y;
    } FillSeed;

    int64_t cap = 4096;
    FillSeed *stack = (FillSeed *)malloc((size_t)cap * sizeof(FillSeed));
    if (!stack)
        return;

    int64_t top = 0;
    stack[top].x = x;
    stack[top].y = y;
    top++;

    while (top > 0)
    {
        top--;
        int64_t sx = stack[top].x;
        int64_t sy = stack[top].y;

        if (sy < 0 || sy >= p->height || sx < 0 || sx >= p->width)
            continue;
        if (p->data[sy * p->width + sx] != target)
            continue;

        // Scan left to find span start
        int64_t lx = sx;
        while (lx > 0 && p->data[sy * p->width + (lx - 1)] == target)
            lx--;

        // Scan right to find span end
        int64_t rx = sx;
        while (rx + 1 < p->width && p->data[sy * p->width + (rx + 1)] == target)
            rx++;

        // Fill the span
        for (int64_t fx = lx; fx <= rx; fx++)
            p->data[sy * p->width + fx] = fill_c;

        // Push seed pixels for rows above and below this span
        for (int64_t row_off = -1; row_off <= 1; row_off += 2)
        {
            int64_t ny = sy + row_off;
            if (ny < 0 || ny >= p->height)
                continue;

            int64_t in_span = 0;
            for (int64_t fx = lx; fx <= rx; fx++)
            {
                if (p->data[ny * p->width + fx] == target)
                {
                    if (!in_span)
                    {
                        if (top >= cap)
                        {
                            int64_t new_cap = cap * 2;
                            FillSeed *ns = (FillSeed *)realloc(
                                stack, (size_t)new_cap * sizeof(FillSeed));
                            if (!ns)
                            {
                                free(stack);
                                return;
                            }
                            stack = ns;
                            cap = new_cap;
                        }
                        stack[top].x = fx;
                        stack[top].y = ny;
                        top++;
                        in_span = 1;
                    }
                }
                else
                {
                    in_span = 0;
                }
            }
        }
    }

    free(stack);
}

void rt_pixels_draw_thick_line(void *pixels,
                               int64_t x1, int64_t y1,
                               int64_t x2, int64_t y2,
                               int64_t thickness, int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawThickLine: null pixels");
        return;
    }
    if (thickness <= 1)
    {
        rt_pixels_draw_line(pixels, x1, y1, x2, y2, color);
        return;
    }

    int64_t radius = thickness / 2;

    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    int64_t adx = dx < 0 ? -dx : dx;
    int64_t ady = dy < 0 ? -dy : dy;
    int64_t sx = dx >= 0 ? 1 : -1;
    int64_t sy = dy >= 0 ? 1 : -1;

    int64_t err = adx - ady;
    int64_t x = x1;
    int64_t y = y1;

    for (;;)
    {
        rt_pixels_draw_disc(pixels, x, y, radius, color);
        if (x == x2 && y == y2)
            break;
        int64_t e2 = err * 2;
        if (e2 > -ady)
        {
            err -= ady;
            x += sx;
        }
        if (e2 < adx)
        {
            err += adx;
            y += sy;
        }
    }
}

void rt_pixels_draw_triangle(void *pixels,
                             int64_t x1, int64_t y1,
                             int64_t x2, int64_t y2,
                             int64_t x3, int64_t y3,
                             int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawTriangle: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    // Sort vertices by y ascending (bubble sort 3 elements)
    if (y1 > y2)
    {
        int64_t tx = x1; x1 = x2; x2 = tx;
        int64_t ty = y1; y1 = y2; y2 = ty;
    }
    if (y1 > y3)
    {
        int64_t tx = x1; x1 = x3; x3 = tx;
        int64_t ty = y1; y1 = y3; y3 = ty;
    }
    if (y2 > y3)
    {
        int64_t tx = x2; x2 = x3; x3 = tx;
        int64_t ty = y2; y2 = y3; y3 = ty;
    }

    int64_t total_h = y3 - y1;
    if (total_h == 0)
        return;

    // Upper half: y1 .. y2
    int64_t upper_h = y2 - y1;
    for (int64_t row = 0; row <= upper_h; row++)
    {
        int64_t scan_y = y1 + row;
        int64_t ax = x1 + (x3 - x1) * row / total_h;
        int64_t bx = x1 + (x2 - x1) * row / (upper_h > 0 ? upper_h : 1);
        if (ax > bx)
        {
            int64_t tmp = ax; ax = bx; bx = tmp;
        }
        for (int64_t col = ax; col <= bx; col++)
            set_pixel_raw(p, col, scan_y, rgba);
    }

    // Lower half: y2 .. y3
    int64_t lower_h = y3 - y2;
    for (int64_t row = 0; row <= lower_h; row++)
    {
        int64_t scan_y = y2 + row;
        int64_t ax = x1 + (x3 - x1) * (upper_h + row) / total_h;
        int64_t bx = x2 + (x3 - x2) * row / (lower_h > 0 ? lower_h : 1);
        if (ax > bx)
        {
            int64_t tmp = ax; ax = bx; bx = tmp;
        }
        for (int64_t col = ax; col <= bx; col++)
            set_pixel_raw(p, col, scan_y, rgba);
    }
}

void rt_pixels_draw_bezier(void *pixels,
                           int64_t x1, int64_t y1,
                           int64_t cx_ctrl, int64_t cy_ctrl,
                           int64_t x2, int64_t y2,
                           int64_t color)
{
    if (!pixels)
    {
        rt_trap("Pixels.DrawBezier: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    // Adaptive step count: enough steps to avoid gaps
    int64_t adx = x2 - x1;     if (adx < 0) adx = -adx;
    int64_t ady = y2 - y1;     if (ady < 0) ady = -ady;
    int64_t acx = cx_ctrl - x1; if (acx < 0) acx = -acx;
    int64_t acy = cy_ctrl - y1; if (acy < 0) acy = -acy;
    int64_t steps = adx > ady ? adx : ady;
    if (acx > steps) steps = acx;
    if (acy > steps) steps = acy;
    steps = steps * 2 + 1;
    if (steps < 2) steps = 2;
    if (steps > 10000) steps = 10000; // Cap to prevent excessive loops

    // Integer de Casteljau: P(t) via linear interpolation at t = i/steps
    for (int64_t i = 0; i <= steps; i++)
    {
        int64_t lx0 = x1 + (cx_ctrl - x1) * i / steps;
        int64_t ly0 = y1 + (cy_ctrl - y1) * i / steps;
        int64_t lx1 = cx_ctrl + (x2 - cx_ctrl) * i / steps;
        int64_t ly1 = cy_ctrl + (y2 - cy_ctrl) * i / steps;
        int64_t bx  = lx0 + (lx1 - lx0) * i / steps;
        int64_t by  = ly0 + (ly1 - ly0) * i / steps;
        set_pixel_raw(p, bx, by, rgba);
    }
}

void rt_pixels_blend_pixel(void *pixels, int64_t x, int64_t y, int64_t color, int64_t alpha)
{
    if (!pixels)
    {
        rt_trap("Pixels.BlendPixel: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return;

    // Clamp alpha to [0, 255]
    if (alpha <= 0)
        return; // fully transparent — no-op
    if (alpha > 255)
        alpha = 255;

    // Fully opaque fast path — same as set_rgb
    if (alpha == 255)
    {
        p->data[y * p->width + x] = (uint32_t)((color << 8) | 0xFF);
        return;
    }

    // Extract source channels from 0x00RRGGBB
    uint32_t sr = (uint32_t)((color >> 16) & 0xFF);
    uint32_t sg = (uint32_t)((color >>  8) & 0xFF);
    uint32_t sb = (uint32_t)( color        & 0xFF);
    uint32_t sa = (uint32_t)alpha;

    // Extract destination channels from 0xRRGGBBAA
    uint32_t dst = p->data[y * p->width + x];
    uint32_t dr = (dst >> 24) & 0xFF;
    uint32_t dg = (dst >> 16) & 0xFF;
    uint32_t db = (dst >>  8) & 0xFF;
    uint32_t da = (dst)       & 0xFF;

    // Porter-Duff "over": out = src * sa/255 + dst * da/255 * (255 - sa)/255
    // Simplified (pre-multiplied integer arithmetic, +127 for rounding):
    uint32_t inv = 255 - sa;
    uint32_t or_ = (sr * sa + dr * inv + 127) / 255;
    uint32_t og  = (sg * sa + dg * inv + 127) / 255;
    uint32_t ob  = (sb * sa + db * inv + 127) / 255;
    uint32_t oa  = sa + (da * inv + 127) / 255;

    p->data[y * p->width + x] = (or_ << 24) | (og << 16) | (ob << 8) | oa;
}
