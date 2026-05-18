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

#include "rt_object.h"

#include <stdint.h>

#define PH_MAX_BODIES 256
#define PH_MAX_JOINTS 64
#define PH_MAX_CONTACTS 256
#define PH_JOINT_ITERATIONS 4

#define RT_PH_MAX_BODIES_STR "256"
#define RT_PH_MAX_JOINTS_STR "64"
#define RT_PH_MAX_CONTACTS_STR "256"

#define RT_PHYSICS2D_WORLD_CLASS_ID INT64_C(-0x500201)
#define RT_PHYSICS2D_BODY_CLASS_ID INT64_C(-0x500202)
#define RT_PHYSICS2D_JOINT_CLASS_ID INT64_C(-0x500203)
#define RT_PHYSICS2D_PROJECTILE_CLASS_ID INT64_C(-0x500204)

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
    struct {
        rt_body_impl *body_a;
        rt_body_impl *body_b;
        double nx;
        double ny;
        double penetration;
    } contacts[PH_MAX_CONTACTS];
    int32_t contact_count;
} rt_world_impl;

/// @brief Joint internal representation.
struct ph_joint {
    void *vptr;   ///< Zia virtual-dispatch pointer (must be first).
    int32_t type; ///< RT_JOINT_DISTANCE, etc.
    void *body_a;
    void *body_b;
    double anchor_x, anchor_y; ///< World-space anchor (hinge)
    double anchor_ax, anchor_ay; ///< Body A local hinge anchor offset from center.
    double anchor_bx, anchor_by; ///< Body B local hinge anchor offset from center.
    double length;             ///< Target distance (distance/rope)
    double stiffness;          ///< Spring stiffness
    double damping;            ///< Spring damping
    int8_t active;
};

static inline int8_t rt_physics2d_is_world_handle(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_PHYSICS2D_WORLD_CLASS_ID;
}

static inline int8_t rt_physics2d_is_body_handle(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_PHYSICS2D_BODY_CLASS_ID;
}

static inline int8_t rt_physics2d_is_joint_handle(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_PHYSICS2D_JOINT_CLASS_ID;
}

/// @brief Velocity-solve all rigid (distance/revolute) joints in @p world for
///        this @p dt step. Defined in rt_physics2d_joint.c.
void rt_physics2d_solve_joints(void *world, double dt);
/// @brief Velocity-solve all spring joints in @p world for this @p dt step.
void rt_physics2d_solve_spring_joints(void *world, double dt);
/// @brief Positional (Baumgarte/NGS) correction pass for joints after the
///        velocity solve, to remove residual constraint drift.
void rt_physics2d_solve_position_joints(void *world, double dt);
