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
//   - A* uses centroid-to-centroid distance weighted by polygon traversal cost.
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
/// @brief Bake a navigation mesh from all Mesh3D nodes in a Scene3D.
void *rt_navmesh3d_bake(
    void *scene, double agent_radius, double agent_height, double max_slope, double cell_size);
/// @brief Bake a scene navmesh using the tiled API shape. Current baseline bakes the full scene.
void *rt_navmesh3d_bake_tiled(void *scene,
                              double tile_size,
                              double agent_radius,
                              double agent_height,
                              double max_slope,
                              double cell_size);
/// @brief Find a path between Vec3 @p from and @p to. @return a path handle
///        (point list), or NULL if unreachable.
void *rt_navmesh3d_find_path(void *navmesh, void *from, void *to);
/// @brief Find a path between Vec3 @p from and @p to as Some(Path3D), or None if unreachable.
void *rt_navmesh3d_find_path_option(void *navmesh, void *from, void *to);
/// @brief Serialize a baked navmesh to a "VNAVMSH2" binary file.
/// @details Records vertices, source/current triangles, traversal costs, blocked flags, area
///          labels, off-mesh links, obstacles, and agent params. Voxel heightfield source arrays
///          are runtime-derived and are not serialized.
int8_t rt_navmesh3d_export(void *navmesh, rt_string path);
/// @brief Reconstruct a path-queryable navmesh from "VNAVMSH2" or legacy "VNAVMSH1".
/// @return Imported navmesh, or NULL on a missing/corrupt/trailing-data file.
void *rt_navmesh3d_import(rt_string path);
/// @brief Snap a Vec3 @p point onto the nearest walkable navmesh location.
void *rt_navmesh3d_sample_position(void *navmesh, void *point);
/// @brief Whether a Vec3 @p point lies on a walkable navmesh polygon.
int8_t rt_navmesh3d_is_walkable(void *navmesh, void *point);
/// @brief Number of triangles in the baked navmesh.
int64_t rt_navmesh3d_get_triangle_count(void *navmesh);
/// @brief Cost of the most recent successful FindPath/CopyPathPoints query.
double rt_navmesh3d_get_last_path_cost(void *navmesh);
/// @brief Add an authored off-mesh traversal link between two walkable points.
int8_t rt_navmesh3d_add_offmesh_link(void *navmesh, void *from, void *to, int8_t bidirectional);
/// @brief Number of authored off-mesh traversal links.
int64_t rt_navmesh3d_get_offmesh_link_count(void *navmesh);
/// @brief Attach kind/cost/state metadata to an authored off-mesh link by index.
int8_t rt_navmesh3d_set_offmesh_link_metadata(
    void *navmesh, int64_t index, rt_string kind, double traversal_cost, int64_t state_flags);
/// @brief Read off-mesh link metadata by index.
rt_string rt_navmesh3d_get_offmesh_link_kind(void *navmesh, int64_t index);
/// @brief Read the traversal-cost metadata of an authored off-mesh link by index.
double rt_navmesh3d_get_offmesh_link_traversal_cost(void *navmesh, int64_t index);
/// @brief Read the state-flag metadata of an authored off-mesh link by index.
int64_t rt_navmesh3d_get_offmesh_link_state(void *navmesh, int64_t index);
/// @brief Add a coarse AABB obstacle that removes overlapping walkable triangles.
int8_t rt_navmesh3d_add_obstacle(void *navmesh, void *min, void *max);
/// @brief Remove an authored coarse AABB obstacle by index and refilter.
int8_t rt_navmesh3d_remove_obstacle(void *navmesh, int64_t index);
/// @brief Update an authored coarse AABB obstacle by index and refilter.
int8_t rt_navmesh3d_update_obstacle(void *navmesh, int64_t index, void *min, void *max);
/// @brief Number of authored coarse AABB obstacles.
int64_t rt_navmesh3d_get_obstacle_count(void *navmesh);
/// @brief Assign nav area and traversal cost metadata to polygons overlapping an AABB volume.
int8_t rt_navmesh3d_set_area(
    void *navmesh, void *min, void *max, rt_string area, double traversal_cost);
/// @brief Read nav area and traversal cost metadata at a walkable position.
rt_string rt_navmesh3d_get_area(void *navmesh, void *point);
/// @brief Read the traversal-cost metadata at a walkable position.
double rt_navmesh3d_get_traversal_cost(void *navmesh, void *point);
/// @brief Re-carve a single tile of a tiled bake in place (O(tile): no adjacency/grid rebuild).
///        Falls back to a whole-mesh refilter for non-tiled meshes. Tile (tx,tz) covers world XZ
///        [meshMin + t*tile_size, meshMin + (t+1)*tile_size).
int8_t rt_navmesh3d_rebuild_tile(void *navmesh, int64_t tile_x, int64_t tile_z);
/// @brief Set the maximum walkable slope (degrees) used when baking.
void rt_navmesh3d_set_max_slope(void *navmesh, double degrees);
/// @brief Draw the navmesh polygons as a debug overlay onto @p canvas.
void rt_navmesh3d_debug_draw(void *navmesh, void *canvas);

/* Runtime integration helper used by NavAgent3D. Returns malloc'd xyz triples. */
/// @brief Compute a path from @p from to @p to and copy it into a freshly
///        malloc'd flat xyz array. @return point count (caller frees the array).
int64_t rt_navmesh3d_copy_path_points(void *navmesh, void *from, void *to, double **out_points_xyz);

/// @brief Test-only: verify the spatial query-grid point location matches a brute-force linear
///        scan over a sample lattice spanning the navmesh bounds. @return 1 if all samples agree
///        (or the navmesh has no grid), 0 on any mismatch / invalid handle.
int8_t rt_navmesh3d_check_query_grid_parity(void *navmesh);

/// @brief Test-only: append an obstacle WITHOUT re-flagging any tile, leaving the tiled navmesh in
///        a deliberately stale state so a subsequent RebuildTile can be shown to affect exactly one
///        tile. @return 1 on success, 0 on invalid handle / OOM.
int8_t rt_navmesh3d_test_inject_obstacle(void *navmesh, void *min, void *max);
/// @brief Test-only: map a world XZ position to its tile coordinates. @return 1 if tiled (outputs
///        written), 0 otherwise.
int8_t rt_navmesh3d_test_tile_of_point(
    void *navmesh, double px, double pz, int64_t *out_tx, int64_t *out_tz);
/// @brief Test-only: edit retained tiled voxel source for one tile without rebuilding it.
int8_t rt_navmesh3d_test_set_tile_source(
    void *navmesh, int64_t tile_x, int64_t tile_z, double height, int8_t walkable);

#ifdef __cplusplus
}
#endif
