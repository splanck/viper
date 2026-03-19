//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_pathfinder.cpp
// Purpose: Unit tests for A* grid pathfinding. Tests path correctness on known
//   grid configurations, wall avoidance, diagonal movement, value clamping,
//   no-path scenarios, start==goal, weighted costs, and NULL safety.
//
// Key invariants:
//   - Paths are returned as List[Integer] of interleaved x,y pairs.
//   - 4-way paths use Manhattan moves only.
//   - 8-way paths may include diagonal moves.
//   - Blocked paths return empty list with LastFound == false.
//
// Ownership/Lifetime:
//   - Uses runtime library. Pathfinder and List objects are GC-managed.
//
// Links: src/runtime/collections/rt_pathfinder.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_box.h"
#include "rt_internal.h"
#include "rt_list.h"
#include "rt_pathfinder.h"
#include <cassert>
#include <cstdio>

// Trap handler for runtime
extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

//=============================================================================
// Helpers
//=============================================================================

/// @brief Get an integer from a boxed value in a list.
static int64_t list_get_i64(void *list, int64_t index)
{
    void *elem = rt_list_get(list, index);
    if (!elem)
        return -999;
    return rt_unbox_i64(elem);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

//=============================================================================
// Tests
//=============================================================================

static void test_creation(void)
{
    TEST("Pathfinder creation");
    void *pf = rt_pathfinder_new(10, 10);
    assert(pf != NULL);
    assert(rt_pathfinder_get_width(pf) == 10);
    assert(rt_pathfinder_get_height(pf) == 10);
    PASS();
}

static void test_walkable_default(void)
{
    TEST("All cells walkable by default");
    void *pf = rt_pathfinder_new(5, 5);
    for (int x = 0; x < 5; x++)
        for (int y = 0; y < 5; y++)
            assert(rt_pathfinder_is_walkable(pf, x, y) == 1);
    PASS();
}

static void test_set_walkable(void)
{
    TEST("Set/get walkability");
    void *pf = rt_pathfinder_new(5, 5);
    rt_pathfinder_set_walkable(pf, 2, 2, 0);
    assert(rt_pathfinder_is_walkable(pf, 2, 2) == 0);
    assert(rt_pathfinder_is_walkable(pf, 3, 3) == 1);
    PASS();
}

static void test_start_equals_goal(void)
{
    TEST("Start == goal returns single point");
    void *pf = rt_pathfinder_new(5, 5);
    void *path = rt_pathfinder_find_path(pf, 2, 2, 2, 2);
    assert(path != NULL);
    assert(rt_list_len(path) == 2); // [x, y] = 2 elements
    assert(list_get_i64(path, 0) == 2);
    assert(list_get_i64(path, 1) == 2);
    assert(rt_pathfinder_get_last_found(pf) == 1);
    PASS();
}

static void test_4way_straight_line(void)
{
    TEST("4-way straight horizontal path");
    void *pf = rt_pathfinder_new(5, 1);
    void *path = rt_pathfinder_find_path(pf, 0, 0, 4, 0);
    assert(path != NULL);
    // Should be 5 waypoints: (0,0), (1,0), (2,0), (3,0), (4,0) = 10 elements
    assert(rt_list_len(path) == 10);
    assert(list_get_i64(path, 0) == 0); // start x
    assert(list_get_i64(path, 1) == 0); // start y
    assert(list_get_i64(path, 8) == 4); // goal x
    assert(list_get_i64(path, 9) == 0); // goal y
    assert(rt_pathfinder_get_last_found(pf) == 1);
    PASS();
}

static void test_4way_manhattan(void)
{
    TEST("4-way path is Manhattan (no diagonals)");
    void *pf = rt_pathfinder_new(5, 5);
    // 4-way (default): path from (0,0) to (2,2) should have 4 moves = 5 waypoints
    void *path = rt_pathfinder_find_path(pf, 0, 0, 2, 2);
    assert(path != NULL);
    assert(rt_list_len(path) == 10); // 5 waypoints × 2 coords

    // Verify all moves are cardinal (dx+dy == 1)
    for (int64_t i = 2; i < rt_list_len(path); i += 2)
    {
        int64_t px = list_get_i64(path, i - 2);
        int64_t py = list_get_i64(path, i - 1);
        int64_t cx = list_get_i64(path, i);
        int64_t cy = list_get_i64(path, i + 1);
        int64_t dx = cx - px;
        int64_t dy = cy - py;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        assert(dx + dy == 1); // Only cardinal moves
    }
    PASS();
}

static void test_8way_diagonal(void)
{
    TEST("8-way path uses diagonal shortcuts");
    void *pf = rt_pathfinder_new(5, 5);
    rt_pathfinder_set_diagonal(pf, 1);
    void *path = rt_pathfinder_find_path(pf, 0, 0, 4, 4);
    assert(path != NULL);
    // Diagonal: (0,0)→(1,1)→(2,2)→(3,3)→(4,4) = 5 waypoints = 10 elements
    assert(rt_list_len(path) == 10);
    assert(rt_pathfinder_get_last_found(pf) == 1);
    PASS();
}

static void test_wall_avoidance(void)
{
    TEST("Path avoids walls");
    void *pf = rt_pathfinder_new(5, 5);
    // Vertical wall at x=2, rows 0-3 (gap at y=4)
    for (int y = 0; y < 4; y++)
        rt_pathfinder_set_walkable(pf, 2, y, 0);

    void *path = rt_pathfinder_find_path(pf, 0, 0, 4, 0);
    assert(path != NULL);
    assert(rt_list_len(path) > 0);
    assert(rt_pathfinder_get_last_found(pf) == 1);

    // Verify no waypoint is on the wall
    for (int64_t i = 0; i < rt_list_len(path); i += 2)
    {
        int64_t x = list_get_i64(path, i);
        int64_t y = list_get_i64(path, i + 1);
        if (x == 2 && y < 4)
            assert(0 && "Path walked through wall!");
    }
    PASS();
}

static void test_no_path(void)
{
    TEST("No path returns empty list");
    void *pf = rt_pathfinder_new(5, 5);
    // Complete wall at x=2
    for (int y = 0; y < 5; y++)
        rt_pathfinder_set_walkable(pf, 2, y, 0);

    void *path = rt_pathfinder_find_path(pf, 0, 0, 4, 0);
    assert(path != NULL);
    assert(rt_list_len(path) == 0);
    assert(rt_pathfinder_get_last_found(pf) == 0);
    PASS();
}

static void test_goal_not_walkable(void)
{
    TEST("Goal not walkable returns empty");
    void *pf = rt_pathfinder_new(5, 5);
    rt_pathfinder_set_walkable(pf, 4, 4, 0);
    void *path = rt_pathfinder_find_path(pf, 0, 0, 4, 4);
    assert(path != NULL);
    assert(rt_list_len(path) == 0);
    assert(rt_pathfinder_get_last_found(pf) == 0);
    PASS();
}

static void test_out_of_bounds(void)
{
    TEST("Out of bounds returns empty");
    void *pf = rt_pathfinder_new(5, 5);
    void *path = rt_pathfinder_find_path(pf, -1, 0, 10, 10);
    assert(path != NULL);
    assert(rt_list_len(path) == 0);
    assert(rt_pathfinder_get_last_found(pf) == 0);
    PASS();
}

static void test_path_length(void)
{
    TEST("FindPathLength returns correct cost");
    void *pf = rt_pathfinder_new(5, 1);
    // 4 moves × 100 cost each = 400
    int64_t cost = rt_pathfinder_find_path_length(pf, 0, 0, 4, 0);
    assert(cost == 400);
    assert(rt_pathfinder_get_last_found(pf) == 1);

    // No path → returns -1
    rt_pathfinder_set_walkable(pf, 2, 0, 0);
    cost = rt_pathfinder_find_path_length(pf, 0, 0, 4, 0);
    assert(cost == -1);
    PASS();
}

static void test_weighted_cost(void)
{
    TEST("Weighted costs influence path choice");
    // 3-wide corridor: top row (y=0), middle (y=1), bottom (y=2)
    // Make middle row very expensive
    void *pf = rt_pathfinder_new(5, 3);
    for (int x = 0; x < 5; x++)
        rt_pathfinder_set_cost(pf, x, 1, 1000); // 10× cost on middle row

    // 8-way so it can detour through top or bottom
    rt_pathfinder_set_diagonal(pf, 1);

    int64_t direct_cost = rt_pathfinder_find_path_length(pf, 0, 1, 4, 1);
    // Reset costs and measure straight path
    for (int x = 0; x < 5; x++)
        rt_pathfinder_set_cost(pf, x, 1, 100);
    int64_t normal_cost = rt_pathfinder_find_path_length(pf, 0, 1, 4, 1);

    // Detour should have been chosen (higher cost avoids expensive middle)
    assert(direct_cost > normal_cost);
    PASS();
}

static void test_max_steps(void)
{
    TEST("MaxSteps limits search");
    void *pf = rt_pathfinder_new(100, 100);
    rt_pathfinder_set_max_steps(pf, 10);
    void *path = rt_pathfinder_find_path(pf, 0, 0, 99, 99);
    // With only 10 steps allowed, can't reach (99,99)
    assert(rt_list_len(path) == 0);
    assert(rt_pathfinder_get_last_found(pf) == 0);
    assert(rt_pathfinder_get_last_steps(pf) <= 10);
    PASS();
}

static void test_cost_get_set(void)
{
    TEST("Cost get/set round-trip");
    void *pf = rt_pathfinder_new(5, 5);
    rt_pathfinder_set_cost(pf, 2, 3, 500);
    assert(rt_pathfinder_get_cost(pf, 2, 3) == 500);
    assert(rt_pathfinder_get_cost(pf, 0, 0) == 100); // default
    PASS();
}

static void test_null_safety(void)
{
    TEST("NULL safety");
    assert(rt_pathfinder_get_width(NULL) == 0);
    assert(rt_pathfinder_get_height(NULL) == 0);
    assert(rt_pathfinder_is_walkable(NULL, 0, 0) == 0);
    assert(rt_pathfinder_get_last_found(NULL) == 0);
    rt_pathfinder_set_walkable(NULL, 0, 0, 1); // No crash
    rt_pathfinder_set_diagonal(NULL, 1);       // No crash
    PASS();
}

static void test_walkable_out_of_bounds(void)
{
    TEST("Walkable check out of bounds returns false");
    void *pf = rt_pathfinder_new(5, 5);
    assert(rt_pathfinder_is_walkable(pf, -1, 0) == 0);
    assert(rt_pathfinder_is_walkable(pf, 5, 0) == 0);
    assert(rt_pathfinder_is_walkable(pf, 0, 5) == 0);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("test_rt_pathfinder:\n");

    test_creation();
    test_walkable_default();
    test_set_walkable();
    test_start_equals_goal();
    test_4way_straight_line();
    test_4way_manhattan();
    test_8way_diagonal();
    test_wall_avoidance();
    test_no_path();
    test_goal_not_walkable();
    test_out_of_bounds();
    test_path_length();
    test_weighted_cost();
    test_max_steps();
    test_cost_get_set();
    test_null_safety();
    test_walkable_out_of_bounds();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
