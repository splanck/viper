//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file surface.hpp
 * @brief Surface management for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Find surface at screen coordinates (respects z-order)
Surface *find_surface_at(int32_t x, int32_t y);

// Find surface by ID
Surface *find_surface_by_id(uint32_t id);

// Get focused surface
Surface *get_focused_surface();

// Get surface that owns the menu bar
Surface *get_menu_surface();

} // namespace displayd
