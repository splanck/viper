//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file cursor.hpp
 * @brief Mouse cursor management for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Cursor bitmap data (24x24, 1 = orange fill, 2 = black outline)
extern const uint8_t g_cursor_data[CURSOR_SIZE * CURSOR_SIZE];

// Cursor functions
void save_cursor_background();
void restore_cursor_background();
void draw_cursor();
void setup_hardware_cursor();

} // namespace displayd
