//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file events.hpp
 * @brief Event queuing for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// Queue a mouse event to a surface
void queue_mouse_event(Surface *surf,
                       uint8_t event_type,
                       int32_t local_x,
                       int32_t local_y,
                       int32_t dx,
                       int32_t dy,
                       uint8_t buttons,
                       uint8_t button);

// Queue a scroll event to a surface
void queue_scroll_event(Surface *surf, int32_t new_position, bool vertical);

// Queue a focus event to a surface
void queue_focus_event(Surface *surf, bool gained);

// Queue a close event to a surface
void queue_close_event(Surface *surf);

// Queue a key event to a surface
void queue_key_event(Surface *surf, uint16_t keycode, uint8_t modifiers, bool pressed);

// Queue a menu event to a surface
void queue_menu_event(Surface *surf,
                      uint8_t menu_index,
                      uint8_t item_index,
                      uint8_t action);

// Flush queued events to client via IPC
void flush_events(Surface *surf);

} // namespace displayd
