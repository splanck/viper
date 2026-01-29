//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file menu.hpp
 * @brief Menu bar and pulldown menu handling for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Calculate X positions for menu titles
void calc_menu_positions(Surface *surf);

// Find which menu title is at the given position (-1 if none)
int32_t find_menu_at(int32_t x, int32_t y);

// Find which menu item is at the given position (-1 if none)
int32_t find_menu_item_at(int32_t x, int32_t y);

// Draw the global menu bar
void draw_menu_bar();

// Draw the currently open pulldown menu
void draw_pulldown_menu();

// Close any open menu
void close_menu();

// Handle menu bar click at (x, y)
// Returns true if the click was consumed by the menu
bool handle_menu_click(int32_t x, int32_t y);

} // namespace displayd
