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

/// @brief Create a box collider with half-extents (hx, hy, hz) along each axis.
void *rt_collider3d_new_box(double hx, double hy, double hz);
/// @brief Create a sphere collider with the given radius.
void *rt_collider3d_new_sphere(double radius);
/// @brief Create a capsule collider (radius + total height including caps).
void *rt_collider3d_new_capsule(double radius, double height);
/// @brief Create a convex-hull collider from a mesh's vertex cloud (computes the hull internally).
void *rt_collider3d_new_convex_hull(void *mesh);
/// @brief Create a triangle-mesh collider (static only — cannot rotate dynamically).
void *rt_collider3d_new_mesh(void *mesh);
/// @brief Create a heightfield collider from a Pixels heightmap with per-axis world-scale.
void *rt_collider3d_new_heightfield(void *heightmap, double scale_x, double scale_y, double scale_z);
/// @brief Create an empty compound collider — combine multiple shapes via `_add_child`.
void *rt_collider3d_new_compound(void);

/// @brief Add a child collider to a compound at @p local_transform (Mat4 in compound's local space).
void rt_collider3d_add_child(void *compound, void *child, void *local_transform);
/// @brief Get the collider's type tag (one of RT_COLLIDER3D_TYPE_*).
int64_t rt_collider3d_get_type(void *collider);
/// @brief Get the local-space AABB minimum corner as a Vec3.
void *rt_collider3d_get_local_bounds_min(void *collider);
/// @brief Get the local-space AABB maximum corner as a Vec3.
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
