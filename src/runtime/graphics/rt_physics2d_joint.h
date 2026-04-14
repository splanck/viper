//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_physics2d_joint.h
// Purpose: Joint/constraint types for the 2D physics engine: DistanceJoint,
//   SpringJoint, HingeJoint, RopeJoint, and circle collision shapes.
//
// Key invariants:
//   - Joints connect two bodies and are solved iteratively per world step.
//   - A world holds at most PH_MAX_JOINTS (64) joints.
//   - Circle bodies use radius for collision (distance-based, no SAT).
//   - Joint type is immutable after creation.
//
// Ownership/Lifetime:
//   - Joint objects are GC-managed (rt_obj_new_i64).
//   - World retains joints; removing releases the reference.
//
// Links: src/runtime/graphics/rt_physics2d_joint.c,
//        src/runtime/graphics/rt_physics2d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Joint type constants
#define RT_JOINT_DISTANCE 0
#define RT_JOINT_SPRING 1
#define RT_JOINT_HINGE 2
#define RT_JOINT_ROPE 3

//=========================================================================
// Distance Joint
//=========================================================================

/// @brief Create a fixed-length distance constraint between two bodies (rigid rod-like behavior).
void *rt_physics2d_distance_joint_new(void *body_a, void *body_b, double length);
/// @brief Read the target distance.
double rt_physics2d_distance_joint_get_length(void *joint);
/// @brief Change the target distance at runtime.
void rt_physics2d_distance_joint_set_length(void *joint, double length);

//=========================================================================
// Spring Joint
//=========================================================================

/// @brief Create a Hooke's-law spring between two bodies (rest length + stiffness + damping).
void *rt_physics2d_spring_joint_new(
    void *body_a, void *body_b, double rest_length, double stiffness, double damping);
/// @brief Read the spring stiffness coefficient.
double rt_physics2d_spring_joint_get_stiffness(void *joint);
/// @brief Set the spring stiffness coefficient.
void rt_physics2d_spring_joint_set_stiffness(void *joint, double stiffness);
/// @brief Read the spring damping coefficient.
double rt_physics2d_spring_joint_get_damping(void *joint);
/// @brief Set the spring damping coefficient.
void rt_physics2d_spring_joint_set_damping(void *joint, double damping);

//=========================================================================
// Hinge Joint
//=========================================================================

/// @brief Create a hinge constraint pinning two bodies at a shared anchor point (free rotation).
void *rt_physics2d_hinge_joint_new(void *body_a, void *body_b, double anchor_x, double anchor_y);
/// @brief Read the current relative rotation angle between the two bodies in radians.
double rt_physics2d_hinge_joint_get_angle(void *joint);

//=========================================================================
// Rope Joint
//=========================================================================

/// @brief Create a rope constraint that allows free movement up to @p max_length apart.
/// Unlike DistanceJoint, bodies can be closer than max_length without being pulled together.
void *rt_physics2d_rope_joint_new(void *body_a, void *body_b, double max_length);
/// @brief Read the maximum allowed separation distance.
double rt_physics2d_rope_joint_get_max_length(void *joint);
/// @brief Set the maximum allowed separation distance.
void rt_physics2d_rope_joint_set_max_length(void *joint, double max_length);

//=========================================================================
// Joint Common
//=========================================================================

/// @brief Get the first body in the joint.
void *rt_physics2d_joint_get_body_a(void *joint);
/// @brief Get the second body in the joint.
void *rt_physics2d_joint_get_body_b(void *joint);
/// @brief Get the joint type (one of RT_JOINT_DISTANCE/SPRING/HINGE/ROPE).
int64_t rt_physics2d_joint_get_type(void *joint);
/// @brief True if the joint is currently active in its world (false after RemoveJoint).
int8_t rt_physics2d_joint_is_active(void *joint);

//=========================================================================
// World Joint Management
//=========================================================================

/// @brief Add a joint to the world's solver list (no-op if already present; max PH_MAX_JOINTS).
void rt_physics2d_world_add_joint(void *world, void *joint);
/// @brief Remove a joint from the world's solver list.
void rt_physics2d_world_remove_joint(void *world, void *joint);
/// @brief Number of joints currently registered in the world.
int64_t rt_physics2d_world_joint_count(void *world);

/// @brief Iterate the joint solver for one substep (called internally from `world_step`).
void rt_physics2d_solve_joints(void *world, double dt);

//=========================================================================
// Circle Bodies
//=========================================================================

/// @brief Create a circular dynamic body at (cx, cy) with the given radius and mass.
void *rt_physics2d_circle_body_new(double cx, double cy, double radius, double mass);
/// @brief Get the body's radius (0 for non-circle bodies).
double rt_physics2d_body_radius(void *body);
/// @brief True if the body was created via `_circle_body_new` (uses circle collision).
int8_t rt_physics2d_body_is_circle(void *body);

#ifdef __cplusplus
}
#endif
