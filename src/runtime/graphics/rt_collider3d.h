//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_collider3d.h
// Purpose: Collider3D runtime surface for reusable 3D collision shapes.
//
// Key invariants:
//   - Collider3D is a reusable shape object; Physics3DBody owns one active
//     collider at a time.
//   - Triangle-mesh and heightfield colliders are static-only in v1.
//   - Compound colliders own child references plus copied local transforms.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_COLLIDER3D_TYPE_BOX 0
#define RT_COLLIDER3D_TYPE_SPHERE 1
#define RT_COLLIDER3D_TYPE_CAPSULE 2
#define RT_COLLIDER3D_TYPE_CONVEX_HULL 3
#define RT_COLLIDER3D_TYPE_MESH 4
#define RT_COLLIDER3D_TYPE_COMPOUND 5
#define RT_COLLIDER3D_TYPE_HEIGHTFIELD 6

void *rt_collider3d_new_box(double hx, double hy, double hz);
void *rt_collider3d_new_sphere(double radius);
void *rt_collider3d_new_capsule(double radius, double height);
void *rt_collider3d_new_convex_hull(void *mesh);
void *rt_collider3d_new_mesh(void *mesh);
void *rt_collider3d_new_heightfield(void *heightmap, double scale_x, double scale_y, double scale_z);
void *rt_collider3d_new_compound(void);

void rt_collider3d_add_child(void *compound, void *child, void *local_transform);
int64_t rt_collider3d_get_type(void *collider);
void *rt_collider3d_get_local_bounds_min(void *collider);
void *rt_collider3d_get_local_bounds_max(void *collider);

/* Internal physics/runtime helpers. */
void rt_collider3d_get_local_bounds_raw(void *collider, double *min_out, double *max_out);
void rt_collider3d_compute_world_aabb_raw(void *collider,
                                          const double *position,
                                          const double *rotation,
                                          const double *scale,
                                          double *min_out,
                                          double *max_out);
int8_t rt_collider3d_is_static_only_raw(void *collider);
void rt_collider3d_get_box_half_extents_raw(void *collider, double *half_extents_out);
double rt_collider3d_get_radius_raw(void *collider);
double rt_collider3d_get_height_raw(void *collider);
void *rt_collider3d_get_mesh_raw(void *collider);
int64_t rt_collider3d_get_child_count_raw(void *collider);
void *rt_collider3d_get_child_raw(void *collider, int64_t index);
void rt_collider3d_get_child_transform_raw(void *compound,
                                           int64_t index,
                                           double *position_out,
                                           double *rotation_out,
                                           double *scale_out);
int8_t rt_collider3d_sample_heightfield_raw(void *collider,
                                            double local_x,
                                            double local_z,
                                            double *height_out,
                                            double *normal_out);

#ifdef __cplusplus
}
#endif
