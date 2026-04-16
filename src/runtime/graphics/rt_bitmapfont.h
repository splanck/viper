//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_bitmapfont.h
// Purpose: Custom bitmap font loading (BDF/PSF formats) and Canvas text rendering,
//   providing variable-size fonts beyond the built-in 8x8 monospace font.
//
// Key invariants:
//   - BitmapFont objects are GC-managed via rt_obj_new_i64.
//   - Glyph bitmaps are heap-allocated; freed in the GC finalizer.
//   - Glyph tables cover codepoints 0-255 (Latin-1). UTF-8 text is decoded to
//     codepoints during measurement/rendering, and missing glyphs use the
//     fallback glyph.
//   - All Canvas drawing functions are no-ops when canvas or font is NULL.
//
// Ownership/Lifetime:
//   - BitmapFont handles are GC-managed; no manual free needed.
//   - Glyph bitmap data is owned by the font and freed on destruction.
//
// Links: src/runtime/graphics/rt_bitmapfont.c (implementation),
//        src/runtime/graphics/rt_font.h (built-in 8x8 font),
//        src/runtime/graphics/rt_drawing.c (Canvas text functions)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// BitmapFont Loading
//=========================================================================

/// @brief Load a BDF (Bitmap Distribution Format) font file.
/// @param path Path to the .bdf file.
/// @return BitmapFont handle, or NULL on missing, truncated, or malformed input.
void *rt_bitmapfont_load_bdf(rt_string path);

/// @brief Load a PSF (PC Screen Font) v1 or v2 file.
/// @param path Path to the .psf file.
/// @return BitmapFont handle, or NULL on missing, truncated, or malformed input.
void *rt_bitmapfont_load_psf(rt_string path);

/// @brief GC finalizer — free glyph bitmap data.
void rt_bitmapfont_destroy(void *font);

//=========================================================================
// BitmapFont Properties
//=========================================================================

/// @brief Get the maximum glyph width (0 for proportional fonts).
int64_t rt_bitmapfont_char_width(void *font);

/// @brief Get the line height in pixels.
int64_t rt_bitmapfont_char_height(void *font);

/// @brief Get the number of loaded glyphs.
int64_t rt_bitmapfont_glyph_count(void *font);

/// @brief Check if all glyphs have the same advance width.
int8_t rt_bitmapfont_is_monospace(void *font);

//=========================================================================
// Text Measurement
//=========================================================================

/// @brief Measure the pixel width of a UTF-8 string rendered with this font.
int64_t rt_bitmapfont_text_width(void *font, rt_string text);

/// @brief Get the text height (same as line height).
int64_t rt_bitmapfont_text_height(void *font);

//=========================================================================
// Canvas Drawing with Custom Font
//=========================================================================

/// @brief Draw text at (x,y) using a custom BitmapFont.
void rt_canvas_text_font(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t color);

/// @brief Draw text with foreground and background colors.
void rt_canvas_text_font_bg(void *canvas,
                            int64_t x,
                            int64_t y,
                            rt_string text,
                            void *font,
                            int64_t fg_color,
                            int64_t bg_color);

/// @brief Draw text with integer scaling (1=normal, 2=double, etc.).
void rt_canvas_text_font_scaled(
    void *canvas, int64_t x, int64_t y, rt_string text, void *font, int64_t scale, int64_t color);

/// @brief Draw text horizontally centered on the canvas.
void rt_canvas_text_font_centered(
    void *canvas, int64_t y, rt_string text, void *font, int64_t color);

/// @brief Draw text right-aligned with a margin from the right edge.
void rt_canvas_text_font_right(
    void *canvas, int64_t margin, int64_t y, rt_string text, void *font, int64_t color);

#ifdef __cplusplus
}
#endif
