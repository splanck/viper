//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_font.h
// Purpose: Embedded 8x8 bitmap font providing per-character glyph bitmaps for software text rendering, where each glyph is 8 bytes (one byte per row).
//
// Key invariants:
//   - ASCII characters 0-127 are supported; undefined chars use a fallback glyph.
//   - Each glyph is exactly 8 bytes; bit 7 of each byte is the leftmost pixel.
//   - Font width and height are always 8 pixels.
//   - Glyph data is read-only; callers must not modify the returned pointer.
//
// Ownership/Lifetime:
//   - Glyph pointers are into a static read-only table; no allocation or lifetime management.
//   - Callers borrow the returned pointer; they must not free it.
//
// Links: src/runtime/graphics/rt_font.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Get the 8-byte bitmap for a character.
    /// @param c ASCII character (0-127).
    /// @return Pointer to 8 bytes of bitmap data (each byte is one row).
    const uint8_t *rt_font_get_glyph(int c);

    /// @brief Get the font character width.
    /// @return Width in pixels (always 8).
    int rt_font_char_width(void);

    /// @brief Get the font character height.
    /// @return Height in pixels (always 8).
    int rt_font_char_height(void);

#ifdef __cplusplus
}
#endif
