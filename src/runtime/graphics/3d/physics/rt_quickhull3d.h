//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/physics/rt_quickhull3d.h
// Purpose: From-scratch 3D convex hull construction (quickhull) for physics
//          collider reduction. Given a point cloud, produces the exact hull
//          vertex set and triangle faces (outward CCW winding).
// Key invariants:
//   - Output vertices are a subset of the input points (no new positions).
//   - Faces wind counter-clockwise seen from outside; normals point outward.
//   - Degenerate inputs (fewer than 4 non-coplanar points) fail cleanly
//     (return 0) rather than emitting a broken hull.
// Ownership/Lifetime:
//   - Output arrays are malloc'd; the caller frees them with free().
// Links: rt_collider3d.c (NewConvexHullReduced), src/tests/unit/test_quickhull3d.cpp
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Compute the convex hull of a 3D point cloud (quickhull).
/// @param points       Interleaved xyz doubles (point_count * 3 entries).
/// @param point_count  Number of input points (>= 4 for a volumetric hull).
/// @param out_vertices Receives malloc'd interleaved xyz hull vertices.
/// @param out_vertex_count Receives the hull vertex count.
/// @param out_indices  Optional (may be NULL): receives malloc'd triangle
///                     indices into out_vertices (3 per face, CCW outward).
/// @param out_index_count Optional: receives the index count (3 * faces).
/// @return 1 on success; 0 on degenerate input (collinear/coplanar), invalid
///         arguments, or allocation failure (no outputs are leaked).
int rt_quickhull3d_build(const double *points,
                         int32_t point_count,
                         double **out_vertices,
                         int32_t *out_vertex_count,
                         int32_t **out_indices,
                         int32_t *out_index_count);

/// @brief Reduce a point set to at most @p max_points while preserving spread.
/// @details Greedy farthest-point (k-center) selection seeded with the
///          extreme points on each axis — the classic support-set reduction
///          for GJK colliders. Output points are a subset of the input.
/// @return Number of points written to out_points (interleaved xyz, caller
///         must provide room for max_points * 3), or 0 on invalid arguments.
int32_t rt_quickhull3d_reduce(const double *points,
                              int32_t point_count,
                              int32_t max_points,
                              double *out_points);

#ifdef __cplusplus
}
#endif
