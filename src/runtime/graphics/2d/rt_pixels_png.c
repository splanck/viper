//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_pixels_png.c
// Purpose: PNG image decode/encode, including zlib inflate, unfilter, and RGBA32 conversion.
// Key invariants:
//   - PNG decode failures return NULL and report asset diagnostics.
//   - PNG save retains its integer success/failure contract.
// Ownership/Lifetime:
//   - Loaded Pixels objects are GC-managed and owned by the caller.
//   - Temporary file/decode buffers are released before return.
// Links: rt_pixels.h, rt_pixels_io_internal.h, rt_asset_error.h
//
//===----------------------------------------------------------------------===//

#include "rt_pixels_io_internal.h"

#include "rt_asset_error.h"
#include "rt_file_stdio.h"
#include "rt_trap.h"

#define PNG_MAX_PIXELS ((size_t)64u * 1024u * 1024u)

static int px_png_stride_checked(uint32_t width,
                                 uint8_t bit_depth,
                                 int samples_per_pixel,
                                 size_t *stride_out) {
    size_t stride = 0;
    if (samples_per_pixel <= 0)
        return 0;
    if (bit_depth < 8) {
        size_t bits = 0;
        if (!px_mul_size((size_t)width, (size_t)samples_per_pixel, &bits))
            return 0;
        if (!px_mul_size(bits, (size_t)bit_depth, &bits))
            return 0;
        if (!px_add_size(bits, 7, &bits))
            return 0;
        stride = bits / 8;
    } else {
        size_t sample_bytes = (size_t)bit_depth / 8;
        if (!px_mul_size((size_t)width, (size_t)samples_per_pixel, &stride))
            return 0;
        if (!px_mul_size(stride, sample_bytes, &stride))
            return 0;
    }
    if (stride_out)
        *stride_out = stride;
    return 1;
}

/// @brief Update a PNG/IEEE CRC32 state with one byte buffer.
/// @details The caller supplies the preconditioned CRC state, typically
///          0xFFFFFFFFu for the first chunk fragment. The returned value is
///          still preconditioned; XOR with 0xFFFFFFFFu after the final update.
///          This allows chunk type and data buffers to be checksummed without
///          concatenating them into a temporary allocation.
/// @param crc Current preconditioned CRC state.
/// @param data Buffer to process; may be NULL only when @p len is zero.
/// @param len Number of bytes in @p data.
/// @return Updated preconditioned CRC state.
static uint32_t png_crc32_update_state(uint32_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

//=============================================================================
// PNG Image I/O
//=============================================================================

// PNG uses big-endian integers
/// @brief Read a big-endian uint32 from a PNG chunk header / data.
static uint32_t png_read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/// @brief Return whether a PNG chunk type is critical.
/// @details PNG marks ancillary chunks by setting bit 5 in the first chunk-type
///          byte. Unknown critical chunks cannot be skipped safely because they
///          may change image interpretation.
/// @param chunk_type Four-byte PNG chunk type.
/// @return Non-zero for critical chunks, zero for ancillary chunks.
static int png_chunk_is_critical(const uint8_t *chunk_type) {
    return chunk_type && ((chunk_type[0] & 0x20u) == 0u);
}

/// @brief Compute the Adler-32 checksum of @p data per RFC 1950 §9.
/// @details Adler-32 is the checksum carried in the trailing 4 bytes of a
///          zlib-compressed PNG IDAT stream. The two running sums @c a (mod 65521)
///          and @c b (mod 65521) are updated per byte; the result packs them as
///          (b << 16) | a. Used by the PNG decoder to verify deflate output before
///          touching the pixel buffer.
static uint32_t png_adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1;
    uint32_t b = 0;
    while (len > 0) {
        size_t chunk = len < 5552u ? len : 5552u;
        for (size_t i = 0; i < chunk; i++) {
            a += data[i];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
        data += chunk;
        len -= chunk;
    }
    return (b << 16) | a;
}

/// @brief Validate the 2-byte ZLIB header (CMF + FLG) prefix of a PNG IDAT stream.
/// @details Performs the four checks RFC 1950 §2.2 mandates:
///          1. CM (low 4 bits of CMF) must equal 8 (deflate).
///          2. CINFO (high 4 bits of CMF) must be ≤ 7 (window size ≤ 32K).
///          3. (CMF * 256 + FLG) must be a multiple of 31 (FCHECK validity).
///          4. FDICT bit (0x20 of FLG) must be 0 — preset dictionaries are
///             illegal in PNG IDAT streams.
/// @return 1 if the header is well-formed for PNG, 0 otherwise.
static int png_validate_zlib_header(const uint8_t *data, size_t len) {
    if (!data || len < 6)
        return 0;
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8u)
        return 0;
    if ((cmf >> 4) > 7u)
        return 0;
    if ((((uint16_t)cmf << 8) | flg) % 31u != 0u)
        return 0;
    if ((flg & 0x20u) != 0u)
        return 0;
    return 1;
}

// Paeth predictor as defined by the PNG spec
/// @brief PNG Paeth filter predictor (RFC 2083 §6.6).
///
/// Predicts the current pixel as whichever of `a` (left), `b`
/// (above), or `c` (upper-left) is closest to `p = a + b - c` —
/// the linear extrapolation. Used as filter type 4.
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

/// @brief Reconstruct one PNG filtered scanline.
/// @details Consumes the leading filter byte plus @p row_stride data bytes from @p *src_io,
///          writing the unfiltered row into @p dst. Returning a status instead of jumping to the
///          decoder cleanup label lets callers free pass-local buffers before aborting.
/// @param dst Destination row buffer with at least @p row_stride bytes.
/// @param src_io In/out pointer to the next compressed-output byte.
/// @param prev Previous reconstructed row, or NULL for the first row.
/// @param row_stride Number of encoded data bytes in the row, excluding the filter byte.
/// @param bpp Bytes per pixel used by PNG's Sub/Average/Paeth filters.
/// @return 1 on success, 0 when the filter type is invalid or inputs are malformed.
static int png_filter_row(
    uint8_t *dst, const uint8_t **src_io, const uint8_t *prev, size_t row_stride, int bpp) {
    if (!dst || !src_io || !*src_io || bpp < 1)
        return 0;
    const uint8_t *src = *src_io;
    uint8_t filt = *src++;
    for (size_t fi = 0; fi < row_stride; fi++) {
        uint8_t rb = src[fi];
        uint8_t fa = (fi >= (size_t)bpp) ? dst[fi - (size_t)bpp] : 0;
        uint8_t fb = prev ? prev[fi] : 0;
        uint8_t fc = (prev && fi >= (size_t)bpp) ? prev[fi - (size_t)bpp] : 0;
        switch (filt) {
            case 0:
                dst[fi] = rb;
                break;
            case 1:
                dst[fi] = (uint8_t)(rb + fa);
                break;
            case 2:
                dst[fi] = (uint8_t)(rb + fb);
                break;
            case 3:
                dst[fi] = (uint8_t)(rb + (uint8_t)(((int)fa + (int)fb) / 2));
                break;
            case 4:
                dst[fi] = (uint8_t)(rb + paeth_predict(fa, fb, fc));
                break;
            default:
                return 0;
        }
    }
    *src_io = src + row_stride;
    return 1;
}

/// @brief Decode a PNG memory buffer into malloc-owned raw RGBA32 pixels.
///
/// Implements a focused PNG reader (RFC 2083): parses the 8-byte
/// signature + IHDR header, walks the IDAT chunks, decompresses
/// them with the in-tree DEFLATE engine, then unfilters each
/// scanline using the standard 5 filters (None, Sub, Up, Average,
/// Paeth) and converts to RGBA. Supports color types 2 (RGB), 6
/// (RGBA), 0 (grayscale), 4 (gray + alpha), and 3 (palette via
/// PLTE/tRNS chunks).
/// @return 1 on success, 0 on any decode failure.
int rt_png_decode_buffer_rgba32(const uint8_t *file_data,
                                size_t file_len,
                                uint32_t **out_pixels,
                                int64_t *out_width,
                                int64_t *out_height) {
    if (out_pixels)
        *out_pixels = NULL;
    if (out_width)
        *out_width = 0;
    if (out_height)
        *out_height = 0;
    if (!file_data || file_len < 8 || file_len > 256u * 1024u * 1024u || !out_pixels ||
        !out_width || !out_height)
        return 0;

    // Verify PNG signature
    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (memcmp(file_data, png_sig, 8) != 0)
        return 0;

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
    uint16_t trns_rgb[3] = {0, 0, 0}; // key color for truecolor RGB
    int has_trns_rgb = 0;
    int ihdr_seen = 0;
    int idat_seen = 0;
    int idat_closed = 0;
    int iend_seen = 0;
    int plte_seen = 0;
    int trns_seen = 0;

    while (pos <= (size_t)file_len && 12u <= (size_t)file_len - pos) {
        uint32_t chunk_len = png_read_u32(file_data + pos);
        const uint8_t *chunk_type = file_data + pos + 4;
        const uint8_t *chunk_data = file_data + pos + 8;

        if ((size_t)chunk_len > (size_t)file_len - pos - 12u) {
            free(idat_buf);
            return 0;
        }
        uint32_t expected_crc = png_read_u32(file_data + pos + 8 + chunk_len);
        uint32_t actual_crc = rt_crc32_compute(chunk_type, (size_t)chunk_len + 4u);
        if (actual_crc != expected_crc) {
            free(idat_buf);
            return 0;
        }

        if (!ihdr_seen && memcmp(chunk_type, "IHDR", 4) != 0) {
            free(idat_buf);
            return 0;
        }

        if (memcmp(chunk_type, "IHDR", 4) == 0) {
            if (ihdr_seen || chunk_len != 13) {
                free(idat_buf);
                return 0;
            }
            width = png_read_u32(chunk_data);
            height = png_read_u32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            uint8_t compression_method = chunk_data[10];
            uint8_t filter_method = chunk_data[11];
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
            if (!valid || width == 0 || height == 0 || compression_method != 0 ||
                filter_method != 0 || (interlace != 0 && interlace != 1)) {
                if (idat_buf)
                    free(idat_buf);
                return 0;
            }
            if ((size_t)width > SIZE_MAX / (size_t)height ||
                (size_t)width * (size_t)height > PNG_MAX_PIXELS) {
                if (idat_buf)
                    free(idat_buf);
                return 0;
            }
            ihdr_seen = 1;
        } else if (memcmp(chunk_type, "PLTE", 4) == 0) {
            if (plte_seen || idat_seen || color_type == 0 || color_type == 4 || chunk_len == 0 ||
                (chunk_len % 3u) != 0u || chunk_len > 768u) {
                free(idat_buf);
                return 0;
            }
            palette_count = (int)(chunk_len / 3);
            if (color_type == 3) {
                int max_palette_count = 1 << bit_depth;
                if (palette_count > max_palette_count) {
                    free(idat_buf);
                    return 0;
                }
            }
            memcpy(palette, chunk_data, (size_t)palette_count * 3);
            plte_seen = 1;
        } else if (memcmp(chunk_type, "tRNS", 4) == 0) {
            if (trns_seen || idat_seen) {
                free(idat_buf);
                return 0;
            }
            if (color_type == 3) {
                if (palette_count <= 0 || chunk_len > (uint32_t)palette_count) {
                    free(idat_buf);
                    return 0;
                }
                // Per-palette-entry alpha values
                trns_count = (int)chunk_len;
                if (trns_count > 256)
                    trns_count = 256;
                memcpy(trns_alpha, chunk_data, (size_t)trns_count);
            } else if (color_type == 0 && chunk_len == 2) {
                // Grayscale key color (16-bit, even for 8-bit images)
                trns_gray = (uint16_t)((chunk_data[0] << 8) | chunk_data[1]);
                has_trns_gray = 1;
            } else if (color_type == 2 && chunk_len == 6) {
                // Truecolor key color (16-bit samples, even for 8-bit images)
                trns_rgb[0] = (uint16_t)((chunk_data[0] << 8) | chunk_data[1]);
                trns_rgb[1] = (uint16_t)((chunk_data[2] << 8) | chunk_data[3]);
                trns_rgb[2] = (uint16_t)((chunk_data[4] << 8) | chunk_data[5]);
                has_trns_rgb = 1;
            } else {
                free(idat_buf);
                return 0;
            }
            trns_seen = 1;
        } else if (memcmp(chunk_type, "IDAT", 4) == 0) {
            if (idat_closed) {
                free(idat_buf);
                return 0;
            }
            if (color_type == 3 && palette_count <= 0) {
                free(idat_buf);
                return 0;
            }
            idat_seen = 1;
            // Accumulate IDAT data
            if (chunk_len > SIZE_MAX - idat_len) // overflow guard
            {
                if (idat_buf)
                    free(idat_buf);
                return 0;
            }
            size_t needed_idat = idat_len + (size_t)chunk_len;
            if (needed_idat > idat_cap) {
                size_t new_cap = idat_cap ? idat_cap : 1;
                while (new_cap < needed_idat) {
                    if (new_cap > SIZE_MAX / 2) {
                        new_cap = needed_idat;
                        break;
                    }
                    new_cap *= 2;
                }
                uint8_t *new_buf = (uint8_t *)realloc(idat_buf, new_cap);
                if (!new_buf) {
                    if (idat_buf)
                        free(idat_buf);
                    return 0;
                }
                idat_buf = new_buf;
                idat_cap = new_cap;
            }
            memcpy(idat_buf + idat_len, chunk_data, chunk_len);
            idat_len += chunk_len;
        } else if (memcmp(chunk_type, "IEND", 4) == 0) {
            iend_seen = 1;
            break;
        } else {
            if (png_chunk_is_critical(chunk_type)) {
                free(idat_buf);
                return 0;
            }
            if (idat_seen)
                idat_closed = 1;
        }

        pos += 12 + chunk_len; // length + type + data + crc
    }

    if (!ihdr_seen || !iend_seen || width == 0 || height == 0 || !idat_buf || idat_len < 2 ||
        (color_type == 3 && palette_count <= 0)) {
        if (idat_buf)
            free(idat_buf);
        return 0;
    }

    // IDAT data is a zlib stream: 2-byte header + DEFLATE data + 4-byte Adler32.
    if (!png_validate_zlib_header(idat_buf, idat_len)) {
        free(idat_buf);
        return 0;
    }
    size_t deflate_len = idat_len - 6; // strip 2-byte zlib header and 4-byte Adler32.
    uint32_t expected_adler = png_read_u32(idat_buf + idat_len - 4);

    uint8_t *raw_data = NULL;
    size_t raw_len = 0;
    uint8_t *img = NULL;
    uint32_t *pixels = NULL;
    int success = 0;

    if (!rt_compress_inflate_raw(
            idat_buf + 2, deflate_len, 256u * 1024u * 1024u, &raw_data, &raw_len)) {
        free(idat_buf);
        return 0;
    }
    free(idat_buf);
    if (png_adler32(raw_data, raw_len) != expected_adler)
        goto cleanup;

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

    size_t stride = 0;
    if (!px_png_stride_checked(width, bit_depth, samples_per_pixel, &stride))
        goto cleanup;

    if (interlace == 1) {
        // Adam7 interlaced PNG: 7 passes
        static const int a7_x0[7] = {0, 4, 0, 2, 0, 1, 0};
        static const int a7_dx[7] = {8, 8, 4, 4, 2, 2, 1};
        static const int a7_y0[7] = {0, 0, 4, 0, 2, 0, 1};
        static const int a7_dy[7] = {8, 8, 8, 4, 4, 2, 2};

        // Allocate full-size image buffer (row-major, sequential)
        size_t image_bytes = 0;
        if (!px_mul_size(stride, (size_t)height, &image_bytes))
            goto cleanup;
        img = (uint8_t *)calloc(image_bytes, 1);
        if (!img)
            goto cleanup;

        const uint8_t *src_ptr = raw_data;
        const uint8_t *src_end = raw_data + raw_len;

        for (int pass = 0; pass < 7; pass++) {
            uint32_t x0 = (uint32_t)a7_x0[pass];
            uint32_t y0 = (uint32_t)a7_y0[pass];
            uint32_t dx_step = (uint32_t)a7_dx[pass];
            uint32_t dy_step = (uint32_t)a7_dy[pass];
            uint32_t sub_w = width > x0 ? (width - x0 + dx_step - 1u) / dx_step : 0;
            uint32_t sub_h = height > y0 ? (height - y0 + dy_step - 1u) / dy_step : 0;
            if (sub_w == 0 || sub_h == 0)
                continue;

            size_t sub_stride = 0;
            if (!px_png_stride_checked(sub_w, bit_depth, samples_per_pixel, &sub_stride))
                goto cleanup;
            // Allocate temp buffer for this sub-image
            size_t sub_image_bytes = 0;
            if (!px_mul_size(sub_stride, (size_t)sub_h, &sub_image_bytes))
                goto cleanup;
            uint8_t *sub_img = (uint8_t *)calloc(sub_image_bytes, 1);
            if (!sub_img)
                goto cleanup;

            for (uint32_t sy = 0; sy < sub_h; sy++) {
                size_t src_avail = (size_t)(src_end - src_ptr);
                if (src_avail <= sub_stride) {
                    free(sub_img);
                    goto cleanup;
                }
                uint8_t *dst_row = sub_img + sy * sub_stride;
                const uint8_t *prev_row = (sy > 0) ? sub_img + (sy - 1) * sub_stride : NULL;
                if (!png_filter_row(dst_row, &src_ptr, prev_row, sub_stride, bpp)) {
                    free(sub_img);
                    goto cleanup;
                }
            }

            // Scatter sub-image pixels into full image
            // Copy raw bytes for each pixel from sub-image row to the correct
            // position in the full-size img buffer
            int px_bytes = (bit_depth < 8) ? 1 : (samples_per_pixel * bit_depth / 8);
            for (uint32_t sy = 0; sy < sub_h; sy++) {
                uint32_t dy = (uint32_t)a7_y0[pass] + sy * (uint32_t)a7_dy[pass];
                if (dy >= height)
                    continue;
                for (uint32_t sx = 0; sx < sub_w; sx++) {
                    uint32_t dx = (uint32_t)a7_x0[pass] + sx * (uint32_t)a7_dx[pass];
                    if (dx >= width)
                        continue;

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
        if (src_ptr != src_end)
            goto cleanup;
    } else {
        // Non-interlaced (sequential)
        size_t filtered_stride = 0;
        if (!px_add_size(stride, 1, &filtered_stride))
            goto cleanup;
        size_t expected = 0;
        if (!px_mul_size(filtered_stride, (size_t)height, &expected))
            goto cleanup;
        if (raw_len != expected)
            goto cleanup;

        size_t image_bytes = 0;
        if (!px_mul_size(stride, (size_t)height, &image_bytes))
            goto cleanup;
        img = (uint8_t *)malloc(image_bytes);
        if (!img)
            goto cleanup;

        const uint8_t *src_ptr = raw_data;
        for (uint32_t y = 0; y < height; y++) {
            uint8_t *dst_row = img + y * stride;
            const uint8_t *prev_row = (y > 0) ? img + (y - 1) * stride : NULL;
            if (!png_filter_row(dst_row, &src_ptr, prev_row, stride, bpp))
                goto cleanup;
        }
    }

    // Create raw pixel storage and convert to our RGBA format (0xRRGGBBAA).
    size_t pixel_count = 0;
    size_t pixel_bytes = 0;
    if (!px_mul_size((size_t)width, (size_t)height, &pixel_count) ||
        !px_mul_size(pixel_count, sizeof(uint32_t), &pixel_bytes))
        goto cleanup;
    pixels = (uint32_t *)malloc(pixel_bytes);
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
                    uint16_t gray_sample = 0;
                    if (bit_depth == 16) {
                        gray_sample = (uint16_t)(((uint16_t)row[x * 2] << 8) | row[x * 2 + 1]);
                        gray = PNG_DOWN16(row + x * 2);
                    } else if (bit_depth == 8) {
                        gray_sample = row[x];
                        gray = row[x];
                    } else {
                        // Sub-byte: unpack from packed row
                        int pixels_per_byte = 8 / bit_depth;
                        int byte_idx = x / pixels_per_byte;
                        int bit_offset = (pixels_per_byte - 1 - (x % pixels_per_byte)) * bit_depth;
                        uint8_t mask = (uint8_t)((1 << bit_depth) - 1);
                        uint8_t val = (row[byte_idx] >> bit_offset) & mask;
                        gray_sample = val;
                        // Scale to 8-bit: e.g., 4-bit 0xF -> 0xFF
                        gray = (uint8_t)(val * 255 / ((1 << bit_depth) - 1));
                    }
                    r = g = b_ch = gray;
                    if (has_trns_gray) {
                        if (gray_sample == trns_gray)
                            alpha = 0;
                    }
                    break;
                }
                case 2: { // RGB
                    if (bit_depth == 16) {
                        const uint8_t *sp = row + x * 6;
                        uint16_t r16 = (uint16_t)(((uint16_t)sp[0] << 8) | sp[1]);
                        uint16_t g16 = (uint16_t)(((uint16_t)sp[2] << 8) | sp[3]);
                        uint16_t b16 = (uint16_t)(((uint16_t)sp[4] << 8) | sp[5]);
                        r = PNG_DOWN16(sp);
                        g = PNG_DOWN16(sp + 2);
                        b_ch = PNG_DOWN16(sp + 4);
                        if (has_trns_rgb && r16 == trns_rgb[0] && g16 == trns_rgb[1] &&
                            b16 == trns_rgb[2])
                            alpha = 0;
                    } else {
                        r = row[x * 3];
                        g = row[x * 3 + 1];
                        b_ch = row[x * 3 + 2];
                        if (has_trns_rgb && (uint16_t)r == trns_rgb[0] &&
                            (uint16_t)g == trns_rgb[1] && (uint16_t)b_ch == trns_rgb[2])
                            alpha = 0;
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
                    if (idx < 0 || idx >= palette_count)
                        goto cleanup;
                    r = palette[idx * 3];
                    g = palette[idx * 3 + 1];
                    b_ch = palette[idx * 3 + 2];
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

            pixels[(size_t)y * (size_t)width + x] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b_ch << 8) | alpha;
        }
    }
    success = 1;

cleanup:
    free(img);
    free(raw_data);
    if (!success) {
        free(pixels);
        return 0;
    }
    *out_pixels = pixels;
    *out_width = (int64_t)width;
    *out_height = (int64_t)height;
    return 1;
}

#undef PNG_DOWN16

/// @brief Decode a PNG file into a Pixels object.
/// @return GC-managed `rt_pixels_impl*` on success, NULL on any decode failure.
void *rt_pixels_load_png(void *path) {
    rt_asset_error_begin_load();
    if (!path) {
        rt_asset_error_end_load_failure();
        rt_trap("Pixels.LoadPng: path must not be null");
        return NULL;
    }

    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath) {
        rt_asset_error_end_load_failure();
        rt_trap("Pixels.LoadPng: invalid path");
        return NULL;
    }

    FILE *f = rt_file_stdio_open_utf8(filepath, "rb");
    if (!f) {
        rt_asset_error_setf(RT_ASSET_ERROR_NOT_FOUND, "Pixels.LoadPng: '%s' not found", filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }

    if (px_fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNREADABLE, "Pixels.LoadPng: failed to seek '%s'", filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    int64_t file_len = px_ftell(f);
    if (px_fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNREADABLE, "Pixels.LoadPng: failed to seek '%s'", filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    if (file_len < 8 || file_len > 256 * 1024 * 1024) {
        fclose(f);
        rt_asset_error_setf(file_len > 256 * 1024 * 1024 ? RT_ASSET_ERROR_TOO_LARGE
                                                         : RT_ASSET_ERROR_BAD_MAGIC,
                            "Pixels.LoadPng: '%s' is not a supported PNG",
                            filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_len);
    if (!file_data) {
        fclose(f);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    if (!px_read_exact(f, file_data, (size_t)file_len)) {
        free(file_data);
        fclose(f);
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNREADABLE, "Pixels.LoadPng: failed to read '%s'", filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    fclose(f);

    uint32_t *raw_pixels = NULL;
    int64_t width = 0;
    int64_t height = 0;
    if (!rt_png_decode_buffer_rgba32(file_data, (size_t)file_len, &raw_pixels, &width, &height)) {
        free(file_data);
        rt_asset_error_setf(
            RT_ASSET_ERROR_CORRUPT, "Pixels.LoadPng: '%s' is not a supported PNG", filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    free(file_data);

    rt_pixels_impl *pixels = pixels_alloc(width, height);
    if (!pixels) {
        free(raw_pixels);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    size_t pixel_count = 0;
    size_t pixel_bytes = 0;
    if (!px_mul_size((size_t)width, (size_t)height, &pixel_count) ||
        !px_mul_size(pixel_count, sizeof(uint32_t), &pixel_bytes)) {
        free(raw_pixels);
        if (rt_obj_release_check0(pixels))
            rt_obj_free(pixels);
        rt_asset_error_setf(
            RT_ASSET_ERROR_TOO_LARGE, "Pixels.LoadPng: '%s' dimensions are too large", filepath);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    memcpy(pixels->data, raw_pixels, pixel_bytes);
    free(raw_pixels);
    rt_asset_error_end_load_success();
    return pixels;
}

/// @brief Save a Pixels object as an RGBA PNG file.
///
/// Writes the 8-byte signature, IHDR (color type 6 / RGBA, bit
/// depth 8, default filter & interlace methods), a single IDAT
/// chunk containing all scanlines compressed with our in-tree
/// DEFLATE encoder (each row prefixed with filter byte 0 = None),
/// and IEND. Each chunk's CRC32 is computed over `type ‖ data` per
/// RFC 2083 §5.3.
/// @return 1 on success, 0 on any failure.
int64_t rt_pixels_save_png(void *pixels_ptr, void *path) {
    if (!pixels_ptr || !path)
        return 0;

    rt_pixels_impl *p = rt_pixels_checked_impl(pixels_ptr, "Pixels.SavePng: invalid pixels");
    if (!p)
        return 0;
    const char *filepath = rt_string_cstr((rt_string)path);
    if (!filepath || p->width <= 0 || p->height <= 0 || p->width > UINT32_MAX ||
        p->height > UINT32_MAX)
        return 0;

    uint32_t w = (uint32_t)p->width;
    uint32_t h = (uint32_t)p->height;
    size_t stride = 0;
    if (!px_mul_size((size_t)w, 4, &stride))
        return 0;

    // Build raw PNG scanline data with filter byte.
    // First row uses filter=0 (None); subsequent rows use filter=1 (Sub)
    // which encodes differences from the left neighbor for better compression.
    size_t row_len = 0;
    if (!px_add_size(stride, 1, &row_len))
        return 0;
    size_t raw_len = 0;
    if (!px_mul_size(row_len, (size_t)h, &raw_len) || raw_len > (size_t)INT64_MAX)
        return 0;
    void *raw_bytes = rt_bytes_new((int64_t)raw_len);
    if (!raw_bytes)
        return 0;
    uint8_t *raw = rt_bytes_data(raw_bytes);

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
            for (size_t i = stride; i > 4; i--)
                dst[i - 1] -= dst[i - 5]; // Sub: each byte minus byte at same position 4 bytes left
        }
    }

    // Compress the raw data using DEFLATE.
    // All error paths after comp_bytes allocation must go through cleanup to
    // release these GC-managed objects (refcount=1).
    void *comp_bytes = NULL;
    uint8_t *zlib_data = NULL;
    FILE *out = NULL;
    char *tmp_path = NULL;
    int64_t result = 0;

    comp_bytes = rt_compress_deflate(raw_bytes);
    if (!comp_bytes)
        goto save_cleanup;

    int64_t comp_len_i64 = rt_bytes_len(comp_bytes);
    if (comp_len_i64 < 0 || (uint64_t)comp_len_i64 > (uint64_t)UINT32_MAX - 6u)
        goto save_cleanup;
    size_t comp_len = (size_t)comp_len_i64;
    const uint8_t *comp_data = rt_bytes_data_const(comp_bytes);

    // Build zlib stream: 2-byte header + deflate data + 4-byte adler32
    // Zlib header: CMF=0x78 (deflate, window=32K), FLG=0x01 (no dict, check=1)
    size_t zlib_len = 2 + comp_len + 4;
    zlib_data = (uint8_t *)malloc(zlib_len);
    if (!zlib_data)
        goto save_cleanup;

    zlib_data[0] = 0x78; // CMF
    zlib_data[1] = 0x01; // FLG
    memcpy(zlib_data + 2, comp_data, comp_len);

    // Compute Adler-32 of the raw (uncompressed) data
    {
        const uint8_t *raw_b = rt_bytes_data_const(raw_bytes);
        int64_t raw_b_len = rt_bytes_len(raw_bytes);
        if (raw_b_len < 0)
            goto save_cleanup;
        uint32_t adler = png_adler32(raw_b, (size_t)raw_b_len);
        zlib_data[2 + comp_len + 0] = (uint8_t)((adler >> 24) & 0xFF);
        zlib_data[2 + comp_len + 1] = (uint8_t)((adler >> 16) & 0xFF);
        zlib_data[2 + comp_len + 2] = (uint8_t)((adler >> 8) & 0xFF);
        zlib_data[2 + comp_len + 3] = (uint8_t)(adler & 0xFF);
    }

    out = rt_file_stdio_open_temp_for_replace_utf8(filepath, &tmp_path);
    if (!out)
        goto save_cleanup;

    // Track write success — any fwrite failure produces a corrupt PNG.
    int write_ok = 1;

    // Write PNG signature
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!px_write_exact(out, sig, 8))
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
        if (!px_write_exact(out, len_buf, 4))
            write_ok = 0;

        uint8_t type_data[4 + 13];
        memcpy(type_data, "IHDR", 4);
        memcpy(type_data + 4, ihdr, 13);
        if (write_ok && !px_write_exact(out, type_data, 17))
            write_ok = 0;

        uint32_t chunk_crc = rt_crc32_compute(type_data, 17);
        uint8_t crc_buf[4] = {(uint8_t)(chunk_crc >> 24),
                              (uint8_t)(chunk_crc >> 16),
                              (uint8_t)(chunk_crc >> 8),
                              (uint8_t)chunk_crc};
        if (write_ok && !px_write_exact(out, crc_buf, 4))
            write_ok = 0;
    }

    // Write IDAT chunk
    if (write_ok) {
        uint8_t len_buf[4] = {(uint8_t)(zlib_len >> 24),
                              (uint8_t)(zlib_len >> 16),
                              (uint8_t)(zlib_len >> 8),
                              (uint8_t)zlib_len};
        if (!px_write_exact(out, len_buf, 4))
            write_ok = 0;

        if (write_ok && !px_write_exact(out, "IDAT", 4))
            write_ok = 0;
        if (write_ok && !px_write_exact(out, zlib_data, zlib_len))
            write_ok = 0;

        uint32_t crc_state = png_crc32_update_state(0xFFFFFFFFu, (const uint8_t *)"IDAT", 4);
        uint32_t chunk_crc = png_crc32_update_state(crc_state, zlib_data, zlib_len) ^ 0xFFFFFFFFu;
        uint8_t crc_buf[4] = {(uint8_t)(chunk_crc >> 24),
                              (uint8_t)(chunk_crc >> 16),
                              (uint8_t)(chunk_crc >> 8),
                              (uint8_t)chunk_crc};
        if (write_ok && !px_write_exact(out, crc_buf, 4))
            write_ok = 0;
    }

    // Write IEND chunk
    if (write_ok) {
        uint8_t iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
        if (!px_write_exact(out, iend, 12))
            write_ok = 0;
    }

    result = write_ok;

save_cleanup:
    free(zlib_data);
    if (out) {
        if (fflush(out) != 0)
            result = 0;
        if (fclose(out) != 0)
            result = 0;
        if (result) {
            result = rt_file_stdio_replace_utf8(tmp_path, filepath) ? 1 : 0;
        }
        if (!result && tmp_path)
            (void)rt_file_stdio_unlink_utf8(tmp_path);
    }
    free(tmp_path);
    if (comp_bytes) {
        if (rt_obj_release_check0(comp_bytes))
            rt_obj_free(comp_bytes);
    }
    if (raw_bytes) {
        if (rt_obj_release_check0(raw_bytes))
            rt_obj_free(raw_bytes);
    }
    return result;
}
