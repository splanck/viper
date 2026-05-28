//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/nav/rt_navmesh3d.h
// Purpose: 3D navigation mesh — slope-filtered walkable triangles from level
//   geometry, A* pathfinding on triangle adjacency graph, position snapping.
//
// Key invariants:
//   - Built from Mesh3D by filtering triangles whose normal.y > cos(max_slope).
//   - Adjacency: triangles sharing 2 vertices are neighbors.
//   - A* uses centroid-to-centroid distance as edge cost, Euclidean heuristic.
//   - FindPath returns a Path3D with waypoints through triangle centroids.
//
// Links: rt_path3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Bake a navigation mesh from a source @p mesh for an agent of the
///        given radius/height. @return a navmesh handle, or NULL.
void *rt_navmesh3d_build(void *mesh, double agent_radius, double agent_height);
/// @brief Find a path between Vec3 @p from and @p to. @return a path handle
///        (point list), or NULL if unreachable.
void *rt_navmesh3d_find_path(void *navmesh, void *from, void *to);
/// @brief Snap a Vec3 @p point onto the nearest walkable navmesh location.
void *rt_navmesh3d_sample_position(void *navmesh, void *point);
/// @brief Whether a Vec3 @p point lies on a walkable navmesh polygon.
int8_t rt_navmesh3d_is_walkable(void *navmesh, void *point);
/// @brief Number of triangles in the baked navmesh.
int64_t rt_navmesh3d_get_triangle_count(void *navmesh);
/// @brief Set the maximum walkable slope (degrees) used when baking.
void rt_navmesh3d_set_max_slope(void *navmesh, double degrees);
/// @brief Draw the navmesh polygons as a debug overlay onto @p canvas.
void rt_navmesh3d_debug_draw(void *navmesh, void *canvas);

/* Runtime integration helper used by NavAgent3D. Returns malloc'd xyz triples. */
/// @brief Compute a path from @p from to @p to and copy it into a freshly
///        malloc'd flat xyz array. @return point count (caller frees the array).
int64_t rt_navmesh3d_copy_path_points(void *navmesh, void *from, void *to, double **out_points_xyz);

#ifdef __cplusplus
}
#endif
