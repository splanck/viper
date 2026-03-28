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
extern "C"
{
#endif

    /* Distance joint — maintains fixed distance between two body centers */
    void *rt_distance_joint3d_new(void *body_a, void *body_b, double distance);
    double rt_distance_joint3d_get_distance(void *joint);
    void rt_distance_joint3d_set_distance(void *joint, double distance);

    /* Spring joint — Hooke's law force toward rest length */
    void *rt_spring_joint3d_new(void *body_a, void *body_b, double rest_length,
                                double stiffness, double damping);
    double rt_spring_joint3d_get_stiffness(void *joint);
    void rt_spring_joint3d_set_stiffness(void *joint, double stiffness);
    double rt_spring_joint3d_get_damping(void *joint);
    void rt_spring_joint3d_set_damping(void *joint, double damping);
    double rt_spring_joint3d_get_rest_length(void *joint);

    /* Joint solving (called from physics world step) */
    void rt_joint3d_solve(void *joint, int32_t joint_type, double dt);

    /* Joint types */
    #define RT_JOINT_DISTANCE 0
    #define RT_JOINT_SPRING   1

#ifdef __cplusplus
}
#endif
