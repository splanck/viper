//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3.
// RTPathFollowTests.cpp - Unit tests for rt_pathfollow
//===----------------------------------------------------------------------===//

#include "rt_pathfollow.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
    rt_pathfollow path = rt_pathfollow_new();
    ASSERT(path != NULL);
    ASSERT(rt_pathfollow_point_count(path) == 0);
    ASSERT(rt_pathfollow_is_active(path) == 0);
    rt_pathfollow_destroy(path);
}

TEST(add_points) {
    rt_pathfollow path = rt_pathfollow_new();

    ASSERT(rt_pathfollow_add_point(path, 0, 0) == 1);
    ASSERT(rt_pathfollow_add_point(path, 100000, 0) == 1); // 100 units
    ASSERT(rt_pathfollow_add_point(path, 100000, 100000) == 1);

    ASSERT(rt_pathfollow_point_count(path) == 3);

    rt_pathfollow_destroy(path);
}

TEST(start_stop) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 100000, 0);

    rt_pathfollow_start(path);
    ASSERT(rt_pathfollow_is_active(path) == 1);

    rt_pathfollow_pause(path);
    ASSERT(rt_pathfollow_is_active(path) == 0);

    rt_pathfollow_start(path);
    ASSERT(rt_pathfollow_is_active(path) == 1);

    rt_pathfollow_stop(path);
    ASSERT(rt_pathfollow_is_active(path) == 0);
    ASSERT(rt_pathfollow_get_x(path) == 0); // Reset to start

    rt_pathfollow_destroy(path);
}

TEST(movement) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    /// @brief Rt_pathfollow_add_point.
    rt_pathfollow_add_point(path, 100000, 0); // 100 units right
                                              /// @brief Rt_pathfollow_set_speed.
    rt_pathfollow_set_speed(path, 50000);     // 50 units/sec
    rt_pathfollow_start(path);

    // After 1 second, should have moved 50 units
    rt_pathfollow_update(path, 1000);
    int64_t x = rt_pathfollow_get_x(path);
    ASSERT(x >= 45000 && x <= 55000); // ~50 units

    rt_pathfollow_destroy(path);
}

TEST(once_mode) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    /// @brief Rt_pathfollow_add_point.
    rt_pathfollow_add_point(path, 10000, 0); // Short path
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_ONCE);
    /// @brief Rt_pathfollow_set_speed.
    rt_pathfollow_set_speed(path, 100000); // Fast
    rt_pathfollow_start(path);

    // Run to completion
    for (int i = 0; i < 10; i++) {
        rt_pathfollow_update(path, 100);
    }

    ASSERT(rt_pathfollow_is_finished(path) == 1);
    ASSERT(rt_pathfollow_is_active(path) == 0);

    rt_pathfollow_destroy(path);
}

TEST(loop_mode) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 10000, 0);
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_LOOP);
    rt_pathfollow_set_speed(path, 100000);
    rt_pathfollow_start(path);

    // Run past end
    for (int i = 0; i < 20; i++) {
        rt_pathfollow_update(path, 100);
    }

    // Should still be active (looping)
    ASSERT(rt_pathfollow_is_active(path) == 1);
    ASSERT(rt_pathfollow_is_finished(path) == 0);

    rt_pathfollow_destroy(path);
}

TEST(pingpong_mode) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 10000, 0);
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_PINGPONG);
    rt_pathfollow_set_speed(path, 100000);
    rt_pathfollow_start(path);

    // Run to end
    for (int i = 0; i < 5; i++) {
        rt_pathfollow_update(path, 50);
    }

    // Should be near end, then reversing
    for (int i = 0; i < 10; i++) {
        rt_pathfollow_update(path, 50);
    }

    // Should still be active
    ASSERT(rt_pathfollow_is_active(path) == 1);

    rt_pathfollow_destroy(path);
}

TEST(progress) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 100000, 0);
    rt_pathfollow_set_speed(path, 50000);
    rt_pathfollow_start(path);

    ASSERT(rt_pathfollow_get_progress(path) == 0);

    /// @brief Rt_pathfollow_update.
    rt_pathfollow_update(path, 1000); // Move 50 units
    int64_t progress = rt_pathfollow_get_progress(path);
    ASSERT(progress >= 400 && progress <= 600); // ~500 (50%)

    rt_pathfollow_destroy(path);
}

TEST(set_progress) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 100000, 0);

    /// @brief Rt_pathfollow_set_progress.
    rt_pathfollow_set_progress(path, 500); // 50%
    int64_t x = rt_pathfollow_get_x(path);
    ASSERT(x >= 45000 && x <= 55000); // ~50 units

    rt_pathfollow_destroy(path);
}

TEST(clear) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 100000, 0);
    rt_pathfollow_add_point(path, 100000, 100000);

    rt_pathfollow_clear(path);
    ASSERT(rt_pathfollow_point_count(path) == 0);
    ASSERT(rt_pathfollow_is_active(path) == 0);

    rt_pathfollow_destroy(path);
}

TEST(segment) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 100000, 0);
    rt_pathfollow_add_point(path, 100000, 100000);
    /// @brief Rt_pathfollow_set_speed.
    rt_pathfollow_set_speed(path, 150000); // Fast
    rt_pathfollow_start(path);

    ASSERT(rt_pathfollow_get_segment(path) == 0);

    // Move to second segment
    rt_pathfollow_update(path, 1000);
    ASSERT(rt_pathfollow_get_segment(path) >= 1);

    rt_pathfollow_destroy(path);
}

TEST(restart_after_finish_rewinds_to_start) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 10000, 0);
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_ONCE);
    rt_pathfollow_set_speed(path, 100000);
    rt_pathfollow_start(path);

    rt_pathfollow_update(path, 1000);
    ASSERT(rt_pathfollow_is_finished(path) == 1);
    ASSERT(rt_pathfollow_get_x(path) == 10000);

    rt_pathfollow_start(path);
    ASSERT(rt_pathfollow_is_active(path) == 1);
    ASSERT(rt_pathfollow_is_finished(path) == 0);
    ASSERT(rt_pathfollow_get_x(path) == 0);

    rt_pathfollow_update(path, 500);
    ASSERT(rt_pathfollow_get_x(path) > 0);

    rt_pathfollow_destroy(path);
}

TEST(short_segments_keep_nonzero_length) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 99, 0);
    rt_pathfollow_set_speed(path, 99);
    rt_pathfollow_start(path);

    rt_pathfollow_update(path, 1000);
    ASSERT(rt_pathfollow_get_x(path) == 99);
    ASSERT(rt_pathfollow_get_progress(path) == 1000);
    ASSERT(rt_pathfollow_is_finished(path) == 1);

    rt_pathfollow_destroy(path);
}

TEST(loop_mode_skips_zero_length_segments) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 1000, 0);
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_LOOP);
    rt_pathfollow_set_speed(path, 500);
    rt_pathfollow_start(path);

    rt_pathfollow_update(path, 1000);
    ASSERT(rt_pathfollow_is_active(path) == 1);
    ASSERT(rt_pathfollow_get_x(path) >= 450 && rt_pathfollow_get_x(path) <= 550);

    rt_pathfollow_destroy(path);
}

TEST(pingpong_mode_skips_zero_length_end_segments) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 1000, 0);
    rt_pathfollow_add_point(path, 1000, 0);
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_PINGPONG);
    rt_pathfollow_set_speed(path, 1500);
    rt_pathfollow_start(path);

    rt_pathfollow_update(path, 1000);
    ASSERT(rt_pathfollow_is_active(path) == 1);
    ASSERT(rt_pathfollow_get_x(path) >= 450 && rt_pathfollow_get_x(path) <= 550);

    rt_pathfollow_destroy(path);
}

TEST(extreme_coordinates_keep_direction_and_progress_safe) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, INT64_MIN + 1000, INT64_MIN + 1000);
    rt_pathfollow_add_point(path, INT64_MAX, INT64_MAX);

    rt_pathfollow_set_progress(path, 500);
    int64_t progress = rt_pathfollow_get_progress(path);
    ASSERT(progress > 0);
    ASSERT(progress < 1000);
    ASSERT(rt_pathfollow_get_angle(path) == 45000);

    rt_pathfollow_destroy(path);
}

// VDOC-280: a slow follower must eventually move instead of truncating every
// frame's sub-unit distance to zero and stalling forever. At speed 1, Update(100)
// is 0.1 units/frame — the old code truncated move_dist to 0 every call and X
// stayed 0 permanently. The carried remainder now accumulates: ten frames make a
// unit, which advances a 1000-unit segment by one per-mille of position.
TEST(slow_movement_accumulates_and_does_not_stall) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 1000, 0);
    rt_pathfollow_set_speed(path, 1); // the finding's probe speed (0.001 units/sec)
    rt_pathfollow_start(path);

    for (int i = 0; i < 100; i++)
        rt_pathfollow_update(path, 100); // 100 * 0.1 units = 10 units of travel

    ASSERT(rt_pathfollow_get_x(path) > 0); // moved, not stalled at 0
    ASSERT(rt_pathfollow_is_active(path) == 1);
    ASSERT(rt_pathfollow_is_finished(path) == 0);

    rt_pathfollow_destroy(path);
}

// VDOC-281: a degenerate zero-length path (all waypoints coincide) must not stay
// active and unfinished forever — a once-mode loop waiting on IsFinished would
// hang. It completes on the first update instead.
TEST(degenerate_zero_length_path_completes_immediately) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 500, 500);
    rt_pathfollow_add_point(path, 500, 500); // identical → zero total length
    rt_pathfollow_start(path);
    ASSERT(rt_pathfollow_is_active(path) == 1); // Start activates a >=2-point path

    rt_pathfollow_update(path, 100);
    ASSERT(rt_pathfollow_is_active(path) == 0);   // no longer stuck active
    ASSERT(rt_pathfollow_is_finished(path) == 1); // once-mode reports completion

    rt_pathfollow_destroy(path);
}

// VDOC-281: a degenerate LOOP path deactivates (nothing to traverse) but keeps
// IsFinished false, honoring the "looping paths never finish" contract.
TEST(degenerate_loop_path_deactivates_without_finishing) {
    rt_pathfollow path = rt_pathfollow_new();
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_add_point(path, 0, 0);
    rt_pathfollow_set_mode(path, RT_PATHFOLLOW_LOOP);
    rt_pathfollow_start(path);

    rt_pathfollow_update(path, 100);
    ASSERT(rt_pathfollow_is_active(path) == 0);
    ASSERT(rt_pathfollow_is_finished(path) == 0);

    rt_pathfollow_destroy(path);
}

/// @brief Main.
int main() {
    printf("RTPathFollowTests:\n");
    RUN_TEST(create_destroy);
    RUN_TEST(add_points);
    RUN_TEST(start_stop);
    RUN_TEST(movement);
    RUN_TEST(once_mode);
    RUN_TEST(loop_mode);
    RUN_TEST(pingpong_mode);
    RUN_TEST(progress);
    RUN_TEST(set_progress);
    RUN_TEST(clear);
    RUN_TEST(segment);
    RUN_TEST(restart_after_finish_rewinds_to_start);
    RUN_TEST(short_segments_keep_nonzero_length);
    RUN_TEST(loop_mode_skips_zero_length_segments);
    RUN_TEST(pingpong_mode_skips_zero_length_end_segments);
    RUN_TEST(extreme_coordinates_keep_direction_and_progress_safe);
    RUN_TEST(slow_movement_accumulates_and_does_not_stall);
    RUN_TEST(degenerate_zero_length_path_completes_immediately);
    RUN_TEST(degenerate_loop_path_deactivates_without_finishing);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
