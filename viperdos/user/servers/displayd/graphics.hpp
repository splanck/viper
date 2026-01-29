//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file graphics.hpp
 * @brief Drawing primitives for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Pixel operations
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t get_pixel(uint32_t x, uint32_t y);

// Rectangle fill
void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);

// Text drawing
void draw_char(int32_t x, int32_t y, char c, uint32_t color);
void draw_text(int32_t x, int32_t y, const char *text, uint32_t color);

} // namespace displayd
