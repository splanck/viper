//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_physics2d.h
// Purpose: 2D physics engine for game entities providing AABB/circle rigid-body simulation,
// impulse-based collision resolution, bitmask collision filtering, and a simple world step loop.
//
// Key invariants:
//   - Bodies are AABB or circle shapes; no rotational physics.
//   - Fast bodies use shape-aware swept collision checks.
//   - A world holds at most PH_MAX_BODIES (256) bodies; exceeding this traps.
//   - Bodies with mass == 0.0 are static (immovable).
//   - Collision filtering: bodies collide only when (A.layer & B.mask) != 0 AND (B.layer & A.mask)
//   != 0.
//   - Collision layer/mask values are 64-bit bitmasks; the default mask is all bits set.
//
// Ownership/Lifetime:
//   - World objects are GC-managed; body handles are reference-counted.
//   - Adding a body to a world transfers logical ownership; removing it releases the world's
//   reference.
//
// Links: src/runtime/graphics/rt_physics2d.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_physics2d_joint.h"
#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// Physics World
//=========================================================================

/// @brief Create a new 2D physics world.
/// @param gravity_x Horizontal gravity.
/// @param gravity_y Vertical gravity (positive = downward in screen coords).
/// @return Opaque world handle.
void *rt_physics2d_world_new(double gravity_x, double gravity_y);

/// @brief Step the physics simulation forward. Clears previous contacts first; non-finite and
/// non-positive dt values then no-op.
/// @param world World handle.
/// @param dt Delta time in seconds.
void rt_physics2d_world_step(void *world, double dt);

/// @brief Add a body to the world. Duplicate adds are ignored; exceeding capacity traps.
/// @param world World handle.
/// @param body Body handle.
void rt_physics2d_world_add(void *world, void *body);

/// @brief Remove a body from the world.
/// @param world World handle.
/// @param body Body handle.
void rt_physics2d_world_remove(void *world, void *body);

/// @brief Get the number of bodies in the world.
/// @param world World handle.
/// @return Body count.
int64_t rt_physics2d_world_body_count(void *world);

/// @brief Set world gravity.
/// @param world World handle.
/// @param gx Horizontal gravity.
/// @param gy Vertical gravity.
void rt_physics2d_world_set_gravity(void *world, double gx, double gy);

/// @brief Number of contacts detected during the most recent world step.
/// The contact list is cleared at the start of every Step() and when bodies are removed.
int64_t rt_physics2d_world_contact_count(void *world);

/// @brief Body A for a contact from the most recent world step, or NULL if index is invalid.
void *rt_physics2d_world_contact_body_a(void *world, int64_t index);

/// @brief Body B for a contact from the most recent world step, or NULL if index is invalid.
void *rt_physics2d_world_contact_body_b(void *world, int64_t index);

/// @brief Contact normal X component from A toward B for the most recent world step.
double rt_physics2d_world_contact_nx(void *world, int64_t index);

/// @brief Contact normal Y component from A toward B for the most recent world step.
double rt_physics2d_world_contact_ny(void *world, int64_t index);

/// @brief Contact penetration depth for overlap contacts, 0 for swept contacts.
double rt_physics2d_world_contact_depth(void *world, int64_t index);

//=========================================================================
// Rigid Body
//=========================================================================

/// @brief Create a new rigid body (AABB shape). Invalid size clamps to 1; invalid mass is static.
/// @param x Initial X position.
/// @param y Initial Y position.
/// @param w Width.
/// @param h Height.
/// @param mass Mass (0 = static/immovable).
/// @return Opaque body handle.
void *rt_physics2d_body_new(double x, double y, double w, double h, double mass);

/// @brief Get body X position.
double rt_physics2d_body_x(void *body);

/// @brief Get body Y position.
double rt_physics2d_body_y(void *body);

/// @brief Get body X position from the start of the last successful simulation step.
double rt_physics2d_body_prev_x(void *body);

/// @brief Get body Y position from the start of the last successful simulation step.
double rt_physics2d_body_prev_y(void *body);

/// @brief Get body width.
double rt_physics2d_body_w(void *body);

/// @brief Get body height.
double rt_physics2d_body_h(void *body);

/// @brief Get body X velocity.
double rt_physics2d_body_vx(void *body);

/// @brief Get body Y velocity.
double rt_physics2d_body_vy(void *body);

/// @brief Set body position.
void rt_physics2d_body_set_pos(void *body, double x, double y);

/// @brief Set body velocity. Static bodies ignore velocity writes.
void rt_physics2d_body_set_vel(void *body, double vx, double vy);

/// @brief Apply a force to the body (accumulated until next step).
void rt_physics2d_body_apply_force(void *body, double fx, double fy);

/// @brief Apply an impulse (instant velocity change).
void rt_physics2d_body_apply_impulse(void *body, double ix, double iy);

/// @brief Get/set restitution (bounciness, clamped to 0-1).
double rt_physics2d_body_restitution(void *body);
void rt_physics2d_body_set_restitution(void *body, double r);

/// @brief Get/set friction (clamped to 0-1).
double rt_physics2d_body_friction(void *body);
void rt_physics2d_body_set_friction(void *body, double f);

/// @brief Check if body is static (mass == 0).
int8_t rt_physics2d_body_is_static(void *body);

/// @brief Get body mass.
double rt_physics2d_body_mass(void *body);

/// @brief Get collision layer bitmask.
int64_t rt_physics2d_body_collision_layer(void *body);

/// @brief Set collision layer bitmask.
void rt_physics2d_body_set_collision_layer(void *body, int64_t layer);

/// @brief Get collision mask bitmask.
int64_t rt_physics2d_body_collision_mask(void *body);

/// @brief Set collision mask bitmask.
void rt_physics2d_body_set_collision_mask(void *body, int64_t mask);

//=========================================================================
// Projectile2D
//=========================================================================

void *rt_projectile2d_new(double p0x, double p0y, double v0x, double v0y, double gx, double gy);
void rt_projectile2d_set_drag(void *p, double drag);
void rt_projectile2d_set_ground_y(void *p, double y);
void rt_projectile2d_reset(void *p);
void rt_projectile2d_advance(void *p, double dt);
double rt_projectile2d_x_at(void *p, double t);
double rt_projectile2d_y_at(void *p, double t);
double rt_projectile2d_vx_at(void *p, double t);
double rt_projectile2d_vy_at(void *p, double t);
int8_t rt_projectile2d_has_landed(void *p);
double rt_projectile2d_total_time(void *p);
double rt_projectile2d_time_to_ground(void *p);

#ifdef __cplusplus
}
#endif
