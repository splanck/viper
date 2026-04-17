//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics2d_internal.h
// Purpose: Shared internal types for the 2D physics engine, used by both
//   rt_physics2d.c and rt_physics2d_joint.c.
//
// Key invariants:
//   - rt_body_impl layout must match GC allocation expectations (vptr first).
//   - rt_world_impl stores both bodies and joints in fixed-capacity arrays.
//
// Ownership/Lifetime:
//   - Internal header — not part of public API.
//
// Links: rt_physics2d.c, rt_physics2d_joint.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#define PH_MAX_BODIES 256
#define PH_MAX_JOINTS 64
#define PH_JOINT_ITERATIONS 4

#define RT_PH_MAX_BODIES_STR "256"

/// @brief Internal representation of a single rigid body (AABB or circle).
typedef struct {
    void *vptr;              ///< Zia virtual-dispatch pointer (must be first).
    double x, y;             ///< Top-left position (AABB) or center (circle).
    double prev_x, prev_y;   ///< Previous-step position used for one-way tile tests.
    double w, h;             ///< Width and height of the AABB (0 for circles).
    double vx, vy;           ///< Velocity in world-units per second.
    double fx, fy;           ///< Accumulated force for the current frame.
    double mass;             ///< Mass in arbitrary units. 0 = static (immovable).
    double inv_mass;         ///< Reciprocal of mass (1/mass), or 0 for static bodies.
    double restitution;      ///< Bounciness coefficient in [0, 1].
    double friction;         ///< Kinetic friction coefficient in [0, 1].
    int64_t collision_layer; ///< Bitmask: which physical layer(s) this body occupies.
    int64_t collision_mask;  ///< Bitmask: which layers this body can collide with.
    double radius;           ///< Circle radius (0 for AABB bodies).
    int8_t is_circle;        ///< 1 = circle shape, 0 = AABB shape.
} rt_body_impl;

/// Forward declaration for joint
typedef struct ph_joint ph_joint;

/// @brief Internal representation of a physics world.
typedef struct {
    void *vptr;
    double gravity_x;
    double gravity_y;
    rt_body_impl *bodies[PH_MAX_BODIES];
    int64_t body_count;
    ph_joint *joints[PH_MAX_JOINTS];
    int32_t joint_count;
} rt_world_impl;

/// @brief Joint internal representation.
struct ph_joint {
    void *vptr;   ///< Zia virtual-dispatch pointer (must be first).
    int32_t type; ///< RT_JOINT_DISTANCE, etc.
    void *body_a;
    void *body_b;
    double anchor_x, anchor_y; ///< World-space anchor (hinge)
    double length;             ///< Target distance (distance/rope)
    double stiffness;          ///< Spring stiffness
    double damping;            ///< Spring damping
    int8_t active;
};

double rt_physics2d_body_prev_x(void *body);
double rt_physics2d_body_prev_y(void *body);
