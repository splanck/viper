//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_lighting2d.h
// Purpose: 2D darkness overlay with dynamic point lights for cave and exploration games.
//
// Key invariants:
//   - Darkness alpha 0=fully lit, 255=pitch black.
//   - Dynamic lights have a lifetime in frames; expired lights are recycled.
//   - Player light pulses automatically via internal timer.
//   - Draw() must be called AFTER all game entities are rendered (post-process).
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: src/runtime/game/rt_lighting2d.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_lighting2d_impl *rt_lighting2d;

/// @brief Create a 2D darkness/lighting overlay sized for at most @p max_lights concurrent lights.
rt_lighting2d rt_lighting2d_new(int64_t max_lights);
/// @brief Free the lighting overlay (also reclaimed by GC).
void rt_lighting2d_destroy(rt_lighting2d lit);

// Configuration
/// @brief Set the global darkness alpha (0 = fully lit, 255 = pitch black).
void rt_lighting2d_set_darkness(rt_lighting2d lit, int64_t alpha);
/// @brief Read the current darkness alpha.
int64_t rt_lighting2d_get_darkness(rt_lighting2d lit);
/// @brief Tint the darkness color (0x00RRGGBB; black = standard cave; blue = night, etc.).
void rt_lighting2d_set_tint_color(rt_lighting2d lit, int64_t color);
/// @brief Read the darkness tint color.
int64_t rt_lighting2d_get_tint_color(rt_lighting2d lit);
/// @brief Configure the player's auto-pulsing light (radius in pixels, RGB color).
void rt_lighting2d_set_player_light(rt_lighting2d lit, int64_t radius, int64_t color);

// Dynamic lights
/// @brief Add a world-space dynamic light (lifetime in frames; 0 = permanent).
void rt_lighting2d_add_light(
    rt_lighting2d lit, int64_t x, int64_t y, int64_t radius, int64_t color, int64_t lifetime);
/// @brief Add a screen-space ("tile") light at the given screen pixel — used for HUD glow.
void rt_lighting2d_add_tile_light(
    rt_lighting2d lit, int64_t screen_x, int64_t screen_y, int64_t radius, int64_t color);
/// @brief Remove all dynamic lights (player light is preserved).
void rt_lighting2d_clear_lights(rt_lighting2d lit);

// Per-frame
/// @brief Tick lifetimes (recycle expired lights) and advance the player-light pulse.
void rt_lighting2d_update(rt_lighting2d lit);
/// @brief Render the darkness overlay with all lights cut out, on top of the already-drawn scene.
/// Call AFTER all entities are rendered. (cam_x, cam_y) is the world-to-screen offset; (player_sx,
/// player_sy) is where the player light should be centered in screen pixels.
void rt_lighting2d_draw(rt_lighting2d lit,
                        void *canvas,
                        int64_t cam_x,
                        int64_t cam_y,
                        int64_t player_sx,
                        int64_t player_sy);

// Queries
/// @brief Number of active dynamic lights.
int64_t rt_lighting2d_get_light_count(rt_lighting2d lit);

#ifdef __cplusplus
}
#endif
