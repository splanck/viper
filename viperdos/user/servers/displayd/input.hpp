//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file input.hpp
 * @brief Input polling for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Poll for keyboard events and route to focused surface
void poll_keyboard();

// Poll for mouse events and handle dragging/resizing
void poll_mouse();

// Complete a window resize (reallocates shared memory)
bool complete_resize(Surface *surf, uint32_t new_width, uint32_t new_height);

} // namespace displayd
