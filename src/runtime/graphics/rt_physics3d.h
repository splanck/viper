//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics3d.h
// Purpose: 3D physics world with AABB/sphere/capsule collision, impulse
//   resolution, collision layers, and character controller with MoveAndSlide.
//
// Key invariants:
//   - Max 256 bodies per world (PH3D_MAX_BODIES).
//   - Symplectic Euler integration (force→velocity, velocity→position).
//   - Collision filtering: bidirectional layer/mask bitmask check.
//   - Character controller uses swept AABB + slide iterations.
//   - Trigger bodies overlap but don't apply physics impulse.
//   - Trigger3D: standalone AABB zone with frame-edge enter/exit detection.
//   - Max 64 tracked bodies per trigger (TRG3D_MAX_TRACKED).
//
// Links: rt_raycast3d.h, misc/plans/3d/20-phase-a-core-game-systems.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Physics3D World */
void *rt_world3d_new(double gx, double gy, double gz);
void rt_world3d_step(void *world, double dt);
void rt_world3d_add(void *world, void *body);
void rt_world3d_remove(void *world, void *body);
int64_t rt_world3d_body_count(void *world);
void rt_world3d_set_gravity(void *world, double gx, double gy, double gz);

/* Collision event queries (populated after each Step) */
int64_t rt_world3d_get_collision_count(void *world);
void *rt_world3d_get_collision_body_a(void *world, int64_t index);
void *rt_world3d_get_collision_body_b(void *world, int64_t index);
void *rt_world3d_get_collision_normal(void *world, int64_t index);
double rt_world3d_get_collision_depth(void *world, int64_t index);

/* Physics3D Body */
void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass);
void *rt_body3d_new_sphere(double radius, double mass);
void *rt_body3d_new_capsule(double radius, double height, double mass);
void rt_body3d_set_position(void *body, double x, double y, double z);
void *rt_body3d_get_position(void *body);
void rt_body3d_set_velocity(void *body, double vx, double vy, double vz);
void *rt_body3d_get_velocity(void *body);
void rt_body3d_apply_force(void *body, double fx, double fy, double fz);
void rt_body3d_apply_impulse(void *body, double ix, double iy, double iz);
void rt_body3d_set_restitution(void *body, double r);
double rt_body3d_get_restitution(void *body);
void rt_body3d_set_friction(void *body, double f);
double rt_body3d_get_friction(void *body);
void rt_body3d_set_collision_layer(void *body, int64_t layer);
int64_t rt_body3d_get_collision_layer(void *body);
void rt_body3d_set_collision_mask(void *body, int64_t mask);
int64_t rt_body3d_get_collision_mask(void *body);
void rt_body3d_set_static(void *body, int8_t s);
int8_t rt_body3d_is_static(void *body);
void rt_body3d_set_trigger(void *body, int8_t t);
int8_t rt_body3d_is_trigger(void *body);
int8_t rt_body3d_is_grounded(void *body);
void *rt_body3d_get_ground_normal(void *body);
double rt_body3d_get_mass(void *body);

/* Trigger3D — standalone AABB zone with enter/exit detection */
void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1);
int8_t rt_trigger3d_contains(void *trigger, void *point);
void rt_trigger3d_update(void *trigger, void *world);
int64_t rt_trigger3d_get_enter_count(void *trigger);
int64_t rt_trigger3d_get_exit_count(void *trigger);
void rt_trigger3d_set_bounds(
    void *trigger, double x0, double y0, double z0, double x1, double y1, double z1);

/* Character Controller */
void *rt_character3d_new(double radius, double height, double mass);
void rt_character3d_move(void *ctrl, void *velocity, double dt);
void rt_character3d_set_step_height(void *ctrl, double h);
double rt_character3d_get_step_height(void *ctrl);
void rt_character3d_set_slope_limit(void *ctrl, double degrees);
int8_t rt_character3d_is_grounded(void *ctrl);
int8_t rt_character3d_just_landed(void *ctrl);
void *rt_character3d_get_position(void *ctrl);
void rt_character3d_set_position(void *ctrl, double x, double y, double z);

#ifdef __cplusplus
}
#endif
