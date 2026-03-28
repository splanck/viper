//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_path3d.h
// Purpose: 3D Catmull-Rom spline path for camera dollies, patrol routes,
//   missile trajectories. Evaluate position/direction at t in [0,1].
//
// Key invariants:
//   - Minimum 2 control points for evaluation.
//   - Catmull-Rom: curve passes through all control points.
//   - Arc length computed numerically (cached, dirty flag).
//
// Links: rt_spline.c (2D version), rt_transform3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_path3d_new(void);
void rt_path3d_add_point(void *path, void *pos);
void *rt_path3d_get_position_at(void *path, double t);
void *rt_path3d_get_direction_at(void *path, double t);
double rt_path3d_get_length(void *path);
int64_t rt_path3d_get_point_count(void *path);
void rt_path3d_set_looping(void *path, int8_t loop);
void rt_path3d_clear(void *path);

#ifdef __cplusplus
}
#endif
