//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics2d.h
// Purpose: 2D physics engine for game entities providing AABB rigid-body simulation, impulse-based
// collision resolution, bitmask collision filtering, and a simple world step loop.
//
// Key invariants:
//   - Bodies are AABB only; no rotational physics.
//   - A world holds at most PH_MAX_BODIES (256) bodies; exceeding this traps.
//   - Bodies with mass == 0.0 are static (immovable).
//   - Collision filtering: bodies collide only when (A.layer & B.mask) != 0 AND (B.layer & A.mask)
//   != 0.
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

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Physics World
    //=========================================================================

    /// @brief Create a new 2D physics world.
    /// @param gravity_x Horizontal gravity.
    /// @param gravity_y Vertical gravity (positive = downward in screen coords).
    /// @return Opaque world handle.
    void *rt_physics2d_world_new(double gravity_x, double gravity_y);

    /// @brief Step the physics simulation forward.
    /// @param world World handle.
    /// @param dt Delta time in seconds.
    void rt_physics2d_world_step(void *world, double dt);

    /// @brief Add a body to the world.
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

    //=========================================================================
    // Rigid Body
    //=========================================================================

    /// @brief Create a new rigid body (AABB shape).
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

    /// @brief Set body velocity.
    void rt_physics2d_body_set_vel(void *body, double vx, double vy);

    /// @brief Apply a force to the body (accumulated until next step).
    void rt_physics2d_body_apply_force(void *body, double fx, double fy);

    /// @brief Apply an impulse (instant velocity change).
    void rt_physics2d_body_apply_impulse(void *body, double ix, double iy);

    /// @brief Get/set restitution (bounciness, 0-1).
    double rt_physics2d_body_restitution(void *body);
    void rt_physics2d_body_set_restitution(void *body, double r);

    /// @brief Get/set friction (0-1).
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

#ifdef __cplusplus
}
#endif
