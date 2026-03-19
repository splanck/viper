//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_pathfinder.h
// Purpose: A* grid pathfinding for 2D games. Supports 4-way and 8-way movement,
//   per-cell movement costs, and integration with Tilemap and Grid2D.
//
// Key invariants:
//   - Grid cells are indexed [0, width) × [0, height).
//   - Walkability and cost are per-cell. Cost=100 is normal (1× multiplier).
//   - FindPath returns a List[Integer] of interleaved x,y pairs (start to goal).
//   - FindPath returns an empty list if no path exists.
//   - Max steps prevents pathological searches from freezing the game.
//
// Ownership/Lifetime:
//   - Pathfinder objects are GC-managed via rt_obj_new_i64 with a finalizer.
//   - The internal cells array is heap-allocated; freed on GC collection.
//
// Links: src/runtime/collections/rt_pathfinder.c (implementation),
//        src/runtime/graphics/rt_tilemap.h (Tilemap integration),
//        src/runtime/collections/rt_grid2d.c (Grid2D integration)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Construction
    //=========================================================================

    /// @brief Create a new pathfinder with a blank walkable grid.
    void *rt_pathfinder_new(int64_t width, int64_t height);

    /// @brief Create a pathfinder from a Tilemap's collision data.
    /// Tiles with collision type != 0 are marked non-walkable.
    void *rt_pathfinder_from_tilemap(void *tilemap);

    /// @brief Create a pathfinder from a Grid2D.
    /// Non-zero cells are marked non-walkable.
    void *rt_pathfinder_from_grid2d(void *grid);

    //=========================================================================
    // Configuration
    //=========================================================================

    void rt_pathfinder_set_walkable(void *pf, int64_t x, int64_t y, int8_t walkable);
    int8_t rt_pathfinder_is_walkable(void *pf, int64_t x, int64_t y);
    void rt_pathfinder_set_cost(void *pf, int64_t x, int64_t y, int64_t cost);
    int64_t rt_pathfinder_get_cost(void *pf, int64_t x, int64_t y);
    void rt_pathfinder_set_diagonal(void *pf, int8_t allow);
    void rt_pathfinder_set_max_steps(void *pf, int64_t max);

    //=========================================================================
    // Properties
    //=========================================================================

    int64_t rt_pathfinder_get_width(void *pf);
    int64_t rt_pathfinder_get_height(void *pf);
    int64_t rt_pathfinder_get_last_steps(void *pf);
    int8_t rt_pathfinder_get_last_found(void *pf);

    //=========================================================================
    // Pathfinding
    //=========================================================================

    /// @brief Find a path from (sx,sy) to (gx,gy).
    /// @return List[Integer] of interleaved x,y pairs, or empty list if no path.
    void *rt_pathfinder_find_path(void *pf, int64_t sx, int64_t sy, int64_t gx, int64_t gy);

    /// @brief Find path cost from (sx,sy) to (gx,gy) without returning the path.
    /// @return Total path cost, or -1 if no path.
    int64_t rt_pathfinder_find_path_length(
        void *pf, int64_t sx, int64_t sy, int64_t gx, int64_t gy);

    /// @brief Find nearest reachable cell with the given Grid2D value.
    /// @return List[Integer] with two elements [x, y], or empty list.
    void *rt_pathfinder_find_nearest(void *pf, int64_t sx, int64_t sy, int64_t target_value);

#ifdef __cplusplus
}
#endif
