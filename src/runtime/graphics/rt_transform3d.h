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
extern "C"
{
#endif

    void *rt_transform3d_new(void);
    void rt_transform3d_set_position(void *xf, double x, double y, double z);
    void *rt_transform3d_get_position(void *xf);
    void rt_transform3d_set_rotation(void *xf, void *quat);
    void *rt_transform3d_get_rotation(void *xf);
    void rt_transform3d_set_euler(void *xf, double pitch, double yaw, double roll);
    void rt_transform3d_set_scale(void *xf, double x, double y, double z);
    void *rt_transform3d_get_scale(void *xf);
    void *rt_transform3d_get_matrix(void *xf);
    void rt_transform3d_translate(void *xf, void *delta);
    void rt_transform3d_rotate(void *xf, void *axis, double angle);
    void rt_transform3d_look_at(void *xf, void *target, void *up);

#ifdef __cplusplus
}
#endif
