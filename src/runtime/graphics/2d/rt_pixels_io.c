//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_pixels_io.c
// Purpose: Image I/O: BMP load/save, GIF load, and the format-dispatching rt_pixels_load. PNG and JPEG live in rt_pixels_png.c / rt_pixels_jpeg.c.
//
// Links: rt_pixels.h (public API), rt_pixels_io_internal.h (shared helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_pixels_io_internal.h"

#define BMP_MAX_PIXELS ((size_t)64u * 1024u * 1024u)

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

/// @brief Decode a 24-bit uncompressed BMP file into a Pixels object.
///
/// Supports the most common BMP variant: BITMAPINFOHEADER, 24bpp,
/// no compression. Handles both bottom-up (positive height — the
/// historical default) and top-down (negative height) row layouts.
/// Source rows are 4-byte aligned; we strip the BGR→RGBA conversion
/// in the inner loop and force alpha = 255 since 24bpp BMP has no
/// alpha channel. Caps width/height at 32768 to bound memory.
/// @return GC-managed `rt_pixels_impl*` on success, NULL on any
///         failure (file open, magic mismatch, unsupported variant,
///         short read).
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
    int success = 0;

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

    // Only support BITMAPINFOHEADER, 24-bit, uncompressed BMP.
    if (info_hdr.header_size != sizeof(bmp_info_header) || info_hdr.bit_count != 24 ||
        info_hdr.compression != 0)
        goto bmp_cleanup;

    int32_t width = info_hdr.width;
    int32_t height = info_hdr.height;
    int bottom_up = 1;

    // Handle negative height (top-down)
    if (height < 0) {
        if (height == INT32_MIN)
            goto bmp_cleanup;
        height = -height;
        bottom_up = 0;
    }

    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
        goto bmp_cleanup;
    if ((size_t)width > SIZE_MAX / (size_t)height ||
        (size_t)width * (size_t)height > BMP_MAX_PIXELS)
        goto bmp_cleanup;

    // Calculate row padding (rows must be 4-byte aligned)
    size_t row_payload = 0;
    if (!px_mul_size((size_t)width, 3, &row_payload))
        goto bmp_cleanup;
    size_t row_size = (row_payload + 3u) & ~(size_t)3u;
    size_t data_size = 0;
    if (!px_mul_size(row_size, (size_t)height, &data_size))
        goto bmp_cleanup;

    uint64_t min_data_offset = (uint64_t)sizeof(bmp_file_header) + (uint64_t)info_hdr.header_size;
    uint64_t data_offset = (uint64_t)file_hdr.data_offset;
    if (data_offset < min_data_offset || data_offset > (uint64_t)INT64_MAX)
        goto bmp_cleanup;
    if (file_hdr.file_size != 0) {
        uint64_t declared_size = (uint64_t)file_hdr.file_size;
        if (data_offset > declared_size || (uint64_t)data_size > declared_size - data_offset)
            goto bmp_cleanup;
    }
    int64_t current_pos = px_ftell(f);
    if (current_pos < 0)
        goto bmp_cleanup;
    if (px_fseek(f, 0, SEEK_END) != 0)
        goto bmp_cleanup;
    int64_t actual_size = px_ftell(f);
    if (actual_size < 0)
        goto bmp_cleanup;
    if (file_hdr.file_size != 0 && (uint64_t)file_hdr.file_size > (uint64_t)actual_size)
        goto bmp_cleanup;
    if (data_offset > (uint64_t)actual_size ||
        (uint64_t)data_size > (uint64_t)actual_size - data_offset)
        goto bmp_cleanup;
    if (px_fseek(f, current_pos, SEEK_SET) != 0)
        goto bmp_cleanup;

    // Allocate row buffer
    row_buf = (uint8_t *)malloc(row_size);
    if (!row_buf)
        goto bmp_cleanup;

    // Create pixels
    pixels = pixels_alloc(width, height);
    if (!pixels)
        goto bmp_cleanup;

    // Seek to pixel data
    if (px_fseek(f, (int64_t)file_hdr.data_offset, SEEK_SET) != 0)
        goto bmp_cleanup;

    // Read pixel data
    for (int32_t y = 0; y < height; y++) {
        if (fread(row_buf, 1, row_size, f) != row_size)
            goto bmp_cleanup;

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
    success = 1;

bmp_cleanup:
    if (!success && pixels) {
        if (rt_obj_release_check0(pixels))
            rt_obj_free(pixels);
        pixels = NULL;
    }
    free(row_buf);
    fclose(f);
    return pixels;
}

/// @brief Save a Pixels object as a 24-bit uncompressed BMP file.
///
/// Writes a standard bottom-up BMP (positive height). Refuses
/// oversized images where the resulting file would overflow the
/// 32-bit `bfSize` field. Pads each row to a 4-byte multiple.
/// @return 1 on success, 0 on any failure (open, oversized, write).
int64_t rt_pixels_save_bmp(void *pixels, void *path) {
    if (!pixels || !path)
        return 0;

    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.SaveBmp: invalid pixels");
    if (!p)
        return 0;
    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath)
        return 0;

    if (p->width <= 0 || p->height <= 0 || p->width > INT32_MAX || p->height > INT32_MAX)
        return 0;

    int32_t width = (int32_t)p->width;
    int32_t height = (int32_t)p->height;

    // Calculate row padding
    size_t row_payload = 0;
    if (!px_mul_size((size_t)width, 3, &row_payload))
        return 0;
    size_t row_size = (row_payload + 3u) & ~(size_t)3u;
    size_t padding = row_size - row_payload;

    // Calculate file size (guard against uint32 overflow for very large images)
    uint64_t data_size_u64 = (uint64_t)row_size * (uint64_t)height;
    if (data_size_u64 > (uint64_t)0xFFFFFFC9u) // UINT32_MAX - 54
        return 0;
    uint32_t data_size = (uint32_t)data_size_u64;
    uint32_t file_size = 54 + data_size; // 14 + 40 + data

    FILE *f = fopen(filepath, "wb");
    if (!f)
        return 0;
    uint8_t *row_buf = NULL;
    int result = 0;

    // Write file header
    bmp_file_header file_hdr = {
        .magic = {'B', 'M'},
        .file_size = file_size,
        .reserved1 = 0,
        .reserved2 = 0,
        .data_offset = 54,
    };
    if (fwrite(&file_hdr, sizeof(file_hdr), 1, f) != 1)
        goto bmp_save_cleanup;

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
        goto bmp_save_cleanup;

    // Allocate row buffer
    row_buf = (uint8_t *)calloc(1, row_size);
    if (!row_buf)
        goto bmp_save_cleanup;

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
        for (size_t i = 0; i < padding; i++)
            row_buf[row_payload + i] = 0;

        if (fwrite(row_buf, 1, row_size, f) != row_size) {
            goto bmp_save_cleanup;
        }
    }

    result = 1;

bmp_save_cleanup:
    free(row_buf);
    if (fflush(f) != 0)
        result = 0;
    if (fclose(f) != 0)
        result = 0;
    if (!result)
        remove(filepath);
    return result;
}

//=============================================================================
// GIF Loading (first frame convenience wrapper)
//=============================================================================

#include "rt_gif.h"

/// @brief Decode a GIF file into a Pixels object (first frame only).
///
/// LZW-decompresses the indexed-color image stream and resolves
/// each index against the Global Color Table. Always reads only
/// the first frame — animated GIFs collapse to their initial
/// snapshot. Used for sprite-sheet and texture loading; for full
/// animation use the dedicated GIF decoder elsewhere.
/// @return Pixels on success, NULL on file/format failure.
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

/// @brief Generic image loader — autodetects format from the file extension.
///
/// Dispatches to `rt_pixels_load_bmp`, `_png`, `_jpeg`, or `_gif`
/// based on the path's lowercase extension. Returns NULL for
/// unrecognised extensions or on any underlying decode failure.
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
