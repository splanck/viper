//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_grid2d.c
/// @brief Implementation of 2D grid container.
///
//===----------------------------------------------------------------------===//

#include "rt_grid2d.h"
#include <stdlib.h>
#include <string.h>

/// Internal structure for Grid2D.
struct rt_grid2d_impl {
    int64_t width;
    int64_t height;
    int64_t *data;            // Row-major storage: data[y * width + x]
};

rt_grid2d rt_grid2d_new(int64_t width, int64_t height, int64_t default_value)
{
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    // Check for overflow
    if (width > INT64_MAX / height) {
        return NULL;
    }

    int64_t size = width * height;

    struct rt_grid2d_impl *grid = malloc(sizeof(struct rt_grid2d_impl));
    if (!grid) {
        return NULL;
    }

    grid->data = malloc((size_t)size * sizeof(int64_t));
    if (!grid->data) {
        free(grid);
        return NULL;
    }

    grid->width = width;
    grid->height = height;

    // Fill with default value
    for (int64_t i = 0; i < size; i++) {
        grid->data[i] = default_value;
    }

    return grid;
}

void rt_grid2d_destroy(rt_grid2d grid)
{
    if (!grid) return;
    free(grid->data);
    free(grid);
}

int64_t rt_grid2d_get(rt_grid2d grid, int64_t x, int64_t y)
{
    if (!grid) return 0;
    if (x < 0 || x >= grid->width || y < 0 || y >= grid->height) {
        return 0;
    }
    return grid->data[y * grid->width + x];
}

void rt_grid2d_set(rt_grid2d grid, int64_t x, int64_t y, int64_t value)
{
    if (!grid) return;
    if (x < 0 || x >= grid->width || y < 0 || y >= grid->height) {
        return;
    }
    grid->data[y * grid->width + x] = value;
}

void rt_grid2d_fill(rt_grid2d grid, int64_t value)
{
    if (!grid) return;

    int64_t size = grid->width * grid->height;
    for (int64_t i = 0; i < size; i++) {
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
    if (!grid) return 0;
    return (x >= 0 && x < grid->width && y >= 0 && y < grid->height) ? 1 : 0;
}

int64_t rt_grid2d_size(rt_grid2d grid)
{
    return grid ? grid->width * grid->height : 0;
}

int8_t rt_grid2d_copy_from(rt_grid2d dest, rt_grid2d src)
{
    if (!dest || !src) return 0;
    if (dest->width != src->width || dest->height != src->height) {
        return 0;
    }

    int64_t size = dest->width * dest->height;
    memcpy(dest->data, src->data, (size_t)size * sizeof(int64_t));
    return 1;
}

int64_t rt_grid2d_count(rt_grid2d grid, int64_t value)
{
    if (!grid) return 0;

    int64_t count = 0;
    int64_t size = grid->width * grid->height;
    for (int64_t i = 0; i < size; i++) {
        if (grid->data[i] == value) {
            count++;
        }
    }
    return count;
}

int8_t rt_grid2d_find(rt_grid2d grid, int64_t value, int64_t *out_x, int64_t *out_y)
{
    if (!grid) return 0;

    for (int64_t y = 0; y < grid->height; y++) {
        for (int64_t x = 0; x < grid->width; x++) {
            if (grid->data[y * grid->width + x] == value) {
                if (out_x) *out_x = x;
                if (out_y) *out_y = y;
                return 1;
            }
        }
    }
    return 0;
}

int64_t rt_grid2d_replace(rt_grid2d grid, int64_t old_value, int64_t new_value)
{
    if (!grid) return 0;

    int64_t count = 0;
    int64_t size = grid->width * grid->height;
    for (int64_t i = 0; i < size; i++) {
        if (grid->data[i] == old_value) {
            grid->data[i] = new_value;
            count++;
        }
    }
    return count;
}
