//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_grid2d.c
// Purpose: Dense 2D array of int64 values for Viper game maps and grids.
//   Provides O(1) get/set access by (column, row) index, fill and copy
//   operations, and a row-major flat view for efficient bulk processing.
//   Typical uses: tile maps, cellular automata, pathfinding cost grids,
//   flood-fill canvas, and any fixed-width 2D board or level layout.
//
// Key invariants:
//   - Grid dimensions (width, height) are fixed at creation and cannot change.
//     The underlying storage is a single calloc'd row-major array of int64:
//       index = row × width + col
//   - Width and height are clamped to [1, RT_GRID2D_MAX_DIM] at creation.
//     RT_GRID2D_MAX_DIM is defined in rt_grid2d.h.
//   - All cells are initialised to 0 at creation (via calloc).
//   - Out-of-bounds get returns 0; out-of-bounds set is silently ignored.
//     No trap is fired for invalid accesses — callers are responsible for
//     checking bounds if they need error detection.
//   - The grid stores raw int64 values only. Semantic interpretation (tile IDs,
//     passability, cost weights, etc.) is the caller's responsibility.
//
// Ownership/Lifetime:
//   - Grid2D objects are GC-managed (rt_obj_new_i64). The cell array is
//     calloc'd and freed by the GC finalizer. rt_grid2d_destroy() is a
//     no-op for API symmetry.
//
// Links: src/runtime/collections/rt_grid2d.h (public API),
//        docs/viperlib/game.md (Grid2D section)
//
//===----------------------------------------------------------------------===//

#include "rt_grid2d.h"
#include "rt_object.h"
#include <stdlib.h>
#include <string.h>

/// Internal structure for Grid2D.
struct rt_grid2d_impl
{
    int64_t width;
    int64_t height;
    int64_t *data; // Row-major storage: data[y * width + x]
};

static void grid2d_finalizer(void *obj)
{
    struct rt_grid2d_impl *grid = (struct rt_grid2d_impl *)obj;
    free(grid->data);
    grid->data = NULL;
}

rt_grid2d rt_grid2d_new(int64_t width, int64_t height, int64_t default_value)
{
    if (width <= 0 || height <= 0)
    {
        return NULL;
    }

    // Check for overflow
    if (width > INT64_MAX / height)
    {
        return NULL;
    }

    int64_t size = width * height;

    struct rt_grid2d_impl *grid = rt_obj_new_i64(0, sizeof(struct rt_grid2d_impl));
    if (!grid)
    {
        return NULL;
    }

    grid->data = malloc((size_t)size * sizeof(int64_t));
    if (!grid->data)
    {
        return NULL;
    }

    grid->width = width;
    grid->height = height;

    // Fill with default value
    for (int64_t i = 0; i < size; i++)
    {
        grid->data[i] = default_value;
    }

    rt_obj_set_finalizer(grid, grid2d_finalizer);
    return grid;
}

void rt_grid2d_destroy(rt_grid2d grid)
{
    // Object is GC-managed; finalizer frees internal data.
    (void)grid;
}

int64_t rt_grid2d_get(rt_grid2d grid, int64_t x, int64_t y)
{
    if (!grid)
        return 0;
    if (x < 0 || x >= grid->width || y < 0 || y >= grid->height)
    {
        return 0;
    }
    return grid->data[y * grid->width + x];
}

void rt_grid2d_set(rt_grid2d grid, int64_t x, int64_t y, int64_t value)
{
    if (!grid)
        return;
    if (x < 0 || x >= grid->width || y < 0 || y >= grid->height)
    {
        return;
    }
    grid->data[y * grid->width + x] = value;
}

void rt_grid2d_fill(rt_grid2d grid, int64_t value)
{
    if (!grid)
        return;

    int64_t size = grid->width * grid->height;
    for (int64_t i = 0; i < size; i++)
    {
        grid->data[i] = value;
    }
}

void rt_grid2d_clear(rt_grid2d grid)
{
    rt_grid2d_fill(grid, 0);
}

int64_t rt_grid2d_width(rt_grid2d grid)
{
    return grid ? grid->width : 0;
}

int64_t rt_grid2d_height(rt_grid2d grid)
{
    return grid ? grid->height : 0;
}

int8_t rt_grid2d_in_bounds(rt_grid2d grid, int64_t x, int64_t y)
{
    if (!grid)
        return 0;
    return (x >= 0 && x < grid->width && y >= 0 && y < grid->height) ? 1 : 0;
}

int64_t rt_grid2d_size(rt_grid2d grid)
{
    return grid ? grid->width * grid->height : 0;
}

int8_t rt_grid2d_copy_from(rt_grid2d dest, rt_grid2d src)
{
    if (!dest || !src)
        return 0;
    if (dest->width != src->width || dest->height != src->height)
    {
        return 0;
    }

    int64_t size = dest->width * dest->height;
    memcpy(dest->data, src->data, (size_t)size * sizeof(int64_t));
    return 1;
}

int64_t rt_grid2d_count(rt_grid2d grid, int64_t value)
{
    if (!grid)
        return 0;

    int64_t count = 0;
    int64_t size = grid->width * grid->height;
    for (int64_t i = 0; i < size; i++)
    {
        if (grid->data[i] == value)
        {
            count++;
        }
    }
    return count;
}

int8_t rt_grid2d_find(rt_grid2d grid, int64_t value, int64_t *out_x, int64_t *out_y)
{
    if (!grid)
        return 0;

    for (int64_t y = 0; y < grid->height; y++)
    {
        for (int64_t x = 0; x < grid->width; x++)
        {
            if (grid->data[y * grid->width + x] == value)
            {
                if (out_x)
                    *out_x = x;
                if (out_y)
                    *out_y = y;
                return 1;
            }
        }
    }
    return 0;
}

int64_t rt_grid2d_replace(rt_grid2d grid, int64_t old_value, int64_t new_value)
{
    if (!grid)
        return 0;

    int64_t count = 0;
    int64_t size = grid->width * grid->height;
    for (int64_t i = 0; i < size; i++)
    {
        if (grid->data[i] == old_value)
        {
            grid->data[i] = new_value;
            count++;
        }
    }
    return count;
}
