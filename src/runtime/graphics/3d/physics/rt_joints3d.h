//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_joints3d.h
// Purpose: 3D physics joint constraints.
//
// Key invariants:
//   - Joints reference two Body3D objects; if either is freed, joint is inert.
//   - Distance joints maintain a fixed separation between centers.
//   - Spring joints apply Hooke's law force toward rest length.
//   - Hinge/SixDof joints keep their authored frame anchors coincident.
//   - Rope joints only constrain bodies when separation exceeds MaxLength.
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

/* Hinge joint — keeps two local anchors together while allowing twist around an axis */
/// @brief Create a hinge-like anchor constraint around a normalized world axis.
void *rt_hinge_joint3d_new(void *body_a, void *body_b, void *anchor, void *axis);
/// @brief Configure a hinge motor driving rotation about the axis toward a target
///   angular velocity (rad/s), bounded by a max-impulse strength.
void rt_hinge_joint3d_set_motor(void *joint, int8_t enabled, double target_velocity, double max_impulse);
/// @brief Current signed hinge angle (radians) about the axis; 0 at creation.
double rt_hinge_joint3d_get_angle(void *joint);
/// @brief Constrain the hinge to [min, max] radians; non-finite values disable the limit.
void rt_hinge_joint3d_set_limits(void *joint, double min_angle, double max_angle);

/* Rope joint — enforces only a maximum center-to-center distance */
/// @brief Create a maximum-distance rope constraint between two 3D bodies.
void *rt_rope_joint3d_new(void *body_a, void *body_b, double max_length);
/// @brief Read the maximum rope length.
double rt_rope_joint3d_get_max_length(void *joint);
/// @brief Change the maximum rope length at runtime.
void rt_rope_joint3d_set_max_length(void *joint, double max_length);

/* SixDof joint — configurable frame-anchor constraint */
/// @brief Create a configurable joint that locks the two frame anchors together by default.
void *rt_sixdof_joint3d_new(void *body_a, void *body_b, void *frame_a, void *frame_b);
/// @brief Set allowed frame-anchor separation along each local/world axis.
void rt_sixdof_joint3d_set_linear_limits(void *joint, void *min, void *max);
/// @brief Set allowed relative pose angle along each SixDof joint-frame axis.
void rt_sixdof_joint3d_set_angular_limits(void *joint, void *min, void *max);
/// @brief Configure a linear motor driving relative velocity (Vec3 per world axis)
///   along unlocked axes, bounded by a max-impulse strength.
void rt_sixdof_joint3d_set_linear_motor(void *joint, int8_t enabled, void *velocity, double max_impulse);

/* Joint solving (called from physics world step) */
/// @brief Solve one substep of a joint constraint (called internally from `world_step`).
void rt_joint3d_solve(void *joint, int32_t joint_type, double dt);

/* Joint types */
#define RT_JOINT_DISTANCE 0
#define RT_JOINT_SPRING 1
#define RT_JOINT_HINGE 2
#define RT_JOINT_ROPE 3
#define RT_JOINT_SIXDOF 4

#ifdef __cplusplus
}
#endif
