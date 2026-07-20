//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/nav/rt_navagent3d.h
// Purpose: Gameplay-facing single-agent navigation layer on top of NavMesh3D.
//
// Key invariants:
//   - NavAgent3D owns a goal, sampled path corners, and local avoidance steering state.
//   - Update() can drive a bound Character3D or directly reposition a SceneNode3D.
//   - UpdateBatch() reads one immutable start-of-tick peer snapshot and publishes
//     selected agents in stable creation order.
//   - Auto-repath is intentionally simple in v1: periodic rebuilds while a goal
//     remains active and the agent is not within the stopping distance.
//
// Ownership/Lifetime:
//   - NavAgent3D is GC-managed and retains its navmesh and optional bindings.
//   - Batch input arrays are borrowed only for the duration of UpdateBatch.
//
// Links: rt_navmesh3d.h, rt_physics3d.h, rt_scene3d.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a navigation agent on the given NavMesh3D with capsule dimensions.
void *rt_navagent3d_new(void *navmesh, double radius, double height);

/// @brief Set the world-space goal position; agent will compute a path on next Update.
void rt_navagent3d_set_target(void *agent, void *position);
/// @brief Clear the current goal (agent stops at the next Update).
void rt_navagent3d_clear_target(void *agent);
/// @brief Tick the agent: traverse path corners, drive bound character/node, optionally repath.
void rt_navagent3d_update(void *agent, double dt);
/// @brief Tick multiple agents through deterministic snapshot/solve/apply phases.
/// @details Valid unique handles are sorted by stable creation order. Every avoidance solve reads
///          the same immutable start-of-tick registry snapshot before any solved position is
///          published, so reversing @p agents cannot alter results. Invalid handles and duplicates
///          are ignored. This main-thread C API borrows the array for the duration of the call.
/// @param agents Array containing @p agent_count candidate NavAgent3D handles.
/// @param agent_count Number of entries in @p agents.
/// @param dt Tick duration in seconds, sanitized identically to individual Update.
/// @return Number of unique valid agents processed, or zero on invalid input/staging failure.
int64_t rt_navagent3d_update_batch(void *const *agents, int64_t agent_count, double dt);
/// @brief Teleport the agent to @p position (clears any active path).
void rt_navagent3d_warp(void *agent, void *position);

/// @brief Get the agent's current world-space position as a Vec3.
void *rt_navagent3d_get_position(void *agent);
/// @brief Get the agent's actual velocity from the previous Update as a Vec3.
void *rt_navagent3d_get_velocity(void *agent);
/// @brief Get the steering output (desired velocity for the bound character/node) as a Vec3.
void *rt_navagent3d_get_desired_velocity(void *agent);
/// @brief True if the agent currently has a valid path to its goal.
int8_t rt_navagent3d_get_has_path(void *agent);
/// @brief True while the current path segment traverses an off-mesh link.
int8_t rt_navagent3d_get_on_offmesh_link(void *agent);
/// @brief Authored kind of the link being traversed (empty when none).
rt_string rt_navagent3d_get_link_kind(void *agent);
/// @brief Distance in world units left along the path (0 when within stopping distance).
double rt_navagent3d_get_remaining_distance(void *agent);
/// @brief Get the radius around the goal at which the agent stops issuing motion.
double rt_navagent3d_get_stopping_distance(void *agent);
/// @brief Set the stopping radius.
void rt_navagent3d_set_stopping_distance(void *agent, double distance);
/// @brief Get the desired walking speed in m/s.
double rt_navagent3d_get_desired_speed(void *agent);
/// @brief Set the desired walking speed (clamped to >=0).
void rt_navagent3d_set_desired_speed(void *agent, double speed);
/// @brief True if the agent automatically rebuilds its path when blocked or the navmesh changes.
int8_t rt_navagent3d_get_auto_repath(void *agent);
/// @brief Toggle auto-repath behavior.
void rt_navagent3d_set_auto_repath(void *agent, int8_t enabled);
/// @brief True if same-NavMesh local separation steering is enabled.
int8_t rt_navagent3d_get_avoidance_enabled(void *agent);
/// @brief Toggle same-NavMesh local separation steering.
void rt_navagent3d_set_avoidance_enabled(void *agent, int8_t enabled);
/// @brief Get the radius used for local avoidance separation.
double rt_navagent3d_get_avoidance_radius(void *agent);
/// @brief Set the radius used for local avoidance separation (clamped to >=0).
void rt_navagent3d_set_avoidance_radius(void *agent, double radius);

/// @brief Bind a Character3D — Update will call its Move() with desired velocity.
void rt_navagent3d_bind_character(void *agent, void *controller);
/// @brief Bind a SceneNode3D — Update will write the agent's position into the node's transform.
void rt_navagent3d_bind_node(void *agent, void *node);

/// @brief Test-only: verify the spatial-grid avoidance query matches a full registry scan for every
///   registered agent. @return 1 if all agree (or none registered), 0 on any mismatch.
int8_t rt_navagent3d_check_avoidance_grid_parity(void);

#ifdef __cplusplus
}
#endif
