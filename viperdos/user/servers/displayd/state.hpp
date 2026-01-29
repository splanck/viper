//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file state.hpp
 * @brief Global state declarations for displayd.
 */

#pragma once

#include "types.hpp"

namespace displayd {

// ============================================================================
// Debug Functions
// ============================================================================

void debug_print(const char *msg);
void debug_print_hex(uint64_t val);
void debug_print_dec(int64_t val);

// ============================================================================
// Framebuffer State
// ============================================================================

extern uint32_t *g_fb;          // Front buffer (actual framebuffer)
extern uint32_t *g_back_buffer; // Back buffer for double buffering
extern uint32_t *g_draw_target; // Current drawing target
extern uint32_t g_fb_width;
extern uint32_t g_fb_height;
extern uint32_t g_fb_pitch;

// ============================================================================
// Surface State
// ============================================================================

extern Surface g_surfaces[MAX_SURFACES];
extern uint32_t g_next_surface_id;
extern uint32_t g_focused_surface;
extern uint32_t g_next_z_order;

// ============================================================================
// Menu State
// ============================================================================

extern int32_t g_active_menu;
extern int32_t g_hovered_menu_item;
extern int32_t g_menu_title_positions[MAX_MENUS];

// ============================================================================
// Cursor State
// ============================================================================

extern int32_t g_cursor_x;
extern int32_t g_cursor_y;
extern uint32_t g_cursor_saved[CURSOR_SIZE * CURSOR_SIZE];
extern bool g_cursor_visible;

// ============================================================================
// Drag/Resize State
// ============================================================================

extern uint32_t g_drag_surface_id;
extern int32_t g_drag_offset_x;
extern int32_t g_drag_offset_y;
extern uint8_t g_last_buttons;
extern int32_t g_last_mouse_x;
extern int32_t g_last_mouse_y;

extern uint32_t g_resize_surface_id;
extern uint8_t g_resize_edge;
extern int32_t g_resize_start_x;
extern int32_t g_resize_start_y;
extern int32_t g_resize_start_width;
extern int32_t g_resize_start_height;
extern int32_t g_resize_start_surf_x;
extern int32_t g_resize_start_surf_y;

extern uint32_t g_scrollbar_surface_id;
extern bool g_scrollbar_vertical;
extern int32_t g_scrollbar_start_y;
extern int32_t g_scrollbar_start_pos;
extern int32_t g_scrollbar_last_sent_pos;

// ============================================================================
// IPC State
// ============================================================================

extern int32_t g_service_channel;
extern int32_t g_poll_set;

// ============================================================================
// Helper Functions
// ============================================================================

void bring_to_front(Surface *surf);

} // namespace displayd
