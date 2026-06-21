//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/font/vg_font.c
// Purpose: Main font API implementation — loading, destruction, glyph
//          rasterization (with cache), kerning lookup, UTF-8 utilities,
//          text measurement, cursor hit-testing, and canvas text rendering.
// Key invariants:
//   - vg_font_get_glyph always returns a cache-backed pointer; the caller must
//     not free it.
//   - vg_font_draw_text delegates pixel output to the extern
//     vg_canvas_draw_glyph (implemented in vg_canvas_integration.c).
//   - UTF-8 decode advances *str past the consumed sequence even on error,
//     returning U+FFFD to allow resilient iteration.
// Ownership/Lifetime:
//   - vg_font_load copies the data buffer; the caller may free the original.
//   - vg_font_load_file allocates and frees the read buffer internally.
//   - vg_font_destroy frees the font and all sub-arrays including the cache.
// Links: lib/gui/include/vg_font.h,
//        lib/gui/src/font/vg_ttf_internal.h,
//        lib/gui/src/font/vg_cache.c,
//        lib/gui/src/font/vg_raster.c,
//        lib/gui/src/font/vg_canvas_integration.c
//
//===----------------------------------------------------------------------===//
#include "vg_ttf_internal.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Live Font Registry
//=============================================================================

#define VG_FONT_MAGIC UINT64_C(0x564750464F4E5431)
#define VG_FONT_DESTROYED_MAGIC UINT64_C(0x564750464F4E5444)

static vg_font_t *g_live_fonts = NULL;

static void vg_font_register_live(vg_font_t *font) {
    if (!font)
        return;
    font->magic = VG_FONT_MAGIC;
    font->live_prev = NULL;
    font->live_next = g_live_fonts;
    if (g_live_fonts)
        g_live_fonts->live_prev = font;
    g_live_fonts = font;
}

static void vg_font_unregister_live(vg_font_t *font) {
    if (!font)
        return;
    if (font->live_prev)
        font->live_prev->live_next = font->live_next;
    else if (g_live_fonts == font)
        g_live_fonts = font->live_next;
    if (font->live_next)
        font->live_next->live_prev = font->live_prev;
    font->live_prev = NULL;
    font->live_next = NULL;
}

bool vg_font_is_live(const vg_font_t *font) {
    if (!font)
        return false;
    for (const vg_font_t *live = g_live_fonts; live; live = live->live_next) {
        if (live == font)
            return live->magic == VG_FONT_MAGIC;
    }
    return false;
}

//=============================================================================
// Font Loading
//=============================================================================

/// @brief True if @p size is a finite, positive, sanely-bounded font size.
static bool vg_font_valid_size(float size) {
    return isfinite(size) && size > 0.0f && size <= 1000000.0f;
}

/// @brief Clamp-convert a font metric to int (NaN→0, saturates at INT_MIN/MAX).
static int vg_font_metric_to_int(double value) {
    if (!isfinite(value))
        return 0;
    if (value > (double)INT_MAX)
        return INT_MAX;
    if (value < (double)INT_MIN)
        return INT_MIN;
    return (int)value;
}

/// @brief Load a font from an in-memory TTF buffer.
///
/// @details Copies the buffer, parses all required TTF tables, and creates a
///          glyph cache. Returns NULL on allocation failure, parse error, or if
///          any required table is missing.
///
/// @param data Pointer to the raw TTF font data.
/// @param size Length of the data buffer in bytes (must be >= 12).
/// @return A newly allocated vg_font_t, or NULL on failure.
vg_font_t *vg_font_load(const uint8_t *data, size_t size) {
    if (!data || size < 12)
        return NULL;

    vg_font_t *font = calloc(1, sizeof(vg_font_t));
    if (!font)
        return NULL;

    // Copy data
    font->data = malloc(size);
    if (!font->data) {
        free(font);
        return NULL;
    }
    memcpy(font->data, data, size);
    font->data_size = size;
    font->owns_data = true;
    vg_font_register_live(font);

    // Parse tables
    if (!ttf_parse_tables(font)) {
        vg_font_destroy(font);
        return NULL;
    }

    // Create glyph cache
    font->cache = vg_cache_create();
    if (!font->cache) {
        vg_font_destroy(font);
        return NULL;
    }

    // Set default name if not found
    if (font->family_name[0] == '\0') {
        strcpy(font->family_name, "Unknown");
    }

    return font;
}

/// @brief Load a font from a file path.
///
/// @details Reads the entire file into a temporary buffer and delegates to
///          vg_font_load. Rejects files larger than 100 MB.
///
/// @param path Null-terminated path to a TrueType font file.
/// @return A newly allocated vg_font_t, or NULL on I/O or parse failure.
vg_font_t *vg_font_load_file(const char *path) {
    if (!path)
        return NULL;

    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    // Get file size
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    if (size <= 0 || size > 100 * 1024 * 1024) { // Max 100MB
        fclose(f);
        return NULL;
    }

    // Read file
    uint8_t *data = malloc((size_t)size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);

    // Load font
    vg_font_t *font = vg_font_load(data, (size_t)size);
    free(data);

    return font;
}

/// @brief Destroy a font and free all associated resources.
///
/// @details Frees the glyph cache, all CMAP arrays, kerning pairs, and (when
///          owns_data is true) the font data buffer. Safe to call with NULL.
///
/// @param font The font to destroy (may be NULL).
void vg_font_destroy(vg_font_t *font) {
    if (!vg_font_is_live(font))
        return;
    vg_font_unregister_live(font);
    font->magic = VG_FONT_DESTROYED_MAGIC;

    // Free cache
    if (font->cache) {
        vg_cache_destroy(font->cache);
    }

    // Free CMAP data
    free(font->cmap4_end_codes);
    free(font->cmap4_start_codes);
    free(font->cmap4_id_deltas);
    free(font->cmap4_id_range_offsets);
    free(font->cmap4_glyph_ids);
    free(font->cmap12_start_codes);
    free(font->cmap12_end_codes);
    free(font->cmap12_start_glyph_ids);

    // Free kerning data
    free(font->kern_pairs);

    // Free font data
    if (font->owns_data) {
        free(font->data);
    }

    free(font);
}

//=============================================================================
// Font Information
//=============================================================================

/// @brief Query typographic metrics for a font at a given pixel size.
///
/// @param font    The font to query.
/// @param size    Target font size in pixels.
/// @param metrics Output struct populated with ascent, descent, line_height,
///               and units_per_em. No-op if font or metrics is NULL.
void vg_font_get_metrics(vg_font_t *font, float size, vg_font_metrics_t *metrics) {
    if (!font || !metrics)
        return;
    metrics->ascent = 0;
    metrics->descent = 0;
    metrics->line_height = 0;
    metrics->units_per_em = font->head.units_per_em;
    if (!vg_font_valid_size(size) || font->head.units_per_em == 0)
        return;

    float scale = size / (float)font->head.units_per_em;

    metrics->ascent = vg_font_metric_to_int((double)font->hhea.ascent * scale + 0.5);
    metrics->descent =
        vg_font_metric_to_int((double)font->hhea.descent * scale - 0.5); // Usually negative
    metrics->line_height = vg_font_metric_to_int(
        (double)(font->hhea.ascent - font->hhea.descent + font->hhea.line_gap) * scale + 0.5);
    metrics->units_per_em = font->head.units_per_em;
}

/// @brief Return the font family name string parsed from the 'name' table.
///
/// @param font The font to query.
/// @return Null-terminated family name, or "Unknown" if font is NULL.
const char *vg_font_get_family(vg_font_t *font) {
    if (!font)
        return "Unknown";
    return font->family_name;
}

/// @brief Test whether the font has a glyph for the given Unicode codepoint.
///
/// @param font      The font to query.
/// @param codepoint Unicode codepoint to test.
/// @return true if a non-zero glyph index exists for the codepoint; false otherwise.
bool vg_font_has_glyph(vg_font_t *font, uint32_t codepoint) {
    if (!font)
        return false;
    return ttf_get_glyph_index(font, codepoint) != 0;
}

//=============================================================================
// Glyph Rasterization (with caching)
//=============================================================================

/// @brief Retrieve a rasterised glyph, populating the cache on first access.
///
/// @details Checks the glyph cache first; on a miss, rasterises and inserts.
///          The returned pointer is owned by the cache and must not be freed.
///
/// @param font      The font to rasterise from.
/// @param size      Target font size in pixels (must be finite and > 0).
/// @param codepoint Unicode codepoint to look up.
/// @return Pointer to a cache-owned vg_glyph_t, or NULL on error.
const vg_glyph_t *vg_font_get_glyph(vg_font_t *font, float size, uint32_t codepoint) {
    if (!font || !vg_font_valid_size(size))
        return NULL;

    // Check cache first
    const vg_glyph_t *cached = vg_cache_get(font->cache, size, codepoint);
    if (cached)
        return cached;

    // Get glyph index
    uint16_t glyph_id = ttf_get_glyph_index(font, codepoint);

    // Rasterize
    vg_glyph_t *glyph = vg_rasterize_glyph(font, glyph_id, size);
    if (!glyph)
        return NULL;

    glyph->codepoint = codepoint;

    // Add to cache
    vg_cache_put(font->cache, size, codepoint, glyph);

    // Free the temporary glyph (cache made a copy)
    free(glyph->bitmap);
    free(glyph);

    // Return cached version
    return vg_cache_get(font->cache, size, codepoint);
}

//=============================================================================
// Kerning
//=============================================================================

/// @brief Return the kerning adjustment in pixels between two adjacent codepoints.
///
/// @details Converts both codepoints to glyph indices, then performs a binary
///          search over the sorted kern_pairs array. Returns 0.0 if no pair
///          is found or if the font has no kerning data.
///
/// @param font  The font to query.
/// @param size  Font size in pixels (used to scale design-unit values).
/// @param left  Unicode codepoint of the left (preceding) character.
/// @param right Unicode codepoint of the right (following) character.
/// @return Kerning adjustment in pixels (may be negative to tighten pairs).
float vg_font_get_kerning(vg_font_t *font, float size, uint32_t left, uint32_t right) {
    if (!font || font->kern_pair_count == 0)
        return 0;

    uint16_t left_id = ttf_get_glyph_index(font, left);
    uint16_t right_id = ttf_get_glyph_index(font, right);

    // Binary search in kerning pairs (they should be sorted)
    int lo = 0;
    int hi = font->kern_pair_count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        ttf_kern_pair_t *pair = &font->kern_pairs[mid];

        if (pair->left == left_id && pair->right == right_id) {
            float scale = size / (float)font->head.units_per_em;
            return pair->value * scale;
        }

        // Compare as 32-bit key
        uint32_t pair_key = ((uint32_t)pair->left << 16) | pair->right;
        uint32_t search_key = ((uint32_t)left_id << 16) | right_id;

        if (pair_key < search_key) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    return 0;
}

//=============================================================================
// UTF-8 Utilities
//=============================================================================

/// @brief Decode one UTF-8 codepoint and advance the string pointer.
///
/// @details Handles 1–4 byte sequences. On any encoding error (overlong,
///          surrogate, out-of-range), the pointer is advanced by one byte
///          and U+FFFD (replacement character) is returned to allow resilient
///          iteration.
///
/// @param str Pointer to the current position in a null-terminated UTF-8 string.
///            Advanced past the consumed sequence (or one byte on error).
/// @return The decoded Unicode codepoint, 0 at end-of-string, or U+FFFD on error.
uint32_t vg_utf8_decode(const char **str) {
    if (!str || !*str)
        return 0;

    const uint8_t *s = (const uint8_t *)*str;
    uint32_t cp = 0;

    if (s[0] == 0) {
        return 0;
    } else if ((s[0] & 0x80) == 0) {
        // 1-byte sequence (ASCII)
        cp = s[0];
        *str += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        if (s[1] == 0 || (s[1] & 0xC0) != 0x80) {
            *str += 1; // Skip invalid leading byte so callers always advance
            return 0xFFFD;
        }
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        if (cp < 0x80) {
            *str += 1;
            return 0xFFFD;
        }
        *str += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        if (s[1] == 0 || s[2] == 0 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) {
            *str += 1;
            return 0xFFFD;
        }
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
            *str += 1;
            return 0xFFFD;
        }
        *str += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        if (s[1] == 0 || s[2] == 0 || s[3] == 0 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 ||
            (s[3] & 0xC0) != 0x80) {
            *str += 1;
            return 0xFFFD;
        }
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) {
            *str += 1;
            return 0xFFFD;
        }
        *str += 4;
    } else {
        // Invalid UTF-8
        *str += 1;
        return 0xFFFD; // Replacement character
    }

    return cp;
}

/// @brief Count the number of Unicode codepoints in a null-terminated UTF-8 string.
///
/// @param str Null-terminated UTF-8 string (may be NULL).
/// @return Number of decoded codepoints, or 0 if str is NULL.
int vg_utf8_strlen(const char *str) {
    if (!str)
        return 0;

    int count = 0;
    while (*str) {
        vg_utf8_decode(&str);
        count++;
    }
    return count;
}

/// @brief Return the byte offset of the codepoint at a given character index.
///
/// @param str   Null-terminated UTF-8 string.
/// @param index Zero-based character (codepoint) index to locate.
/// @return Byte offset from str to the start of the character at index, or 0 if
///         str is NULL or index is past the end.
int vg_utf8_offset(const char *str, int index) {
    if (!str)
        return 0;

    const char *start = str;
    for (int i = 0; i < index && *str; i++) {
        vg_utf8_decode(&str);
    }
    return (int)(str - start);
}

//=============================================================================
// Text Measurement
//=============================================================================

/// @brief Measure the pixel dimensions of a UTF-8 text string.
///
/// @details Iterates codepoints, accumulates advance widths including kerning,
///          and returns the total width and line height. Does not handle
///          multi-line text (newlines are counted as zero-advance glyphs).
///
/// @param font    The font to measure with.
/// @param size    Font size in pixels.
/// @param text    Null-terminated UTF-8 string to measure.
/// @param metrics Output struct receiving width, height, and glyph_count.
///               Zeroed on entry; no-op if metrics is NULL.
void vg_font_measure_text(vg_font_t *font,
                          float size,
                          const char *text,
                          vg_text_metrics_t *metrics) {
    if (!metrics)
        return;
    metrics->width = 0;
    metrics->height = 0;
    metrics->glyph_count = 0;

    if (!font || !text || !vg_font_valid_size(size))
        return;

    vg_font_metrics_t fm;
    vg_font_get_metrics(font, size, &fm);
    metrics->height = (float)fm.line_height;

    float x = 0;
    uint32_t prev_cp = 0;
    const char *p = text;

    while (*p) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0)
            break;

        // Add kerning
        if (prev_cp) {
            x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t *glyph = vg_font_get_glyph(font, size, cp);
        if (glyph) {
            x += glyph->advance;
            metrics->glyph_count++;
        }

        prev_cp = cp;
    }

    metrics->width = x;
}

//=============================================================================
// Hit Testing
//=============================================================================

/// @brief Map a pixel x-coordinate to the nearest character index in a text string.
///
/// @details Accumulates advance widths with kerning; returns the index of the
///          character whose left-to-right midpoint is closest to target_x.
///          Returns the length of the string when target_x is past the last glyph.
///
/// @param font     The font to measure with.
/// @param size     Font size in pixels.
/// @param text     Null-terminated UTF-8 string.
/// @param target_x Pixel x-coordinate to map (relative to the text origin).
/// @return Zero-based character index, or -1 on invalid input.
int vg_font_hit_test(vg_font_t *font, float size, const char *text, float target_x) {
    if (!font || !text || !vg_font_valid_size(size))
        return -1;

    float x = 0;
    uint32_t prev_cp = 0;
    const char *p = text;
    int index = 0;

    while (*p) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0)
            break;

        // Add kerning
        if (prev_cp) {
            x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t *glyph = vg_font_get_glyph(font, size, cp);
        if (glyph) {
            float glyph_center = x + glyph->advance * 0.5f;
            if (target_x < glyph_center) {
                return index;
            }
            x += glyph->advance;
        }

        prev_cp = cp;
        index++;
    }

    return index; // Past end
}

/// @brief Return the pixel x-position of the cursor before a given character index.
///
/// @details Iterates codepoints accumulating advance widths and kerning until
///          target_index is reached. Used for cursor positioning in text editors.
///
/// @param font         The font to measure with.
/// @param size         Font size in pixels.
/// @param text         Null-terminated UTF-8 string.
/// @param target_index Zero-based index of the character before which the cursor
///                     should appear. Clamped to the end of the string.
/// @return Pixel x-offset from the text origin, or 0 on invalid input.
float vg_font_get_cursor_x(vg_font_t *font, float size, const char *text, int target_index) {
    if (!font || !text || !vg_font_valid_size(size) || target_index < 0)
        return 0;

    float x = 0;
    uint32_t prev_cp = 0;
    const char *p = text;
    int index = 0;

    while (*p && index < target_index) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0)
            break;

        // Add kerning
        if (prev_cp) {
            x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t *glyph = vg_font_get_glyph(font, size, cp);
        if (glyph) {
            x += glyph->advance;
        }

        prev_cp = cp;
        index++;
    }

    return x;
}

//=============================================================================
// Text Rendering
//=============================================================================

// Forward declaration of canvas drawing function
// This will be implemented in the integration layer
extern void vg_canvas_draw_glyph(
    void *canvas, int x, int y, const uint8_t *bitmap, int width, int height, uint32_t color);

/// @brief Render a UTF-8 string onto a canvas at the given baseline position.
///
/// @details Iterates codepoints, applying kerning between adjacent glyphs and
///          advancing the cursor by each glyph's advance width. Newlines reset
///          the cursor x to x and increment y by line_height. Delegates pixel
///          output to vg_canvas_draw_glyph.
///
/// @param canvas vgfx_window_t handle to draw into.
/// @param font   The font to render with.
/// @param size   Font size in pixels.
/// @param x      Pixel x-coordinate of the left edge of the first glyph.
/// @param y      Pixel y-coordinate of the baseline.
/// @param text   Null-terminated UTF-8 string to render.
/// @param color  Foreground colour in 0xRRGGBB format.
void vg_font_draw_text(
    void *canvas, vg_font_t *font, float size, float x, float y, const char *text, uint32_t color) {
    if (!canvas || !font || !text || size <= 0)
        return;

    float cursor_x = x;
    uint32_t prev_cp = 0;
    const char *p = text;

    while (*p) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0)
            break;

        // Handle newlines
        if (cp == '\n') {
            vg_font_metrics_t fm;
            vg_font_get_metrics(font, size, &fm);
            cursor_x = x;
            y += fm.line_height;
            prev_cp = 0;
            continue;
        }

        // Add kerning
        if (prev_cp) {
            cursor_x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t *glyph = vg_font_get_glyph(font, size, cp);
        if (glyph && glyph->bitmap) {
            // Calculate draw position
            int draw_x = (int)(cursor_x + glyph->bearing_x + 0.5f);
            int draw_y = (int)(y - glyph->bearing_y + 0.5f);

            // Draw glyph
            vg_canvas_draw_glyph(
                canvas, draw_x, draw_y, glyph->bitmap, glyph->width, glyph->height, color);
        }

        if (glyph) {
            cursor_x += glyph->advance;
        }

        prev_cp = cp;
    }
}
