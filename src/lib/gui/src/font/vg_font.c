// vg_font.c - Main font API implementation
#include "vg_ttf_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//=============================================================================
// Font Loading
//=============================================================================

vg_font_t* vg_font_load(const uint8_t* data, size_t size) {
    if (!data || size < 12) return NULL;

    vg_font_t* font = calloc(1, sizeof(vg_font_t));
    if (!font) return NULL;

    // Copy data
    font->data = malloc(size);
    if (!font->data) {
        free(font);
        return NULL;
    }
    memcpy(font->data, data, size);
    font->data_size = size;
    font->owns_data = true;

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

vg_font_t* vg_font_load_file(const char* path) {
    if (!path) return NULL;

    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 100 * 1024 * 1024) {  // Max 100MB
        fclose(f);
        return NULL;
    }

    // Read file
    uint8_t* data = malloc(size);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1, size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }

    fclose(f);

    // Load font
    vg_font_t* font = vg_font_load(data, size);
    free(data);

    return font;
}

void vg_font_destroy(vg_font_t* font) {
    if (!font) return;

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

void vg_font_get_metrics(vg_font_t* font, float size, vg_font_metrics_t* metrics) {
    if (!font || !metrics) return;

    float scale = size / (float)font->head.units_per_em;

    metrics->ascent = (int)(font->hhea.ascent * scale + 0.5f);
    metrics->descent = (int)(font->hhea.descent * scale - 0.5f);  // Usually negative
    metrics->line_height = (int)((font->hhea.ascent - font->hhea.descent + font->hhea.line_gap) * scale + 0.5f);
    metrics->units_per_em = font->head.units_per_em;
}

const char* vg_font_get_family(vg_font_t* font) {
    if (!font) return "Unknown";
    return font->family_name;
}

bool vg_font_has_glyph(vg_font_t* font, uint32_t codepoint) {
    if (!font) return false;
    return ttf_get_glyph_index(font, codepoint) != 0;
}

//=============================================================================
// Glyph Rasterization (with caching)
//=============================================================================

const vg_glyph_t* vg_font_get_glyph(vg_font_t* font, float size, uint32_t codepoint) {
    if (!font || size <= 0) return NULL;

    // Check cache first
    const vg_glyph_t* cached = vg_cache_get(font->cache, size, codepoint);
    if (cached) return cached;

    // Get glyph index
    uint16_t glyph_id = ttf_get_glyph_index(font, codepoint);

    // Rasterize
    vg_glyph_t* glyph = vg_rasterize_glyph(font, glyph_id, size);
    if (!glyph) return NULL;

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

float vg_font_get_kerning(vg_font_t* font, float size, uint32_t left, uint32_t right) {
    if (!font || font->kern_pair_count == 0) return 0;

    uint16_t left_id = ttf_get_glyph_index(font, left);
    uint16_t right_id = ttf_get_glyph_index(font, right);

    // Binary search in kerning pairs (they should be sorted)
    int lo = 0;
    int hi = font->kern_pair_count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        ttf_kern_pair_t* pair = &font->kern_pairs[mid];

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

uint32_t vg_utf8_decode(const char** str) {
    if (!str || !*str) return 0;

    const uint8_t* s = (const uint8_t*)*str;
    uint32_t cp;

    if (s[0] == 0) {
        return 0;
    } else if ((s[0] & 0x80) == 0) {
        // 1-byte sequence (ASCII)
        cp = s[0];
        *str += 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        if ((s[1] & 0xC0) != 0x80) return 0;
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *str += 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *str += 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *str += 4;
    } else {
        // Invalid UTF-8
        *str += 1;
        return 0xFFFD;  // Replacement character
    }

    return cp;
}

int vg_utf8_strlen(const char* str) {
    if (!str) return 0;

    int count = 0;
    while (*str) {
        vg_utf8_decode(&str);
        count++;
    }
    return count;
}

int vg_utf8_offset(const char* str, int index) {
    if (!str) return 0;

    const char* start = str;
    for (int i = 0; i < index && *str; i++) {
        vg_utf8_decode(&str);
    }
    return (int)(str - start);
}

//=============================================================================
// Text Measurement
//=============================================================================

void vg_font_measure_text(vg_font_t* font, float size, const char* text,
                          vg_text_metrics_t* metrics) {
    if (!metrics) return;
    metrics->width = 0;
    metrics->height = 0;
    metrics->glyph_count = 0;

    if (!font || !text || size <= 0) return;

    vg_font_metrics_t fm;
    vg_font_get_metrics(font, size, &fm);
    metrics->height = (float)fm.line_height;

    float x = 0;
    uint32_t prev_cp = 0;
    const char* p = text;

    while (*p) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0) break;

        // Add kerning
        if (prev_cp) {
            x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t* glyph = vg_font_get_glyph(font, size, cp);
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

int vg_font_hit_test(vg_font_t* font, float size, const char* text, float target_x) {
    if (!font || !text || size <= 0) return -1;

    float x = 0;
    uint32_t prev_cp = 0;
    const char* p = text;
    int index = 0;

    while (*p) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0) break;

        // Add kerning
        if (prev_cp) {
            x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t* glyph = vg_font_get_glyph(font, size, cp);
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

    return index;  // Past end
}

float vg_font_get_cursor_x(vg_font_t* font, float size, const char* text, int target_index) {
    if (!font || !text || size <= 0 || target_index < 0) return 0;

    float x = 0;
    uint32_t prev_cp = 0;
    const char* p = text;
    int index = 0;

    while (*p && index < target_index) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0) break;

        // Add kerning
        if (prev_cp) {
            x += vg_font_get_kerning(font, size, prev_cp, cp);
        }

        // Get glyph
        const vg_glyph_t* glyph = vg_font_get_glyph(font, size, cp);
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
extern void vg_canvas_draw_glyph(void* canvas, int x, int y,
                                  const uint8_t* bitmap, int width, int height,
                                  uint32_t color);

void vg_font_draw_text(void* canvas, vg_font_t* font, float size,
                       float x, float y, const char* text, uint32_t color) {
    if (!canvas || !font || !text || size <= 0) return;

    float cursor_x = x;
    uint32_t prev_cp = 0;
    const char* p = text;

    while (*p) {
        uint32_t cp = vg_utf8_decode(&p);
        if (cp == 0) break;

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
        const vg_glyph_t* glyph = vg_font_get_glyph(font, size, cp);
        if (glyph && glyph->bitmap) {
            // Calculate draw position
            int draw_x = (int)(cursor_x + glyph->bearing_x + 0.5f);
            int draw_y = (int)(y - glyph->bearing_y + 0.5f);

            // Draw glyph
            vg_canvas_draw_glyph(canvas, draw_x, draw_y,
                                  glyph->bitmap, glyph->width, glyph->height,
                                  color);
        }

        if (glyph) {
            cursor_x += glyph->advance;
        }

        prev_cp = cp;
    }
}
