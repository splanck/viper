//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_cursor.h
// Purpose: Manage themed Wayland cursor surfaces without link-time dependencies.
// Key invariants: Cursor updates use the most recent pointer-enter serial.
// Ownership/Lifetime: One cursor object owns its theme library, theme, and wl_surface.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_input.h"

struct wl_cursor_theme;
struct wl_cursor_image;
struct wl_cursor;

typedef struct vgfx_wayland_cursor {
    vgfx_wayland_connection_t *connection;
    vgfx_wayland_input_t *input;
    void *library;
    struct wl_cursor_theme *theme;
    struct wl_proxy *surface;
    struct wl_cursor_theme *(*theme_load)(const char *, int, struct wl_proxy *);
    void (*theme_destroy)(struct wl_cursor_theme *);
    struct wl_cursor *(*theme_get_cursor)(struct wl_cursor_theme *, const char *);
    struct wl_proxy *(*image_get_buffer)(struct wl_cursor_image *);
    int32_t type;
    int32_t visible;
} vgfx_wayland_cursor_t;

/// @brief Return the primary Xcursor theme name for one public cursor type.
const char *vgfx_wayland_cursor_name(int32_t type);

int vgfx_wayland_cursor_open(vgfx_wayland_cursor_t *cursor,
                             vgfx_wayland_connection_t *connection,
                             vgfx_wayland_input_t *input);
void vgfx_wayland_cursor_close(vgfx_wayland_cursor_t *cursor);
void vgfx_wayland_cursor_set(vgfx_wayland_cursor_t *cursor, int32_t type, int32_t visible);
