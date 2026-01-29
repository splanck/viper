//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file state.cpp
 * @brief Global state definitions for displayd.
 */

#include "state.hpp"

namespace displayd {

// ============================================================================
// Debug Functions
// ============================================================================

void debug_print(const char *msg) {
    sys::print(msg);
}

void debug_print_hex(uint64_t val) {
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    sys::print(buf);
}

void debug_print_dec(int64_t val) {
    if (val < 0) {
        sys::print("-");
        val = -val;
    }
    if (val == 0) {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// ============================================================================
// Framebuffer State
// ============================================================================

uint32_t *g_fb = nullptr;
uint32_t *g_back_buffer = nullptr;
uint32_t *g_draw_target = nullptr;
uint32_t g_fb_width = 0;
uint32_t g_fb_height = 0;
uint32_t g_fb_pitch = 0;

// ============================================================================
// Surface State
// ============================================================================

Surface g_surfaces[MAX_SURFACES];
uint32_t g_next_surface_id = 1;
uint32_t g_focused_surface = 0;
uint32_t g_next_z_order = 1;

// ============================================================================
// Menu State
// ============================================================================

int32_t g_active_menu = -1;
int32_t g_hovered_menu_item = -1;
int32_t g_menu_title_positions[MAX_MENUS];

// ============================================================================
// Cursor State
// ============================================================================

int32_t g_cursor_x = 0;
int32_t g_cursor_y = 0;
uint32_t g_cursor_saved[CURSOR_SIZE * CURSOR_SIZE];
bool g_cursor_visible = true;

// ============================================================================
// Drag/Resize State
// ============================================================================

uint32_t g_drag_surface_id = 0;
int32_t g_drag_offset_x = 0;
int32_t g_drag_offset_y = 0;
uint8_t g_last_buttons = 0;
int32_t g_last_mouse_x = 0;
int32_t g_last_mouse_y = 0;

uint32_t g_resize_surface_id = 0;
uint8_t g_resize_edge = 0;
int32_t g_resize_start_x = 0;
int32_t g_resize_start_y = 0;
int32_t g_resize_start_width = 0;
int32_t g_resize_start_height = 0;
int32_t g_resize_start_surf_x = 0;
int32_t g_resize_start_surf_y = 0;

uint32_t g_scrollbar_surface_id = 0;
bool g_scrollbar_vertical = true;
int32_t g_scrollbar_start_y = 0;
int32_t g_scrollbar_start_pos = 0;
int32_t g_scrollbar_last_sent_pos = 0;

// ============================================================================
// IPC State
// ============================================================================

int32_t g_service_channel = -1;
int32_t g_poll_set = -1;

// ============================================================================
// Helper Functions
// ============================================================================

void bring_to_front(Surface *surf) {
    if (!surf)
        return;
    surf->z_order = g_next_z_order++;
}

} // namespace displayd
