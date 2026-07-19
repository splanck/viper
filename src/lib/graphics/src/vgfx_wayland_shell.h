//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_shell.h
// Purpose: Own the core and xdg-shell objects that give a Wayland window its role.
// Key invariants:
//   - A successful open has acknowledged its first xdg_surface configure.
//   - Destruction follows role object, xdg surface, then wl_surface order.
// Ownership/Lifetime:
//   - The shell surface borrows its connection and owns its three protocol proxies.
//   - Listener callbacks are valid until vgfx_wayland_shell_close() returns.
// Links: src/lib/graphics/src/vgfx_wayland_connection.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_connection.h"

typedef struct vgfx_wayland_shell {
    vgfx_wayland_connection_t *connection;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    int32_t configured;
    int32_t close_requested;
    int32_t width;
    int32_t height;
    int32_t maximized;
    int32_t fullscreen;
    int32_t activated;
    uint32_t decoration_mode;
    struct wl_proxy *entered_outputs[16];
    uint32_t entered_output_count;
    void (*output_observer)(void *data, struct wl_proxy *output, int32_t entered);
    void *output_observer_data;
} vgfx_wayland_shell_t;

/// @brief Create an xdg toplevel and complete its initial configure handshake.
int vgfx_wayland_shell_open(vgfx_wayland_shell_t *shell,
                            vgfx_wayland_connection_t *connection,
                            const char *title,
                            const char *app_id,
                            char *error,
                            uint32_t error_size);

/// @brief Destroy all owned role and surface objects and zero the shell.
void vgfx_wayland_shell_close(vgfx_wayland_shell_t *shell);
