//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_scale.h
// Purpose: Track Wayland outputs and integer framebuffer scaling for one surface.
// Key invariants: The active scale is the maximum scale of all entered outputs.
// Ownership/Lifetime: Owns bound wl_output proxies and borrows connection/shell.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_shell.h"

typedef struct vgfx_wayland_output {
    struct wl_proxy *proxy;
    uint32_t name;
    int32_t scale;
    int32_t mode_width;
    int32_t mode_height;
    int32_t entered;
} vgfx_wayland_output_t;

typedef struct vgfx_wayland_scale {
    vgfx_wayland_connection_t *connection;
    vgfx_wayland_shell_t *shell;
    vgfx_wayland_output_t outputs[16];
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;
    uint32_t output_count;
    int32_t scale;
    int32_t preferred_buffer_scale;
    uint32_t preferred_scale_120;
    int32_t logical_width;
    int32_t logical_height;
    uint64_t generation;
} vgfx_wayland_scale_t;

int vgfx_wayland_scale_open(vgfx_wayland_scale_t *scale,
                            vgfx_wayland_connection_t *connection,
                            vgfx_wayland_shell_t *shell);
void vgfx_wayland_scale_close(vgfx_wayland_scale_t *scale);
float vgfx_wayland_scale_factor(const vgfx_wayland_scale_t *scale);
/// @brief Select the maximum valid scale among entered outputs (testable policy helper).
int32_t vgfx_wayland_scale_select_factor(const vgfx_wayland_output_t *outputs, uint32_t count);
/// @brief Publish the logical destination used by an optional wp_viewport.
void vgfx_wayland_scale_set_logical_size(vgfx_wayland_scale_t *scale,
                                         int32_t width,
                                         int32_t height);
int vgfx_wayland_scale_monitor_size(const vgfx_wayland_scale_t *scale,
                                    int32_t *width,
                                    int32_t *height);
