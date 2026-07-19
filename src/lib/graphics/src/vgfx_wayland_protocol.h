//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_protocol.h
// Purpose: Repository-owned stable xdg-shell client protocol metadata and requests.
// Key invariants:
//   - Metadata matches stable xdg-shell through interface version 6.
//   - Requests use only the dynamically resolved Wayland client ABI.
// Ownership/Lifetime:
//   - Interface descriptors have process lifetime and own no dynamic memory.
//   - Returned proxies are owned by the caller and tied to its display connection.
// Links: stable/xdg-shell/xdg-shell.xml,
//        docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_loader.h"

struct wl_output;
struct wl_registry;
struct wl_seat;
struct wl_surface;
struct xdg_popup;
struct zwp_text_input_manager_v3;
struct zwp_text_input_v3;
struct wp_viewporter;
struct wp_viewport;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;
struct zwp_relative_pointer_manager_v1;
struct zwp_relative_pointer_v1;
struct zwp_pointer_constraints_v1;
struct zwp_locked_pointer_v1;
struct xdg_activation_v1;
struct xdg_activation_token_v1;
struct xdg_positioner;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base;

extern const struct wl_interface vgfx_xdg_wm_base_interface;
extern const struct wl_interface vgfx_xdg_positioner_interface;
extern const struct wl_interface vgfx_xdg_surface_interface;
extern const struct wl_interface vgfx_xdg_toplevel_interface;
extern const struct wl_interface vgfx_xdg_popup_interface;
extern const struct wl_interface vgfx_zwp_text_input_manager_v3_interface;
extern const struct wl_interface vgfx_zwp_text_input_v3_interface;
extern const struct wl_interface vgfx_wp_viewporter_interface;
extern const struct wl_interface vgfx_wp_viewport_interface;
extern const struct wl_interface vgfx_wp_fractional_scale_manager_v1_interface;
extern const struct wl_interface vgfx_wp_fractional_scale_v1_interface;
extern const struct wl_interface vgfx_zxdg_decoration_manager_v1_interface;
extern const struct wl_interface vgfx_zxdg_toplevel_decoration_v1_interface;
extern const struct wl_interface vgfx_zwp_relative_pointer_manager_v1_interface;
extern const struct wl_interface vgfx_zwp_relative_pointer_v1_interface;
extern const struct wl_interface vgfx_zwp_pointer_constraints_v1_interface;
extern const struct wl_interface vgfx_zwp_locked_pointer_v1_interface;
extern const struct wl_interface vgfx_xdg_activation_v1_interface;
extern const struct wl_interface vgfx_xdg_activation_token_v1_interface;

typedef struct vgfx_xdg_wm_base_listener {
    void (*ping)(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
} vgfx_xdg_wm_base_listener_t;

enum {
    VGFX_WL_MARSHAL_FLAG_DESTROY = 1,
    VGFX_WL_REGISTRY_BIND = 0,
    VGFX_WL_COMPOSITOR_CREATE_SURFACE = 0,
    VGFX_WL_SURFACE_DESTROY = 0,
    VGFX_WL_SURFACE_ATTACH = 1,
    VGFX_WL_SURFACE_DAMAGE = 2,
    VGFX_WL_SURFACE_FRAME = 3,
    VGFX_WL_SURFACE_COMMIT = 6,
    VGFX_XDG_WM_BASE_DESTROY = 0,
    VGFX_XDG_WM_BASE_GET_XDG_SURFACE = 2,
    VGFX_XDG_WM_BASE_PONG = 3,
    VGFX_XDG_SURFACE_DESTROY = 0,
    VGFX_XDG_SURFACE_GET_TOPLEVEL = 1,
    VGFX_XDG_SURFACE_ACK_CONFIGURE = 4,
    VGFX_XDG_TOPLEVEL_DESTROY = 0,
    VGFX_XDG_TOPLEVEL_SET_TITLE = 2,
    VGFX_XDG_TOPLEVEL_SET_APP_ID = 3,
    VGFX_XDG_TOPLEVEL_SET_MAX_SIZE = 7,
    VGFX_XDG_TOPLEVEL_SET_MIN_SIZE = 8,
    VGFX_XDG_TOPLEVEL_SET_MAXIMIZED = 9,
    VGFX_XDG_TOPLEVEL_UNSET_MAXIMIZED = 10,
    VGFX_XDG_TOPLEVEL_SET_FULLSCREEN = 11,
    VGFX_XDG_TOPLEVEL_UNSET_FULLSCREEN = 12,
    VGFX_XDG_TOPLEVEL_SET_MINIMIZED = 13,
};

/// @brief Bind a registry global, capping the requested version at the caller's tested version.
struct wl_proxy *vgfx_wayland_registry_bind(const vgfx_wayland_client_api_t *api,
                                             struct wl_registry *registry,
                                             uint32_t name,
                                             const struct wl_interface *interface,
                                             uint32_t advertised_version,
                                             uint32_t tested_version);

/// @brief Add the one-event xdg_wm_base listener.
int vgfx_xdg_wm_base_add_listener(const vgfx_wayland_client_api_t *api,
                                  struct xdg_wm_base *wm_base,
                                  const vgfx_xdg_wm_base_listener_t *listener,
                                  void *data);

/// @brief Reply to an xdg_wm_base ping.
void vgfx_xdg_wm_base_pong(const vgfx_wayland_client_api_t *api,
                           struct xdg_wm_base *wm_base,
                           uint32_t serial);
