//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_cloth3d.h
// Purpose: Cloth3D — from-scratch verlet cloth (chains for capes/hair tails,
//   patches for banners/flags) with sphere/capsule collision, pinning, wind,
//   fixed-substep determinism, and bone-chain or mesh output bindings.
// Key invariants:
//   - Simulation state is doubles-only with a fixed substep (default 1/120):
//     identical step sequences replay bit-identical on VM and native.
//   - Chains bound to a bone chain simulate in the skeleton's model space;
//     patches simulate in the bound mesh's local space.
// Ownership/Lifetime:
//   - GC-managed handle; retains its bound mesh/animator; finalizer frees
//     the point/constraint arrays and releases the bindings.
// Links: misc/plans/thirdpersonupgrade/27-cloth.md, ADR 0096,
//        src/runtime/graphics/3d/physics/rt_cloth3d.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a hanging chain: segments+1 points spanning total_length.
void *rt_cloth3d_new_chain(int64_t segments, double total_length);
/// @brief Create a rectangular patch grid of w x h points spanning width x height.
void *rt_cloth3d_new_patch(int64_t w, int64_t h, double width, double height);

/// @brief Get the velocity damping factor (0..1 per substep, default 0.02).
double rt_cloth3d_get_damping(void *cloth);
/// @brief Set the velocity damping factor (0..1).
void rt_cloth3d_set_damping(void *cloth, double damping);
/// @brief Get the constraint relaxation iterations per substep (default 4).
int64_t rt_cloth3d_get_iterations(void *cloth);
/// @brief Set the constraint relaxation iterations (1..32).
void rt_cloth3d_set_iterations(void *cloth, int64_t iterations);
/// @brief Get the gravity scale (default 1).
double rt_cloth3d_get_gravity_scale(void *cloth);
/// @brief Set the gravity scale (0 disables gravity).
void rt_cloth3d_set_gravity_scale(void *cloth, double scale);
/// @brief Get the wind response coefficient (default 1).
double rt_cloth3d_get_wind_response(void *cloth);
/// @brief Set the wind response coefficient (0 disables wind coupling).
void rt_cloth3d_set_wind_response(void *cloth, double response);
/// @brief Number of simulated points.
int64_t rt_cloth3d_get_point_count(void *cloth);

/// @brief Fluent: pin the point at @p index to its current position.
void *rt_cloth3d_pin(void *cloth, int64_t index);
/// @brief Fluent: add a static sphere collider (center Vec3, radius).
void *rt_cloth3d_add_sphere(void *cloth, void *center, double radius);
/// @brief Fluent: add a static capsule collider (segment a..b Vec3s, radius).
void *rt_cloth3d_add_capsule(void *cloth, void *a, void *b, double radius);
/// @brief Set the wind velocity: direction Vec3 scaled by @p strength.
void rt_cloth3d_set_wind(void *cloth, void *direction, double strength);
/// @brief Current position of point @p index as a Vec3.
void *rt_cloth3d_get_point(void *cloth, int64_t index);

/// @brief Fluent: bind a patch to a Mesh3D rewritten in place each step.
void *rt_cloth3d_bind_mesh(void *cloth, void *mesh);
/// @brief Fluent: bind a chain to an animator's linear bone chain from
///   @p root_bone; the root anchors the chain and simulated directions are
///   written back as aim rotations in the post-animation override slot.
void *rt_cloth3d_bind_bone_chain(void *cloth, void *animator, rt_string root_bone);

/// @brief Advance the simulation by @p dt seconds (fixed internal substeps),
///   including anchor sync and output bindings. World-registered cloths are
///   stepped automatically by World3D.StepSimulation.
void rt_cloth3d_step(void *cloth, double dt);

/// @brief Register a cloth to tick inside World3D.StepSimulation (retained).
void rt_game3d_world_add_cloth(void *world, void *cloth);
/// @brief Unregister a world-ticked cloth (released).
void rt_game3d_world_remove_cloth(void *world, void *cloth);

#ifdef __cplusplus
}
#endif
