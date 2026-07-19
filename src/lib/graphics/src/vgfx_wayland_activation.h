//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_activation.h
// Purpose: Request compositor-authorized activation from recent seat interaction.
// Key invariants: Requests are issued only with xdg-activation and a non-zero seat serial.
// Ownership/Lifetime: Owns an optional pending token proxy and borrows all other objects.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_input.h"

typedef struct vgfx_wayland_activation {
    vgfx_wayland_connection_t *connection;
    vgfx_wayland_input_t *input;
    struct wl_proxy *surface;
    struct xdg_activation_token_v1 *token;
    const char *app_id;
    uint64_t completed;
} vgfx_wayland_activation_t;

void vgfx_wayland_activation_init(vgfx_wayland_activation_t *activation,
                                  vgfx_wayland_connection_t *connection,
                                  vgfx_wayland_input_t *input,
                                  struct wl_proxy *surface,
                                  const char *app_id);
int vgfx_wayland_activation_request(vgfx_wayland_activation_t *activation);
void vgfx_wayland_activation_close(vgfx_wayland_activation_t *activation);
uint32_t vgfx_wayland_activation_serial(const vgfx_wayland_input_t *input);
