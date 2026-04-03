//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_pixels_io.c
// Purpose: Image file I/O for Viper.Graphics.Pixels. Supports loading and saving
//   BMP, PNG, and JPEG image formats. PNG uses zlib compression via rt_compress.
//   JPEG includes a minimal baseline decoder (no progressive/arithmetic support).
//
// Key invariants:
//   - All loaders return a new GC-managed Pixels object or NULL on failure.
//   - BMP supports 24-bit uncompressed only (both top-down and bottom-up).
//   - PNG supports 8-bit and 16-bit grayscale, RGB, RGBA, palette, and tRNS.
//   - JPEG supports baseline 8-bit YCbCr (4:4:4, 4:2:2, 4:2:0, 4:1:1).
//   - Pixel format is 32-bit RGBA: 0xRRGGBBAA in row-major order.
//
// Ownership/Lifetime:
//   - Returned Pixels objects are GC-managed via pixels_alloc().
//   - File handles are opened and closed within each function (no leaks).
//
// Links: src/runtime/graphics/rt_pixels_internal.h (shared struct),
//        src/runtime/graphics/rt_pixels.c (core operations),
//        src/runtime/graphics/rt_pixels.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_pixels_internal.h"
#include "rt_pixels.h"

#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_crc32.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Use 64-bit seek/tell to support files larger than 2 GB on Windows
// where `long` (and thus ftell/fseek) is only 32 bits even on 64-bit builds.
#if defined(_WIN32)
#define px_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define px_ftell(fp) _ftelli64((fp))
#else
#define px_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define px_ftell(fp) ftello((fp))
#endif

//=============================================================================
// BMP Image I/O
//=============================================================================

// BMP file format structures (packed)
#pragma pack(push, 1)

typedef struct bmp_file_header {
    uint8_t magic[2];     // 'B', 'M'
    uint32_t file_size;   // Total file size
    uint16_t reserved1;   // 0
    uint16_t reserved2;   // 0
    uint32_t data_offset; // Offset to pixel data
} bmp_file_header;

typedef struct bmp_info_header {
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

void *rt_pixels_load_bmp(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    uint8_t *row_buf = NULL;
    rt_pixels_impl *pixels = NULL;

    // Read file header
    bmp_file_header file_hdr;
    if (fread(&file_hdr, sizeof(file_hdr), 1, f) != 1)
        goto bmp_cleanup;

    // Check magic
    if (file_hdr.magic[0] != 'B' || file_hdr.magic[1] != 'M')
        goto bmp_cleanup;

    // Read info header
    bmp_info_header info_hdr;
    if (fread(&info_hdr, sizeof(info_hdr), 1, f) != 1)
        goto bmp_cleanup;

    // Only support 24-bit uncompressed
    if (info_hdr.bit_count != 24 || info_hdr.compression != 0)
        goto bmp_cleanup;

    int32_t width = info_hdr.width;
    int32_t height = info_hdr.height;
    int bottom_up = 1;

    // Handle negative height (top-down)
    if (height < 0) {
        height = -height;
        bottom_up = 0;
    }

    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
        goto bmp_cleanup;

    // Calculate row padding (rows must be 4-byte aligned)
    int row_size = ((width * 3 + 3) / 4) * 4;

    // Allocate row buffer
    row_buf = (uint8_t *)malloc((size_t)row_size);
    if (!row_buf)
        goto bmp_cleanup;

    // Create pixels
    pixels = pixels_alloc(width, height);
    if (!pixels)
        goto bmp_cleanup;

    // Seek to pixel data
    if (fseek(f, (long)file_hdr.data_offset, SEEK_SET) != 0) {
        pixels = NULL; // signal failure; pixels_alloc object is GC-managed
        goto bmp_cleanup;
    }

    // Read pixel data
    for (int32_t y = 0; y < height; y++) {
        if (fread(row_buf, 1, (size_t)row_size, f) != (size_t)row_size) {
            pixels = NULL;
            goto bmp_cleanup;
        }

        // Determine destination row (bottom-up reverses row order)
        int32_t dst_y = bottom_up ? (height - 1 - y) : y;
        uint32_t *dst_row = pixels->data + dst_y * width;

        // Convert BGR to RGBA
        for (int32_t x = 0; x < width; x++) {
            uint8_t b = row_buf[x * 3 + 0];
            uint8_t g = row_buf[x * 3 + 1];
            uint8_t r = row_buf[x * 3 + 2];
            // Pack as 0xRRGGBBAA (alpha = 255 for opaque)
            dst_row[x] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
        }
    }

bmp_cleanup:
    free(row_buf);
    fclose(f);
    return pixels;
}

int64_t rt_pixels_save_bmp(void *pixels, void *path) {
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
    if (fwrite(&file_hdr, sizeof(file_hdr), 1, f) != 1) {
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
    if (fwrite(&info_hdr, sizeof(info_hdr), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    // Allocate row buffer
    uint8_t *row_buf = (uint8_t *)calloc(1, (size_t)row_size);
    if (!row_buf) {
        fclose(f);
        return 0;
    }

    // Write pixel data (bottom-up)
    for (int32_t y = height - 1; y >= 0; y--) {
        uint32_t *src_row = p->data + y * width;

        // Convert RGBA to BGR
        for (int32_t x = 0; x < width; x++) {
            uint32_t pixel = src_row[x];
            // Pixel format is 0xRRGGBBAA
            row_buf[x * 3 + 0] = (uint8_t)((pixel >> 8) & 0xFF);  // B
            row_buf[x * 3 + 1] = (uint8_t)((pixel >> 16) & 0xFF); // G
            row_buf[x * 3 + 2] = (uint8_t)((pixel >> 24) & 0xFF); // R
        }

        // Zero padding bytes
        for (int i = 0; i < padding; i++)
            row_buf[width * 3 + i] = 0;

        if (fwrite(row_buf, 1, (size_t)row_size, f) != (size_t)row_size) {
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
static uint32_t png_read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// Paeth predictor as defined by the PNG spec
static uint8_t paeth_predict(uint8_t a, uint8_t b, uint8_t c) {
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

void *rt_pixels_load_png(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    // Read entire file into memory (64-bit safe)
    px_fseek(f, 0, SEEK_END);
    int64_t file_len = px_ftell(f);
    px_fseek(f, 0, SEEK_SET);
    if (file_len < 8 || file_len > 256 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_len);
    if (!file_data) {
        fclose(f);
        return NULL;
    }
    if (fread(file_data, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    // Verify PNG signature
    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(file_data, png_sig, 8) != 0) {
        free(file_data);
        return NULL;
    }

    // Parse IHDR and collect IDAT chunks
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;
    uint8_t *idat_buf = NULL;
    size_t idat_len = 0;
    size_t idat_cap = 0;
    size_t pos = 8;

    // Palette for indexed color (type 3): up to 256 RGB entries
    uint8_t palette[256 * 3];
    int palette_count = 0;
    // tRNS transparency: per-palette alpha or key color
    uint8_t trns_alpha[256];
    int trns_count = 0;
    uint16_t trns_gray = 0; // key color for grayscale
    int has_trns_gray = 0;

    while (pos + 12 <= (size_t)file_len) {
        uint32_t chunk_len = png_read_u32(file_data + pos);
        const uint8_t *chunk_type = file_data + pos + 4;
        const uint8_t *chunk_data = file_data + pos + 8;

        if (pos + 12 + chunk_len > (size_t)file_len)
            break;

        if (memcmp(chunk_type, "IHDR", 4) == 0 && chunk_len >= 13) {
            width = png_read_u32(chunk_data);
            height = png_read_u32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            interlace = chunk_data[12];
            // Validate per PNG spec: valid color_type + bit_depth combinations
            int valid = 0;
            if (color_type == 0) // Grayscale: 1,2,4,8,16
                valid = (bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 ||
                         bit_depth == 16);
            else if (color_type == 2) // RGB: 8,16
                valid = (bit_depth == 8 || bit_depth == 16);
            else if (color_type == 3) // Indexed: 1,2,4,8
                valid = (bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8);
            else if (color_type == 4) // Grayscale+Alpha: 8,16
                valid = (bit_depth == 8 || bit_depth == 16);
            else if (color_type == 6) // RGBA: 8,16
                valid = (bit_depth == 8 || bit_depth == 16);
            if (!valid) {
                free(file_data);
                if (idat_buf)
                    free(idat_buf);
                return NULL;
            }
        } else if (memcmp(chunk_type, "PLTE", 4) == 0) {
            palette_count = (int)(chunk_len / 3);
            if (palette_count > 256)
                palette_count = 256;
            memcpy(palette, chunk_data, (size_t)palette_count * 3);
        } else if (memcmp(chunk_type, "tRNS", 4) == 0) {
            if (color_type == 3) {
                // Per-palette-entry alpha values
                trns_count = (int)chunk_len;
                if (trns_count > 256)
                    trns_count = 256;
                memcpy(trns_alpha, chunk_data, (size_t)trns_count);
            } else if (color_type == 0 && chunk_len >= 2) {
                // Grayscale key color (16-bit, even for 8-bit images)
                trns_gray = (uint16_t)((chunk_data[0] << 8) | chunk_data[1]);
                has_trns_gray = 1;
            }
        } else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            // Accumulate IDAT data
            if (chunk_len > SIZE_MAX - idat_len) // overflow guard
            {
                free(file_data);
                if (idat_buf)
                    free(idat_buf);
                return NULL;
            }
            if (idat_len + chunk_len > idat_cap) {
                idat_cap = (idat_len + chunk_len) * 2;
                uint8_t *new_buf = (uint8_t *)realloc(idat_buf, idat_cap);
                if (!new_buf) {
                    free(file_data);
                    if (idat_buf)
                        free(idat_buf);
                    return NULL;
                }
                idat_buf = new_buf;
            }
            memcpy(idat_buf + idat_len, chunk_data, chunk_len);
            idat_len += chunk_len;
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            break;
        }

        pos += 12 + chunk_len; // length + type + data + crc
    }

    free(file_data);

    if (width == 0 || height == 0 || !idat_buf || idat_len < 2) {
        if (idat_buf)
            free(idat_buf);
        return NULL;
    }

    // IDAT data is a zlib stream: 2-byte header + DEFLATE data + 4-byte Adler32
    // Skip the 2-byte zlib header and use our DEFLATE decompressor
    size_t deflate_len = idat_len - 2; // skip zlib header, ignore trailing adler32
    if (deflate_len <= 4) {
        free(idat_buf);
        return NULL;
    }
    deflate_len -= 4; // strip adler32 checksum

    // Create a Bytes object with the raw DEFLATE data for rt_compress_inflate
    void *comp_bytes = rt_bytes_new((int64_t)deflate_len);
    if (!comp_bytes) {
        free(idat_buf);
        return NULL;
    }
    // Copy deflate data (skip 2-byte zlib header)
    {
        // Access internal bytes data
        typedef struct {
            int64_t len;
            uint8_t *data;
        } bytes_t;

        bytes_t *b = (bytes_t *)comp_bytes;
        memcpy(b->data, idat_buf + 2, deflate_len);
    }
    free(idat_buf);

    // Decompress — all error paths after this point must go through cleanup
    // to release comp_bytes and raw_bytes (GC-managed, refcount=1).
    void *raw_bytes = NULL;
    uint8_t *img = NULL;
    rt_pixels_impl *pixels = NULL;

    raw_bytes = rt_compress_inflate(comp_bytes);
    if (!raw_bytes)
        goto cleanup;

    // Access decompressed data
    typedef struct {
        int64_t len;
        uint8_t *data;
    } bytes_t;

    bytes_t *raw = (bytes_t *)raw_bytes;

    // Compute bytes-per-pixel at the filter level (before sub-byte unpacking).
    // For sub-byte depths (1,2,4-bit), each row is ceil(width*bit_depth/8) bytes.
    int samples_per_pixel = 1; // number of channels
    if (color_type == 2)
        samples_per_pixel = 3;
    else if (color_type == 4)
        samples_per_pixel = 2;
    else if (color_type == 6)
        samples_per_pixel = 4;
    // types 0 and 3: 1 sample per pixel

    int bpp = samples_per_pixel * (bit_depth >= 8 ? bit_depth / 8 : 1);
    // For sub-byte depths, bpp is 1 (minimum for filter byte calculations)
    if (bpp < 1)
        bpp = 1;

    // Helper: compute row stride for a given image width
    #define PNG_STRIDE(w) (bit_depth < 8 \
        ? (((size_t)(w) * (size_t)bit_depth * (size_t)samples_per_pixel + 7) / 8) \
        : ((size_t)(w) * (size_t)samples_per_pixel * ((size_t)bit_depth / 8)))

    // Helper: reconstruct one filtered row
    #define PNG_FILTER_ROW(dst, src, prev, row_stride) do { \
        uint8_t filt = *(src)++; \
        for (size_t fi = 0; fi < (row_stride); fi++) { \
            uint8_t rb = (src)[fi]; \
            uint8_t fa = (fi >= (size_t)bpp) ? (dst)[fi - bpp] : 0; \
            uint8_t fb = (prev) ? (prev)[fi] : 0; \
            uint8_t fc = ((prev) && fi >= (size_t)bpp) ? (prev)[fi - bpp] : 0; \
            switch (filt) { \
                case 0: (dst)[fi] = rb; break; \
                case 1: (dst)[fi] = rb + fa; break; \
                case 2: (dst)[fi] = rb + fb; break; \
                case 3: (dst)[fi] = rb + (uint8_t)(((int)fa + (int)fb) / 2); break; \
                case 4: (dst)[fi] = rb + paeth_predict(fa, fb, fc); break; \
                default: goto cleanup; \
            } \
        } \
        (src) += (row_stride); \
    } while(0)

    size_t stride = PNG_STRIDE(width);

    if (interlace == 1) {
        // Adam7 interlaced PNG: 7 passes
        static const int a7_x0[7] = {0, 4, 0, 2, 0, 1, 0};
        static const int a7_dx[7] = {8, 8, 4, 4, 2, 2, 1};
        static const int a7_y0[7] = {0, 0, 4, 0, 2, 0, 1};
        static const int a7_dy[7] = {8, 8, 8, 4, 4, 2, 2};

        // Allocate full-size image buffer (row-major, sequential)
        img = (uint8_t *)calloc(stride * height, 1);
        if (!img)
            goto cleanup;

        const uint8_t *src_ptr = raw->data;
        const uint8_t *src_end = raw->data + raw->len;

        for (int pass = 0; pass < 7; pass++) {
            uint32_t sub_w = (width + (uint32_t)a7_dx[pass] - 1 - (uint32_t)a7_x0[pass]) / (uint32_t)a7_dx[pass];
            uint32_t sub_h = (height + (uint32_t)a7_dy[pass] - 1 - (uint32_t)a7_y0[pass]) / (uint32_t)a7_dy[pass];
            if (sub_w == 0 || sub_h == 0)
                continue;

            size_t sub_stride = PNG_STRIDE(sub_w);
            // Allocate temp buffer for this sub-image
            uint8_t *sub_img = (uint8_t *)calloc(sub_stride * sub_h, 1);
            if (!sub_img)
                goto cleanup;

            for (uint32_t sy = 0; sy < sub_h; sy++) {
                if (src_ptr + 1 + sub_stride > src_end) {
                    free(sub_img);
                    goto cleanup;
                }
                uint8_t *dst_row = sub_img + sy * sub_stride;
                const uint8_t *prev_row = (sy > 0) ? sub_img + (sy - 1) * sub_stride : NULL;
                PNG_FILTER_ROW(dst_row, src_ptr, prev_row, sub_stride);
            }

            // Scatter sub-image pixels into full image
            // Copy raw bytes for each pixel from sub-image row to the correct
            // position in the full-size img buffer
            int px_bytes = (bit_depth < 8) ? 1 : (samples_per_pixel * bit_depth / 8);
            for (uint32_t sy = 0; sy < sub_h; sy++) {
                uint32_t dy = (uint32_t)a7_y0[pass] + sy * (uint32_t)a7_dy[pass];
                if (dy >= height) continue;
                for (uint32_t sx = 0; sx < sub_w; sx++) {
                    uint32_t dx = (uint32_t)a7_x0[pass] + sx * (uint32_t)a7_dx[pass];
                    if (dx >= width) continue;

                    if (bit_depth >= 8) {
                        // Byte-aligned: copy px_bytes
                        const uint8_t *sp = sub_img + sy * sub_stride + sx * (size_t)px_bytes;
                        uint8_t *dp = img + dy * stride + dx * (size_t)px_bytes;
                        memcpy(dp, sp, (size_t)px_bytes);
                    } else {
                        // Sub-byte: extract bit value from sub-image, set in full image
                        int ppb = 8 / bit_depth;
                        int s_byte = sx / ppb;
                        int s_bit = (ppb - 1 - (sx % ppb)) * bit_depth;
                        uint8_t mask = (uint8_t)((1 << bit_depth) - 1);
                        uint8_t val = (sub_img[sy * sub_stride + s_byte] >> s_bit) & mask;

                        int d_byte = dx / ppb;
                        int d_bit = (ppb - 1 - (dx % ppb)) * bit_depth;
                        img[dy * stride + d_byte] =
                            (img[dy * stride + d_byte] & ~(mask << d_bit)) | (val << d_bit);
                    }
                }
            }
            free(sub_img);
        }
    } else {
        // Non-interlaced (sequential)
        if (height > 0 && (stride + 1) > SIZE_MAX / (size_t)height)
            goto cleanup;
        size_t expected = (stride + 1) * (size_t)height;
        if ((size_t)raw->len < expected)
            goto cleanup;

        img = (uint8_t *)malloc(stride * height);
        if (!img)
            goto cleanup;

        const uint8_t *src_ptr = raw->data;
        for (uint32_t y = 0; y < height; y++) {
            uint8_t *dst_row = img + y * stride;
            const uint8_t *prev_row = (y > 0) ? img + (y - 1) * stride : NULL;
            PNG_FILTER_ROW(dst_row, src_ptr, prev_row, stride);
        }
    }

    #undef PNG_STRIDE

    // Create Pixels object and convert to our RGBA format (0xRRGGBBAA)
    pixels = pixels_alloc((int64_t)width, (int64_t)height);
    if (!pixels)
        goto cleanup;

    // Helper: downscale 16-bit big-endian sample to 8-bit with rounding.
    #define PNG_DOWN16(p) ((uint8_t)(((((uint16_t)(p)[0] << 8) | (p)[1]) + 128) >> 8))

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = img + y * stride;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r = 0, g = 0, b_ch = 0, alpha = 0xFF;

            switch (color_type) {
                case 0: { // Grayscale
                    uint8_t gray;
                    if (bit_depth == 16) {
                        gray = PNG_DOWN16(row + x * 2);
                    } else if (bit_depth == 8) {
                        gray = row[x];
                    } else {
                        // Sub-byte: unpack from packed row
                        int pixels_per_byte = 8 / bit_depth;
                        int byte_idx = x / pixels_per_byte;
                        int bit_offset = (pixels_per_byte - 1 - (x % pixels_per_byte)) * bit_depth;
                        uint8_t mask = (uint8_t)((1 << bit_depth) - 1);
                        uint8_t val = (row[byte_idx] >> bit_offset) & mask;
                        // Scale to 8-bit: e.g., 4-bit 0xF -> 0xFF
                        gray = (uint8_t)(val * 255 / ((1 << bit_depth) - 1));
                    }
                    r = g = b_ch = gray;
                    if (has_trns_gray) {
                        uint8_t key = (bit_depth == 16) ? (uint8_t)(trns_gray >> 8)
                                                        : (uint8_t)trns_gray;
                        if (gray == key)
                            alpha = 0;
                    }
                    break;
                }
                case 2: { // RGB
                    if (bit_depth == 16) {
                        r = PNG_DOWN16(row + x * 6);
                        g = PNG_DOWN16(row + x * 6 + 2);
                        b_ch = PNG_DOWN16(row + x * 6 + 4);
                    } else {
                        r = row[x * 3];
                        g = row[x * 3 + 1];
                        b_ch = row[x * 3 + 2];
                    }
                    break;
                }
                case 3: { // Indexed
                    int idx;
                    if (bit_depth == 8) {
                        idx = row[x];
                    } else {
                        int pixels_per_byte = 8 / bit_depth;
                        int byte_idx = x / pixels_per_byte;
                        int bit_offset = (pixels_per_byte - 1 - (x % pixels_per_byte)) * bit_depth;
                        uint8_t mask = (uint8_t)((1 << bit_depth) - 1);
                        idx = (row[byte_idx] >> bit_offset) & mask;
                    }
                    if (idx < palette_count) {
                        r = palette[idx * 3];
                        g = palette[idx * 3 + 1];
                        b_ch = palette[idx * 3 + 2];
                    }
                    alpha = (idx < trns_count) ? trns_alpha[idx] : 0xFF;
                    break;
                }
                case 4: { // Grayscale + Alpha
                    if (bit_depth == 16) {
                        r = g = b_ch = PNG_DOWN16(row + x * 4);
                        alpha = PNG_DOWN16(row + x * 4 + 2);
                    } else {
                        r = g = b_ch = row[x * 2];
                        alpha = row[x * 2 + 1];
                    }
                    break;
                }
                case 6: { // RGBA
                    if (bit_depth == 16) {
                        r = PNG_DOWN16(row + x * 8);
                        g = PNG_DOWN16(row + x * 8 + 2);
                        b_ch = PNG_DOWN16(row + x * 8 + 4);
                        alpha = PNG_DOWN16(row + x * 8 + 6);
                    } else {
                        r = row[x * 4];
                        g = row[x * 4 + 1];
                        b_ch = row[x * 4 + 2];
                        alpha = row[x * 4 + 3];
                    }
                    break;
                }
                default:
                    break;
            }

            pixels->data[y * width + x] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b_ch << 8) | alpha;
        }
    }

cleanup:
    free(img);
    if (raw_bytes) {
        rt_obj_release_check0(raw_bytes);
        rt_obj_free(raw_bytes);
    }
    if (comp_bytes) {
        rt_obj_release_check0(comp_bytes);
        rt_obj_free(comp_bytes);
    }
    return pixels;
}

int64_t rt_pixels_save_png(void *pixels_ptr, void *path) {
    if (!pixels_ptr || !path)
        return 0;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels_ptr;
    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath || p->width <= 0 || p->height <= 0)
        return 0;

    uint32_t w = (uint32_t)p->width;
    uint32_t h = (uint32_t)p->height;
    size_t stride = (size_t)w * 4; // RGBA

    // Build raw PNG scanline data with filter byte.
    // First row uses filter=0 (None); subsequent rows use filter=1 (Sub)
    // which encodes differences from the left neighbor for better compression.
    size_t raw_len = (stride + 1) * h;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw)
        return 0;

    for (uint32_t y = 0; y < h; y++) {
        uint8_t *dst = raw + y * (stride + 1) + 1;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t pixel = p->data[y * w + x];
            dst[x * 4 + 0] = (uint8_t)((pixel >> 24) & 0xFF); // R
            dst[x * 4 + 1] = (uint8_t)((pixel >> 16) & 0xFF); // G
            dst[x * 4 + 2] = (uint8_t)((pixel >> 8) & 0xFF);  // B
            dst[x * 4 + 3] = (uint8_t)(pixel & 0xFF);         // A
        }
        if (y == 0) {
            raw[y * (stride + 1)] = 0; // Filter: None (no left neighbor for first row)
        } else {
            raw[y * (stride + 1)] = 1; // Filter: Sub
            for (int32_t i = stride - 1; i >= 4; i--)
                dst[i] -= dst[i - 4]; // Sub: each byte minus byte at same position 4 bytes left
        }
    }

    // Compress the raw data using DEFLATE.
    // All error paths after raw_bytes/comp_bytes allocation must go through
    // cleanup to release these GC-managed objects (refcount=1).
    void *raw_bytes = rt_bytes_new((int64_t)raw_len);
    if (!raw_bytes) {
        free(raw);
        return 0;
    }
    {
        typedef struct {
            int64_t len;
            uint8_t *data;
        } bytes_t;

        bytes_t *b = (bytes_t *)raw_bytes;
        memcpy(b->data, raw, raw_len);
    }
    free(raw);

    void *comp_bytes = NULL;
    uint8_t *zlib_data = NULL;
    FILE *out = NULL;
    int64_t result = 0;

    comp_bytes = rt_compress_deflate(raw_bytes);
    if (!comp_bytes)
        goto save_cleanup;

    typedef struct {
        int64_t len;
        uint8_t *data;
    } bytes_t;

    bytes_t *comp = (bytes_t *)comp_bytes;

    // Build zlib stream: 2-byte header + deflate data + 4-byte adler32
    // Zlib header: CMF=0x78 (deflate, window=32K), FLG=0x01 (no dict, check=1)
    size_t zlib_len = 2 + (size_t)comp->len + 4;
    zlib_data = (uint8_t *)malloc(zlib_len);
    if (!zlib_data)
        goto save_cleanup;

    zlib_data[0] = 0x78; // CMF
    zlib_data[1] = 0x01; // FLG
    memcpy(zlib_data + 2, comp->data, (size_t)comp->len);

    // Compute Adler-32 of the raw (uncompressed) data
    {
        bytes_t *raw_b = (bytes_t *)raw_bytes;
        uint32_t a = 1, b_v = 0;
        for (int64_t i = 0; i < raw_b->len; i++) {
            a = (a + raw_b->data[i]) % 65521;
            b_v = (b_v + a) % 65521;
        }
        uint32_t adler = (b_v << 16) | a;
        zlib_data[2 + comp->len + 0] = (uint8_t)((adler >> 24) & 0xFF);
        zlib_data[2 + comp->len + 1] = (uint8_t)((adler >> 16) & 0xFF);
        zlib_data[2 + comp->len + 2] = (uint8_t)((adler >> 8) & 0xFF);
        zlib_data[2 + comp->len + 3] = (uint8_t)(adler & 0xFF);
    }

    out = fopen(filepath, "wb");
    if (!out)
        goto save_cleanup;

    // Track write success — any fwrite failure produces a corrupt PNG.
    int write_ok = 1;

    // Write PNG signature
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (fwrite(sig, 1, 8, out) != 8)
        write_ok = 0;

    // Write IHDR chunk
    if (write_ok) {
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
        if (fwrite(len_buf, 1, 4, out) != 4)
            write_ok = 0;

        uint8_t type_data[4 + 13];
        memcpy(type_data, "IHDR", 4);
        memcpy(type_data + 4, ihdr, 13);
        if (write_ok && fwrite(type_data, 1, 17, out) != 17)
            write_ok = 0;

        uint32_t chunk_crc = rt_crc32_compute(type_data, 17);
        uint8_t crc_buf[4] = {(uint8_t)(chunk_crc >> 24),
                              (uint8_t)(chunk_crc >> 16),
                              (uint8_t)(chunk_crc >> 8),
                              (uint8_t)chunk_crc};
        if (write_ok && fwrite(crc_buf, 1, 4, out) != 4)
            write_ok = 0;
    }

    // Write IDAT chunk
    if (write_ok) {
        uint8_t len_buf[4] = {(uint8_t)(zlib_len >> 24),
                              (uint8_t)(zlib_len >> 16),
                              (uint8_t)(zlib_len >> 8),
                              (uint8_t)zlib_len};
        if (fwrite(len_buf, 1, 4, out) != 4)
            write_ok = 0;

        size_t td_len = 4 + zlib_len;
        uint8_t *type_data = (uint8_t *)malloc(td_len);
        if (!type_data)
            goto save_cleanup;
        memcpy(type_data, "IDAT", 4);
        memcpy(type_data + 4, zlib_data, zlib_len);
        if (write_ok && fwrite(type_data, 1, td_len, out) != td_len)
            write_ok = 0;

        uint32_t chunk_crc = rt_crc32_compute(type_data, td_len);
        uint8_t crc_buf[4] = {(uint8_t)(chunk_crc >> 24),
                              (uint8_t)(chunk_crc >> 16),
                              (uint8_t)(chunk_crc >> 8),
                              (uint8_t)chunk_crc};
        if (write_ok && fwrite(crc_buf, 1, 4, out) != 4)
            write_ok = 0;
        free(type_data);
    }

    // Write IEND chunk
    if (write_ok) {
        uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
        if (fwrite(iend, 1, 12, out) != 12)
            write_ok = 0;
    }

    result = write_ok;

save_cleanup:
    free(zlib_data);
    if (out) {
        fflush(out);
        fclose(out);
        // Remove corrupt PNG if write failed partway through
        if (!result)
            remove(filepath);
    }
    if (comp_bytes) {
        rt_obj_release_check0(comp_bytes);
        rt_obj_free(comp_bytes);
    }
    if (raw_bytes) {
        rt_obj_release_check0(raw_bytes);
        rt_obj_free(raw_bytes);
    }
    return result;
}

//=============================================================================
// JPEG Decoder (Baseline DCT, Huffman-coded)
//=============================================================================

// JPEG marker constants
#define JPEG_SOI  0xFFD8
#define JPEG_EOI  0xFFD9
#define JPEG_SOF0 0xFFC0 // Baseline DCT
#define JPEG_DHT  0xFFC4
#define JPEG_DQT  0xFFDB
#define JPEG_SOS  0xFFDA
#define JPEG_DRI  0xFFDD
#define JPEG_RST0 0xFFD0

// Zigzag order for 8x8 block
static const uint8_t jpeg_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

// Huffman table (max 16-bit codes)
typedef struct {
    uint8_t bits[17];     // bits[i] = number of codes of length i (1..16)
    uint8_t huffval[256]; // symbol values
    // Derived tables for fast decode:
    int maxcode[18];      // max code value + 1 for each length (-1 if none)
    int valptr[17];       // index into huffval for first code of each length
    int mincode[17];      // minimum code for each length
} jpeg_huff_t;

// JPEG decoder context
typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;

    // Image properties
    uint16_t width, height;
    uint8_t num_components;
    uint8_t comp_id[4];
    uint8_t comp_h_samp[4]; // horizontal sampling factor
    uint8_t comp_v_samp[4]; // vertical sampling factor
    uint8_t comp_qt[4];     // quantization table index

    // Quantization tables (up to 4)
    int16_t qt[4][64];
    int qt_valid[4];

    // Huffman tables (DC: 0-1, AC: 2-3)
    jpeg_huff_t huff[4];
    int huff_valid[4];

    // SOS component mapping
    uint8_t scan_comp_count;
    uint8_t scan_comp_idx[4];   // index into comp_* arrays
    uint8_t scan_dc_table[4];
    uint8_t scan_ac_table[4];

    // Restart interval
    uint16_t restart_interval;

    // Bitstream reader state
    uint32_t bitbuf;
    int bits_left;

    // DC prediction per component
    int16_t dc_pred[4];
} jpeg_ctx_t;

static int jpeg_read_u8(jpeg_ctx_t *ctx) {
    if (ctx->pos >= ctx->len)
        return -1;
    return ctx->data[ctx->pos++];
}

static int jpeg_read_u16(jpeg_ctx_t *ctx) {
    if (ctx->pos + 2 > ctx->len)
        return -1;
    int val = (ctx->data[ctx->pos] << 8) | ctx->data[ctx->pos + 1];
    ctx->pos += 2;
    return val;
}

// Bitstream reader: reads next byte, handling 0xFF00 byte-stuffing
static int jpeg_next_byte(jpeg_ctx_t *ctx) {
    if (ctx->pos >= ctx->len)
        return -1;
    uint8_t b = ctx->data[ctx->pos++];
    if (b == 0xFF) {
        if (ctx->pos >= ctx->len)
            return -1;
        uint8_t next = ctx->data[ctx->pos];
        if (next == 0x00) {
            ctx->pos++; // skip stuffed zero
        } else if (next >= 0xD0 && next <= 0xD7) {
            // RST marker — skip it and return next data byte
            ctx->pos++;
            return jpeg_next_byte(ctx);
        } else {
            return -1; // unexpected marker
        }
    }
    return b;
}

static int jpeg_get_bits(jpeg_ctx_t *ctx, int count) {
    while (ctx->bits_left < count) {
        int b = jpeg_next_byte(ctx);
        if (b < 0)
            return -1;
        ctx->bitbuf = (ctx->bitbuf << 8) | (uint32_t)b;
        ctx->bits_left += 8;
    }
    ctx->bits_left -= count;
    return (int)((ctx->bitbuf >> ctx->bits_left) & ((1u << count) - 1));
}

// Build derived Huffman tables for fast decoding
static void jpeg_build_huff(jpeg_huff_t *h) {
    int code = 0;
    int si = 0;
    for (int i = 1; i <= 16; i++) {
        h->mincode[i] = code;
        h->valptr[i] = si;
        if (h->bits[i] == 0) {
            h->maxcode[i] = -1;
        } else {
            h->maxcode[i] = code + h->bits[i] - 1;
            si += h->bits[i];
        }
        code = (code + h->bits[i]) << 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
}

// Decode one Huffman symbol
static int jpeg_huff_decode(jpeg_ctx_t *ctx, jpeg_huff_t *h) {
    int code = 0;
    for (int i = 1; i <= 16; i++) {
        int bit = jpeg_get_bits(ctx, 1);
        if (bit < 0)
            return -1;
        code = (code << 1) | bit;
        if (h->maxcode[i] >= 0 && code <= h->maxcode[i]) {
            int idx = h->valptr[i] + (code - h->mincode[i]);
            return h->huffval[idx];
        }
    }
    return -1; // invalid code
}

// Extend a partial value to a signed coefficient
static int jpeg_extend(int val, int bits) {
    if (bits == 0)
        return 0;
    int vt = 1 << (bits - 1);
    if (val < vt)
        val += (-1 << bits) + 1;
    return val;
}

// Decode one 8x8 block of DCT coefficients
static int jpeg_decode_block(jpeg_ctx_t *ctx, int16_t block[64],
                             jpeg_huff_t *dc_ht, jpeg_huff_t *ac_ht,
                             int16_t *dc_pred, const int16_t qt[64]) {
    memset(block, 0, sizeof(int16_t) * 64);

    // DC coefficient
    int s = jpeg_huff_decode(ctx, dc_ht);
    if (s < 0)
        return -1;
    int dc_val = 0;
    if (s > 0) {
        dc_val = jpeg_get_bits(ctx, s);
        if (dc_val < 0)
            return -1;
        dc_val = jpeg_extend(dc_val, s);
    }
    *dc_pred += (int16_t)dc_val;
    block[0] = *dc_pred * qt[0];

    // AC coefficients
    for (int k = 1; k < 64; k++) {
        int rs = jpeg_huff_decode(ctx, ac_ht);
        if (rs < 0)
            return -1;
        int rrrr = (rs >> 4) & 0x0F; // zero run length
        int ssss = rs & 0x0F;        // coefficient size

        if (ssss == 0) {
            if (rrrr == 0)
                break; // EOB
            if (rrrr == 15) {
                k += 15; // ZRL: skip 16 zeros
                continue;
            }
            break;
        }

        k += rrrr;
        if (k >= 64)
            return -1;

        int ac_val = jpeg_get_bits(ctx, ssss);
        if (ac_val < 0)
            return -1;
        ac_val = jpeg_extend(ac_val, ssss);
        block[jpeg_zigzag[k]] = (int16_t)(ac_val * qt[jpeg_zigzag[k]]);
    }

    return 0;
}

// AAN (Arai, Agui, Nakajima) integer IDCT
// Fixed-point: 12-bit fractional precision
#define JPEG_FIX_1  4096
#define JPEG_FIX(x) ((int32_t)((x) * 4096.0 + 0.5))
#define JPEG_DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

static void jpeg_idct_row(int32_t *row) {
    int32_t x0 = row[0], x1 = row[1], x2 = row[2], x3 = row[3];
    int32_t x4 = row[4], x5 = row[5], x6 = row[6], x7 = row[7];

    // Even part
    int32_t s0 = x0 + x4;
    int32_t s1 = x0 - x4;
    int32_t s2 = JPEG_DESCALE(x2 * JPEG_FIX(0.541196100) + x6 * JPEG_FIX(0.541196100 - 1.847759065), 12);
    int32_t s3 = JPEG_DESCALE(x2 * JPEG_FIX(0.541196100 + 0.765366865) + x6 * JPEG_FIX(0.541196100), 12);

    int32_t e0 = s0 + s3;
    int32_t e1 = s1 + s2;
    int32_t e2 = s1 - s2;
    int32_t e3 = s0 - s3;

    // Odd part
    int32_t t0 = x1 + x7;
    int32_t t1 = x5 + x3;
    int32_t t2 = x1 + x3;
    int32_t t3 = x5 + x7;
    int32_t z5 = JPEG_DESCALE((t2 - t3) * JPEG_FIX(1.175875602), 12);

    t0 = JPEG_DESCALE(t0 * JPEG_FIX(-0.899976223), 12);
    t1 = JPEG_DESCALE(t1 * JPEG_FIX(-2.562915447), 12);
    t2 = JPEG_DESCALE(t2 * JPEG_FIX(-1.961570560), 12) + z5;
    t3 = JPEG_DESCALE(t3 * JPEG_FIX(-0.390180644), 12) + z5;

    int32_t o0 = JPEG_DESCALE(x7 * JPEG_FIX(0.298631336), 12) + t0 + t2;
    int32_t o1 = JPEG_DESCALE(x5 * JPEG_FIX(2.053119869), 12) + t1 + t3;
    int32_t o2 = JPEG_DESCALE(x3 * JPEG_FIX(3.072711026), 12) + t1 + t2;
    int32_t o3 = JPEG_DESCALE(x1 * JPEG_FIX(1.501321110), 12) + t0 + t3;

    row[0] = e0 + o3;
    row[1] = e1 + o2;
    row[2] = e2 + o1;
    row[3] = e3 + o0;
    row[4] = e3 - o0;
    row[5] = e2 - o1;
    row[6] = e1 - o2;
    row[7] = e0 - o3;
}

static void jpeg_idct_col(int32_t *workspace, int col) {
    int32_t x0 = workspace[col + 0 * 8], x1 = workspace[col + 1 * 8];
    int32_t x2 = workspace[col + 2 * 8], x3 = workspace[col + 3 * 8];
    int32_t x4 = workspace[col + 4 * 8], x5 = workspace[col + 5 * 8];
    int32_t x6 = workspace[col + 6 * 8], x7 = workspace[col + 7 * 8];

    int32_t s0 = x0 + x4;
    int32_t s1 = x0 - x4;
    int32_t s2 = JPEG_DESCALE(x2 * JPEG_FIX(0.541196100) + x6 * JPEG_FIX(0.541196100 - 1.847759065), 12);
    int32_t s3 = JPEG_DESCALE(x2 * JPEG_FIX(0.541196100 + 0.765366865) + x6 * JPEG_FIX(0.541196100), 12);

    int32_t e0 = s0 + s3;
    int32_t e1 = s1 + s2;
    int32_t e2 = s1 - s2;
    int32_t e3 = s0 - s3;

    int32_t t0 = x1 + x7;
    int32_t t1 = x5 + x3;
    int32_t t2 = x1 + x3;
    int32_t t3 = x5 + x7;
    int32_t z5 = JPEG_DESCALE((t2 - t3) * JPEG_FIX(1.175875602), 12);

    t0 = JPEG_DESCALE(t0 * JPEG_FIX(-0.899976223), 12);
    t1 = JPEG_DESCALE(t1 * JPEG_FIX(-2.562915447), 12);
    t2 = JPEG_DESCALE(t2 * JPEG_FIX(-1.961570560), 12) + z5;
    t3 = JPEG_DESCALE(t3 * JPEG_FIX(-0.390180644), 12) + z5;

    int32_t o0 = JPEG_DESCALE(x7 * JPEG_FIX(0.298631336), 12) + t0 + t2;
    int32_t o1 = JPEG_DESCALE(x5 * JPEG_FIX(2.053119869), 12) + t1 + t3;
    int32_t o2 = JPEG_DESCALE(x3 * JPEG_FIX(3.072711026), 12) + t1 + t2;
    int32_t o3 = JPEG_DESCALE(x1 * JPEG_FIX(1.501321110), 12) + t0 + t3;

    // Descale with rounding and shift for final output
    workspace[col + 0 * 8] = JPEG_DESCALE(e0 + o3, 5);
    workspace[col + 1 * 8] = JPEG_DESCALE(e1 + o2, 5);
    workspace[col + 2 * 8] = JPEG_DESCALE(e2 + o1, 5);
    workspace[col + 3 * 8] = JPEG_DESCALE(e3 + o0, 5);
    workspace[col + 4 * 8] = JPEG_DESCALE(e3 - o0, 5);
    workspace[col + 5 * 8] = JPEG_DESCALE(e2 - o1, 5);
    workspace[col + 6 * 8] = JPEG_DESCALE(e1 - o2, 5);
    workspace[col + 7 * 8] = JPEG_DESCALE(e0 - o3, 5);
}

static void jpeg_idct_block(int16_t block[64], uint8_t out[64]) {
    int32_t workspace[64];
    for (int i = 0; i < 64; i++)
        workspace[i] = (int32_t)block[i];

    // IDCT on rows
    for (int i = 0; i < 8; i++)
        jpeg_idct_row(workspace + i * 8);

    // IDCT on columns
    for (int i = 0; i < 8; i++)
        jpeg_idct_col(workspace, i);

    // Level shift (+128) and clamp to [0, 255]
    for (int i = 0; i < 64; i++) {
        int val = (int)workspace[i] + 128;
        if (val < 0)
            val = 0;
        if (val > 255)
            val = 255;
        out[i] = (uint8_t)val;
    }
}

// Clamp int to [0, 255]
static uint8_t jpeg_clamp(int val) {
    if (val < 0)
        return 0;
    if (val > 255)
        return 255;
    return (uint8_t)val;
}

/// @brief Decode a JPEG image from a memory buffer.
/// @param data Pointer to JPEG data (must start with 0xFFD8 SOI marker).
/// @param len Length of data in bytes.
/// @return New Pixels object, or NULL on failure. Caller does NOT free data.
void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len) {
    if (!data || len < 2 || data[0] != 0xFF || data[1] != 0xD8)
        return NULL;

    /* jpeg_ctx_t.data is uint8_t* but we only read from it. Use memcpy
     * to reinterpret the pointer without triggering -Wcast-qual. */
    uint8_t *file_data;
    memcpy(&file_data, &data, sizeof(file_data));

    jpeg_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.data = file_data;
    ctx.len = len;
    ctx.pos = 2; // past SOI

    rt_pixels_impl *pixels = NULL;
    uint8_t **comp_data = NULL;
    int exif_orientation = 1; // default: no rotation

    // Parse markers
    while (ctx.pos + 1 < ctx.len) {
        int b1 = jpeg_read_u8(&ctx);
        if (b1 != 0xFF)
            break;

        // Skip padding 0xFF bytes
        int marker;
        do {
            marker = jpeg_read_u8(&ctx);
        } while (marker == 0xFF && ctx.pos < ctx.len);

        if (marker < 0)
            break;

        uint16_t mk = (uint16_t)(0xFF00 | marker);

        if (mk == JPEG_EOI)
            break;

        // Markers without length
        if (mk >= JPEG_RST0 && mk <= JPEG_RST0 + 7)
            continue;

        // Read marker length
        int seg_len = jpeg_read_u16(&ctx);
        if (seg_len < 2)
            break;
        size_t data_len = (size_t)(seg_len - 2);
        size_t seg_start = ctx.pos;

        switch (mk) {
            case JPEG_DQT: {
                // Parse quantization table(s)
                while (ctx.pos < seg_start + data_len) {
                    int info = jpeg_read_u8(&ctx);
                    if (info < 0)
                        goto jpeg_fail;
                    int precision = (info >> 4) & 0x0F; // 0=8bit, 1=16bit
                    int table_id = info & 0x0F;
                    if (table_id > 3)
                        goto jpeg_fail;
                    for (int i = 0; i < 64; i++) {
                        if (precision == 0) {
                            int v = jpeg_read_u8(&ctx);
                            if (v < 0)
                                goto jpeg_fail;
                            ctx.qt[table_id][jpeg_zigzag[i]] = (int16_t)v;
                        } else {
                            int v = jpeg_read_u16(&ctx);
                            if (v < 0)
                                goto jpeg_fail;
                            ctx.qt[table_id][jpeg_zigzag[i]] = (int16_t)v;
                        }
                    }
                    ctx.qt_valid[table_id] = 1;
                }
                break;
            }
            case JPEG_DHT: {
                // Parse Huffman table(s)
                while (ctx.pos < seg_start + data_len) {
                    int info = jpeg_read_u8(&ctx);
                    if (info < 0)
                        goto jpeg_fail;
                    int table_class = (info >> 4) & 0x0F; // 0=DC, 1=AC
                    int table_id = info & 0x0F;
                    if (table_id > 1)
                        goto jpeg_fail;
                    int idx = table_class * 2 + table_id; // DC0, DC1, AC0, AC1
                    jpeg_huff_t *ht = &ctx.huff[idx];
                    int total = 0;
                    ht->bits[0] = 0;
                    for (int i = 1; i <= 16; i++) {
                        int n = jpeg_read_u8(&ctx);
                        if (n < 0)
                            goto jpeg_fail;
                        ht->bits[i] = (uint8_t)n;
                        total += n;
                    }
                    if (total > 256)
                        goto jpeg_fail;
                    for (int i = 0; i < total; i++) {
                        int v = jpeg_read_u8(&ctx);
                        if (v < 0)
                            goto jpeg_fail;
                        ht->huffval[i] = (uint8_t)v;
                    }
                    jpeg_build_huff(ht);
                    ctx.huff_valid[idx] = 1;
                }
                break;
            }
            case JPEG_SOF0: {
                // Baseline DCT
                int prec = jpeg_read_u8(&ctx);
                if (prec != 8)
                    goto jpeg_fail; // Only 8-bit precision
                int h = jpeg_read_u16(&ctx);
                int w = jpeg_read_u16(&ctx);
                if (w <= 0 || h <= 0 || w > 32768 || h > 32768)
                    goto jpeg_fail;
                ctx.width = (uint16_t)w;
                ctx.height = (uint16_t)h;
                int nf = jpeg_read_u8(&ctx);
                if (nf < 1 || nf > 4)
                    goto jpeg_fail;
                ctx.num_components = (uint8_t)nf;
                for (int i = 0; i < nf; i++) {
                    ctx.comp_id[i] = (uint8_t)jpeg_read_u8(&ctx);
                    int samp = jpeg_read_u8(&ctx);
                    ctx.comp_h_samp[i] = (uint8_t)((samp >> 4) & 0x0F);
                    ctx.comp_v_samp[i] = (uint8_t)(samp & 0x0F);
                    ctx.comp_qt[i] = (uint8_t)jpeg_read_u8(&ctx);
                }
                break;
            }
            case JPEG_DRI: {
                int ri = jpeg_read_u16(&ctx);
                if (ri >= 0)
                    ctx.restart_interval = (uint16_t)ri;
                break;
            }
            case JPEG_SOS: {
                // Start of Scan — decode the entropy-coded data
                int ns = jpeg_read_u8(&ctx);
                if (ns < 1 || ns > 4)
                    goto jpeg_fail;
                ctx.scan_comp_count = (uint8_t)ns;
                for (int i = 0; i < ns; i++) {
                    int cs = jpeg_read_u8(&ctx);
                    int td_ta = jpeg_read_u8(&ctx);
                    if (cs < 0 || td_ta < 0)
                        goto jpeg_fail;
                    // Find component index
                    int ci = -1;
                    for (int j = 0; j < ctx.num_components; j++) {
                        if (ctx.comp_id[j] == (uint8_t)cs) {
                            ci = j;
                            break;
                        }
                    }
                    if (ci < 0)
                        goto jpeg_fail;
                    ctx.scan_comp_idx[i] = (uint8_t)ci;
                    ctx.scan_dc_table[i] = (uint8_t)((td_ta >> 4) & 0x0F);
                    ctx.scan_ac_table[i] = (uint8_t)(td_ta & 0x0F);
                }
                // Skip Ss, Se, Ah/Al (spectral selection — always 0,63,0 for baseline)
                jpeg_read_u8(&ctx);
                jpeg_read_u8(&ctx);
                jpeg_read_u8(&ctx);

                // Now at the start of entropy-coded data
                ctx.bitbuf = 0;
                ctx.bits_left = 0;
                memset(ctx.dc_pred, 0, sizeof(ctx.dc_pred));

                // Determine MCU layout
                int max_h = 1, max_v = 1;
                for (int i = 0; i < ctx.num_components; i++) {
                    if (ctx.comp_h_samp[i] > max_h)
                        max_h = ctx.comp_h_samp[i];
                    if (ctx.comp_v_samp[i] > max_v)
                        max_v = ctx.comp_v_samp[i];
                }

                int mcu_w = max_h * 8;
                int mcu_h = max_v * 8;
                int mcus_x = (ctx.width + mcu_w - 1) / mcu_w;
                int mcus_y = (ctx.height + mcu_h - 1) / mcu_h;

                // Allocate component buffers (full-resolution for each component's
                // sampled size)
                comp_data = (uint8_t **)calloc((size_t)ctx.num_components, sizeof(uint8_t *));
                if (!comp_data)
                    goto jpeg_fail;
                for (int i = 0; i < ctx.num_components; i++) {
                    int cw = mcus_x * ctx.comp_h_samp[i] * 8;
                    int ch = mcus_y * ctx.comp_v_samp[i] * 8;
                    comp_data[i] = (uint8_t *)calloc((size_t)cw * (size_t)ch, 1);
                    if (!comp_data[i])
                        goto jpeg_fail;
                }

                // Decode MCUs
                int16_t block[64];
                uint8_t idct_out[64];
                int restart_count = 0;

                for (int mcu_y = 0; mcu_y < mcus_y; mcu_y++) {
                    for (int mcu_x = 0; mcu_x < mcus_x; mcu_x++) {
                        // Handle restart markers
                        if (ctx.restart_interval > 0 && restart_count > 0 &&
                            (restart_count % ctx.restart_interval) == 0) {
                            // Align to byte boundary
                            ctx.bits_left = 0;
                            ctx.bitbuf = 0;
                            memset(ctx.dc_pred, 0, sizeof(ctx.dc_pred));
                            // Skip any RST marker bytes (handled in jpeg_next_byte)
                        }
                        restart_count++;

                        // For each component in the scan
                        for (int si = 0; si < ctx.scan_comp_count; si++) {
                            int ci = ctx.scan_comp_idx[si];
                            int h_samp = ctx.comp_h_samp[ci];
                            int v_samp = ctx.comp_v_samp[ci];
                            int qt_idx = ctx.comp_qt[ci];
                            int dc_idx = ctx.scan_dc_table[si];
                            int ac_idx = ctx.scan_ac_table[si] + 2;

                            if (!ctx.qt_valid[qt_idx] || !ctx.huff_valid[dc_idx] ||
                                !ctx.huff_valid[ac_idx])
                                goto jpeg_fail;

                            // Decode each block in this component's MCU contribution
                            for (int bv = 0; bv < v_samp; bv++) {
                                for (int bh = 0; bh < h_samp; bh++) {
                                    if (jpeg_decode_block(&ctx, block, &ctx.huff[dc_idx],
                                                          &ctx.huff[ac_idx], &ctx.dc_pred[ci],
                                                          ctx.qt[qt_idx]) != 0)
                                        goto jpeg_fail;

                                    jpeg_idct_block(block, idct_out);

                                    // Place decoded 8x8 block into component buffer
                                    int comp_stride = mcus_x * h_samp * 8;
                                    int bx = (mcu_x * h_samp + bh) * 8;
                                    int by = (mcu_y * v_samp + bv) * 8;
                                    for (int yy = 0; yy < 8; yy++) {
                                        for (int xx = 0; xx < 8; xx++) {
                                            comp_data[ci][(by + yy) * comp_stride + (bx + xx)] =
                                                idct_out[yy * 8 + xx];
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Convert component buffers to RGBA pixels
                pixels = pixels_alloc((int64_t)ctx.width, (int64_t)ctx.height);
                if (!pixels)
                    goto jpeg_fail;

                if (ctx.num_components == 1) {
                    // Grayscale
                    int comp_stride = mcus_x * ctx.comp_h_samp[0] * 8;
                    for (int y = 0; y < ctx.height; y++) {
                        for (int x = 0; x < ctx.width; x++) {
                            uint8_t gray = comp_data[0][y * comp_stride + x];
                            pixels->data[y * ctx.width + x] =
                                ((uint32_t)gray << 24) | ((uint32_t)gray << 16) |
                                ((uint32_t)gray << 8) | 0xFF;
                        }
                    }
                } else if (ctx.num_components >= 3) {
                    // YCbCr -> RGB with chroma upsampling
                    int y_stride = mcus_x * ctx.comp_h_samp[0] * 8;
                    int cb_stride = mcus_x * ctx.comp_h_samp[1] * 8;
                    int cr_stride = mcus_x * ctx.comp_h_samp[2] * 8;
                    int h_ratio = max_h / ctx.comp_h_samp[1]; // chroma upsample factor
                    int v_ratio = max_v / ctx.comp_v_samp[1];

                    for (int y = 0; y < ctx.height; y++) {
                        for (int x = 0; x < ctx.width; x++) {
                            int yy_val = comp_data[0][y * y_stride + x];
                            int cb_x = x / h_ratio;
                            int cb_y = y / v_ratio;
                            int cb_val = comp_data[1][cb_y * cb_stride + cb_x] - 128;
                            int cr_val = comp_data[2][cb_y * cr_stride + cb_x] - 128;

                            // YCbCr -> RGB (ITU-R BT.601)
                            int r = yy_val + ((cr_val * 359) >> 8);
                            int g = yy_val - ((cb_val * 88 + cr_val * 183) >> 8);
                            int b = yy_val + ((cb_val * 454) >> 8);

                            pixels->data[y * ctx.width + x] =
                                ((uint32_t)jpeg_clamp(r) << 24) |
                                ((uint32_t)jpeg_clamp(g) << 16) |
                                ((uint32_t)jpeg_clamp(b) << 8) | 0xFF;
                        }
                    }
                }

                // Finished SOS — skip to after entropy data (already consumed via bitstream)
                break;
            }
            default:
                // APP1 (0xFFE1): EXIF data — extract orientation tag
                if (mk == 0xFFE1 && data_len >= 14) {
                    const uint8_t *exif = ctx.data + seg_start;
                    if (memcmp(exif, "Exif\0\0", 6) == 0) {
                        const uint8_t *tiff = exif + 6;
                        size_t tiff_len = data_len - 6;
                        if (tiff_len >= 8) {
                            int big = (tiff[0] == 'M' && tiff[1] == 'M');
                            uint32_t ifd_off = big
                                ? ((uint32_t)tiff[4] << 24 | (uint32_t)tiff[5] << 16 |
                                   (uint32_t)tiff[6] << 8 | tiff[7])
                                : ((uint32_t)tiff[7] << 24 | (uint32_t)tiff[6] << 16 |
                                   (uint32_t)tiff[5] << 8 | tiff[4]);
                            if (ifd_off + 2 <= tiff_len) {
                                uint16_t count = big
                                    ? (uint16_t)((tiff[ifd_off] << 8) | tiff[ifd_off + 1])
                                    : (uint16_t)(tiff[ifd_off] | (tiff[ifd_off + 1] << 8));
                                for (int ei = 0; ei < count; ei++) {
                                    size_t entry = ifd_off + 2 + (size_t)ei * 12;
                                    if (entry + 12 > tiff_len) break;
                                    uint16_t tag = big
                                        ? (uint16_t)((tiff[entry] << 8) | tiff[entry + 1])
                                        : (uint16_t)(tiff[entry] | (tiff[entry + 1] << 8));
                                    if (tag == 0x0112) { // Orientation
                                        exif_orientation = big
                                            ? (int)((tiff[entry + 8] << 8) | tiff[entry + 9])
                                            : (int)(tiff[entry + 8] | (tiff[entry + 9] << 8));
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                ctx.pos = seg_start + data_len;
                break;
        }
    }

    // Apply EXIF orientation transform
    if (pixels && exif_orientation > 1 && exif_orientation <= 8) {
        rt_pixels_impl *rotated = NULL;
        switch (exif_orientation) {
            case 2: rotated = (rt_pixels_impl *)rt_pixels_flip_h(pixels); break;
            case 3: rotated = (rt_pixels_impl *)rt_pixels_rotate_180(pixels); break;
            case 4: rotated = (rt_pixels_impl *)rt_pixels_flip_v(pixels); break;
            case 5: {
                void *t = rt_pixels_rotate_cw(pixels);
                if (t) { rotated = (rt_pixels_impl *)rt_pixels_flip_h(t); rt_obj_release_check0(t); rt_obj_free(t); }
                break;
            }
            case 6: rotated = (rt_pixels_impl *)rt_pixels_rotate_cw(pixels); break;
            case 7: {
                void *t = rt_pixels_rotate_ccw(pixels);
                if (t) { rotated = (rt_pixels_impl *)rt_pixels_flip_h(t); rt_obj_release_check0(t); rt_obj_free(t); }
                break;
            }
            case 8: rotated = (rt_pixels_impl *)rt_pixels_rotate_ccw(pixels); break;
            default: break;
        }
        if (rotated) {
            // Original pixels will be GC'd; return the rotated version
            pixels = rotated;
        }
    }

    // Cleanup
    if (comp_data) {
        for (int i = 0; i < ctx.num_components; i++)
            free(comp_data[i]);
        free(comp_data);
    }
    return pixels;

jpeg_fail:
    if (comp_data) {
        for (int i = 0; i < ctx.num_components; i++)
            free(comp_data[i]);
        free(comp_data);
    }
    return NULL;
}

/// @brief Load a JPEG image from a file path (wrapper around rt_jpeg_decode_buffer).
void *rt_pixels_load_jpeg(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_len <= 0 || file_len > 256 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_len);
    if (!file_data) {
        fclose(f);
        return NULL;
    }
    if (fread(file_data, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    void *result = rt_jpeg_decode_buffer(file_data, (size_t)file_len);
    free(file_data);
    return result;
}

//=============================================================================
// GIF Loading (first frame convenience wrapper)
//=============================================================================

#include "rt_gif.h"

void *rt_pixels_load_gif(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    gif_frame_t *frames = NULL;
    int frame_count = 0, w = 0, h = 0;
    if (gif_decode_file(filepath, &frames, &frame_count, &w, &h) <= 0)
        return NULL;

    // Take ownership of the first frame's Pixels, free the rest
    void *result = frames[0].pixels;
    for (int i = 1; i < frame_count; i++) {
        // Release extra frames (GC-managed)
        if (frames[i].pixels) {
            if (rt_obj_release_check0(frames[i].pixels))
                rt_obj_free(frames[i].pixels);
        }
    }
    free(frames);
    return result;
}

//=============================================================================
// Auto-detect loader
//=============================================================================

void *rt_pixels_load(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return NULL;

    FILE *af = fopen(filepath, "rb");
    if (!af)
        return NULL;
    uint8_t hdr[8];
    size_t n = fread(hdr, 1, 8, af);
    fclose(af);

    if (n >= 8 && hdr[0] == 137 && hdr[1] == 'P' && hdr[2] == 'N' && hdr[3] == 'G')
        return rt_pixels_load_png(path);
    if (n >= 2 && hdr[0] == 0xFF && hdr[1] == 0xD8)
        return rt_pixels_load_jpeg(path);
    if (n >= 2 && hdr[0] == 'B' && hdr[1] == 'M')
        return rt_pixels_load_bmp(path);
    if (n >= 3 && hdr[0] == 'G' && hdr[1] == 'I' && hdr[2] == 'F')
        return rt_pixels_load_gif(path);
    return NULL;
}
