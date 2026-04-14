//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_raycast3d.h
// Purpose: 3D raycasting and AABB collision — ray-triangle (Möller–Trumbore),
//   ray-mesh (with AABB early-out), ray-AABB (slab method), ray-sphere,
//   and AABB-AABB overlap/penetration tests.
//
// Key invariants:
//   - All ray functions take Vec3 origin + direction; direction must be normalized.
//   - Distance return: >= 0 = hit distance, -1 = no hit.
//   - IntersectMesh returns a RayHit3D object (or NULL) with hit point, normal, triangle index.
//   - AABB functions take Vec3 min/max corners.
//
// Links: plans/fps-support.md, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ray intersection */
/// @brief Möller-Trumbore ray-triangle test. Returns hit distance, -1 on miss.
double rt_ray3d_intersect_triangle(void *origin, void *dir, void *v0, void *v1, void *v2);
/// @brief Test ray against every triangle in a transformed mesh; returns RayHit3D or NULL.
void *rt_ray3d_intersect_mesh(void *origin, void *dir, void *mesh, void *transform);
/// @brief Slab-method ray-AABB test. Returns hit distance, -1 on miss.
double rt_ray3d_intersect_aabb(void *origin, void *dir, void *aabb_min, void *aabb_max);
/// @brief Analytic ray-sphere test. Returns hit distance, -1 on miss.
double rt_ray3d_intersect_sphere(void *origin, void *dir, void *center, double radius);

/* AABB collision */
/// @brief True if two AABBs overlap.
int8_t rt_aabb3d_overlaps(void *min_a, void *max_a, void *min_b, void *max_b);
/// @brief Get the per-axis penetration depth Vec3 between two overlapping AABBs (zero if disjoint).
void *rt_aabb3d_penetration(void *min_a, void *max_a, void *min_b, void *max_b);

/* Shape-shape collision */
/// @brief True if two spheres overlap (distance between centers < sum of radii).
int8_t rt_sphere3d_overlaps(void *center_a, double radius_a, void *center_b, double radius_b);
/// @brief Get the penetration vector between two overlapping spheres (zero if disjoint).
void *rt_sphere3d_penetration(void *center_a, double radius_a, void *center_b, double radius_b);
/// @brief Find the closest point on an AABB to @p point.
void *rt_aabb3d_closest_point(void *aabb_min, void *aabb_max, void *point);
/// @brief True if a sphere overlaps an AABB.
int8_t rt_aabb3d_sphere_overlaps(void *aabb_min, void *aabb_max, void *center, double radius);
/// @brief Find the closest point on a line segment to @p point.
void *rt_segment3d_closest_point(void *seg_a, void *seg_b, void *point);
/// @brief True if a capsule overlaps a sphere.
int8_t rt_capsule3d_sphere_overlaps(
    void *cap_a, void *cap_b, double cap_radius, void *sphere_center, double sphere_radius);
/// @brief True if a capsule overlaps an AABB.
int8_t rt_capsule3d_aabb_overlaps(
    void *cap_a, void *cap_b, double radius, void *aabb_min, void *aabb_max);

/* RayHit3D accessors */
/// @brief Get the hit distance from the ray origin.
double rt_ray3d_hit_distance(void *hit);
/// @brief Get the world-space hit point as a Vec3.
void *rt_ray3d_hit_point(void *hit);
/// @brief Get the surface normal at the hit point as a Vec3.
void *rt_ray3d_hit_normal(void *hit);
/// @brief Get the triangle index that was hit (within the mesh).
int64_t rt_ray3d_hit_triangle(void *hit);

#ifdef __cplusplus
}
#endif
