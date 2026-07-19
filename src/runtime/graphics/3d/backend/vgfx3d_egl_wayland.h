//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_egl_wayland.h
// Purpose: Expose a header-free dynamic EGL binding for a Wayland wl_surface.
// Key invariants: No EGL or wayland-egl development headers or link dependency are required.
// Ownership/Lifetime: A binding owns its EGL display/context/surface and wl_egl_window.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

typedef struct vgfx3d_egl_wayland vgfx3d_egl_wayland_t;

int vgfx3d_egl_wayland_available(void);
void *vgfx3d_egl_wayland_get_proc(const char *name);
vgfx3d_egl_wayland_t *vgfx3d_egl_wayland_create(void *display,
                                                void *surface,
                                                int32_t width,
                                                int32_t height);
int vgfx3d_egl_wayland_make_current(vgfx3d_egl_wayland_t *binding);
int vgfx3d_egl_wayland_swap(vgfx3d_egl_wayland_t *binding);
int vgfx3d_egl_wayland_set_swap_interval(vgfx3d_egl_wayland_t *binding, int32_t interval);
void vgfx3d_egl_wayland_resize(vgfx3d_egl_wayland_t *binding,
                               int32_t width,
                               int32_t height);
void vgfx3d_egl_wayland_destroy(vgfx3d_egl_wayland_t *binding);
