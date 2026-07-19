//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_relative.h
// Purpose: Provide native Wayland pointer lock and unbounded relative motion.
// Key invariants: Native mode requires both relative-pointer and pointer-constraints globals.
// Ownership/Lifetime: Owns per-pointer proxies and borrows connection, input, surface, and window.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_input.h"

typedef struct vgfx_wayland_relative {
    vgfx_wayland_connection_t *connection;
    vgfx_wayland_input_t *input;
    struct vgfx_window *window;
    struct wl_proxy *surface;
    struct zwp_relative_pointer_v1 *relative_pointer;
    struct zwp_locked_pointer_v1 *locked_pointer;
    int32_t enabled;
    int32_t locked;
} vgfx_wayland_relative_t;

void vgfx_wayland_relative_init(vgfx_wayland_relative_t *relative,
                                vgfx_wayland_connection_t *connection,
                                vgfx_wayland_input_t *input,
                                struct wl_proxy *surface,
                                struct vgfx_window *window);
int vgfx_wayland_relative_set_enabled(vgfx_wayland_relative_t *relative, int32_t enabled);
void vgfx_wayland_relative_close(vgfx_wayland_relative_t *relative);
double vgfx_wayland_relative_fixed(wl_fixed_t value);
