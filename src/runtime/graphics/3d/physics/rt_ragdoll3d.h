//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_ragdoll3d.h
// Purpose: Viper.Graphics3D.Ragdoll3D — auto-built capsule-body + 6-DoF-joint
//   ragdoll rigs from a Skeleton3D, with animation handoff (velocity seeding),
//   per-step palette write-back, blend-out to animation, and powered PD drive
//   toward the animated pose.
// Key invariants:
//   - Rig bodies/joints only exist in a physics world between Activate and
//     Deactivate; the builder itself registers nothing.
//   - Palette write-back runs between animation update and scene sync (the
//     Game3D step calls rt_ragdoll3d_step there); raw users call it manually.
// Ownership/Lifetime:
//   - GC-managed handle retaining its skeleton, bodies, joints, and (while
//     active or blending) the world/controller/node references.
// Links: misc/plans/thirdpersonupgrade/07-ragdoll.md, rt_physics3d.h,
//   rt_joints3d.h, rt_animcontroller3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Build a ragdoll rig description from a skeleton (bodies build lazily).
void *rt_ragdoll3d_from_skeleton(void *skeleton);
/// @brief Total mass distributed across rig bodies (default 70).
double rt_ragdoll3d_get_total_mass(void *ragdoll);
/// @brief Set the distributed total mass (rebuilds an inactive rig).
void rt_ragdoll3d_set_total_mass(void *ragdoll, double mass);
/// @brief Capsule radius as a fraction of bone length (default 0.22).
double rt_ragdoll3d_get_radius_scale(void *ragdoll);
/// @brief Set the capsule radius scale (rebuilds an inactive rig).
void rt_ragdoll3d_set_radius_scale(void *ragdoll, double scale);
/// @brief Minimum bone length that receives its own body (default 0.12).
double rt_ragdoll3d_get_min_bone_length(void *ragdoll);
/// @brief Set the minimum bodied bone length (rebuilds an inactive rig).
void rt_ragdoll3d_set_min_bone_length(void *ragdoll, double length);
/// @brief Number of rig bodies (builds the rig on first query).
int64_t rt_ragdoll3d_get_body_count(void *ragdoll);
/// @brief True between Activate and Deactivate.
int8_t rt_ragdoll3d_get_active(void *ragdoll);
/// @brief Override a bone's joint limits (swing/twist degrees) before Activate.
void rt_ragdoll3d_set_joint_limits(void *ragdoll,
                                   rt_string bone_name,
                                   double swing_deg,
                                   double twist_deg);
/// @brief Hand off from animation: pose bodies from the controller's current
///   pose (velocities from the previous palette), add bodies + joints to
///   @p world, and start palette write-back. Node supplies the world transform.
void rt_ragdoll3d_activate(void *ragdoll, void *world, void *controller, void *node);
/// @brief Remove the rig from the world and blend the palette back to live
///   animation over @p blend_seconds.
void rt_ragdoll3d_deactivate(void *ragdoll, double blend_seconds);
/// @brief Drive masked joints toward the animated pose (PD); mask is a bit per
///   rig body slot (-1 = all, 0 = off). Stiffness scales the drive strength.
void rt_ragdoll3d_set_powered(void *ragdoll, int64_t bone_mask, double stiffness);
/// @brief Per-step sync: powered drive + palette write-back + node root-follow
///   (active), or blend-out progression (deactivating). Game3D calls this
///   between the physics step and scene sync; raw users call it manually.
void rt_ragdoll3d_step(void *ragdoll, double dt);
/// @brief Borrowed rig body for a bone name (NULL when unmapped).
void *rt_ragdoll3d_get_body(void *ragdoll, rt_string bone_name);

#ifdef __cplusplus
}
#endif
