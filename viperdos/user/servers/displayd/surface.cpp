//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file surface.cpp
 * @brief Surface management for displayd.
 */

#include "surface.hpp"
#include "state.hpp"

namespace displayd {

Surface *find_surface_at(int32_t x, int32_t y) {
    Surface *best = nullptr;
    uint32_t best_z = 0;
    bool found_any = false;

    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        Surface *surf = &g_surfaces[i];
        if (!surf->in_use || !surf->visible || surf->minimized)
            continue;

        // Check if in window bounds (including decorations)
        int32_t win_x = surf->x - static_cast<int32_t>(BORDER_WIDTH);
        int32_t win_y = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
        int32_t win_x2 = surf->x + static_cast<int32_t>(surf->width + BORDER_WIDTH);
        int32_t win_y2 = surf->y + static_cast<int32_t>(surf->height + BORDER_WIDTH);

        if (x >= win_x && x < win_x2 && y >= win_y && y < win_y2) {
            // Pick the one with highest z-order (top-most)
            // Use >= for first match to handle z_order = 0 (SYSTEM surfaces)
            if (!found_any || surf->z_order > best_z) {
                best = surf;
                best_z = surf->z_order;
                found_any = true;
            }
        }
    }
    return best;
}

Surface *find_surface_by_id(uint32_t id) {
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (g_surfaces[i].in_use && g_surfaces[i].id == id) {
            return &g_surfaces[i];
        }
    }
    return nullptr;
}

Surface *get_focused_surface() {
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (g_surfaces[i].in_use && g_surfaces[i].id == g_focused_surface) {
            return &g_surfaces[i];
        }
    }
    return nullptr;
}

Surface *get_menu_surface() {
    // First, try the focused surface
    Surface *focused = get_focused_surface();
    if (focused && focused->menu_count > 0) {
        return focused;
    }

    // Fall back to a SYSTEM surface with menus (the desktop)
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (g_surfaces[i].in_use && (g_surfaces[i].flags & SURFACE_FLAG_SYSTEM) &&
            g_surfaces[i].menu_count > 0) {
            return &g_surfaces[i];
        }
    }

    return nullptr;
}

} // namespace displayd
