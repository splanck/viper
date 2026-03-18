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
extern "C"
{
#endif

    /// Joint type constants
    #define RT_JOINT_DISTANCE 0
    #define RT_JOINT_SPRING   1
    #define RT_JOINT_HINGE    2
    #define RT_JOINT_ROPE     3

    //=========================================================================
    // Distance Joint
    //=========================================================================

    void   *rt_physics2d_distance_joint_new(void *body_a, void *body_b, double length);
    double  rt_physics2d_distance_joint_get_length(void *joint);
    void    rt_physics2d_distance_joint_set_length(void *joint, double length);

    //=========================================================================
    // Spring Joint
    //=========================================================================

    void   *rt_physics2d_spring_joint_new(void *body_a, void *body_b,
                                          double rest_length, double stiffness,
                                          double damping);
    double  rt_physics2d_spring_joint_get_stiffness(void *joint);
    void    rt_physics2d_spring_joint_set_stiffness(void *joint, double stiffness);
    double  rt_physics2d_spring_joint_get_damping(void *joint);
    void    rt_physics2d_spring_joint_set_damping(void *joint, double damping);

    //=========================================================================
    // Hinge Joint
    //=========================================================================

    void   *rt_physics2d_hinge_joint_new(void *body_a, void *body_b,
                                         double anchor_x, double anchor_y);
    double  rt_physics2d_hinge_joint_get_angle(void *joint);

    //=========================================================================
    // Rope Joint
    //=========================================================================

    void   *rt_physics2d_rope_joint_new(void *body_a, void *body_b, double max_length);
    double  rt_physics2d_rope_joint_get_max_length(void *joint);
    void    rt_physics2d_rope_joint_set_max_length(void *joint, double max_length);

    //=========================================================================
    // Joint Common
    //=========================================================================

    void   *rt_physics2d_joint_get_body_a(void *joint);
    void   *rt_physics2d_joint_get_body_b(void *joint);
    int64_t rt_physics2d_joint_get_type(void *joint);
    int8_t  rt_physics2d_joint_is_active(void *joint);

    //=========================================================================
    // World Joint Management
    //=========================================================================

    void    rt_physics2d_world_add_joint(void *world, void *joint);
    void    rt_physics2d_world_remove_joint(void *world, void *joint);
    int64_t rt_physics2d_world_joint_count(void *world);

    /// Called from world_step to solve all joint constraints
    void    rt_physics2d_solve_joints(void *world, double dt);

    //=========================================================================
    // Circle Bodies
    //=========================================================================

    void   *rt_physics2d_circle_body_new(double cx, double cy, double radius, double mass);
    double  rt_physics2d_body_radius(void *body);
    int8_t  rt_physics2d_body_is_circle(void *body);

#ifdef __cplusplus
}
#endif
