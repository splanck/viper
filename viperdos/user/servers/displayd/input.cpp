//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file input.cpp
 * @brief Input polling for displayd.
 */

#include "input.hpp"
#include "compositor.hpp"
#include "cursor.hpp"
#include "events.hpp"
#include "menu.hpp"
#include "state.hpp"
#include "surface.hpp"
#include "window.hpp"

namespace displayd {

// Complete a window resize by reallocating shared memory and notifying client
// NOTE: Full resize with SHM reallocation is disabled due to a crash bug.
// For now, resize is visual-only (frame changes but content stays same size).
bool complete_resize(Surface * /*surf*/, uint32_t /*new_width*/, uint32_t /*new_height*/) {
    // TODO: Fix SHM reallocation crash before re-enabling
    // The crash appears to be related to handle management after channel_send
    return true;
}

void poll_keyboard() {
    // Drain all pending events from the kernel queue (limit to 64 per call)
    for (int i = 0; i < 64; i++) {
        // Check if input is available from kernel
        if (sys::input_has_event() == 0)
            return;

        // Get the event from kernel
        sys::InputEvent ev;
        if (sys::input_get_event(&ev) != 0)
            return;

        // Route keyboard events to focused surface
        if (ev.type == sys::InputEventType::KeyPress ||
            ev.type == sys::InputEventType::KeyRelease) {
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused) {
                bool pressed = (ev.type == sys::InputEventType::KeyPress);
                queue_key_event(focused, ev.code, ev.modifiers, pressed);
            }
        }
        // Note: Mouse events from event_queue are discarded here since
        // poll_mouse() uses get_mouse_state() directly for mouse position
    }
}

void poll_mouse() {
    sys::MouseState state;
    int result = sys::get_mouse_state(&state);
    if (result != 0) {
        return;
    }

    static uint32_t call_count = 0;
    call_count++;
    if (call_count % 100 == 0) {
        debug_print("[displayd] got state=(");
        debug_print_dec(state.x);
        debug_print(",");
        debug_print_dec(state.y);
        debug_print(")\n");
    }

    bool cursor_moved = (state.x != g_last_mouse_x || state.y != g_last_mouse_y);

    // Update cursor position
    if (cursor_moved) {
        if (g_cursor_visible)
            restore_cursor_background();
        g_cursor_x = state.x;
        g_cursor_y = state.y;
        if (!g_cursor_visible)
            sys::move_hw_cursor(static_cast<uint32_t>(state.x), static_cast<uint32_t>(state.y));

        // Handle menu hover (when a pulldown menu is open)
        if (g_active_menu >= 0) {
            int32_t new_hover = find_menu_item_at(g_cursor_x, g_cursor_y);
            if (new_hover != g_hovered_menu_item) {
                g_hovered_menu_item = new_hover;
                composite(); // Redraw to show hover highlight
            }
            // Also check if hovering over a different menu title
            int32_t hover_menu = find_menu_at(g_cursor_x, g_cursor_y);
            if (hover_menu >= 0 && hover_menu != g_active_menu) {
                // Switch to the hovered menu
                g_active_menu = hover_menu;
                g_hovered_menu_item = -1;
                composite();
            }
        }

        // Handle resizing
        if (g_resize_surface_id != 0) {
            Surface *resize_surf = find_surface_by_id(g_resize_surface_id);
            if (resize_surf) {
                int32_t dx = g_cursor_x - g_resize_start_x;
                int32_t dy = g_cursor_y - g_resize_start_y;

                // Calculate new size based on which edges are being dragged
                int32_t new_width = g_resize_start_width;
                int32_t new_height = g_resize_start_height;
                int32_t new_x = g_resize_start_surf_x;
                int32_t new_y = g_resize_start_surf_y;

                if (g_resize_edge & 2) // Right
                {
                    new_width = g_resize_start_width + dx;
                }
                if (g_resize_edge & 1) // Left
                {
                    new_width = g_resize_start_width - dx;
                    new_x = g_resize_start_surf_x + dx;
                }
                if (g_resize_edge & 8) // Bottom
                {
                    new_height = g_resize_start_height + dy;
                }

                // Clamp to minimum size
                if (new_width < static_cast<int32_t>(MIN_WINDOW_WIDTH)) {
                    if (g_resize_edge & 1) // Left edge: adjust x
                        new_x = g_resize_start_surf_x + g_resize_start_width - MIN_WINDOW_WIDTH;
                    new_width = MIN_WINDOW_WIDTH;
                }
                if (new_height < static_cast<int32_t>(MIN_WINDOW_HEIGHT)) {
                    new_height = MIN_WINDOW_HEIGHT;
                }

                // Note: Actual resize would require reallocating shared memory
                // For now, just update the window frame dimensions for visual feedback
                // The client won't see the actual content resize
                resize_surf->x = new_x;
                resize_surf->y = new_y;
                // Don't update width/height without realloc - just visual resize of frame
            }
            composite();
        }
        // Handle dragging
        else if (g_drag_surface_id != 0) {
            Surface *drag_surf = find_surface_by_id(g_drag_surface_id);
            if (drag_surf) {
                drag_surf->x = g_cursor_x - g_drag_offset_x;
                int32_t new_y =
                    g_cursor_y - g_drag_offset_y + static_cast<int32_t>(TITLE_BAR_HEIGHT);
                // Clamp Y so title bar stays below menu bar
                if (new_y < MIN_WINDOW_Y) {
                    new_y = MIN_WINDOW_Y;
                }
                drag_surf->y = new_y;
            }
            composite();
        }
        // Handle scrollbar dragging
        else if (g_scrollbar_surface_id != 0) {
            Surface *scroll_surf = find_surface_by_id(g_scrollbar_surface_id);
            if (scroll_surf && g_scrollbar_vertical && scroll_surf->vscroll.enabled) {
                // Calculate scroll position based on cursor movement
                int32_t track_height =
                    static_cast<int32_t>(scroll_surf->height) - SCROLLBAR_MIN_THUMB;
                if (track_height > 0) {
                    int32_t dy = g_cursor_y - g_scrollbar_start_y;
                    int32_t max_scroll =
                        scroll_surf->vscroll.content_size - scroll_surf->vscroll.viewport_size;
                    if (max_scroll > 0) {
                        // Calculate new position: proportional to cursor movement
                        int32_t new_pos = g_scrollbar_start_pos + (dy * max_scroll) / track_height;

                        // Clamp to valid range
                        if (new_pos < 0)
                            new_pos = 0;
                        if (new_pos > max_scroll)
                            new_pos = max_scroll;

                        if (new_pos != scroll_surf->vscroll.scroll_pos) {
                            scroll_surf->vscroll.scroll_pos = new_pos;

                            // Throttle: only send event if delta exceeds threshold
                            int32_t delta = new_pos - g_scrollbar_last_sent_pos;
                            if (delta < 0)
                                delta = -delta;
                            if (delta >= SCROLL_THROTTLE_DELTA) {
                                queue_scroll_event(scroll_surf, new_pos, true);
                                g_scrollbar_last_sent_pos = new_pos;
                            }

                            composite();
                        }
                    }
                }
            }
        } else {
            // Queue mouse move event to focused surface if in client area
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused) {
                int32_t local_x = g_cursor_x - focused->x;
                int32_t local_y = g_cursor_y - focused->y;

                // Only send move events within client area
                if (local_x >= 0 && local_x < static_cast<int32_t>(focused->width) &&
                    local_y >= 0 && local_y < static_cast<int32_t>(focused->height)) {
                    int32_t dx = g_cursor_x - g_last_mouse_x;
                    int32_t dy = g_cursor_y - g_last_mouse_y;
                    queue_mouse_event(focused, 0, local_x, local_y, dx, dy, state.buttons, 0);
                }
            }

            save_cursor_background();
            draw_cursor();
        }

        g_last_mouse_x = state.x;
        g_last_mouse_y = state.y;
    }

    // Handle button changes
    if (state.buttons != g_last_buttons) {
        uint8_t pressed = state.buttons & ~g_last_buttons;
        uint8_t released = g_last_buttons & ~state.buttons;

        Surface *surf = find_surface_at(g_cursor_x, g_cursor_y);

        if (pressed) {
            // Debug click routing
            debug_print("[click] at (");
            debug_print_dec(g_cursor_x);
            debug_print(",");
            debug_print_dec(g_cursor_y);
            debug_print(") surf=");
            if (surf) {
                debug_print_dec(surf->id);
                debug_print(" z=");
                debug_print_dec(surf->z_order);
                if (surf->flags & SURFACE_FLAG_SYSTEM)
                    debug_print(" SYSTEM");
            } else {
                debug_print("NULL");
            }
            debug_print("\n");
            // ----------------------------------------------------------------
            // Global Menu Bar Handling (Amiga/Mac style - always on top)
            // ----------------------------------------------------------------
            bool menu_handled = false;

            // Check if click is in the menu bar area (y=0 to MENU_BAR_HEIGHT)
            if (g_cursor_y < static_cast<int32_t>(MENU_BAR_HEIGHT)) {
                int32_t clicked_menu = find_menu_at(g_cursor_x, g_cursor_y);

                if (clicked_menu >= 0) {
                    // Clicked on a menu title
                    if (g_active_menu == clicked_menu) {
                        // Same menu - toggle closed
                        g_active_menu = -1;
                    } else {
                        // Different menu or no menu open - open this menu
                        g_active_menu = clicked_menu;
                    }
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                } else if (g_active_menu >= 0) {
                    // Menu is open but didn't click on a title - close it
                    g_active_menu = -1;
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                }
            }
            // Check if click is in an open pulldown menu
            else if (g_active_menu >= 0) {
                int32_t item_idx = find_menu_item_at(g_cursor_x, g_cursor_y);

                if (item_idx >= 0) {
                    // Clicked on a menu item - execute it
                    Surface *menu_surf = get_menu_surface();
                    if (menu_surf && g_active_menu < menu_surf->menu_count) {
                        const MenuDef &menu = menu_surf->menus[g_active_menu];
                        if (item_idx < menu.item_count) {
                            const MenuItem &item = menu.items[item_idx];
                            // Only trigger if enabled and not a separator
                            if (item.enabled && item.label[0] != '\0' && item.action != 0) {
                                queue_menu_event(menu_surf,
                                                 static_cast<uint8_t>(g_active_menu),
                                                 static_cast<uint8_t>(item_idx),
                                                 item.action);
                            }
                        }
                    }
                    g_active_menu = -1;
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                } else {
                    // Clicked outside pulldown - close menu
                    g_active_menu = -1;
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                }
            }

            // If menu handled the click, skip normal window handling
            if (menu_handled) {
                // Do nothing further
            } else if (surf) {
                // Handle focus change and bring to front
                // Don't change focus to SYSTEM surfaces (like desktop/workbench)
                // They should never receive focus or come to front
                if (surf->id != g_focused_surface && !(surf->flags & SURFACE_FLAG_SYSTEM)) {
                    Surface *old_focused = find_surface_by_id(g_focused_surface);
                    if (old_focused) {
                        queue_focus_event(old_focused, false);
                    }
                    g_focused_surface = surf->id;
                    queue_focus_event(surf, true);
                    bring_to_front(surf);
                }

                // Check for resize edges first
                uint8_t edge = get_resize_edge(surf, g_cursor_x, g_cursor_y);
                if (edge != 0) {
                    // Start resizing
                    g_resize_surface_id = surf->id;
                    g_resize_edge = edge;
                    g_resize_start_x = g_cursor_x;
                    g_resize_start_y = g_cursor_y;
                    g_resize_start_width = static_cast<int32_t>(surf->width);
                    g_resize_start_height = static_cast<int32_t>(surf->height);
                    g_resize_start_surf_x = surf->x;
                    g_resize_start_surf_y = surf->y;
                }
                // Check if clicked on title bar (for dragging/close)
                else {
                    int32_t title_y1 =
                        surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
                    int32_t title_y2 = surf->y - static_cast<int32_t>(BORDER_WIDTH);

                    if (g_cursor_y >= title_y1 && g_cursor_y < title_y2) {
                        // Check for window control buttons
                        int32_t btn_spacing = static_cast<int32_t>(CLOSE_BUTTON_SIZE + 4);
                        int32_t close_x = surf->x + static_cast<int32_t>(surf->width) -
                                          static_cast<int32_t>(CLOSE_BUTTON_SIZE) - 4;
                        int32_t max_x = close_x - btn_spacing;
                        int32_t min_x = max_x - btn_spacing;
                        int32_t btn_size = static_cast<int32_t>(CLOSE_BUTTON_SIZE);

                        if (g_cursor_x >= close_x && g_cursor_x < close_x + btn_size) {
                            // Close button clicked - queue close event
                            queue_close_event(surf);
                        } else if (g_cursor_x >= max_x && g_cursor_x < max_x + btn_size) {
                            // Maximize button clicked
                            if (surf->maximized) {
                                // Restore from maximized
                                surf->maximized = false;
                                surf->x = surf->saved_x;
                                surf->y = surf->saved_y;
                            } else {
                                // Maximize - move to top-left corner
                                surf->saved_x = surf->x;
                                surf->saved_y = surf->y;
                                surf->maximized = true;
                                surf->x = static_cast<int32_t>(BORDER_WIDTH);
                                surf->y = static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
                            }
                            composite();
                        } else if (g_cursor_x >= min_x && g_cursor_x < min_x + btn_size) {
                            // Minimize button clicked
                            surf->minimized = true;
                            // If this was focused, find next surface to focus
                            if (g_focused_surface == surf->id) {
                                g_focused_surface = 0;
                                // Find highest z-order non-minimized surface
                                uint32_t best_z = 0;
                                for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                                    if (g_surfaces[i].in_use && !g_surfaces[i].minimized &&
                                        g_surfaces[i].z_order > best_z) {
                                        best_z = g_surfaces[i].z_order;
                                        g_focused_surface = g_surfaces[i].id;
                                    }
                                }
                            }
                            composite();
                        } else {
                            // Start dragging (but not if maximized)
                            if (!surf->maximized) {
                                g_drag_surface_id = surf->id;
                                g_drag_offset_x = g_cursor_x - surf->x;
                                g_drag_offset_y =
                                    g_cursor_y - surf->y + static_cast<int32_t>(TITLE_BAR_HEIGHT);
                            }
                        }
                    } else {
                        // Check for scrollbar click first
                        int32_t scroll_pos = check_vscrollbar_click(surf, g_cursor_x, g_cursor_y);
                        if (scroll_pos >= 0) {
                            // Start scrollbar drag
                            g_scrollbar_surface_id = surf->id;
                            g_scrollbar_vertical = true;
                            g_scrollbar_start_y = g_cursor_y;
                            g_scrollbar_start_pos = surf->vscroll.scroll_pos;
                            g_scrollbar_last_sent_pos = scroll_pos;

                            // Update scroll position and notify client
                            surf->vscroll.scroll_pos = scroll_pos;
                            queue_scroll_event(surf, scroll_pos, true);
                        } else {
                            // Clicked in client area - queue button down event
                            int32_t local_x = g_cursor_x - surf->x;
                            int32_t local_y = g_cursor_y - surf->y;

                            if (local_x >= 0 && local_x < static_cast<int32_t>(surf->width) &&
                                local_y >= 0 && local_y < static_cast<int32_t>(surf->height)) {
                                // Determine which button (0=left, 1=right, 2=middle)
                                uint8_t button = 0;
                                if (pressed & 0x01)
                                    button = 0; // Left
                                else if (pressed & 0x02)
                                    button = 1; // Right
                                else if (pressed & 0x04)
                                    button = 2; // Middle

                                debug_print("[click] -> queue to ");
                                debug_print_dec(surf->id);
                                debug_print("\n");
                                queue_mouse_event(
                                    surf, 1, local_x, local_y, 0, 0, state.buttons, button);
                            }
                        }
                    }
                }

                composite();
            }
        }

        if (released) {
            // Complete resize if we were resizing
            if (g_resize_surface_id != 0) {
                Surface *resize_surf = find_surface_by_id(g_resize_surface_id);
                if (resize_surf) {
                    // Calculate final dimensions
                    int32_t dx = g_cursor_x - g_resize_start_x;
                    int32_t dy = g_cursor_y - g_resize_start_y;

                    int32_t new_width = g_resize_start_width;
                    int32_t new_height = g_resize_start_height;

                    if (g_resize_edge & 2) // Right
                        new_width = g_resize_start_width + dx;
                    if (g_resize_edge & 1) // Left
                        new_width = g_resize_start_width - dx;
                    if (g_resize_edge & 8) // Bottom
                        new_height = g_resize_start_height + dy;

                    // Clamp to minimum size
                    if (new_width < static_cast<int32_t>(MIN_WINDOW_WIDTH))
                        new_width = MIN_WINDOW_WIDTH;
                    if (new_height < static_cast<int32_t>(MIN_WINDOW_HEIGHT))
                        new_height = MIN_WINDOW_HEIGHT;

                    // Complete the resize with new SHM
                    complete_resize(resize_surf,
                                    static_cast<uint32_t>(new_width),
                                    static_cast<uint32_t>(new_height));
                    composite();
                }
            }

            // Send final scroll event if we were scrolling (ensure exact final position)
            if (g_scrollbar_surface_id != 0) {
                Surface *scroll_surf = find_surface_by_id(g_scrollbar_surface_id);
                if (scroll_surf && scroll_surf->vscroll.scroll_pos != g_scrollbar_last_sent_pos) {
                    queue_scroll_event(scroll_surf, scroll_surf->vscroll.scroll_pos, true);
                }
            }

            g_drag_surface_id = 0;
            g_resize_surface_id = 0;
            g_resize_edge = 0;
            g_scrollbar_surface_id = 0;

            // Queue button up event to focused surface
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused) {
                int32_t local_x = g_cursor_x - focused->x;
                int32_t local_y = g_cursor_y - focused->y;

                // Determine which button was released
                uint8_t button = 0;
                if (released & 0x01)
                    button = 0; // Left
                else if (released & 0x02)
                    button = 1; // Right
                else if (released & 0x04)
                    button = 2; // Middle

                queue_mouse_event(focused, 2, local_x, local_y, 0, 0, state.buttons, button);
            }
        }

        g_last_buttons = state.buttons;
    }
}

} // namespace displayd
