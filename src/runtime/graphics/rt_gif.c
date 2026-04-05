//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gif.c
// Purpose: GIF87a/89a decoder with LZW decompression and multi-frame animation.
// Key invariants:
//   - LZW code table limited to 4096 entries (12-bit codes maximum)
//   - Interlaced images use 4-pass row reordering
//   - Disposal method 3 (restore-to-previous) saves/restores canvas state
//   - All output pixels are RGBA (0xRRGGBBAA); transparency via alpha=0
// Ownership/Lifetime:
//   - Each decoded frame produces a GC-managed Pixels object via pixels_alloc
//   - The gif_frame_t array itself is malloc'd; caller frees it
// Links: rt_gif.h (public API), rt_pixels_internal.h (pixels_alloc)
//
//===----------------------------------------------------------------------===//

#include "rt_gif.h"

#include "rt_pixels_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// GIF file reader
//===----------------------------------------------------------------------===//

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} gif_reader_t;

static int gif_read(gif_reader_t *r, void *buf, size_t count) {
    if (r->pos + count > r->len)
        return 0;
    memcpy(buf, r->data + r->pos, count);
    r->pos += count;
    return 1;
}

static int gif_read_u8(gif_reader_t *r) {
    if (r->pos >= r->len)
        return -1;
    return r->data[r->pos++];
}

static int gif_read_u16_le(gif_reader_t *r) {
    if (r->pos + 2 > r->len)
        return -1;
    int val = r->data[r->pos] | (r->data[r->pos + 1] << 8);
    r->pos += 2;
    return val;
}

/// @brief Skip a sequence of sub-blocks (each prefixed by a length byte).
static void gif_skip_sub_blocks(gif_reader_t *r) {
    while (r->pos < r->len) {
        int block_size = gif_read_u8(r);
        if (block_size <= 0)
            break; // block terminator (0x00)
        r->pos += (size_t)block_size;
    }
}

//===----------------------------------------------------------------------===//
// LZW Decompressor
//===----------------------------------------------------------------------===//

#define LZW_MAX_TABLE_SIZE 4096
#define LZW_MAX_BITS 12

typedef struct {
    uint16_t prefix; // index of prefix entry, or 0xFFFF for root entries
    uint8_t suffix;  // last byte of this string
    uint16_t length; // total string length
} lzw_entry_t;

typedef struct {
    lzw_entry_t table[LZW_MAX_TABLE_SIZE];
    int table_size;
    int min_code_size;
    int code_size;
    int clear_code;
    int end_code;
    int next_code;

    // Bit reader state
    uint32_t bitbuf;
    int bits_left;
    const uint8_t *block_data;
    size_t block_len;
    size_t block_pos;
} lzw_state_t;

/// @brief Read LZW sub-blocks into a flat buffer.
/// @return malloc'd buffer of concatenated sub-block data, or NULL.
static uint8_t *gif_read_sub_blocks(gif_reader_t *r, size_t *out_len) {
    size_t cap = 256;
    size_t len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf)
        return NULL;

    while (r->pos < r->len) {
        int block_size = gif_read_u8(r);
        if (block_size <= 0)
            break;
        if (len + (size_t)block_size > cap) {
            cap = (len + (size_t)block_size) * 2;
            uint8_t *new_buf = (uint8_t *)realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        if (!gif_read(r, buf + len, (size_t)block_size)) {
            free(buf);
            return NULL;
        }
        len += (size_t)block_size;
    }

    *out_len = len;
    return buf;
}

static void lzw_init(lzw_state_t *s, int min_code_size, const uint8_t *data, size_t len) {
    s->min_code_size = min_code_size;
    s->clear_code = 1 << min_code_size;
    s->end_code = s->clear_code + 1;
    s->code_size = min_code_size + 1;
    s->next_code = s->end_code + 1;
    s->table_size = s->next_code;

    // Initialize root entries (one per color index)
    for (int i = 0; i < s->clear_code; i++) {
        s->table[i].prefix = 0xFFFF;
        s->table[i].suffix = (uint8_t)i;
        s->table[i].length = 1;
    }

    s->bitbuf = 0;
    s->bits_left = 0;
    s->block_data = data;
    s->block_len = len;
    s->block_pos = 0;
}

static int lzw_read_code(lzw_state_t *s) {
    while (s->bits_left < s->code_size) {
        if (s->block_pos >= s->block_len)
            return -1;
        s->bitbuf |= (uint32_t)s->block_data[s->block_pos++] << s->bits_left;
        s->bits_left += 8;
    }
    int code = (int)(s->bitbuf & ((1u << s->code_size) - 1));
    s->bitbuf >>= s->code_size;
    s->bits_left -= s->code_size;
    return code;
}

/// @brief Emit the string for a code into the output buffer.
static int lzw_emit_string(
    lzw_state_t *s, int code, uint8_t *out, size_t out_cap, size_t *out_pos) {
    if (code < 0 || code >= s->table_size)
        return -1;
    int len = s->table[code].length;
    if (*out_pos + (size_t)len > out_cap)
        return -1;
    // Write backwards then the order is correct
    size_t pos = *out_pos + (size_t)len - 1;
    int c = code;
    for (int i = 0; i < len; i++) {
        if (c < 0 || c >= s->table_size)
            return -1;
        out[pos--] = s->table[c].suffix;
        c = (int)s->table[c].prefix;
        if (c == 0xFFFF)
            break;
    }
    *out_pos += (size_t)len;
    return 0;
}

/// @brief Get the first byte of the string for a code.
static uint8_t lzw_first_byte(lzw_state_t *s, int code) {
    while (code >= 0 && code < s->table_size && s->table[code].prefix != 0xFFFF)
        code = (int)s->table[code].prefix;
    return (code >= 0 && code < s->table_size) ? s->table[code].suffix : 0;
}

/// @brief Decompress LZW data into color indices.
/// @return malloc'd index buffer, or NULL on failure.
static uint8_t *lzw_decompress(int min_code_size,
                               const uint8_t *data,
                               size_t data_len,
                               size_t expected_pixels,
                               size_t *out_len) {
    if (min_code_size < 2 || min_code_size > 11)
        return NULL;

    lzw_state_t state;
    lzw_init(&state, min_code_size, data, data_len);

    size_t cap = expected_pixels + 256;
    uint8_t *output = (uint8_t *)malloc(cap);
    if (!output)
        return NULL;
    size_t pos = 0;

    int prev_code = -1;
    while (1) {
        int code = lzw_read_code(&state);
        if (code < 0 || code == state.end_code)
            break;

        if (code == state.clear_code) {
            // Reset table
            state.code_size = state.min_code_size + 1;
            state.next_code = state.end_code + 1;
            state.table_size = state.next_code;
            prev_code = -1;
            continue;
        }

        if (code < state.table_size) {
            // Code is in table — emit it
            if (lzw_emit_string(&state, code, output, cap, &pos) != 0)
                break;

            // Add new entry: prev_string + first_byte_of_current_string
            if (prev_code >= 0 && state.next_code < LZW_MAX_TABLE_SIZE) {
                state.table[state.next_code].prefix = (uint16_t)prev_code;
                state.table[state.next_code].suffix = lzw_first_byte(&state, code);
                state.table[state.next_code].length = state.table[prev_code].length + 1;
                state.next_code++;
                state.table_size = state.next_code;
                // Grow code size if needed
                if (state.next_code >= (1 << state.code_size) && state.code_size < LZW_MAX_BITS)
                    state.code_size++;
            }
        } else if (code == state.next_code && prev_code >= 0) {
            // KwKwK special case: code not yet in table
            uint8_t first = lzw_first_byte(&state, prev_code);
            if (lzw_emit_string(&state, prev_code, output, cap, &pos) != 0)
                break;
            if (pos < cap)
                output[pos++] = first;

            // Add new entry
            if (state.next_code < LZW_MAX_TABLE_SIZE) {
                state.table[state.next_code].prefix = (uint16_t)prev_code;
                state.table[state.next_code].suffix = first;
                state.table[state.next_code].length = state.table[prev_code].length + 1;
                state.next_code++;
                state.table_size = state.next_code;
                if (state.next_code >= (1 << state.code_size) && state.code_size < LZW_MAX_BITS)
                    state.code_size++;
            }
        } else {
            break; // invalid code
        }

        prev_code = code;
    }

    *out_len = pos;
    return output;
}

//===----------------------------------------------------------------------===//
// GIF Decoder
//===----------------------------------------------------------------------===//

/// @brief Interlace pass parameters: start row, row step.
static const int gif_interlace_start[4] = {0, 4, 2, 1};
static const int gif_interlace_step[4] = {8, 8, 4, 2};

int gif_decode_file(const char *filepath,
                    gif_frame_t **out_frames,
                    int *out_frame_count,
                    int *out_width,
                    int *out_height) {
    if (!filepath || !out_frames || !out_frame_count)
        return 0;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_len <= 0 || file_len > 100 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_len);
    if (!file_data) {
        fclose(f);
        return 0;
    }
    if (fread(file_data, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(file_data);
        fclose(f);
        return 0;
    }
    fclose(f);

    gif_reader_t reader = {file_data, (size_t)file_len, 0};
    gif_reader_t *r = &reader;

    // Verify GIF signature
    uint8_t sig[6];
    if (!gif_read(r, sig, 6) || (memcmp(sig, "GIF87a", 6) != 0 && memcmp(sig, "GIF89a", 6) != 0)) {
        free(file_data);
        return 0;
    }

    // Logical screen descriptor
    int screen_w = gif_read_u16_le(r);
    int screen_h = gif_read_u16_le(r);
    if (screen_w <= 0 || screen_h <= 0 || screen_w > 32768 || screen_h > 32768) {
        free(file_data);
        return 0;
    }

    int packed = gif_read_u8(r);
    int has_gct = (packed >> 7) & 1;
    int gct_size_field = packed & 0x07;
    int bg_color_index = gif_read_u8(r);
    gif_read_u8(r); // pixel aspect ratio (ignored)

    // Global color table
    uint8_t gct[256 * 3];
    int gct_count = 0;
    memset(gct, 0, sizeof(gct));
    if (has_gct) {
        gct_count = 1 << (gct_size_field + 1);
        if (!gif_read(r, gct, (size_t)gct_count * 3)) {
            free(file_data);
            return 0;
        }
    }

    // Allocate frame array (grow as needed)
    int frame_cap = 16;
    int frame_count = 0;
    gif_frame_t *frames = (gif_frame_t *)calloc((size_t)frame_cap, sizeof(gif_frame_t));
    if (!frames) {
        free(file_data);
        return 0;
    }

    // Canvas for frame compositing (RGBA)
    size_t canvas_size = (size_t)screen_w * (size_t)screen_h;
    uint32_t *canvas = (uint32_t *)calloc(canvas_size, sizeof(uint32_t));
    uint32_t *prev_canvas = (uint32_t *)calloc(canvas_size, sizeof(uint32_t));
    if (!canvas || !prev_canvas) {
        free(canvas);
        free(prev_canvas);
        free(frames);
        free(file_data);
        return 0;
    }

    // Fill canvas with background color
    uint32_t bg_rgba = 0x00000000;
    if (has_gct && bg_color_index < gct_count) {
        bg_rgba = ((uint32_t)gct[bg_color_index * 3] << 24) |
                  ((uint32_t)gct[bg_color_index * 3 + 1] << 16) |
                  ((uint32_t)gct[bg_color_index * 3 + 2] << 8) | 0xFF;
    }
    for (size_t i = 0; i < canvas_size; i++)
        canvas[i] = bg_rgba;

    // Current GCE state
    int gce_delay_ms = 0;
    int gce_dispose = 0;
    int gce_transparent = -1;
    int gce_valid = 0;

    // Process blocks
    while (r->pos < r->len) {
        int block_type = gif_read_u8(r);
        if (block_type < 0 || block_type == 0x3B) // trailer
            break;

        if (block_type == 0x21) {
            // Extension block
            int ext_label = gif_read_u8(r);
            if (ext_label == 0xF9) {
                // Graphics Control Extension
                int block_size = gif_read_u8(r);
                if (block_size >= 4) {
                    int gce_packed = gif_read_u8(r);
                    gce_dispose = (gce_packed >> 2) & 0x07;
                    int has_transparent = gce_packed & 0x01;
                    int delay = gif_read_u16_le(r);
                    gce_delay_ms = delay * 10; // centiseconds to ms
                    int trans_idx = gif_read_u8(r);
                    gce_transparent = has_transparent ? trans_idx : -1;
                    gce_valid = 1;
                    // Skip remaining (block terminator)
                    if (block_size > 4)
                        r->pos += (size_t)(block_size - 4);
                }
                gif_read_u8(r); // block terminator
            } else {
                // Skip other extensions
                gif_skip_sub_blocks(r);
            }
            continue;
        }

        if (block_type == 0x2C) {
            // Image descriptor
            int img_left = gif_read_u16_le(r);
            int img_top = gif_read_u16_le(r);
            int img_w = gif_read_u16_le(r);
            int img_h = gif_read_u16_le(r);
            int img_packed = gif_read_u8(r);
            int has_lct = (img_packed >> 7) & 1;
            int interlaced = (img_packed >> 6) & 1;
            int lct_size_field = img_packed & 0x07;

            if (img_w <= 0 || img_h <= 0)
                goto next_frame;

            // Local color table (overrides GCT for this frame)
            uint8_t lct[256 * 3];
            int lct_count = 0;
            const uint8_t *color_table = gct;
            int color_count = gct_count;
            if (has_lct) {
                lct_count = 1 << (lct_size_field + 1);
                if (!gif_read(r, lct, (size_t)lct_count * 3))
                    break;
                color_table = lct;
                color_count = lct_count;
            }

            // LZW minimum code size
            int min_code_size = gif_read_u8(r);
            if (min_code_size < 2 || min_code_size > 11)
                goto skip_image_data;

            // Read LZW sub-blocks
            size_t lzw_data_len = 0;
            uint8_t *lzw_data = gif_read_sub_blocks(r, &lzw_data_len);
            if (!lzw_data)
                continue;

            // Decompress LZW
            size_t pixel_count = (size_t)img_w * (size_t)img_h;
            size_t index_len = 0;
            uint8_t *indices =
                lzw_decompress(min_code_size, lzw_data, lzw_data_len, pixel_count, &index_len);
            free(lzw_data);
            if (!indices)
                continue;

            // Save canvas for dispose method 3 (restore to previous)
            if (gce_dispose == 3)
                memcpy(prev_canvas, canvas, canvas_size * sizeof(uint32_t));

            // Apply decoded pixels to canvas
            size_t idx = 0;
            for (int y = 0; y < img_h && idx < index_len; y++) {
                // De-interlace: map logical row y to actual row
                int actual_y;
                if (interlaced) {
                    actual_y = -1;
                    int row_in_pass = y;
                    for (int pass = 0; pass < 4; pass++) {
                        int pass_rows =
                            (img_h - gif_interlace_start[pass] + gif_interlace_step[pass] - 1) /
                            gif_interlace_step[pass];
                        if (row_in_pass < pass_rows) {
                            actual_y =
                                gif_interlace_start[pass] + row_in_pass * gif_interlace_step[pass];
                            break;
                        }
                        row_in_pass -= pass_rows;
                    }
                    if (actual_y < 0)
                        actual_y = y;
                } else {
                    actual_y = y;
                }

                int canvas_y = img_top + actual_y;
                if (canvas_y < 0 || canvas_y >= screen_h) {
                    idx += (size_t)img_w;
                    continue;
                }

                for (int x = 0; x < img_w && idx < index_len; x++, idx++) {
                    int canvas_x = img_left + x;
                    if (canvas_x < 0 || canvas_x >= screen_w)
                        continue;

                    int color_idx = indices[idx];
                    if (gce_transparent >= 0 && color_idx == gce_transparent)
                        continue; // transparent — don't overwrite canvas

                    if (color_idx < color_count) {
                        uint32_t rgba = ((uint32_t)color_table[color_idx * 3] << 24) |
                                        ((uint32_t)color_table[color_idx * 3 + 1] << 16) |
                                        ((uint32_t)color_table[color_idx * 3 + 2] << 8) | 0xFF;
                        canvas[canvas_y * screen_w + canvas_x] = rgba;
                    }
                }
            }
            free(indices);

            // Create Pixels object for this frame (snapshot of current canvas)
            rt_pixels_impl *px = pixels_alloc((int64_t)screen_w, (int64_t)screen_h);
            if (px) {
                memcpy(px->data, canvas, canvas_size * sizeof(uint32_t));

                // Grow frames array if needed
                if (frame_count >= frame_cap) {
                    frame_cap *= 2;
                    gif_frame_t *new_frames =
                        (gif_frame_t *)realloc(frames, (size_t)frame_cap * sizeof(gif_frame_t));
                    if (!new_frames)
                        break;
                    frames = new_frames;
                }

                frames[frame_count].pixels = px;
                frames[frame_count].delay_ms = gce_valid ? gce_delay_ms : 100;
                frames[frame_count].dispose_method = gce_dispose;
                frame_count++;
            }

            // Apply disposal method for next frame
            switch (gce_dispose) {
                case 2: // Restore to background
                    for (int y = img_top; y < img_top + img_h && y < screen_h; y++) {
                        if (y < 0)
                            continue;
                        for (int x = img_left; x < img_left + img_w && x < screen_w; x++) {
                            if (x < 0)
                                continue;
                            canvas[y * screen_w + x] = bg_rgba;
                        }
                    }
                    break;
                case 3: // Restore to previous
                    memcpy(canvas, prev_canvas, canvas_size * sizeof(uint32_t));
                    break;
                default: // 0 or 1: do not dispose (keep canvas as-is)
                    break;
            }

            // Reset GCE for next frame
            gce_valid = 0;
            gce_delay_ms = 0;
            gce_dispose = 0;
            gce_transparent = -1;
            continue;

        skip_image_data:
            gif_skip_sub_blocks(r);
        next_frame:
            gce_valid = 0;
            continue;
        }

        // Unknown block type — skip
    }

    free(canvas);
    free(prev_canvas);
    free(file_data);

    if (frame_count == 0) {
        free(frames);
        return 0;
    }

    *out_frames = frames;
    *out_frame_count = frame_count;
    if (out_width)
        *out_width = screen_w;
    if (out_height)
        *out_height = screen_h;
    return frame_count;
}
