//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_reflectionprobe3d.h
// Purpose: Public C ABI for local reflection probes — 6-face scene capture into
//   a CubeMap3D with a box influence volume and scripted re-capture flagging.
// Key invariants:
//   - Capture is explicit/scripted; never runs per frame.
// Ownership/Lifetime:
//   - GC-managed; getters returning objects retain for the caller.
// Links: rt_reflectionprobe3d.c, misc/plans/thirdpersonupgrade/15-reflection-probes.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a probe at @p position with a parallax/influence box.
void *rt_reflectionprobe3d_new(void *position, void *box_min, void *box_max);
/// @brief Probe capture position as a fresh Vec3.
void *rt_reflectionprobe3d_get_position(void *probe);
/// @brief Influence multiplier over the proxy box (>= 1, default 1).
void rt_reflectionprobe3d_set_influence_scale(void *probe, double scale);
double rt_reflectionprobe3d_get_influence_scale(void *probe);
/// @brief Capture face resolution (16..512, default 64).
void rt_reflectionprobe3d_set_resolution(void *probe, int64_t resolution);
int64_t rt_reflectionprobe3d_get_resolution(void *probe);
/// @brief Scripted re-capture request flag (time-of-day hooks set it).
void rt_reflectionprobe3d_set_capture_dirty(void *probe, int8_t dirty);
int8_t rt_reflectionprobe3d_get_capture_dirty(void *probe);
/// @brief True when @p position lies inside the influence-scaled box.
int8_t rt_reflectionprobe3d_contains(void *probe, void *position);
/// @brief Retained captured CubeMap3D (NULL before the first capture).
void *rt_reflectionprobe3d_get_cubemap(void *probe);
/// @brief Render 6 faces of @p scene from the probe through @p canvas; 1 on success.
int8_t rt_reflectionprobe3d_capture(void *probe, void *canvas, void *scene);

#ifdef __cplusplus
}
#endif
