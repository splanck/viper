//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_joints3d.h
// Purpose: 3D physics joint constraints — distance and spring joints.
//
// Key invariants:
//   - Joints reference two Body3D objects; if either is freed, joint is inert.
//   - Distance joints maintain a fixed separation between anchor points.
//   - Spring joints apply Hooke's law force toward rest length.
//   - Joints are solved iteratively after collision resolution in step().
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64.
//
// Links: rt_joints3d.c, rt_physics3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Distance joint — maintains fixed distance between two body centers */
/// @brief Create a fixed-length distance constraint between two 3D bodies.
void *rt_distance_joint3d_new(void *body_a, void *body_b, double distance);
/// @brief Read the target distance.
double rt_distance_joint3d_get_distance(void *joint);
/// @brief Change the target distance at runtime.
void rt_distance_joint3d_set_distance(void *joint, double distance);

/* Spring joint — Hooke's law force toward rest length */
/// @brief Create a 3D spring constraint with rest length, stiffness, and damping coefficients.
void *rt_spring_joint3d_new(
    void *body_a, void *body_b, double rest_length, double stiffness, double damping);
/// @brief Read the spring stiffness.
double rt_spring_joint3d_get_stiffness(void *joint);
/// @brief Set the spring stiffness.
void rt_spring_joint3d_set_stiffness(void *joint, double stiffness);
/// @brief Read the spring damping coefficient.
double rt_spring_joint3d_get_damping(void *joint);
/// @brief Set the spring damping coefficient.
void rt_spring_joint3d_set_damping(void *joint, double damping);
/// @brief Read the spring rest length.
double rt_spring_joint3d_get_rest_length(void *joint);

/* Joint solving (called from physics world step) */
/// @brief Solve one substep of a joint constraint (called internally from `world_step`).
void rt_joint3d_solve(void *joint, int32_t joint_type, double dt);

/* Joint types */
#define RT_JOINT_DISTANCE 0
#define RT_JOINT_SPRING 1

#ifdef __cplusplus
}
#endif
