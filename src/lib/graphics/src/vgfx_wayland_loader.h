//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_loader.h
// Purpose: Header-free dynamic dispatch surface for the Wayland client ABI.
// Key invariants:
//   - No Wayland development headers or link-time client dependency is required.
//   - A dispatch table is either completely initialized or completely zeroed.
// Ownership/Lifetime:
//   - The table owns its shared-object handle until vgfx_wayland_loader_close().
//   - Interface descriptors are borrowed from the loaded client library.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

struct wl_display;
struct wl_event_queue;
struct wl_proxy;

typedef int32_t wl_fixed_t;

struct wl_array {
    size_t size;
    size_t alloc;
    void *data;
};

struct wl_message {
    const char *name;
    const char *signature;
    const struct wl_interface **types;
};

struct wl_interface {
    const char *name;
    int version;
    int method_count;
    const struct wl_message *methods;
    int event_count;
    const struct wl_message *events;
};

typedef void (*vgfx_wayland_log_handler_t)(const char *format, ...);

typedef struct vgfx_wayland_client_api {
    void *library;

    struct wl_display *(*display_connect)(const char *name);
    void (*display_disconnect)(struct wl_display *display);
    int (*display_dispatch)(struct wl_display *display);
    int (*display_dispatch_pending)(struct wl_display *display);
    int (*display_roundtrip)(struct wl_display *display);
    int (*display_flush)(struct wl_display *display);
    int (*display_get_fd)(struct wl_display *display);
    int (*display_prepare_read)(struct wl_display *display);
    int (*display_read_events)(struct wl_display *display);
    void (*display_cancel_read)(struct wl_display *display);
    int (*display_get_error)(struct wl_display *display);

    void (*proxy_destroy)(struct wl_proxy *proxy);
    int (*proxy_add_listener)(struct wl_proxy *proxy, void (**implementation)(void), void *data);
    struct wl_proxy *(*proxy_marshal_flags)(struct wl_proxy *proxy,
                                            uint32_t opcode,
                                            const struct wl_interface *interface,
                                            uint32_t version,
                                            uint32_t flags,
                                            ...);
    uint32_t (*proxy_get_version)(struct wl_proxy *proxy);
    void (*log_set_handler_client)(vgfx_wayland_log_handler_t handler);

    const struct wl_interface *display_interface;
    const struct wl_interface *registry_interface;
    const struct wl_interface *callback_interface;
    const struct wl_interface *compositor_interface;
    const struct wl_interface *subcompositor_interface;
    const struct wl_interface *subsurface_interface;
    const struct wl_interface *surface_interface;
    const struct wl_interface *region_interface;
    const struct wl_interface *shm_interface;
    const struct wl_interface *shm_pool_interface;
    const struct wl_interface *buffer_interface;
    const struct wl_interface *seat_interface;
    const struct wl_interface *pointer_interface;
    const struct wl_interface *keyboard_interface;
    const struct wl_interface *touch_interface;
    const struct wl_interface *output_interface;
    const struct wl_interface *data_device_manager_interface;
    const struct wl_interface *data_device_interface;
    const struct wl_interface *data_source_interface;
    const struct wl_interface *data_offer_interface;
} vgfx_wayland_client_api_t;

/// @brief Load the system Wayland client ABI.
/// @param api Destination table, zeroed on every failure.
/// @param library_name Optional explicit shared-object name; NULL tries supported system names.
/// @param error Destination for a stable diagnostic when non-NULL and error_size is non-zero.
/// @param error_size Capacity of error including its terminator.
/// @return 1 only when every required symbol was resolved.
int vgfx_wayland_loader_open(vgfx_wayland_client_api_t *api,
                             const char *library_name,
                             char *error,
                             uint32_t error_size);

/// @brief Close a loaded Wayland client ABI and zero the table.
void vgfx_wayland_loader_close(vgfx_wayland_client_api_t *api);
