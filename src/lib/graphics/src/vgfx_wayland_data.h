//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_data.h
// Purpose: Implement Wayland clipboard selections and file drag-and-drop.
// Key invariants:
//   - All transfer descriptors are nonblocking and payloads are size bounded.
//   - Offer proxies remain alive until their selection or drag role ends.
// Ownership/Lifetime: One data object owns its device, sources, offers, and transfer buffers.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_input.h"

#include <stddef.h>
#include <stdint.h>

typedef struct vgfx_wayland_offer {
    struct wl_proxy *proxy;
    struct vgfx_wayland_offer *next;
    int32_t text_utf8;
    int32_t text_plain;
    int32_t uri_list;
    uint32_t action;
} vgfx_wayland_offer_t;

typedef struct vgfx_wayland_transfer {
    int32_t fd;
    int32_t outbound;
    int32_t uri_list;
    char *bytes;
    size_t size;
    size_t capacity;
    size_t offset;
} vgfx_wayland_transfer_t;

typedef struct vgfx_wayland_data {
    vgfx_wayland_connection_t *connection;
    vgfx_wayland_input_t *input;
    struct vgfx_window *window;
    struct wl_proxy *device;
    struct wl_proxy *source;
    vgfx_wayland_offer_t *offers;
    vgfx_wayland_offer_t *selection;
    vgfx_wayland_offer_t *drag;
    char *local_text;
    int32_t drag_x;
    int32_t drag_y;
    int32_t pending_selection;
    int32_t owns_selection;
    vgfx_wayland_transfer_t transfers[8];
} vgfx_wayland_data_t;

int vgfx_wayland_data_open(vgfx_wayland_data_t *data,
                           vgfx_wayland_connection_t *connection,
                           vgfx_wayland_input_t *input,
                           struct vgfx_window *window);
void vgfx_wayland_data_close(vgfx_wayland_data_t *data);
void vgfx_wayland_data_tick(vgfx_wayland_data_t *data);
int vgfx_wayland_data_has_text(const vgfx_wayland_data_t *data);
char *vgfx_wayland_data_get_text(vgfx_wayland_data_t *data);
int vgfx_wayland_data_set_text(vgfx_wayland_data_t *data, const char *text);
