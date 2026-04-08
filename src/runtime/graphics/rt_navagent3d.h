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

void *rt_navagent3d_new(void *navmesh, double radius, double height);

void rt_navagent3d_set_target(void *agent, void *position);
void rt_navagent3d_clear_target(void *agent);
void rt_navagent3d_update(void *agent, double dt);
void rt_navagent3d_warp(void *agent, void *position);

void *rt_navagent3d_get_position(void *agent);
void *rt_navagent3d_get_velocity(void *agent);
void *rt_navagent3d_get_desired_velocity(void *agent);
int8_t rt_navagent3d_get_has_path(void *agent);
double rt_navagent3d_get_remaining_distance(void *agent);
double rt_navagent3d_get_stopping_distance(void *agent);
void rt_navagent3d_set_stopping_distance(void *agent, double distance);
double rt_navagent3d_get_desired_speed(void *agent);
void rt_navagent3d_set_desired_speed(void *agent, double speed);
int8_t rt_navagent3d_get_auto_repath(void *agent);
void rt_navagent3d_set_auto_repath(void *agent, int8_t enabled);

void rt_navagent3d_bind_character(void *agent, void *controller);
void rt_navagent3d_bind_node(void *agent, void *node);

#ifdef __cplusplus
}
#endif
