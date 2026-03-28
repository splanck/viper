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

rt_platformer_ctrl rt_platformer_ctrl_new(void);
void rt_platformer_ctrl_destroy(rt_platformer_ctrl ctrl);

// Configuration
void rt_platformer_ctrl_set_jump_buffer(rt_platformer_ctrl ctrl, int64_t ms);
void rt_platformer_ctrl_set_coyote_time(rt_platformer_ctrl ctrl, int64_t ms);
void rt_platformer_ctrl_set_acceleration(rt_platformer_ctrl ctrl,
                                         int64_t ground,
                                         int64_t air,
                                         int64_t decel);
void rt_platformer_ctrl_set_jump_force(rt_platformer_ctrl ctrl,
                                       int64_t full_force,
                                       int64_t cut_force);
void rt_platformer_ctrl_set_max_speed(rt_platformer_ctrl ctrl, int64_t normal, int64_t sprint);
void rt_platformer_ctrl_set_gravity(rt_platformer_ctrl ctrl, int64_t gravity, int64_t max_fall);
void rt_platformer_ctrl_set_apex_bonus(rt_platformer_ctrl ctrl,
                                       int64_t threshold,
                                       int64_t gravity_mult_pct);

// Per-frame update
void rt_platformer_ctrl_update(rt_platformer_ctrl ctrl,
                               int64_t dt,
                               int8_t input_left,
                               int8_t input_right,
                               int8_t jump_pressed,
                               int8_t jump_held,
                               int8_t on_ground,
                               int8_t sprint);

// Output queries
int64_t rt_platformer_ctrl_get_vx(rt_platformer_ctrl ctrl);
int64_t rt_platformer_ctrl_get_vy(rt_platformer_ctrl ctrl);
int8_t rt_platformer_ctrl_should_jump(rt_platformer_ctrl ctrl);
int64_t rt_platformer_ctrl_get_jump_force(rt_platformer_ctrl ctrl);
int64_t rt_platformer_ctrl_get_facing(rt_platformer_ctrl ctrl);
int8_t rt_platformer_ctrl_is_moving(rt_platformer_ctrl ctrl);

// Direct velocity control (for external physics like wall jump, knockback)
void rt_platformer_ctrl_set_vx(rt_platformer_ctrl ctrl, int64_t vx);
void rt_platformer_ctrl_set_vy(rt_platformer_ctrl ctrl, int64_t vy);

#ifdef __cplusplus
}
#endif
