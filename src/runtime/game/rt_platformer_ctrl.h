//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_platformer_ctrl.h
// Purpose: Platformer input controller with jump buffer, coyote time,
//   variable jump height, and acceleration/deceleration curves.
//
// Key invariants:
//   - All timing is ms-based (delta-time independent).
//   - Update() must be called once per frame with dt and input state.
//   - ShouldJump is a one-shot flag consumed on read.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: src/runtime/game/rt_platformer_ctrl.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_platformer_ctrl_impl *rt_platformer_ctrl;

/// @brief Create a platformer input controller with default tuning.
rt_platformer_ctrl rt_platformer_ctrl_new(void);
/// @brief Free the controller (also reclaimed by GC).
void rt_platformer_ctrl_destroy(rt_platformer_ctrl ctrl);

// Configuration
/// @brief Set the jump-buffer window in ms (jump-press just before landing still triggers).
void rt_platformer_ctrl_set_jump_buffer(rt_platformer_ctrl ctrl, int64_t ms);
/// @brief Set the coyote-time window in ms (jump still works just after walking off a ledge).
void rt_platformer_ctrl_set_coyote_time(rt_platformer_ctrl ctrl, int64_t ms);
/// @brief Set per-axis acceleration: ground accel, in-air accel, and deceleration when no input.
void rt_platformer_ctrl_set_acceleration(rt_platformer_ctrl ctrl,
                                         int64_t ground,
                                         int64_t air,
                                         int64_t decel);
/// @brief Set jump force: @p full_force is the initial impulse, @p cut_force is the minimum
/// kept when the jump button is released early (variable jump height).
void rt_platformer_ctrl_set_jump_force(rt_platformer_ctrl ctrl,
                                       int64_t full_force,
                                       int64_t cut_force);
/// @brief Set max horizontal speeds for normal walking and sprinting (centipixels/s).
void rt_platformer_ctrl_set_max_speed(rt_platformer_ctrl ctrl, int64_t normal, int64_t sprint);
/// @brief Set gravity (per-tick velocity delta) and terminal-velocity cap.
void rt_platformer_ctrl_set_gravity(rt_platformer_ctrl ctrl, int64_t gravity, int64_t max_fall);
/// @brief Apply reduced gravity near the apex of a jump (smoother feel) — threshold in centipixels/s
/// of |vy|, gravity_mult_pct in percent (50 = half gravity at apex).
void rt_platformer_ctrl_set_apex_bonus(rt_platformer_ctrl ctrl,
                                       int64_t threshold,
                                       int64_t gravity_mult_pct);

// Per-frame update
/// @brief Tick the controller: process input flags, integrate velocity, decay timers.
void rt_platformer_ctrl_update(rt_platformer_ctrl ctrl,
                               int64_t dt,
                               int8_t input_left,
                               int8_t input_right,
                               int8_t jump_pressed,
                               int8_t jump_held,
                               int8_t on_ground,
                               int8_t sprint);

// Output queries
/// @brief Read the current horizontal velocity (centipixels/s).
int64_t rt_platformer_ctrl_get_vx(rt_platformer_ctrl ctrl);
/// @brief Read the current vertical velocity (centipixels/s, positive = downward).
int64_t rt_platformer_ctrl_get_vy(rt_platformer_ctrl ctrl);
/// @brief One-shot flag: true if the controller wants to jump *this* frame (consumed on read).
int8_t rt_platformer_ctrl_should_jump(rt_platformer_ctrl ctrl);
/// @brief Get the jump impulse magnitude to apply when ShouldJump is consumed.
int64_t rt_platformer_ctrl_get_jump_force(rt_platformer_ctrl ctrl);
/// @brief Get the facing direction (-1 = left, 1 = right, 0 = idle since spawn).
int64_t rt_platformer_ctrl_get_facing(rt_platformer_ctrl ctrl);
/// @brief True if the player is actively moving (|vx| above a small threshold).
int8_t rt_platformer_ctrl_is_moving(rt_platformer_ctrl ctrl);

// Direct velocity control (for external physics like wall jump, knockback)
/// @brief Override horizontal velocity (e.g., for wall-jump or knockback impulses).
void rt_platformer_ctrl_set_vx(rt_platformer_ctrl ctrl, int64_t vx);
/// @brief Override vertical velocity (e.g., for spring pads or hard knockback).
void rt_platformer_ctrl_set_vy(rt_platformer_ctrl ctrl, int64_t vy);

#ifdef __cplusplus
}
#endif
