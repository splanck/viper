//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_joints3d_internal.h
// Purpose: Shared vector/quaternion/anchor math primitives for the 3D joint
//          solvers. Split out of rt_joints3d.c so the math library and the
//          per-joint solvers (distance/spring/hinge/etc.) can live in separate
//          translation units.
//
// Key invariants:
//   - These helpers are engine-internal; the header must not be included
//     outside the physics/ directory.
//   - All clamp/sanitize helpers map non-finite input to safe defaults so the
//     solver never propagates NaN.
//   - Body access uses the private shared rt_body3d_kinematics prefix view.
//
// Ownership/Lifetime:
//   - Pure functions over caller-owned double arrays / body views; no
//     allocation or ownership transfer.
//
// Links: src/runtime/graphics/3d/physics/rt_joints3d.c (joint solvers + API),
//        src/runtime/graphics/3d/physics/rt_joints3d_math.c (definitions)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_body3d_kinematics_internal.h"

//=============================================================================
// Joint math limits
//=============================================================================

#define RT_JOINT3D_MAX_PARAM 1.0e9
#define RT_JOINT3D_MAX_FORCE 1.0e9
#define RT_JOINT3D_MAX_COORD 1.0e12
#define RT_JOINT3D_MAX_DT 1.0
#define RT_JOINT3D_TWO_PI 6.28318530717958647692

/* Body pose/velocity access uses a private prefix view whose layout is asserted to match the
 * private rt_body3d payload in rt_physics3d.c. */

typedef struct {
    double m[16];
} joint3d_mat4_view;

//=============================================================================
// Vector / quaternion / anchor math (defined in rt_joints3d_math.c)
//=============================================================================

double joint3d_sanitize_nonnegative(double value);
double joint3d_clamp_force(double value);
double joint3d_clamp_coord(double value);
double joint3d_sanitize_dt(double dt);
int joint3d_vec3_all_finite(const double *v);
void joint3d_vec3_sanitize(double *v);
void joint3d_vec3_set(double *dst, double x, double y, double z);
void joint3d_vec3_sub(const double *a, const double *b, double *out);
double joint3d_vec3_dot(const double *a, const double *b);
double joint3d_len3(double x, double y, double z);
double joint3d_vec3_len(const double *v);
int joint3d_vec3_normalize(double *v);
int joint3d_read_vec3(void *obj, double *out);
void joint3d_canonicalize_limits(double *min_v, double *max_v);
joint3d_mat4_view *joint3d_mat4_checked(void *obj);
int joint3d_read_mat4_translation(void *obj, double *out);
void joint3d_quat_mul(const double *a, const double *b, double *out);
void joint3d_quat_conjugate(const double *q, double *out);
void joint3d_quat_normalize(double *q);
void joint3d_quat_from_axis_angle(const double *axis, double angle, double *out);
void joint3d_quat_prepend_axis_angle(double *orientation, const double *axis, double angle);
void joint3d_quat_to_rotation_vector(const double *q, double *out);
void joint3d_quat_rotate_vec3(const double *q, const double *v, double *out);
void joint3d_world_anchor(const rt_body3d_kinematics *body,
                          const double *local_anchor,
                          double *out);
void joint3d_local_from_world(const rt_body3d_kinematics *body,
                              const double *world_point,
                              double *out);
void joint3d_world_inv_inertia_mul(const rt_body3d_kinematics *body,
                                   const double *v,
                                   double *out);
double joint3d_effective_inv_inertia_about_axis(const rt_body3d_kinematics *body,
                                                const double *axis);
void joint3d_world_axis_from_local(const rt_body3d_kinematics *body,
                                   const double *local_axis,
                                   double *out);
/* Sleep/wake + broadphase bridge (defined in rt_physics3d.c, where the full
 * rt_body3d layout is known). Joint solvers only see the kinematics prefix
 * view, so they cannot touch motion_mode/is_sleeping/broadphase directly. */

/// @brief Gate a joint solve on pair activity and wake the pair when it runs.
/// @details Returns 0 (skip solving) when neither endpoint can drive the
///   constraint: both dynamics asleep, static anchors, and motionless
///   kinematics don't need the joint enforced this substep. When any endpoint
///   is an active driver, both dynamic endpoints are woken (so a sleeping
///   partner picks impulses up again) and 1 is returned.
int joint3d_pair_begin_solve(rt_body3d_kinematics *body_a, rt_body3d_kinematics *body_b);

/// @brief Record that a joint position-correction moved @p body, so cached
///   query-broadphase entries revalidate against the new pose.
void joint3d_mark_body_moved(rt_body3d_kinematics *body);

void joint3d_correct_anchor_pair(rt_body3d_kinematics *body_a,
                                 rt_body3d_kinematics *body_b,
                                 const double *local_anchor_a,
                                 const double *local_anchor_b,
                                 double stiffness);
void joint3d_correct_anchor_pair_limited(rt_body3d_kinematics *body_a,
                                         rt_body3d_kinematics *body_b,
                                         const double *local_anchor_a,
                                         const double *local_anchor_b,
                                         const double *linear_min,
                                         const double *linear_max);
void joint3d_correct_anchor_pair_limited_frame(rt_body3d_kinematics *body_a,
                                               rt_body3d_kinematics *body_b,
                                               const double *local_anchor_a,
                                               const double *local_anchor_b,
                                               const double *linear_min,
                                               const double *linear_max,
                                               const double *frame_quat);
void joint3d_remove_relative_linear_velocity_locked_axes_frame(rt_body3d_kinematics *body_a,
                                                               rt_body3d_kinematics *body_b,
                                                               const double *linear_min,
                                                               const double *linear_max,
                                                               const double *frame_quat);
void joint3d_remove_relative_linear_velocity(rt_body3d_kinematics *body_a,
                                             rt_body3d_kinematics *body_b,
                                             double amount);
void joint3d_remove_relative_linear_velocity_locked_axes(rt_body3d_kinematics *body_a,
                                                         rt_body3d_kinematics *body_b,
                                                         const double *linear_min,
                                                         const double *linear_max);
void joint3d_remove_relative_angular_velocity(rt_body3d_kinematics *body_a,
                                              rt_body3d_kinematics *body_b,
                                              const double *allowed_axis);

/// @brief True if a body view is solvable (finite state, non-negative inv mass).
/// Defined in rt_joints3d.c.
int joint3d_body_is_finite(const rt_body3d_kinematics *body);
