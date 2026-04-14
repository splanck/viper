//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_transform3d.h
// Purpose: Transform3D — position + quaternion rotation + scale with lazy
//   TRS matrix recomputation. Eliminates manual Mat4 composition chains.
//
// Key invariants:
//   - Dirty flag tracks when matrix needs recompute.
//   - Quaternion stored as (x,y,z,w) matching Quat convention.
//   - Matrix is row-major double[16], matching Mat4 layout.
//
// Links: rt_canvas3d_internal.h, rt_scene3d.c (build_trs_matrix)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a Transform3D at the origin with identity rotation and unit scale.
void *rt_transform3d_new(void);
/// @brief Set the translation component (marks the matrix dirty).
void rt_transform3d_set_position(void *xf, double x, double y, double z);
/// @brief Get the translation as a Vec3.
void *rt_transform3d_get_position(void *xf);
/// @brief Set the rotation component from a Quaternion.
void rt_transform3d_set_rotation(void *xf, void *quat);
/// @brief Get the rotation as a Quaternion.
void *rt_transform3d_get_rotation(void *xf);
/// @brief Set rotation from Euler angles in radians (pitch=X, yaw=Y, roll=Z, intrinsic order).
void rt_transform3d_set_euler(void *xf, double pitch, double yaw, double roll);
/// @brief Set the per-axis scale.
void rt_transform3d_set_scale(void *xf, double x, double y, double z);
/// @brief Get the scale as a Vec3.
void *rt_transform3d_get_scale(void *xf);
/// @brief Get the composed TRS matrix as a Mat4 (lazily recomputed when dirty).
void *rt_transform3d_get_matrix(void *xf);
/// @brief Add @p delta (Vec3) to the current position.
void rt_transform3d_translate(void *xf, void *delta);
/// @brief Multiply the current rotation by an axis-angle rotation (axis = Vec3, angle in radians).
void rt_transform3d_rotate(void *xf, void *axis, double angle);
/// @brief Aim the transform's -Z forward at @p target with up vector @p up (Vec3 handles).
void rt_transform3d_look_at(void *xf, void *target, void *up);

#ifdef __cplusplus
}
#endif
