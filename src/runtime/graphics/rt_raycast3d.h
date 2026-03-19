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
extern "C"
{
#endif

    /* Ray intersection */
    double  rt_ray3d_intersect_triangle(void *origin, void *dir,
                                         void *v0, void *v1, void *v2);
    void   *rt_ray3d_intersect_mesh(void *origin, void *dir,
                                     void *mesh, void *transform);
    double  rt_ray3d_intersect_aabb(void *origin, void *dir,
                                     void *aabb_min, void *aabb_max);
    double  rt_ray3d_intersect_sphere(void *origin, void *dir,
                                       void *center, double radius);

    /* AABB collision */
    int8_t  rt_aabb3d_overlaps(void *min_a, void *max_a, void *min_b, void *max_b);
    void   *rt_aabb3d_penetration(void *min_a, void *max_a, void *min_b, void *max_b);

    /* RayHit3D accessors */
    double  rt_ray3d_hit_distance(void *hit);
    void   *rt_ray3d_hit_point(void *hit);
    void   *rt_ray3d_hit_normal(void *hit);
    int64_t rt_ray3d_hit_triangle(void *hit);

#ifdef __cplusplus
}
#endif
