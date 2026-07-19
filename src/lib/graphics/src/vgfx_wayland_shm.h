//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_shm.h
// Purpose: Release-tracked wl_shm presentation buffers for the native Wayland backend.
// Key invariants:
//   - A busy buffer is never overwritten before wl_buffer.release.
//   - All width, height, stride, offset, and pool-size arithmetic is checked.
// Ownership/Lifetime:
//   - The presenter owns its mapped storage, pool proxy, and two buffer proxies.
//   - The target wl_surface and connection are borrowed from the shell.
// Links: src/lib/graphics/src/vgfx_wayland_shell.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_shell.h"

#include <stddef.h>
#include <stdint.h>

typedef struct vgfx_wayland_shm_slot {
    struct wl_proxy *buffer;
    uint8_t *pixels;
    int32_t busy;
    struct vgfx_wayland_shm_presenter *presenter;
} vgfx_wayland_shm_slot_t;

typedef struct vgfx_wayland_shm_presenter {
    vgfx_wayland_shell_t *shell;
    struct wl_surface *surface;
    struct wl_proxy *pool;
    void *mapping;
    size_t mapping_size;
    size_t buffer_size;
    int32_t width;
    int32_t height;
    int32_t stride;
    uint64_t generation;
    struct wl_proxy *frame_callback;
    const uint8_t *queued_rgba;
    size_t queued_rgba_size;
    int32_t queued;
    vgfx_wayland_shm_slot_t slots[2];
} vgfx_wayland_shm_presenter_t;

/// @brief Allocate two compositor-visible buffers for an xdg shell surface.
int vgfx_wayland_shm_open(vgfx_wayland_shm_presenter_t *presenter,
                          vgfx_wayland_shell_t *shell,
                          int32_t width,
                          int32_t height,
                          char *error,
                          uint32_t error_size);

/// @brief Allocate two compositor-visible buffers for an arbitrary surface on the shell display.
/// @details This is used by synchronized client-decoration subsurfaces; the shell remains the
///          owner of the connection while @p surface is borrowed until presenter close.
int vgfx_wayland_shm_open_surface(vgfx_wayland_shm_presenter_t *presenter,
                                  vgfx_wayland_shell_t *shell,
                                  struct wl_surface *surface,
                                  int32_t width,
                                  int32_t height,
                                  char *error,
                                  uint32_t error_size);

/// @brief Queue opaque RGBA pixels and commit at compositor frame cadence.
/// @details The source pixels remain borrowed until the next frame callback or presenter close.
/// @return 1 when accepted (including a coalesced update), 0 for invalid arguments.
int vgfx_wayland_shm_present(vgfx_wayland_shm_presenter_t *presenter,
                             const uint8_t *rgba,
                             size_t rgba_size);

/// @brief Destroy buffers/pool, unmap storage, and zero the presenter.
void vgfx_wayland_shm_close(vgfx_wayland_shm_presenter_t *presenter);
