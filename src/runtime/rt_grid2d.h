//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_grid2d.h
/// @brief 2D grid container for integer values.
///
/// Provides a convenient abstraction for 2D arrays commonly used in games
/// for tile maps, pixel buffers, and other grid-based data structures.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_GRID2D_H
#define VIPER_RT_GRID2D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle to a Grid2D instance.
typedef struct rt_grid2d_impl *rt_grid2d;

/// Creates a new Grid2D with the specified dimensions.
/// @param width The width of the grid (columns).
/// @param height The height of the grid (rows).
/// @param default_value The initial value for all cells.
/// @return A new Grid2D instance, or NULL on failure.
rt_grid2d rt_grid2d_new(int64_t width, int64_t height, int64_t default_value);

/// Destroys a Grid2D and frees its memory.
/// @param grid The grid to destroy.
void rt_grid2d_destroy(rt_grid2d grid);

/// Gets the value at the specified coordinates.
/// @param grid The grid.
/// @param x The x coordinate (column).
/// @param y The y coordinate (row).
/// @return The value at (x, y), or 0 if out of bounds.
int64_t rt_grid2d_get(rt_grid2d grid, int64_t x, int64_t y);

/// Sets the value at the specified coordinates.
/// @param grid The grid.
/// @param x The x coordinate (column).
/// @param y The y coordinate (row).
/// @param value The value to set.
void rt_grid2d_set(rt_grid2d grid, int64_t x, int64_t y, int64_t value);

/// Fills the entire grid with a value.
/// @param grid The grid.
/// @param value The value to fill with.
void rt_grid2d_fill(rt_grid2d grid, int64_t value);

/// Clears the grid (fills with zeros).
/// @param grid The grid.
void rt_grid2d_clear(rt_grid2d grid);

/// Gets the width of the grid.
/// @param grid The grid.
/// @return The width (number of columns).
int64_t rt_grid2d_width(rt_grid2d grid);

/// Gets the height of the grid.
/// @param grid The grid.
/// @return The height (number of rows).
int64_t rt_grid2d_height(rt_grid2d grid);

/// Checks if coordinates are within bounds.
/// @param grid The grid.
/// @param x The x coordinate.
/// @param y The y coordinate.
/// @return 1 if in bounds, 0 otherwise.
int8_t rt_grid2d_in_bounds(rt_grid2d grid, int64_t x, int64_t y);

/// Gets the total number of cells.
/// @param grid The grid.
/// @return width * height.
int64_t rt_grid2d_size(rt_grid2d grid);

/// Copies values from another grid (must be same dimensions).
/// @param dest The destination grid.
/// @param src The source grid.
/// @return 1 on success, 0 if dimensions don't match.
int8_t rt_grid2d_copy_from(rt_grid2d dest, rt_grid2d src);

/// Counts cells matching a specific value.
/// @param grid The grid.
/// @param value The value to count.
/// @return The number of cells with that value.
int64_t rt_grid2d_count(rt_grid2d grid, int64_t value);

/// Finds the first cell with a specific value (row-major order).
/// @param grid The grid.
/// @param value The value to find.
/// @param out_x Output: x coordinate of found cell.
/// @param out_y Output: y coordinate of found cell.
/// @return 1 if found, 0 if not found.
int8_t rt_grid2d_find(rt_grid2d grid, int64_t value, int64_t *out_x, int64_t *out_y);

/// Replaces all occurrences of a value with another.
/// @param grid The grid.
/// @param old_value The value to replace.
/// @param new_value The replacement value.
/// @return The number of cells replaced.
int64_t rt_grid2d_replace(rt_grid2d grid, int64_t old_value, int64_t new_value);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_GRID2D_H
