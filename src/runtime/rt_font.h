//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_font.h
// Purpose: Embedded 8x8 bitmap font for text rendering.
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
