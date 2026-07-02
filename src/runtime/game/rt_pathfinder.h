//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_pathfinder.h
// Purpose: A* grid pathfinding for 2D games. Supports 4-way and 8-way movement,
//   per-cell movement costs, and integration with Tilemap and Grid2D.
//
// Key invariants:
//   - Grid cells are indexed [0, width) × [0, height).
//   - Walkability and cost are per-cell. Cost=100 is normal (1× multiplier).
//   - Walkable costs are clamped to [1, 30000]; use walkability for walls.
//   - FindPath returns a List[Seq[Integer]] of x,y waypoint pairs (start to goal).
//   - FindPath returns an empty list if no path exists.
//   - Max steps prevents pathological searches from freezing the game.
//
// Ownership/Lifetime:
//   - Pathfinder objects are GC-managed via rt_obj_new_i64 with a finalizer.
//   - The internal cells array is heap-allocated; freed on GC collection.
//
// Links: src/runtime/game/rt_pathfinder.c (implementation),
//        src/runtime/graphics/rt_tilemap.h (Tilemap integration),
//        src/runtime/game/rt_grid2d.c (Grid2D integration)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_PATHFINDER_CLASS_ID INT64_C(-0x510207)
#define RT_PATH_RESULT_CLASS_ID INT64_C(-0x51021C)

//=========================================================================
// Construction
//=========================================================================

/// @brief Create a new pathfinder with a blank walkable grid.
/// @return NULL when width/height are non-positive or exceed 4096.
void *rt_pathfinder_new(int64_t width, int64_t height);

/// @brief Create a pathfinder from a Tilemap's collision data.
/// Tiles with collision type != 0 are marked non-walkable.
void *rt_pathfinder_from_tilemap(void *tilemap);

/// @brief Create a pathfinder from a Grid2D.
/// Non-zero cells are marked non-walkable.
void *rt_pathfinder_from_grid2d(void *grid);

/// @brief Release a pathfinder handle and its internal cell array.
void rt_pathfinder_destroy(void *pf);

//=========================================================================
// Configuration
//=========================================================================

/// @brief Mark grid cell (x, y) as walkable (1) or blocked (0).
void rt_pathfinder_set_walkable(void *pf, int64_t x, int64_t y, int8_t walkable);
/// @brief Query whether grid cell (x, y) is walkable (0 if out of bounds).
int8_t rt_pathfinder_is_walkable(void *pf, int64_t x, int64_t y);
/// @brief Set the movement cost of entering cell (x, y) (>= 1).
void rt_pathfinder_set_cost(void *pf, int64_t x, int64_t y, int64_t cost);
/// @brief Get the movement cost of cell (x, y).
int64_t rt_pathfinder_get_cost(void *pf, int64_t x, int64_t y);
/// @brief Allow (1) or forbid (0) diagonal movement during pathfinding.
void rt_pathfinder_set_diagonal(void *pf, int8_t allow);
/// @brief Cap the number of node expansions per search (0 = unlimited).
void rt_pathfinder_set_max_steps(void *pf, int64_t max);

//=========================================================================
// Properties
//=========================================================================

/// @brief Grid width in cells.
int64_t rt_pathfinder_get_width(void *pf);
/// @brief Grid height in cells.
int64_t rt_pathfinder_get_height(void *pf);
/// @brief Number of node expansions performed by the most recent search.
int64_t rt_pathfinder_get_last_steps(void *pf);
/// @brief Whether the most recent search found a path (1) or not (0).
int8_t rt_pathfinder_get_last_found(void *pf);

//=========================================================================
// PathResult
//=========================================================================

/// @brief Query whether a Viper.Game.PathResult contains a path.
/// @param result PathResult object.
/// @return 1 when the search found a path, otherwise 0.
int8_t rt_path_result_found(void *result);

/// @brief Read the search expansion count stored in a PathResult.
/// @param result PathResult object.
/// @return Number of expanded nodes, or 0 for invalid input.
int64_t rt_path_result_steps(void *result);

/// @brief Read the path movement cost stored in a PathResult.
/// @details A* searches store their fixed-point movement cost. Searches that do
///          not compute a cost, such as FindNearestResult, store -1.
/// @param result PathResult object.
/// @return Movement cost, or -1 when unavailable.
int64_t rt_path_result_cost(void *result);

/// @brief Read the cell-to-cell step count stored in a PathResult.
/// @param result PathResult object.
/// @return Cell-to-cell step count, or -1 when no path was found.
int64_t rt_path_result_step_count(void *result);

/// @brief Read the legacy Length alias from a PathResult.
/// @details Compatibility alias for @ref rt_path_result_step_count. New code
///          should use StepCount so the value is not confused with waypoint
///          list length or weighted path cost.
/// @param result PathResult object.
/// @return Cell-to-cell step count, or -1 when no path was found.
int64_t rt_path_result_length(void *result);

/// @brief Return a retained path list from a PathResult.
/// @details The path is a Viper.Collections.List of two-integer Seq waypoints,
///          matching rt_pathfinder_find_path. The caller receives a retained
///          reference and may keep it after releasing the PathResult.
/// @param result PathResult object.
/// @return Retained path list, or a new empty list for invalid input.
void *rt_path_result_path(void *result);

//=========================================================================
// Pathfinding
//=========================================================================

/// @brief Find a path from (sx,sy) to (gx,gy).
/// @return List[Seq[Integer]] of x,y waypoint pairs, or empty list if no path.
void *rt_pathfinder_find_path(void *pf, int64_t sx, int64_t sy, int64_t gx, int64_t gy);

/// @brief Find a path and return a composable PathResult snapshot.
/// @details Preserves the same search behavior as rt_pathfinder_find_path while
///          returning found-state, node expansion count, path length, movement
///          cost, and path list in one object. This avoids reading mutable
///          LastFound and LastSteps after the search.
/// @return New Viper.Game.PathResult object, or NULL on allocation failure.
void *rt_pathfinder_find_path_result(void *pf, int64_t sx, int64_t sy, int64_t gx, int64_t gy);

/// @brief Find path length from (sx,sy) to (gx,gy) without returning the path.
/// @return Cell-to-cell step count, or -1 if no path.
int64_t rt_pathfinder_find_path_length(void *pf, int64_t sx, int64_t sy, int64_t gx, int64_t gy);

/// @brief Find nearest reachable cell with the given source tile/grid value.
/// @return List[Seq[Integer]] of x,y waypoint pairs from start to target, or empty list.
void *rt_pathfinder_find_nearest(void *pf, int64_t sx, int64_t sy, int64_t target_value);

/// @brief Find the nearest target value and return a PathResult snapshot.
/// @details Uses the same breadth-first nearest-value search as
///          rt_pathfinder_find_nearest. The Cost property is -1 because this
///          operation does not compute weighted A* movement cost.
/// @return New Viper.Game.PathResult object, or NULL on allocation failure.
void *rt_pathfinder_find_nearest_result(void *pf, int64_t sx, int64_t sy, int64_t target_value);

#ifdef __cplusplus
}
#endif
