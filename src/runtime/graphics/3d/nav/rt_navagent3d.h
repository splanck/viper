//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_navagent3d.h
// Purpose: Gameplay-facing single-agent navigation layer on top of NavMesh3D.
//
// Key invariants:
//   - NavAgent3D owns a goal, sampled path corners, and simple steering state.
//   - Update() can drive a bound Character3D or directly reposition a SceneNode3D.
//   - Auto-repath is intentionally simple in v1: periodic rebuilds while a goal
//     remains active and the agent is not within the stopping distance.
//
// Links: rt_navmesh3d.h, rt_physics3d.h, rt_scene3d.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

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

/// @brief Bind a Character3D — Update will call its Move() with desired velocity.
void rt_navagent3d_bind_character(void *agent, void *controller);
/// @brief Bind a SceneNode3D — Update will write the agent's position into the node's transform.
void rt_navagent3d_bind_node(void *agent, void *node);

#ifdef __cplusplus
}
#endif
