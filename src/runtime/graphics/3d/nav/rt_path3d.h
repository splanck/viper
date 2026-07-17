//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/nav/rt_path3d.h
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

/// @brief Create an empty 3D spline path.
void *rt_path3d_new(void);
/// @brief Append a Vec3 control point @p pos to the path.
void rt_path3d_add_point(void *path, void *pos);
/// @brief Sample the interpolated position at normalized parameter @p t
///        ([0,1] over the whole path). @return a Vec3.
void *rt_path3d_get_position_at(void *path, double t);
/// @brief Sample the (normalized) tangent/direction at parameter @p t.
void *rt_path3d_get_direction_at(void *path, double t);
/// @brief Total arc length of the path in world units.
double rt_path3d_get_length(void *path);
/// @brief Number of control points.
int64_t rt_path3d_get_point_count(void *path);
/// @brief Set whether the path wraps from the last point back to the first.
void rt_path3d_set_looping(void *path, int8_t loop);
/// @brief Remove all control points.
void rt_path3d_clear(void *path);
/// @brief Internal: arclength-normalized spline evaluation (constant-speed t in
///        [0,1] along the same Catmull-Rom curve GetPositionAt samples). Writes
///        the position and, when non-NULL, the unit tangent.
void rt_path3d_eval_spline_raw(void *path, double t, double *pos_out, double *tan_out);

#ifdef __cplusplus
}
#endif
