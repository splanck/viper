#pragma once

#include "../include/types.hpp"

/**
 * @file font.hpp
 * @brief Built-in bitmap font metrics and glyph lookup.
 *
 * @details
 * The graphics console uses a small, fixed-width bitmap font for rendering
 * text into the framebuffer. This header defines the base glyph dimensions,
 * a simple integer scaling model (expressed as a rational factor), and the
 * glyph lookup function used by the renderer.
 *
 * Glyphs are stored as 1-bit-per-pixel rows (MSB first). The lookup routine
 * maps an ASCII character into a pointer to `BASE_HEIGHT` rows of glyph data.
 */
namespace font
{

// Base font dimensions (before scaling)
constexpr u32 BASE_WIDTH = 8;
constexpr u32 BASE_HEIGHT = 16;

// Scale factor as fraction: SCALE_NUM / SCALE_DEN
// 1/1 = 8x16, 3/2 = 12x24, 2/1 = 16x32
constexpr u32 SCALE_NUM = 3;
constexpr u32 SCALE_DEN = 2;

// Effective font dimensions
constexpr u32 WIDTH = (BASE_WIDTH * SCALE_NUM) / SCALE_DEN;
constexpr u32 HEIGHT = (BASE_HEIGHT * SCALE_NUM) / SCALE_DEN;

/**
 * @brief Look up glyph bitmap data for a character.
 *
 * @details
 * Returns a pointer to the bitmap rows for the requested character. The data
 * is returned in the font's native (unscaled) resolution: `BASE_HEIGHT` bytes,
 * one byte per row, with the most-significant bit corresponding to the leftmost
 * pixel of the row.
 *
 * Characters outside the supported range are typically mapped to a fallback
 * glyph (implementation-defined, commonly `?`).
 *
 * @param c ASCII character to look up.
 * @return Pointer to an array of `BASE_HEIGHT` bytes containing the glyph rows.
 */
const u8 *get_glyph(char c);

} // namespace font
