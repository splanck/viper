//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTQuadtreeTests.cpp - Unit tests for rt_quadtree
//===----------------------------------------------------------------------===//

#include "rt_quadtree.h"
#include <cassert>
#include <cstdio>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name)                                                                             \
    do {                                                                                           \
        printf("  %s...", #name);                                                                  \
        test_##name();                                                                             \
        printf(" OK\n");                                                                           \
        tests_passed++;                                                                            \
    } while (0)

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf(" FAILED at line %d: %s\n", __LINE__, #cond);                                   \
            tests_failed++;                                                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

TEST(create_destroy) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000); // 1000x1000
    ASSERT(tree != NULL);
    ASSERT(rt_quadtree_item_count(tree) == 0);
    rt_quadtree_destroy(tree);
}

TEST(insert) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    ASSERT(rt_quadtree_insert(tree, 1, 100000, 100000, 10000, 10000) == 1);
    ASSERT(rt_quadtree_insert(tree, 2, 500000, 500000, 20000, 20000) == 1);
    ASSERT(rt_quadtree_insert(tree, 3, 900000, 900000, 10000, 10000) == 1);

    ASSERT(rt_quadtree_item_count(tree) == 3);
    rt_quadtree_destroy(tree);
}

TEST(insert_out_of_bounds) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 100000, 100000);

    // Completely outside bounds
    ASSERT(rt_quadtree_insert(tree, 1, 200000, 200000, 10000, 10000) == 0);

    rt_quadtree_destroy(tree);
}

TEST(remove) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    rt_quadtree_insert(tree, 1, 100000, 100000, 10000, 10000);
    rt_quadtree_insert(tree, 2, 500000, 500000, 10000, 10000);

    ASSERT(rt_quadtree_item_count(tree) == 2);

    ASSERT(rt_quadtree_remove(tree, 1) == 1);
    ASSERT(rt_quadtree_item_count(tree) == 1);

    // Remove non-existent
    ASSERT(rt_quadtree_remove(tree, 99) == 0);

    rt_quadtree_destroy(tree);
}

TEST(query_rect) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    rt_quadtree_insert(tree, 1, 100000, 100000, 10000, 10000);
    rt_quadtree_insert(tree, 2, 150000, 150000, 10000, 10000);
    rt_quadtree_insert(tree, 3, 800000, 800000, 10000, 10000);

    // Query area around items 1 and 2
    int64_t count = rt_quadtree_query_rect(tree, 50000, 50000, 200000, 200000);
    ASSERT(count == 2);
    ASSERT(rt_quadtree_result_count(tree) == 2);

    // Check results
    int64_t id1 = rt_quadtree_get_result(tree, 0);
    int64_t id2 = rt_quadtree_get_result(tree, 1);
    ASSERT((id1 == 1 && id2 == 2) || (id1 == 2 && id2 == 1));

    rt_quadtree_destroy(tree);
}

TEST(query_point) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    rt_quadtree_insert(tree, 1, 100000, 100000, 20000, 20000);
    rt_quadtree_insert(tree, 2, 500000, 500000, 20000, 20000);

    // Query near item 1
    int64_t count = rt_quadtree_query_point(tree, 100000, 100000, 50000);
    ASSERT(count == 1);
    ASSERT(rt_quadtree_get_result(tree, 0) == 1);

    rt_quadtree_destroy(tree);
}

TEST(query_point_excludes_corner_false_positive) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000, 1000);

    // Rect query would catch this box in the corner, but the circle does not reach it.
    rt_quadtree_insert(tree, 1, 120, 120, 10, 10);
    rt_quadtree_insert(tree, 2, 100, 100, 10, 10);

    int64_t count = rt_quadtree_query_point(tree, 100, 100, 20);
    ASSERT(count == 1);
    ASSERT(rt_quadtree_get_result(tree, 0) == 2);

    rt_quadtree_destroy(tree);
}

TEST(update) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    rt_quadtree_insert(tree, 1, 100000, 100000, 10000, 10000);

    // Move item to new location
    ASSERT(rt_quadtree_update(tree, 1, 800000, 800000, 10000, 10000) == 1);

    // Should not be found at old location
    int64_t count = rt_quadtree_query_point(tree, 100000, 100000, 50000);
    ASSERT(count == 0);

    // Should be found at new location
    count = rt_quadtree_query_point(tree, 800000, 800000, 50000);
    ASSERT(count == 1);

    rt_quadtree_destroy(tree);
}

TEST(update_out_of_bounds_is_transactional) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000, 1000);

    ASSERT(rt_quadtree_insert(tree, 1, 100, 100, 20, 20) == 1);
    ASSERT(rt_quadtree_update(tree, 1, 2000, 2000, 20, 20) == 0);

    int64_t count = rt_quadtree_query_point(tree, 100, 100, 30);
    ASSERT(count == 1);
    ASSERT(rt_quadtree_get_result(tree, 0) == 1);

    count = rt_quadtree_query_point(tree, 2000, 2000, 30);
    ASSERT(count == 0);

    rt_quadtree_destroy(tree);
}

TEST(clear) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    rt_quadtree_insert(tree, 1, 100000, 100000, 10000, 10000);
    rt_quadtree_insert(tree, 2, 500000, 500000, 10000, 10000);
    rt_quadtree_insert(tree, 3, 900000, 900000, 10000, 10000);

    rt_quadtree_clear(tree);
    ASSERT(rt_quadtree_item_count(tree) == 0);

    rt_quadtree_destroy(tree);
}

TEST(get_pairs) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    // Insert overlapping items
    rt_quadtree_insert(tree, 1, 100000, 100000, 50000, 50000);
    /// @brief Rt_quadtree_insert.
    rt_quadtree_insert(tree, 2, 120000, 120000, 50000, 50000); // Overlaps with 1
                                                               /// @brief Rt_quadtree_insert.
    rt_quadtree_insert(tree, 3, 800000, 800000, 50000, 50000); // Far away

    int64_t pair_count = rt_quadtree_get_pairs(tree);
    ASSERT(pair_count > 0);

    // Check at least one pair exists
    int64_t first = rt_quadtree_pair_first(tree, 0);
    int64_t second = rt_quadtree_pair_second(tree, 0);
    ASSERT(first >= 0 && second >= 0);
    ASSERT(first != second);

    rt_quadtree_destroy(tree);
}

TEST(many_items) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    // Insert many items
    for (int i = 0; i < 100; i++) {
        int64_t x = (i % 10) * 100000;
        int64_t y = (i / 10) * 100000;
        rt_quadtree_insert(tree, i, x, y, 10000, 10000);
    }

    ASSERT(rt_quadtree_item_count(tree) == 100);

    // Query should still work
    int64_t count = rt_quadtree_query_rect(tree, 0, 0, 200000, 200000);
    ASSERT(count > 0);

    rt_quadtree_destroy(tree);
}

TEST(invalid_result_index) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    rt_quadtree_insert(tree, 1, 100000, 100000, 10000, 10000);
    rt_quadtree_query_rect(tree, 0, 0, 200000, 200000);

    // Invalid index
    ASSERT(rt_quadtree_get_result(tree, 100) == -1);
    ASSERT(rt_quadtree_get_result(tree, -1) == -1);

    rt_quadtree_destroy(tree);
}

// GAME-H-5: Duplicate ID guard — inserting same ID twice must return 0 on second call
// and must NOT create a ghost item that persists in query results.
TEST(duplicate_id_rejected) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000000, 1000000);

    ASSERT(rt_quadtree_insert(tree, 42, 500000, 500000, 10000, 10000) == 1);
    ASSERT(rt_quadtree_insert(tree, 42, 600000, 600000, 10000, 10000) == 0); // rejected
    ASSERT(rt_quadtree_item_count(tree) == 1);                               // still 1

    // After remove, the same ID can be reinserted
    rt_quadtree_remove(tree, 42);
    ASSERT(rt_quadtree_insert(tree, 42, 700000, 700000, 10000, 10000) == 1);

    rt_quadtree_destroy(tree);
}

// GAME-C-3: Truncation flag — dense query capped at RT_QUADTREE_MAX_RESULTS must
// set the truncation flag so callers can detect partial results.
//
// Items are spread on a 17×16 grid with spacing 5500 and half-size 2 so that
// no item straddles any quadrant boundary at any subdivision depth, guaranteeing
// all N items land in the node tree and are reachable by the query.
TEST(query_truncation_detected) {
    const int N = RT_QUADTREE_MAX_RESULTS + 10; // 266 > 256

    // World 100000×100000; grid spacing 5500 starting at offset 2750 keeps
    // every item center away from the binary-subdivision midpoints (50000,
    // 25000, 12500, ...) so get_quadrant() never returns -1 for any item.
    rt_quadtree tree = rt_quadtree_new(0, 0, 100000, 100000);

    for (int i = 0; i < N; i++) {
        int64_t cx = (int64_t)(i % 17) * 5500 + 2750; // 2750..90750
        int64_t cy = (int64_t)(i / 17) * 5500 + 2750; // 2750..85250 (row 15 max)
        rt_quadtree_insert(tree, (int64_t)(i + 1), cx, cy, 4, 4);
    }

    // Query entire world — all N items should be found, but result is capped.
    int64_t count = rt_quadtree_query_rect(tree, 0, 0, 100000, 100000);
    ASSERT(count == RT_QUADTREE_MAX_RESULTS);
    ASSERT(rt_quadtree_query_was_truncated(tree) == 1);

    rt_quadtree_destroy(tree);
}

// GAME-C-3 complement: small result sets must NOT set the truncation flag.
TEST(query_no_truncation_small_result) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 100000, 100000);

    for (int i = 0; i < 10; i++) {
        int64_t cx = (int64_t)(i + 1) * 8000; // well-spaced, no boundary straddling
        rt_quadtree_insert(tree, (int64_t)(i + 1), cx, 50000, 100, 100);
    }

    int64_t count = rt_quadtree_query_rect(tree, 0, 0, 100000, 100000);
    ASSERT(count <= RT_QUADTREE_MAX_RESULTS);
    ASSERT(rt_quadtree_query_was_truncated(tree) == 0);

    rt_quadtree_destroy(tree);
}

TEST(reuses_inactive_slots_after_remove) {
    rt_quadtree tree = rt_quadtree_new(0, 0, 1000, 1000);

    for (int64_t i = 0; i < 5000; i++) {
        ASSERT(rt_quadtree_insert(tree, i + 1, 100, 100, 10, 10) == 1);
        ASSERT(rt_quadtree_remove(tree, i + 1) == 1);
    }

    ASSERT(rt_quadtree_item_count(tree) == 0);
    ASSERT(rt_quadtree_insert(tree, 999999, 100, 100, 10, 10) == 1);

    rt_quadtree_destroy(tree);
}

/// @brief Main.
int main() {
    printf("RTQuadtreeTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(insert);
    RUN_TEST(insert_out_of_bounds);
    RUN_TEST(remove);
    RUN_TEST(query_rect);
    RUN_TEST(query_point);
    RUN_TEST(query_point_excludes_corner_false_positive);
    RUN_TEST(update);
    RUN_TEST(update_out_of_bounds_is_transactional);
    RUN_TEST(clear);
    RUN_TEST(get_pairs);
    RUN_TEST(many_items);
    RUN_TEST(invalid_result_index);
    RUN_TEST(duplicate_id_rejected);
    RUN_TEST(query_truncation_detected);
    RUN_TEST(query_no_truncation_small_result);
    RUN_TEST(reuses_inactive_slots_after_remove);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
