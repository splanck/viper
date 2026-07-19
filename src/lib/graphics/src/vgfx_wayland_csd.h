//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_csd.h
// Purpose: Built-in Wayland client-side frame used when server decorations are unavailable.
// Key invariants:
//   - The application content surface and framebuffer dimensions remain unchanged.
//   - One synchronized subsurface supplies title-bar, border, and resize input regions.
// Ownership/Lifetime:
//   - The CSD object owns its child surface, subsurface role, SHM presenter, and RGBA source.
//   - It borrows the shell, input object, and ZannaGFX window.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_input.h"
#include "vgfx_wayland_shm.h"

typedef struct vgfx_wayland_csd {
    vgfx_wayland_shell_t *shell;
    vgfx_wayland_input_t *input;
    struct vgfx_window *window;
    struct wl_surface *surface;
    struct wl_proxy *subsurface;
    vgfx_wayland_shm_presenter_t presenter;
    uint8_t *pixels;
    size_t pixels_size;
    int32_t width;
    int32_t height;
    int32_t pointer_x;
    int32_t pointer_y;
    int32_t active;
} vgfx_wayland_csd_t;

/// @brief Create the fallback frame when the compositor will not draw server decorations.
/// @return One when active or unnecessary, zero only for a failed required fallback setup.
int vgfx_wayland_csd_open(vgfx_wayland_csd_t *csd,
                          vgfx_wayland_shell_t *shell,
                          vgfx_wayland_input_t *input,
                          struct vgfx_window *window,
                          int32_t width,
                          int32_t height,
                          char *error,
                          uint32_t error_size);

/// @brief Resize and redraw an active fallback frame; inactive objects are accepted.
int vgfx_wayland_csd_resize(vgfx_wayland_csd_t *csd,
                            int32_t width,
                            int32_t height,
                            char *error,
                            uint32_t error_size);

/// @brief Destroy all fallback-frame resources and clear installed input callbacks.
void vgfx_wayland_csd_close(vgfx_wayland_csd_t *csd);
