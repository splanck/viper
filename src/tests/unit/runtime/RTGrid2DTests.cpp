//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime/RTGrid2DTests.cpp
// Purpose: Validate the Grid2D 2D array container.
//
//===----------------------------------------------------------------------===//

#include "rt_grid2d.h"

#include <cassert>
#include <cstdio>

static void test_create_and_destroy() {
    printf("  test_create_and_destroy...\n");

    rt_grid2d grid = rt_grid2d_new(10, 10, 0);
    assert(grid != nullptr);

    assert(rt_grid2d_width(grid) == 10);
    assert(rt_grid2d_height(grid) == 10);
    assert(rt_grid2d_size(grid) == 100);

    rt_grid2d_destroy(grid);
}

static void test_create_with_default_value() {
    printf("  test_create_with_default_value...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 42);
    assert(grid != nullptr);

    // All cells should have the default value
    for (int64_t y = 0; y < 5; y++) {
        for (int64_t x = 0; x < 5; x++) {
            assert(rt_grid2d_get(grid, x, y) == 42);
        }
    }

    rt_grid2d_destroy(grid);
}

static void test_get_set() {
    printf("  test_get_set...\n");

    rt_grid2d grid = rt_grid2d_new(10, 10, 0);
    assert(grid != nullptr);

    rt_grid2d_set(grid, 5, 5, 123);
    assert(rt_grid2d_get(grid, 5, 5) == 123);

    rt_grid2d_set(grid, 0, 0, 1);
    rt_grid2d_set(grid, 9, 9, 2);
    assert(rt_grid2d_get(grid, 0, 0) == 1);
    assert(rt_grid2d_get(grid, 9, 9) == 2);

    rt_grid2d_destroy(grid);
}

static void test_out_of_bounds_get() {
    printf("  test_out_of_bounds_get...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 99);
    assert(grid != nullptr);

    // Out of bounds should return 0
    assert(rt_grid2d_get(grid, -1, 0) == 0);
    assert(rt_grid2d_get(grid, 0, -1) == 0);
    assert(rt_grid2d_get(grid, 5, 0) == 0);
    assert(rt_grid2d_get(grid, 0, 5) == 0);
    assert(rt_grid2d_get(grid, 100, 100) == 0);

    rt_grid2d_destroy(grid);
}

static void test_out_of_bounds_set_ignored() {
    printf("  test_out_of_bounds_set_ignored...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 0);
    assert(grid != nullptr);

    // Out of bounds set should be silently ignored
    rt_grid2d_set(grid, -1, 0, 999);
    rt_grid2d_set(grid, 5, 0, 999);

    // Verify no corruption - check all valid cells are still 0
    for (int64_t y = 0; y < 5; y++) {
        for (int64_t x = 0; x < 5; x++) {
            assert(rt_grid2d_get(grid, x, y) == 0);
        }
    }

    rt_grid2d_destroy(grid);
}

static void test_in_bounds() {
    printf("  test_in_bounds...\n");

    rt_grid2d grid = rt_grid2d_new(10, 8, 0);
    assert(grid != nullptr);

    assert(rt_grid2d_in_bounds(grid, 0, 0) == 1);
    assert(rt_grid2d_in_bounds(grid, 9, 7) == 1);
    assert(rt_grid2d_in_bounds(grid, 5, 4) == 1);

    assert(rt_grid2d_in_bounds(grid, -1, 0) == 0);
    assert(rt_grid2d_in_bounds(grid, 0, -1) == 0);
    assert(rt_grid2d_in_bounds(grid, 10, 0) == 0);
    assert(rt_grid2d_in_bounds(grid, 0, 8) == 0);

    rt_grid2d_destroy(grid);
}

static void test_fill() {
    printf("  test_fill...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 0);
    assert(grid != nullptr);

    rt_grid2d_fill(grid, 7);

    for (int64_t y = 0; y < 5; y++) {
        for (int64_t x = 0; x < 5; x++) {
            assert(rt_grid2d_get(grid, x, y) == 7);
        }
    }

    rt_grid2d_destroy(grid);
}

static void test_clear() {
    printf("  test_clear...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 99);
    assert(grid != nullptr);

    rt_grid2d_clear(grid);

    for (int64_t y = 0; y < 5; y++) {
        for (int64_t x = 0; x < 5; x++) {
            assert(rt_grid2d_get(grid, x, y) == 0);
        }
    }

    rt_grid2d_destroy(grid);
}

static void test_count() {
    printf("  test_count...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 0);
    assert(grid != nullptr);

    assert(rt_grid2d_count(grid, 0) == 25);
    assert(rt_grid2d_count(grid, 1) == 0);

    rt_grid2d_set(grid, 0, 0, 1);
    rt_grid2d_set(grid, 1, 1, 1);
    rt_grid2d_set(grid, 2, 2, 1);

    assert(rt_grid2d_count(grid, 1) == 3);
    assert(rt_grid2d_count(grid, 0) == 22);

    rt_grid2d_destroy(grid);
}

static void test_replace() {
    printf("  test_replace...\n");

    rt_grid2d grid = rt_grid2d_new(5, 5, 1);
    assert(grid != nullptr);

    // Set some cells to 2
    rt_grid2d_set(grid, 0, 0, 2);
    rt_grid2d_set(grid, 4, 4, 2);

    // Replace all 1s with 3s
    int64_t replaced = rt_grid2d_replace(grid, 1, 3);
    assert(replaced == 23);

    assert(rt_grid2d_get(grid, 0, 0) == 2);
    assert(rt_grid2d_get(grid, 4, 4) == 2);
    assert(rt_grid2d_get(grid, 1, 1) == 3);

    rt_grid2d_destroy(grid);
}

static void test_copy_from() {
    printf("  test_copy_from...\n");

    rt_grid2d src = rt_grid2d_new(5, 5, 0);
    rt_grid2d dest = rt_grid2d_new(5, 5, 99);
    assert(src != nullptr);
    assert(dest != nullptr);

    // Set some values in source
    rt_grid2d_set(src, 0, 0, 1);
    rt_grid2d_set(src, 4, 4, 2);
    rt_grid2d_set(src, 2, 2, 3);

    // Copy
    assert(rt_grid2d_copy_from(dest, src) == 1);

    // Verify copy
    assert(rt_grid2d_get(dest, 0, 0) == 1);
    assert(rt_grid2d_get(dest, 4, 4) == 2);
    assert(rt_grid2d_get(dest, 2, 2) == 3);
    assert(rt_grid2d_get(dest, 1, 1) == 0);

    rt_grid2d_destroy(src);
    rt_grid2d_destroy(dest);
}

static void test_copy_from_dimension_mismatch() {
    printf("  test_copy_from_dimension_mismatch...\n");

    rt_grid2d src = rt_grid2d_new(5, 5, 0);
    rt_grid2d dest = rt_grid2d_new(10, 10, 99);
    assert(src != nullptr);
    assert(dest != nullptr);

    // Copy should fail due to dimension mismatch
    assert(rt_grid2d_copy_from(dest, src) == 0);

    // Dest should be unchanged
    assert(rt_grid2d_get(dest, 0, 0) == 99);

    rt_grid2d_destroy(src);
    rt_grid2d_destroy(dest);
}

static void test_invalid_dimensions() {
    printf("  test_invalid_dimensions...\n");

    assert(rt_grid2d_new(0, 10, 0) == nullptr);
    assert(rt_grid2d_new(10, 0, 0) == nullptr);
    assert(rt_grid2d_new(-1, 10, 0) == nullptr);
    assert(rt_grid2d_new(10, -1, 0) == nullptr);
}

static void test_tile_map_use_case() {
    printf("  test_tile_map_use_case...\n");

    // Simulate a simple tile map
    const int64_t TILE_EMPTY = 0;
    const int64_t TILE_WALL = 1;
    const int64_t TILE_DOT = 2;

    rt_grid2d map = rt_grid2d_new(28, 31, TILE_EMPTY);
    assert(map != nullptr);

    // Set up borders
    for (int64_t x = 0; x < 28; x++) {
        rt_grid2d_set(map, x, 0, TILE_WALL);
        rt_grid2d_set(map, x, 30, TILE_WALL);
    }
    for (int64_t y = 0; y < 31; y++) {
        rt_grid2d_set(map, 0, y, TILE_WALL);
        rt_grid2d_set(map, 27, y, TILE_WALL);
    }

    // Fill interior with dots
    for (int64_t y = 1; y < 30; y++) {
        for (int64_t x = 1; x < 27; x++) {
            rt_grid2d_set(map, x, y, TILE_DOT);
        }
    }

    // Count tiles
    int64_t wall_count = rt_grid2d_count(map, TILE_WALL);
    int64_t dot_count = rt_grid2d_count(map, TILE_DOT);

    // Expected: borders = 28 + 28 + 29 + 29 = 114
    assert(wall_count == 114);
    // Expected: interior = 26 * 29 = 754
    assert(dot_count == 754);

    // Check walkability
    assert(rt_grid2d_get(map, 0, 0) == TILE_WALL);
    assert(rt_grid2d_get(map, 14, 15) == TILE_DOT);

    rt_grid2d_destroy(map);
}

int main() {
    printf("RTGrid2DTests:\n");

    test_create_and_destroy();
    test_create_with_default_value();
    test_get_set();
    test_out_of_bounds_get();
    test_out_of_bounds_set_ignored();
    test_in_bounds();
    test_fill();
    test_clear();
    test_count();
    test_replace();
    test_copy_from();
    test_copy_from_dimension_mismatch();
    test_invalid_dimensions();
    test_tile_map_use_case();

    printf("All Grid2D tests passed!\n");
    return 0;
}
