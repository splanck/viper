//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_grid2d.h
// Purpose: 2D grid container for integer values with fixed dimensions, providing element access, fill, copy, and region operations for tile maps and pixel buffers.
//
// Key invariants:
//   - Grid dimensions are fixed at creation time and cannot be changed.
//   - Out-of-bounds accesses return 0 or are silently ignored rather than trapping.
//   - Elements are stored in row-major order.
//   - Coordinates are 0-based (row 0, col 0 is the top-left corner).
//
// Ownership/Lifetime:
//   - Caller owns the grid handle; destroy with rt_grid2d_destroy.
//   - No reference counting; explicit destruction is required.
//
// Links: src/runtime/collections/rt_grid2d.c (implementation)
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_GRID2D_H
#define VIPER_RT_GRID2D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Opaque handle to a Grid2D instance.
    typedef struct rt_grid2d_impl *rt_grid2d;

    /// @brief Create a new Grid2D with the specified dimensions.
    /// @param width The width of the grid (number of columns).
    /// @param height The height of the grid (number of rows).
    /// @param default_value The initial value for all cells.
    /// @return A new Grid2D instance, or NULL on failure.
    rt_grid2d rt_grid2d_new(int64_t width, int64_t height, int64_t default_value);

    /// @brief Destroy a Grid2D and free its memory.
    /// @param grid The grid to destroy.
    void rt_grid2d_destroy(rt_grid2d grid);

    /// @brief Get the value at the specified coordinates.
    /// @param grid The grid.
    /// @param x The x coordinate (column).
    /// @param y The y coordinate (row).
    /// @return The value at (x, y), or 0 if out of bounds.
    int64_t rt_grid2d_get(rt_grid2d grid, int64_t x, int64_t y);

    /// @brief Set the value at the specified coordinates.
    /// @param grid The grid.
    /// @param x The x coordinate (column).
    /// @param y The y coordinate (row).
    /// @param value The value to set.
    void rt_grid2d_set(rt_grid2d grid, int64_t x, int64_t y, int64_t value);

    /// @brief Fill the entire grid with a value.
    /// @param grid The grid.
    /// @param value The value to fill every cell with.
    void rt_grid2d_fill(rt_grid2d grid, int64_t value);

    /// @brief Clear the grid (fill all cells with zeros).
    /// @param grid The grid.
    void rt_grid2d_clear(rt_grid2d grid);

    /// @brief Get the width of the grid.
    /// @param grid The grid.
    /// @return The width (number of columns).
    int64_t rt_grid2d_width(rt_grid2d grid);

    /// @brief Get the height of the grid.
    /// @param grid The grid.
    /// @return The height (number of rows).
    int64_t rt_grid2d_height(rt_grid2d grid);

    /// @brief Check if coordinates are within bounds.
    /// @param grid The grid.
    /// @param x The x coordinate.
    /// @param y The y coordinate.
    /// @return 1 if in bounds, 0 otherwise.
    int8_t rt_grid2d_in_bounds(rt_grid2d grid, int64_t x, int64_t y);

    /// @brief Get the total number of cells.
    /// @param grid The grid.
    /// @return The total cell count (width * height).
    int64_t rt_grid2d_size(rt_grid2d grid);

    /// @brief Copy values from another grid (must be same dimensions).
    /// @param dest The destination grid.
    /// @param src The source grid.
    /// @return 1 on success, 0 if dimensions don't match.
    int8_t rt_grid2d_copy_from(rt_grid2d dest, rt_grid2d src);

    /// @brief Count cells matching a specific value.
    /// @param grid The grid.
    /// @param value The value to count.
    /// @return The number of cells containing @p value.
    int64_t rt_grid2d_count(rt_grid2d grid, int64_t value);

    /// @brief Find the first cell with a specific value (row-major order).
    /// @param grid The grid.
    /// @param value The value to find.
    /// @param out_x Output: x coordinate of the found cell.
    /// @param out_y Output: y coordinate of the found cell.
    /// @return 1 if found (with @p out_x and @p out_y set), 0 if not found.
    int8_t rt_grid2d_find(rt_grid2d grid, int64_t value, int64_t *out_x, int64_t *out_y);

    /// @brief Replace all occurrences of a value with another.
    /// @param grid The grid.
    /// @param old_value The value to replace.
    /// @param new_value The replacement value.
    /// @return The number of cells that were replaced.
    int64_t rt_grid2d_replace(rt_grid2d grid, int64_t old_value, int64_t new_value);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_GRID2D_H
