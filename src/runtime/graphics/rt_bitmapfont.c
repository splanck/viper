//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_bitmapfont.c
// Purpose: Custom bitmap font loading (BDF/PSF formats) and Canvas rendering.
//   Parses BDF (text-based) and PSF v1/v2 (binary) font files into an internal
//   glyph table, then draws text to a Canvas using the loaded glyphs.
//
// Key invariants:
//   - BDF parser: line-by-line text parsing, hex bitmap decoding.
//   - PSF parser: binary header + sequential raw glyph bitmaps.
//   - Glyph bitmaps are packed 1-bit (MSB-left, row-major), same as rt_font.c.
//   - Rendering uses vgfx_pset / vgfx_fill_rect, matching existing Canvas text.
//
// Ownership/Lifetime:
//   - BitmapFont objects allocated via rt_obj_new_i64 (GC-managed).
//   - Per-glyph bitmap arrays are malloc'd; freed in rt_bitmapfont_destroy.
//
// Links: rt_bitmapfont.h (public API), rt_font.h (built-in font pattern),
//        rt_drawing.c (existing Canvas.Text rendering)
//
//===----------------------------------------------------------------------===//

#include "rt_bitmapfont.h"
#include "rt_error.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Data Structures
//=============================================================================

/// @brief A single glyph in a bitmap font.
typedef struct {
    uint8_t *bitmap;  ///< Packed 1-bit bitmap (row-major, MSB-left). NULL if no glyph.
    int16_t width;    ///< Glyph width in pixels.
    int16_t height;   ///< Glyph height in pixels.
    int16_t x_offset; ///< Horizontal bearing (BDF BBX x-offset).
    int16_t y_offset; ///< Vertical bearing from baseline (BDF BBX y-offset).
    int16_t advance;  ///< Horizontal advance after glyph.
} rt_glyph;

/// @brief Maximum codepoints in the glyph table (full BMP / UTF-16 plane 0).
#define BF_MAX_GLYPHS 65536

/// @brief Internal bitmap font structure.
typedef struct {
    rt_glyph glyphs[BF_MAX_GLYPHS]; ///< Glyph table indexed by codepoint.
    int16_t line_height;            ///< Line height in pixels (ascent + descent).
    int16_t max_width;              ///< Widest glyph advance.
    int16_t ascent;                 ///< Distance from baseline to top.
    int8_t monospace;               ///< 1 if all loaded glyphs have same advance.
    int64_t glyph_count;            ///< Number of valid (non-NULL bitmap) glyphs.
} rt_bitmapfont_impl;

//=============================================================================
// Fallback Glyph
//=============================================================================

/// @brief Bytes per row for a given pixel width (ceil(width/8)).
static inline int bf_row_bytes(int width) {
    return (width + 7) / 8;
}

static int bf_next_codepoint(const char *str, size_t byte_len, size_t *index, int *codepoint_out);

/// @brief Get a glyph for a codepoint, returning a fallback if not available.
static const rt_glyph *bf_get_glyph(const rt_bitmapfont_impl *font, int codepoint) {
    if (codepoint >= 0 && codepoint < BF_MAX_GLYPHS && font->glyphs[codepoint].bitmap)
        return &font->glyphs[codepoint];

    // Fallback: try '?', then space
    if (font->glyphs['?'].bitmap)
        return &font->glyphs['?'];
    if (font->glyphs[' '].bitmap)
        return &font->glyphs[' '];

    return NULL;
}

/// @brief Grow a `(min_x, max_x)` interval to also cover `[left, right)`.
/// @details Initializes the bounds on the first call (`*has_bounds` flips
///          from 0 to 1) so callers don't need a separate initialization
///          pass. Empty spans (`right <= left`) are silently skipped so the
///          caller can pass through every glyph regardless of whether it
///          has visible pixels.
static void bf_extend_bounds(
    int64_t left, int64_t right, int64_t *min_x, int64_t *max_x, int8_t *has_bounds) {
    if (!min_x || !max_x || !has_bounds || right <= left)
        return;

    if (!*has_bounds) {
        *min_x = left;
        *max_x = right;
        *has_bounds = 1;
        return;
    }

    if (left < *min_x)
        *min_x = left;
    if (right > *max_x)
        *max_x = right;
}

/// @brief Compute the horizontal pixel bounds of a rendered string.
/// @details Walks the string codepoint by codepoint, tracking both the
///          per-glyph advance (the cursor's nominal step) and the actual
///          ink extent (which may overhang on either side via positive
///          `x_offset` reaching past the advance, or negative `x_offset`
///          starting before the cursor). The returned `(min_x, max_x)`
///          interval covers the union of advance and ink extents — used by
///          centering / right-alignment / width-measurement APIs to size
///          text accurately even with overhanging glyphs (italics,
///          decorative scripts).
static void bf_text_bounds(
    rt_bitmapfont_impl *font, rt_string text, int64_t *min_x, int64_t *max_x, int8_t *has_bounds) {
    if (min_x)
        *min_x = 0;
    if (max_x)
        *max_x = 0;
    if (has_bounds)
        *has_bounds = 0;
    if (!font || !text || !min_x || !max_x || !has_bounds)
        return;

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t pen_x = 0;
    size_t len = rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;
    while (bf_next_codepoint(str, len, &index, &codepoint)) {
        const rt_glyph *g = bf_get_glyph(font, codepoint);
        int64_t span_left = pen_x;
        int64_t span_right = pen_x + (g ? g->advance : font->max_width);

        if (g && g->bitmap && g->width > 0 && g->height > 0) {
            int64_t glyph_left = pen_x + g->x_offset;
            int64_t glyph_right = glyph_left + g->width;
            if (glyph_left < span_left)
                span_left = glyph_left;
            if (glyph_right > span_right)
                span_right = glyph_right;
        }

        bf_extend_bounds(span_left, span_right, min_x, max_x, has_bounds);
        pen_x += g ? g->advance : font->max_width;
    }
}

/// @brief Decode the UTF-8 codepoint at `*index` and advance `*index` past it.
/// @details Handles 1- through 4-byte UTF-8 sequences with the standard
///          continuation-byte (`0b10xxxxxx`) verification on every trailing
///          byte. Several invalid-input cases collapse to the substitution
///          character `'?'` rather than failing:
///          - **Overlong encoding** (e.g., a 3-byte form for a value
///            < 0x800): rejected. Allowing overlongs is a known
///            security-hole pattern — a string filter that only checks
///            the canonical encoding can be bypassed via overlong
///            re-encoding.
///          - **Surrogate pair half** (`0xD800-0xDFFF`): rejected.
///            UTF-8 must not encode UTF-16 surrogate halves; their sole
///            purpose is in UTF-16 pair encoding.
///          - **Out-of-range codepoint** (above `U+10FFFF`): rejected.
///          - **Truncated trailing byte** (sequence runs past `byte_len`):
///            falls back to a single-byte read.
///          - **Bad continuation byte**: falls back to a single-byte read.
///
///          The "fall back to single byte" cases produce `'?'` and only
///          consume one byte, so the caller resyncs naturally to the next
///          codepoint boundary on the following call.
/// @return 1 if a codepoint was decoded and `*index` advanced; 0 if
///         `*index` is already at or past `byte_len` (end of string).
static int bf_next_codepoint(const char *str, size_t byte_len, size_t *index, int *codepoint_out) {
    if (!str || !index || !codepoint_out || *index >= byte_len)
        return 0;

    size_t i = *index;
    unsigned char c0 = (unsigned char)str[i];
    uint32_t cp = '?';
    size_t advance = 1;

    if (c0 < 0x80) {
        cp = c0;
    } else if ((c0 & 0xE0u) == 0xC0u && i + 1 < byte_len) {
        unsigned char c1 = (unsigned char)str[i + 1];
        if ((c1 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
            advance = 2;
            if (cp < 0x80u)
                cp = '?';
        }
    } else if ((c0 & 0xF0u) == 0xE0u && i + 2 < byte_len) {
        unsigned char c1 = (unsigned char)str[i + 1];
        unsigned char c2 = (unsigned char)str[i + 2];
        if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6) |
                 (uint32_t)(c2 & 0x3Fu);
            advance = 3;
            if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu))
                cp = '?';
        }
    } else if ((c0 & 0xF8u) == 0xF0u && i + 3 < byte_len) {
        unsigned char c1 = (unsigned char)str[i + 1];
        unsigned char c2 = (unsigned char)str[i + 2];
        unsigned char c3 = (unsigned char)str[i + 3];
        if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u && (c3 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x07u) << 18) | ((uint32_t)(c1 & 0x3Fu) << 12) |
                 ((uint32_t)(c2 & 0x3Fu) << 6) | (uint32_t)(c3 & 0x3Fu);
            advance = 4;
            if (cp < 0x10000u || cp > 0x10FFFFu)
                cp = '?';
        }
    }

    *index = i + advance;
    *codepoint_out = (int)cp;
    return 1;
}

static void bf_release_font(rt_bitmapfont_impl *font) {
    if (!font)
        return;
    if (rt_obj_release_check0(font))
        rt_obj_free(font);
}

//=============================================================================
// BDF Parser
//=============================================================================

/// @brief Parse a single hex digit (0-9, a-f, A-F) to value 0-15. Returns -1 on error.
static int bf_hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/// @brief Parse a hex byte from two characters. Returns -1 on error.
static int bf_hex_byte(const char *s) {
    if (!s[0] || !s[1])
        return -1;
    int hi = bf_hex_digit(s[0]);
    int lo = bf_hex_digit(s[1]);
    if (hi < 0 || lo < 0)
        return -1;
    return (hi << 4) | lo;
}

/// @brief Load a font from a BDF (Glyph Bitmap Distribution Format) file.
/// Parses ENCODING, BBX, DWIDTH, BITMAP entries and reconstructs per-glyph 1-bit
/// bitmaps. Supports up to 256 codepoints and fails closed on malformed or
/// truncated files. Returns NULL on file/parse failure or empty font.
void *rt_bitmapfont_load_bdf(rt_string path) {
    if (!path)
        return NULL;

    const char *cpath = rt_string_cstr(path);
    if (!cpath)
        return NULL;

    FILE *f = fopen(cpath, "r");
    if (!f)
        return NULL;

    rt_bitmapfont_impl *font =
        (rt_bitmapfont_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bitmapfont_impl));
    if (!font) {
        fclose(f);
        return NULL;
    }
    memset(font, 0, sizeof(rt_bitmapfont_impl));
    rt_obj_set_finalizer(font, rt_bitmapfont_destroy);

    char line[1024];
    int encoding = -1;
    int bbx_w = 0, bbx_h = 0, bbx_xoff = 0, bbx_yoff = 0;
    int default_bbx_w = 0, default_bbx_h = 0;
    int in_bitmap = 0;
    int bitmap_row = 0;
    uint8_t *cur_bitmap = NULL;
    int font_ascent = 0;
    int first_advance = -1;
    int all_same_advance = 1;
    int dwidth = 0;
    int saw_endfont = 0;
    int parse_failed = 0;

    while (fgets(line, sizeof(line), f)) {
        // Strip trailing whitespace
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = '\0';

        if (in_bitmap) {
            if (strncmp(line, "ENDCHAR", 7) == 0) {
                // Store glyph
                if (bitmap_row != bbx_h) {
                    parse_failed = 1;
                } else if (encoding >= 0 && encoding < BF_MAX_GLYPHS && cur_bitmap) {
                    rt_glyph *g = &font->glyphs[encoding];
                    g->bitmap = cur_bitmap;
                    g->width = (int16_t)bbx_w;
                    g->height = (int16_t)bbx_h;
                    g->x_offset = (int16_t)bbx_xoff;
                    g->y_offset = (int16_t)bbx_yoff;
                    g->advance = (int16_t)(dwidth > 0 ? dwidth : bbx_w);
                    font->glyph_count++;

                    if (g->advance > font->max_width)
                        font->max_width = g->advance;

                    if (first_advance < 0)
                        first_advance = g->advance;
                    else if (g->advance != first_advance)
                        all_same_advance = 0;
                } else {
                    free(cur_bitmap);
                }

                in_bitmap = 0;
                cur_bitmap = NULL;
                encoding = -1;
                dwidth = 0;
            } else {
                // Parse hex row
                if (cur_bitmap && bitmap_row < bbx_h) {
                    int rb = bf_row_bytes(bbx_w);
                    for (int b = 0; b < rb && line[b * 2] != '\0' && line[b * 2 + 1] != '\0'; b++) {
                        int val = bf_hex_byte(&line[b * 2]);
                        if (val >= 0) {
                            cur_bitmap[bitmap_row * rb + b] = (uint8_t)val;
                        } else {
                            parse_failed = 1;
                            break;
                        }
                    }
                    bitmap_row++;
                } else {
                    parse_failed = 1;
                }
            }
            if (parse_failed)
                break;
            continue;
        }

        if (strncmp(line, "ENCODING ", 9) == 0) {
            encoding = atoi(line + 9);
        } else if (strncmp(line, "BBX ", 4) == 0) {
            if (sscanf(line + 4, "%d %d %d %d", &bbx_w, &bbx_h, &bbx_xoff, &bbx_yoff) != 4)
                continue;
        } else if (strncmp(line, "FONTBOUNDINGBOX ", 16) == 0) {
            sscanf(line + 16, "%d %d", &default_bbx_w, &default_bbx_h);
            if (font->line_height == 0)
                font->line_height = (int16_t)default_bbx_h;
        } else if (strncmp(line, "FONT_ASCENT ", 12) == 0) {
            font_ascent = atoi(line + 12);
            font->ascent = (int16_t)font_ascent;
        } else if (strncmp(line, "FONT_DESCENT ", 13) == 0) {
            int descent = atoi(line + 13);
            font->line_height = (int16_t)(font_ascent + descent);
        } else if (strncmp(line, "DWIDTH ", 7) == 0) {
            dwidth = atoi(line + 7);
        } else if (strncmp(line, "BITMAP", 6) == 0 && (line[6] == '\0' || line[6] == '\r')) {
            if (bbx_w <= 0)
                bbx_w = default_bbx_w;
            if (bbx_h <= 0)
                bbx_h = default_bbx_h;

            if (bbx_w <= 0 || bbx_h <= 0 || bbx_w > 4096 || bbx_h > 4096) {
                parse_failed = 1;
                break;
            }
            int rb = bf_row_bytes(bbx_w);
            int64_t alloc_size = (int64_t)rb * bbx_h;
            if (alloc_size > 0 && alloc_size <= 1024 * 1024) {
                cur_bitmap = (uint8_t *)calloc(1, (size_t)alloc_size);
            } else {
                parse_failed = 1;
                break;
            }
            bitmap_row = 0;
            in_bitmap = 1;
        } else if (strncmp(line, "ENDFONT", 7) == 0) {
            saw_endfont = 1;
        }
    }

    fclose(f);
    free(cur_bitmap); /* Free any partial glyph from truncated file */

    if (parse_failed || in_bitmap || !saw_endfont || font->glyph_count == 0) {
        // No glyphs loaded — invalid file
        bf_release_font(font);
        return NULL;
    }

    font->monospace = (int8_t)all_same_advance;

    // If line_height wasn't set by FONT_ASCENT/DESCENT, use bounding box
    if (font->line_height <= 0)
        font->line_height = (int16_t)default_bbx_h;

    return font;
}

//=============================================================================
// PSF Parser
//=============================================================================

/// @brief PSF v1 magic bytes.
#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

/// @brief PSF v2 magic bytes.
#define PSF2_MAGIC0 0x72
#define PSF2_MAGIC1 0xB5
#define PSF2_MAGIC2 0x4A
#define PSF2_MAGIC3 0x86

/// @brief Load a font from a PSF (PC Screen Font) v1 or v2 file.
/// Auto-detects version via magic bytes. Glyphs are always monospace; v1 is
/// fixed at 8 pixels wide, v2 reads width from the header. Returns NULL on
/// magic mismatch or truncated/corrupt input.
void *rt_bitmapfont_load_psf(rt_string path) {
    if (!path)
        return NULL;

    const char *cpath = rt_string_cstr(path);
    if (!cpath)
        return NULL;

    FILE *f = fopen(cpath, "rb");
    if (!f)
        return NULL;

    // Read first 4 bytes to detect version
    uint8_t magic[4];
    if (fread(magic, 1, 4, f) != 4) {
        fclose(f);
        return NULL;
    }

    int glyph_count = 0;
    int glyph_height = 0;
    int glyph_width = 0;
    int glyph_byte_size = 0;
    long data_offset = 0;
    int parse_failed = 0;

    if (magic[0] == PSF1_MAGIC0 && magic[1] == PSF1_MAGIC1) {
        // PSF v1: 4-byte header (magic[2] = mode, magic[3] = charsize)
        glyph_byte_size = magic[3];
        glyph_height = glyph_byte_size; // PSF1: 1 byte per row, rows = charsize
        glyph_width = 8;                // Always 8 pixels wide
        glyph_count = (magic[2] & 0x01) ? 512 : 256;
        data_offset = 4;
    } else if (magic[0] == PSF2_MAGIC0 && magic[1] == PSF2_MAGIC1 && magic[2] == PSF2_MAGIC2 &&
               magic[3] == PSF2_MAGIC3) {
        // PSF v2: 32-byte header
        uint8_t hdr[28]; // Remaining 28 bytes of header
        if (fread(hdr, 1, 28, f) != 28) {
            fclose(f);
            return NULL;
        }

        // Fields are little-endian uint32:
        // offset 0: version
        uint32_t header_size = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                               ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
        // offset 8: flags
        uint32_t num_glyphs = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) |
                              ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
        uint32_t bytes_per_glyph = (uint32_t)hdr[16] | ((uint32_t)hdr[17] << 8) |
                                   ((uint32_t)hdr[18] << 16) | ((uint32_t)hdr[19] << 24);
        uint32_t height = (uint32_t)hdr[20] | ((uint32_t)hdr[21] << 8) | ((uint32_t)hdr[22] << 16) |
                          ((uint32_t)hdr[23] << 24);
        uint32_t width = (uint32_t)hdr[24] | ((uint32_t)hdr[25] << 8) | ((uint32_t)hdr[26] << 16) |
                         ((uint32_t)hdr[27] << 24);

        glyph_count = (int)num_glyphs;
        glyph_height = (int)height;
        glyph_width = (int)width;
        glyph_byte_size = (int)bytes_per_glyph;
        data_offset = (long)header_size;
    } else {
        fclose(f);
        return NULL;
    }

    if (glyph_count <= 0 || glyph_height <= 0 || glyph_width <= 0 || glyph_byte_size <= 0) {
        fclose(f);
        return NULL;
    }

    if (glyph_byte_size < bf_row_bytes(glyph_width) * glyph_height) {
        fclose(f);
        return NULL;
    }

    // Clamp to our max
    if (glyph_count > BF_MAX_GLYPHS)
        glyph_count = BF_MAX_GLYPHS;

    rt_bitmapfont_impl *font =
        (rt_bitmapfont_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_bitmapfont_impl));
    if (!font) {
        fclose(f);
        return NULL;
    }
    memset(font, 0, sizeof(rt_bitmapfont_impl));
    rt_obj_set_finalizer(font, rt_bitmapfont_destroy);

    if (fseek(f, data_offset, SEEK_SET) != 0) {
        fclose(f);
        bf_release_font(font);
        return NULL;
    }

    int rb = bf_row_bytes(glyph_width);

    for (int i = 0; i < glyph_count; i++) {
        uint8_t *raw = (uint8_t *)malloc((size_t)glyph_byte_size);
        if (!raw) {
            parse_failed = 1;
            break;
        }

        if (fread(raw, 1, (size_t)glyph_byte_size, f) != (size_t)glyph_byte_size) {
            free(raw);
            parse_failed = 1;
            break;
        }

        // PSF glyph bitmaps are already packed MSB-left, row-major
        // but we need to copy only the relevant bytes per row
        int64_t alloc_size = (int64_t)rb * glyph_height;
        if (alloc_size <= 0 || alloc_size > 1024 * 1024) {
            free(raw);
            parse_failed = 1;
            break;
        }
        uint8_t *bitmap = (uint8_t *)calloc(1, (size_t)alloc_size);
        if (!bitmap) {
            free(raw);
            parse_failed = 1;
            break;
        }

        // Copy row data (PSF rows may have padding at end)
        int psf_rb = bf_row_bytes(glyph_width);
        for (int row = 0; row < glyph_height && row * psf_rb < glyph_byte_size; row++) {
            int copy = psf_rb < (glyph_byte_size - row * psf_rb) ? psf_rb
                                                                 : (glyph_byte_size - row * psf_rb);
            if (copy > rb)
                copy = rb;
            memcpy(bitmap + row * rb, raw + row * psf_rb, (size_t)copy);
        }
        free(raw);

        rt_glyph *g = &font->glyphs[i];
        g->bitmap = bitmap;
        g->width = (int16_t)glyph_width;
        g->height = (int16_t)glyph_height;
        g->x_offset = 0;
        g->y_offset = 0;
        g->advance = (int16_t)glyph_width;
        font->glyph_count++;
    }

    fclose(f);

    if (parse_failed || font->glyph_count == 0 || font->glyph_count != glyph_count) {
        bf_release_font(font);
        return NULL;
    }

    font->line_height = (int16_t)glyph_height;
    font->max_width = (int16_t)glyph_width;
    font->ascent = (int16_t)glyph_height;
    font->monospace = 1; // PSF fonts are always monospace

    return font;
}

//=============================================================================
// Destructor
//=============================================================================

/// @brief GC finalizer that frees every per-glyph bitmap allocation.
/// The font struct itself is GC-managed.
void rt_bitmapfont_destroy(void *font_ptr) {
    if (!font_ptr)
        return;

    rt_bitmapfont_impl *font = (rt_bitmapfont_impl *)font_ptr;
    for (int i = 0; i < BF_MAX_GLYPHS; i++) {
        free(font->glyphs[i].bitmap);
        font->glyphs[i].bitmap = NULL;
    }
}

//=============================================================================
// Properties
//=============================================================================

/// @brief Glyph width for monospace fonts (returns 0 for proportional fonts).
int64_t rt_bitmapfont_char_width(void *font_ptr) {
    if (!font_ptr)
        return 0;
    rt_bitmapfont_impl *font = (rt_bitmapfont_impl *)font_ptr;
    return font->monospace ? font->max_width : 0;
}

/// @brief Line height in pixels (ascent + descent).
int64_t rt_bitmapfont_char_height(void *font_ptr) {
    if (!font_ptr)
        return 0;
    return ((rt_bitmapfont_impl *)font_ptr)->line_height;
}

/// @brief Number of valid (non-empty) glyphs loaded.
int64_t rt_bitmapfont_glyph_count(void *font_ptr) {
    if (!font_ptr)
        return 0;
    return ((rt_bitmapfont_impl *)font_ptr)->glyph_count;
}

/// @brief Returns 1 if every glyph has the same advance width, 0 otherwise.
int8_t rt_bitmapfont_is_monospace(void *font_ptr) {
    if (!font_ptr)
        return 0;
    return ((rt_bitmapfont_impl *)font_ptr)->monospace;
}

//=============================================================================
// Text Measurement
//=============================================================================

/// @brief Compute the rendered width of @p text in pixels for this font.
/// Accounts for per-glyph advance widths plus left/right overhangs.
int64_t rt_bitmapfont_text_width(void *font_ptr, rt_string text) {
    if (!font_ptr || !text)
        return 0;

    rt_bitmapfont_impl *font = (rt_bitmapfont_impl *)font_ptr;
    int64_t min_x = 0;
    int64_t max_x = 0;
    int8_t has_bounds = 0;
    bf_text_bounds(font, text, &min_x, &max_x, &has_bounds);
    if (!has_bounds)
        return 0;
    return max_x - min_x;
}

/// @brief Single-line text height (same as line_height; multi-line callers must accumulate).
int64_t rt_bitmapfont_text_height(void *font_ptr) {
    if (!font_ptr)
        return 0;
    return ((rt_bitmapfont_impl *)font_ptr)->line_height;
}

//=============================================================================
// Canvas Drawing — Glyph Renderer
//=============================================================================

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_graphics_internal.h"

/// @brief Draw a single glyph at `(px, py)` using `vgfx_pset` per lit pixel.
/// @details Resolves the destination as `(px + x_offset, py + (ascent -
///          y_offset - height))` so the BDF baseline / bearing offsets
///          translate correctly to a top-of-line `(px, py)` reference. The
///          glyph bitmap is packed MSB-left, row-major; iterates row × col
///          and emits a `vgfx_pset` for each set bit.
static void bf_draw_glyph(vgfx_window_t win,
                          const rt_glyph *g,
                          int64_t px,
                          int64_t py,
                          int16_t ascent,
                          vgfx_color_t color) {
    if (!g || !g->bitmap)
        return;

    // BDF y_offset is from baseline; we draw relative to top of line
    int64_t draw_x = px + g->x_offset;
    int64_t draw_y = py + (ascent - g->y_offset - g->height);
    int rb = bf_row_bytes(g->width);

    for (int row = 0; row < g->height; row++) {
        for (int col = 0; col < g->width; col++) {
            int byte_idx = col / 8;
            int bit_idx = 7 - (col % 8);
            if (g->bitmap[row * rb + byte_idx] & (1 << bit_idx)) {
                vgfx_pset(win, (int32_t)(draw_x + col), (int32_t)(draw_y + row), color);
            }
        }
    }
}

/// @brief Draw a single glyph with integer scaling.
static void bf_draw_glyph_scaled(vgfx_window_t win,
                                 const rt_glyph *g,
                                 int64_t px,
                                 int64_t py,
                                 int16_t ascent,
                                 int64_t scale,
                                 vgfx_color_t color) {
    if (!g || !g->bitmap || scale < 1)
        return;

    int64_t draw_x = px + g->x_offset * scale;
    int64_t draw_y = py + (ascent - g->y_offset - g->height) * scale;
    int rb = bf_row_bytes(g->width);

    for (int row = 0; row < g->height; row++) {
        for (int col = 0; col < g->width; col++) {
            int byte_idx = col / 8;
            int bit_idx = 7 - (col % 8);
            if (g->bitmap[row * rb + byte_idx] & (1 << bit_idx)) {
                vgfx_fill_rect(win,
                               (int32_t)(draw_x + col * scale),
                               (int32_t)(draw_y + row * scale),
                               (int32_t)scale,
                               (int32_t)scale,
                               color);
            }
        }
    }
}

/// @brief Draw a single glyph with foreground and background colors.
static void bf_draw_glyph_bg(vgfx_window_t win,
                             const rt_glyph *g,
                             int64_t px,
                             int64_t py,
                             int16_t ascent,
                             int16_t line_h,
                             vgfx_color_t fg,
                             vgfx_color_t bg) {
    if (!g)
        return;

    int64_t bg_left = px;
    int64_t bg_right = px + g->advance;

    if (g->bitmap) {
        int64_t draw_x = px + g->x_offset;
        int64_t draw_y = py + (ascent - g->y_offset - g->height);
        if (draw_x < bg_left)
            bg_left = draw_x;
        if (draw_x + g->width > bg_right)
            bg_right = draw_x + g->width;

        if (bg_right > bg_left) {
            vgfx_fill_rect(
                win, (int32_t)bg_left, (int32_t)py, (int32_t)(bg_right - bg_left), (int32_t)line_h, bg);
        }

        int rb = bf_row_bytes(g->width);

        for (int row = 0; row < g->height; row++) {
            for (int col = 0; col < g->width; col++) {
                int byte_idx = col / 8;
                int bit_idx = 7 - (col % 8);
                if (g->bitmap[row * rb + byte_idx] & (1 << bit_idx)) {
                    vgfx_pset(win, (int32_t)(draw_x + col), (int32_t)(draw_y + row), fg);
                }
            }
        }
    } else if (bg_right > bg_left) {
        vgfx_fill_rect(
            win, (int32_t)bg_left, (int32_t)py, (int32_t)(bg_right - bg_left), (int32_t)line_h, bg);
    }
}

//=============================================================================
// Canvas Drawing — Public API
//=============================================================================

/// @brief Render a string at (x, y) on @p canvas using the bitmap font.
/// @p y is the top of the line; per-glyph baseline offsets are applied internally.
/// Color is the canvas color format (typically 0x00RRGGBB).
void rt_canvas_text_font(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, void *font_ptr, int64_t color) {
    if (!canvas_ptr || !font_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    rt_bitmapfont_impl *font = (rt_bitmapfont_impl *)font_ptr;
    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    vgfx_color_t col = (vgfx_color_t)color;
    int64_t cx = x;
    size_t len = rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (bf_next_codepoint(str, len, &index, &codepoint)) {
        const rt_glyph *g = bf_get_glyph(font, codepoint);
        if (g) {
            bf_draw_glyph(canvas->gfx_win, g, cx, y, font->ascent, col);
            cx += g->advance;
        } else {
            cx += font->max_width;
        }
    }
}

/// @brief Like `_text_font` but fills the glyph advance × line_height background first.
/// Useful for opaque text overlays (status bars, code editors).
void rt_canvas_text_font_bg(void *canvas_ptr,
                            int64_t x,
                            int64_t y,
                            rt_string text,
                            void *font_ptr,
                            int64_t fg_color,
                            int64_t bg_color) {
    if (!canvas_ptr || !font_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    rt_bitmapfont_impl *font = (rt_bitmapfont_impl *)font_ptr;
    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    vgfx_color_t fg = (vgfx_color_t)fg_color;
    vgfx_color_t bg = (vgfx_color_t)bg_color;
    int64_t cx = x;
    size_t len = rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (bf_next_codepoint(str, len, &index, &codepoint)) {
        const rt_glyph *g = bf_get_glyph(font, codepoint);
        if (g) {
            bf_draw_glyph_bg(canvas->gfx_win, g, cx, y, font->ascent, font->line_height, fg, bg);
            cx += g->advance;
        } else {
            vgfx_fill_rect(canvas->gfx_win,
                           (int32_t)cx,
                           (int32_t)y,
                           (int32_t)font->max_width,
                           (int32_t)font->line_height,
                           bg);
            cx += font->max_width;
        }
    }
}

/// @brief Render text at integer scale (each glyph pixel becomes a `scale × scale` rect).
/// @p scale must be >= 1; smaller values cause the call to be a silent no-op.
void rt_canvas_text_font_scaled(void *canvas_ptr,
                                int64_t x,
                                int64_t y,
                                rt_string text,
                                void *font_ptr,
                                int64_t scale,
                                int64_t color) {
    if (!canvas_ptr || !font_ptr || !text || scale < 1)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    rt_bitmapfont_impl *font = (rt_bitmapfont_impl *)font_ptr;
    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    vgfx_color_t col = (vgfx_color_t)color;
    int64_t cx = x;
    size_t len = rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (bf_next_codepoint(str, len, &index, &codepoint)) {
        const rt_glyph *g = bf_get_glyph(font, codepoint);
        if (g) {
            bf_draw_glyph_scaled(canvas->gfx_win, g, cx, y, font->ascent, scale, col);
            cx += g->advance * scale;
        } else {
            cx += font->max_width * scale;
        }
    }
}

/// @brief Render text horizontally centered in the canvas at row @p y.
/// Width is derived from the canvas size; uses `_text_font` underneath.
void rt_canvas_text_font_centered(
    void *canvas_ptr, int64_t y, rt_string text, void *font_ptr, int64_t color) {
    if (!canvas_ptr || !font_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    int32_t win_w = 0, win_h = 0;
    vgfx_get_size(canvas->gfx_win, &win_w, &win_h);

    int64_t min_x = 0;
    int64_t max_x = 0;
    int8_t has_bounds = 0;
    bf_text_bounds((rt_bitmapfont_impl *)font_ptr, text, &min_x, &max_x, &has_bounds);
    int64_t tw = has_bounds ? (max_x - min_x) : 0;
    int64_t cx = (win_w - tw) / 2 - min_x;

    rt_canvas_text_font(canvas_ptr, cx, y, text, font_ptr, color);
}

/// @brief Render text right-aligned with @p margin pixels of padding from the canvas right edge.
void rt_canvas_text_font_right(
    void *canvas_ptr, int64_t margin, int64_t y, rt_string text, void *font_ptr, int64_t color) {
    if (!canvas_ptr || !font_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    int32_t win_w = 0, win_h = 0;
    vgfx_get_size(canvas->gfx_win, &win_w, &win_h);

    int64_t min_x = 0;
    int64_t max_x = 0;
    int8_t has_bounds = 0;
    bf_text_bounds((rt_bitmapfont_impl *)font_ptr, text, &min_x, &max_x, &has_bounds);
    int64_t cx = has_bounds ? (win_w - margin - max_x) : (win_w - margin);

    rt_canvas_text_font(canvas_ptr, cx, y, text, font_ptr, color);
}

#else // !VIPER_ENABLE_GRAPHICS — stubs

static void rt_bitmapfont_canvas_unavailable_(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, 0, msg);
}

void rt_canvas_text_font(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)font;
    (void)color;
    rt_bitmapfont_canvas_unavailable_("Canvas.TextFont: graphics support not compiled in");
}

void rt_canvas_text_font_bg(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t fg, int64_t bg) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)font;
    (void)fg;
    (void)bg;
    rt_bitmapfont_canvas_unavailable_("Canvas.TextFontBg: graphics support not compiled in");
}

void rt_canvas_text_font_scaled(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t scale, int64_t color) {
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)font;
    (void)scale;
    (void)color;
    rt_bitmapfont_canvas_unavailable_("Canvas.TextFontScaled: graphics support not compiled in");
}

void rt_canvas_text_font_centered(
    void *canvas, int64_t y, rt_string text, void *font, int64_t color) {
    (void)canvas;
    (void)y;
    (void)text;
    (void)font;
    (void)color;
    rt_bitmapfont_canvas_unavailable_("Canvas.TextFontCentered: graphics support not compiled in");
}

void rt_canvas_text_font_right(
    void *canvas, int64_t margin, int64_t y, rt_string text, void *font, int64_t color) {
    (void)canvas;
    (void)margin;
    (void)y;
    (void)text;
    (void)font;
    (void)color;
    rt_bitmapfont_canvas_unavailable_("Canvas.TextFontRight: graphics support not compiled in");
}

#endif // VIPER_ENABLE_GRAPHICS
