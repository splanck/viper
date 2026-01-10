// vg_font.h - Viper GUI Font Engine
// TTF font loading, glyph rasterization, and text rendering
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

typedef struct vg_font vg_font_t;
typedef struct rt_canvas rt_canvas_t;

//=============================================================================
// Glyph Information
//=============================================================================

typedef struct vg_glyph {
    uint32_t codepoint;      // Unicode codepoint
    int width;               // Bitmap width in pixels
    int height;              // Bitmap height in pixels
    int bearing_x;           // Horizontal bearing (left edge offset)
    int bearing_y;           // Vertical bearing (baseline to top)
    int advance;             // Horizontal advance width
    uint8_t* bitmap;         // 8-bit alpha coverage bitmap (owned by cache)
} vg_glyph_t;

//=============================================================================
// Font Metrics
//=============================================================================

typedef struct vg_font_metrics {
    int ascent;              // Distance from baseline to top
    int descent;             // Distance from baseline to bottom (negative)
    int line_height;         // Recommended line spacing
    int units_per_em;        // Design units per em
} vg_font_metrics_t;

//=============================================================================
// Text Measurement
//=============================================================================

typedef struct vg_text_metrics {
    float width;             // Total width of text
    float height;            // Height (typically line_height)
    int glyph_count;         // Number of glyphs
} vg_text_metrics_t;

//=============================================================================
// Font Loading
//=============================================================================

/// Load font from memory buffer
/// @param data     TTF file data
/// @param size     Size of data in bytes
/// @return         Font handle or NULL on failure
vg_font_t* vg_font_load(const uint8_t* data, size_t size);

/// Load font from file path
/// @param path     Path to TTF file
/// @return         Font handle or NULL on failure
vg_font_t* vg_font_load_file(const char* path);

/// Destroy font and free resources
/// @param font     Font handle
void vg_font_destroy(vg_font_t* font);

//=============================================================================
// Font Information
//=============================================================================

/// Get font metrics at given size
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param metrics  Output metrics structure
void vg_font_get_metrics(vg_font_t* font, float size, vg_font_metrics_t* metrics);

/// Get font family name
/// @param font     Font handle
/// @return         Font family name string
const char* vg_font_get_family(vg_font_t* font);

/// Check if font has a specific glyph
/// @param font     Font handle
/// @param codepoint Unicode codepoint
/// @return         true if glyph exists
bool vg_font_has_glyph(vg_font_t* font, uint32_t codepoint);

//=============================================================================
// Glyph Rasterization
//=============================================================================

/// Get rasterized glyph (cached)
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param codepoint Unicode codepoint
/// @return         Glyph data or NULL if not found
const vg_glyph_t* vg_font_get_glyph(vg_font_t* font, float size, uint32_t codepoint);

/// Get kerning adjustment between two glyphs
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param left     Left codepoint
/// @param right    Right codepoint
/// @return         Kerning adjustment in pixels
float vg_font_get_kerning(vg_font_t* font, float size, uint32_t left, uint32_t right);

//=============================================================================
// Text Measurement
//=============================================================================

/// Measure text string
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param text     UTF-8 text string
/// @param metrics  Output metrics
void vg_font_measure_text(vg_font_t* font, float size, const char* text,
                          vg_text_metrics_t* metrics);

/// Get character index at x position (for cursor placement)
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param text     UTF-8 text string
/// @param x        X position in pixels
/// @return         Character index (0-based) or -1 if past end
int vg_font_hit_test(vg_font_t* font, float size, const char* text, float x);

/// Get x position of character index
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param text     UTF-8 text string
/// @param index    Character index
/// @return         X position in pixels
float vg_font_get_cursor_x(vg_font_t* font, float size, const char* text, int index);

//=============================================================================
// Text Rendering
//=============================================================================

/// Draw text to canvas
/// @param canvas   Canvas to draw on (opaque handle from rt_canvas)
/// @param font     Font handle
/// @param size     Font size in pixels
/// @param x        X position
/// @param y        Y position (baseline)
/// @param text     UTF-8 text string
/// @param color    Text color (0xAARRGGBB)
void vg_font_draw_text(void* canvas, vg_font_t* font, float size,
                       float x, float y, const char* text, uint32_t color);

//=============================================================================
// UTF-8 Utilities
//=============================================================================

/// Decode next UTF-8 codepoint from string
/// @param str      Pointer to string pointer (updated on return)
/// @return         Decoded codepoint, or 0 on end/error
uint32_t vg_utf8_decode(const char** str);

/// Get length of UTF-8 string in codepoints
/// @param str      UTF-8 string
/// @return         Number of codepoints
int vg_utf8_strlen(const char* str);

/// Get byte offset of character at index
/// @param str      UTF-8 string
/// @param index    Character index
/// @return         Byte offset
int vg_utf8_offset(const char* str, int index);

#ifdef __cplusplus
}
#endif
