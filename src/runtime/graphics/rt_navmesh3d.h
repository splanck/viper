//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_navmesh3d.h
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
extern "C"
{
#endif

    void *rt_navmesh3d_build(void *mesh, double agent_radius, double agent_height);
    void *rt_navmesh3d_find_path(void *navmesh, void *from, void *to);
    void *rt_navmesh3d_sample_position(void *navmesh, void *point);
    int8_t rt_navmesh3d_is_walkable(void *navmesh, void *point);
    int64_t rt_navmesh3d_get_triangle_count(void *navmesh);
    void rt_navmesh3d_set_max_slope(void *navmesh, double degrees);
    void rt_navmesh3d_debug_draw(void *navmesh, void *canvas);

#ifdef __cplusplus
}
#endif
