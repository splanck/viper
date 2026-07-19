//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_wayland_input.h
// Purpose: Translate core Wayland seat input into ZannaGFX events and state.
// Key invariants:
//   - Pointer coordinates are converted from wl_fixed_t without Wayland headers.
//   - Keyboard layout state is optional and dynamically loaded from xkbcommon.
// Ownership/Lifetime:
//   - An input object borrows its connection, surface, and ZannaGFX window.
//   - It owns pointer/keyboard proxies and all xkb objects it creates.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "vgfx_wayland_connection.h"
#include "vgfx.h"

#include <stdint.h>

struct vgfx_window;

typedef struct vgfx_wayland_xkb {
    void *library;
    void *context;
    void *keymap;
    void *state;
    void *(*context_new)(int flags);
    void (*context_unref)(void *context);
    void *(*keymap_new_from_string)(void *context, const char *string, int format, int flags);
    void (*keymap_unref)(void *keymap);
    void *(*state_new)(void *keymap);
    void (*state_unref)(void *state);
    int (*state_update_mask)(void *state,
                             uint32_t depressed,
                             uint32_t latched,
                             uint32_t locked,
                             uint32_t depressed_layout,
                             uint32_t latched_layout,
                             uint32_t locked_layout);
    uint32_t (*state_key_get_utf32)(void *state, uint32_t key);
    int (*state_mod_name_is_active)(void *state, const char *name, int type);
    int (*keymap_key_repeats)(void *keymap, uint32_t key);
} vgfx_wayland_xkb_t;

typedef struct vgfx_wayland_input {
    vgfx_wayland_connection_t *connection;
    struct wl_proxy *surface;
    struct vgfx_window *window;
    struct wl_proxy *pointer;
    struct wl_proxy *keyboard;
    struct wl_proxy *touch;
    struct wl_proxy *pointer_surface;
    vgfx_wayland_xkb_t xkb;
    uint32_t capabilities;
    uint32_t pointer_serial;
    uint32_t keyboard_serial;
    int32_t pointer_x;
    int32_t pointer_y;
    int32_t modifiers;
    int32_t repeat_rate;
    int32_t repeat_delay;
    uint32_t repeat_code;
    vgfx_key_t repeat_key;
    int64_t repeat_at_ms;
    int32_t repeat_active;
    void (*pointer_entered)(void *data, uint32_t serial);
    void *pointer_entered_data;
    void (*foreign_pointer_event)(void *data,
                                  struct wl_proxy *surface,
                                  uint32_t serial,
                                  uint32_t time,
                                  int32_t x,
                                  int32_t y,
                                  uint32_t button,
                                  int32_t pressed);
    void *foreign_pointer_data;
    struct {
        int32_t id;
        int32_t x;
        int32_t y;
        float major;
        float minor;
        float orientation;
        int32_t active;
    } contacts[16];
} vgfx_wayland_input_t;

/// @brief Attach a seat listener and create devices advertised by the compositor.
int vgfx_wayland_input_open(vgfx_wayland_input_t *input,
                            vgfx_wayland_connection_t *connection,
                            struct wl_proxy *surface,
                            struct vgfx_window *window,
                            char *error,
                            uint32_t error_size);

/// @brief Destroy device proxies and keyboard-layout state; accepts partial objects.
void vgfx_wayland_input_close(vgfx_wayland_input_t *input);

/// @brief Emit compositor-configured keyboard repeats due at @p now_ms.
/// @return Number of repeat key events emitted, capped to prevent event storms.
int vgfx_wayland_input_tick(vgfx_wayland_input_t *input, int64_t now_ms);

/// @brief Clamp an event wait timeout so a pending key repeat is delivered on time.
int32_t vgfx_wayland_input_clamp_timeout(const vgfx_wayland_input_t *input,
                                         int32_t requested_ms,
                                         int64_t now_ms);

/// @brief Convert one evdev key code from wl_keyboard.key to a stable Zanna key.
/// @details Exposed to the backend test target; unknown and modifier-only codes return UNKNOWN.
vgfx_key_t vgfx_wayland_translate_evdev_key(uint32_t key);

/// @brief Round one signed 24.8 Wayland fixed coordinate to the nearest integer pixel.
int32_t vgfx_wayland_fixed_to_pixel(wl_fixed_t value);
