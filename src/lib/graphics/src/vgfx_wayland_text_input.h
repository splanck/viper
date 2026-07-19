//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_text_input.h
// Purpose: Bridge Zanna's text-input contract to zwp_text_input_v3.
// Key invariants:
//   - Protocol updates are published only at done boundaries.
//   - Surrounding-text byte offsets are translated to public codepoint offsets.
// Ownership/Lifetime: The object owns its protocol proxy and copied surrounding text.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_input.h"

typedef struct vgfx_wayland_text_input {
    vgfx_wayland_connection_t *connection;
    vgfx_wayland_input_t *input;
    struct vgfx_window *window;
    struct zwp_text_input_v3 *proxy;
    char *surrounding;
    char *pending_preedit;
    char *pending_commit;
    int32_t cursor_byte;
    int32_t anchor_byte;
    int32_t preedit_begin_byte;
    int32_t preedit_end_byte;
    uint32_t delete_before_bytes;
    uint32_t delete_after_bytes;
    uint32_t commit_serial;
    vgfx_text_input_state_t state;
    int32_t desired_enabled;
    int32_t entered;
    int32_t protocol_enabled;
    int32_t composition_active;
    int32_t have_preedit;
    int32_t have_commit;
    int32_t have_delete;
} vgfx_wayland_text_input_t;

int vgfx_wayland_text_input_open(vgfx_wayland_text_input_t *text_input,
                                 vgfx_wayland_connection_t *connection,
                                 vgfx_wayland_input_t *input,
                                 struct vgfx_window *window);
void vgfx_wayland_text_input_close(vgfx_wayland_text_input_t *text_input);
int vgfx_wayland_text_input_set_enabled(vgfx_wayland_text_input_t *text_input, int32_t enabled);
int vgfx_wayland_text_input_set_state(vgfx_wayland_text_input_t *text_input,
                                      const vgfx_text_input_state_t *state);
