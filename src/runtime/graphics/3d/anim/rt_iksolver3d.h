//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_iksolver3d.h
// Purpose: IKSolver3D runtime surface and controller-integration helpers.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a two-bone IK solver for a parent -> mid -> end chain.
void *rt_ik_solver3d_two_bone(void *skeleton, int64_t root, int64_t mid, int64_t end);
/// @brief Create a one-bone look-at/aim solver.
void *rt_ik_solver3d_look_at(void *skeleton, int64_t bone);
/// @brief Create a FABRIK solver from a Seq[Integer] bone chain.
void *rt_ik_solver3d_fabrik(void *skeleton, void *chain);
/// @brief Set the world-space target for the solver. Non-Vec3 targets are ignored.
void rt_ik_solver3d_set_target(void *solver, void *target);
/// @brief Set solver weight, clamped to 0..1.
void rt_ik_solver3d_set_weight(void *solver, double weight);
/// @brief Set a world-space pole target orienting a two-bone chain's mid joint.
void rt_ik_solver3d_set_pole(void *solver, void *pole);
/// @brief Set a ground normal; the chain's end (foot) bone is oriented so its sole-up axis aligns
///        with it after the position solve. Non-Vec3 normals are ignored.
void rt_ik_solver3d_set_ground_normal(void *solver, void *normal);
/// @brief Solve the IK constraint against the skeleton bind pose.
void rt_ik_solver3d_solve(void *solver);

/* Runtime integration helpers. */
/// @brief Return the Skeleton3D handle retained by this solver.
void *rt_ik_solver3d_get_skeleton(void *solver);
/// @brief Apply the solver in place to a controller local-pose buffer and refresh globals.
int8_t rt_ik_solver3d_apply_to_pose(void *solver, float *locals, float *globals, int32_t bone_count);

#ifdef __cplusplus
}
#endif
