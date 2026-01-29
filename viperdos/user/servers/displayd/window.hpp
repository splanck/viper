//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file window.hpp
 * @brief Window decorations and scrollbars for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Draw window decorations (title bar, borders, buttons)
void draw_window_decorations(Surface *surf);

// Draw scrollbars
void draw_vscrollbar(Surface *surf);
void draw_hscrollbar(Surface *surf);

// Get resize edge at position (0=none, 1=left, 2=right, 4=top, 8=bottom, or combo)
uint8_t get_resize_edge(Surface *surf, int32_t x, int32_t y);

// Check if click is on vertical scrollbar, returns scroll position or -1
int32_t check_vscrollbar_click(Surface *surf, int32_t x, int32_t y);

} // namespace displayd
