//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// RTCollisionTests.cpp - Unit tests for rt_collision (rect and static helpers)
//===----------------------------------------------------------------------===//

#include "rt_collision.h"
#include <cassert>
#include <cstdio>
#include <cmath>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("  %s...", #name); \
    test_##name(); \
    printf(" OK\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabs((a) - (b)) < (eps))

// ============ CollisionRect tests ============

TEST(rect_create_destroy) {
    rt_collision_rect r = rt_collision_rect_new(10.0, 20.0, 100.0, 50.0);
    ASSERT(r != NULL);
    ASSERT_NEAR(rt_collision_rect_x(r), 10.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_y(r), 20.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_width(r), 100.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_height(r), 50.0, 0.001);
    rt_collision_rect_destroy(r);
}

TEST(rect_right_bottom) {
    rt_collision_rect r = rt_collision_rect_new(10.0, 20.0, 100.0, 50.0);
    ASSERT_NEAR(rt_collision_rect_right(r), 110.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_bottom(r), 70.0, 0.001);
    rt_collision_rect_destroy(r);
}

TEST(rect_center) {
    rt_collision_rect r = rt_collision_rect_new(0.0, 0.0, 100.0, 100.0);
    ASSERT_NEAR(rt_collision_rect_center_x(r), 50.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_center_y(r), 50.0, 0.001);
    rt_collision_rect_destroy(r);
}

TEST(rect_set_position) {
    rt_collision_rect r = rt_collision_rect_new(0.0, 0.0, 50.0, 50.0);
    rt_collision_rect_set_position(r, 100.0, 200.0);
    ASSERT_NEAR(rt_collision_rect_x(r), 100.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_y(r), 200.0, 0.001);
    rt_collision_rect_destroy(r);
}

TEST(rect_set_center) {
    rt_collision_rect r = rt_collision_rect_new(0.0, 0.0, 100.0, 100.0);
    rt_collision_rect_set_center(r, 200.0, 200.0);
    ASSERT_NEAR(rt_collision_rect_x(r), 150.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_y(r), 150.0, 0.001);
    rt_collision_rect_destroy(r);
}

TEST(rect_move) {
    rt_collision_rect r = rt_collision_rect_new(50.0, 50.0, 10.0, 10.0);
    rt_collision_rect_move(r, 10.0, -5.0);
    ASSERT_NEAR(rt_collision_rect_x(r), 60.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_y(r), 45.0, 0.001);
    rt_collision_rect_destroy(r);
}

TEST(rect_contains_point) {
    rt_collision_rect r = rt_collision_rect_new(0.0, 0.0, 100.0, 100.0);
    ASSERT(rt_collision_rect_contains_point(r, 50.0, 50.0) == 1);
    ASSERT(rt_collision_rect_contains_point(r, 0.0, 0.0) == 1);
    ASSERT(rt_collision_rect_contains_point(r, 99.0, 99.0) == 1);
    ASSERT(rt_collision_rect_contains_point(r, 100.0, 100.0) == 0);
    ASSERT(rt_collision_rect_contains_point(r, -1.0, 50.0) == 0);
    rt_collision_rect_destroy(r);
}

TEST(rect_overlaps) {
    rt_collision_rect r1 = rt_collision_rect_new(0.0, 0.0, 100.0, 100.0);
    rt_collision_rect r2 = rt_collision_rect_new(50.0, 50.0, 100.0, 100.0);
    rt_collision_rect r3 = rt_collision_rect_new(200.0, 200.0, 50.0, 50.0);

    ASSERT(rt_collision_rect_overlaps(r1, r2) == 1);
    ASSERT(rt_collision_rect_overlaps(r1, r3) == 0);
    rt_collision_rect_destroy(r1);
    rt_collision_rect_destroy(r2);
    rt_collision_rect_destroy(r3);
}

TEST(rect_overlap_depth) {
    rt_collision_rect r1 = rt_collision_rect_new(0.0, 0.0, 100.0, 100.0);
    rt_collision_rect r2 = rt_collision_rect_new(80.0, 70.0, 100.0, 100.0);

    double ox = rt_collision_rect_overlap_x(r1, r2);
    double oy = rt_collision_rect_overlap_y(r1, r2);
    ASSERT_NEAR(ox, 20.0, 0.001);  // 100 - 80
    ASSERT_NEAR(oy, 30.0, 0.001);  // 100 - 70
    rt_collision_rect_destroy(r1);
    rt_collision_rect_destroy(r2);
}

TEST(rect_expand) {
    rt_collision_rect r = rt_collision_rect_new(50.0, 50.0, 100.0, 100.0);
    rt_collision_rect_expand(r, 10.0);
    ASSERT_NEAR(rt_collision_rect_x(r), 40.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_y(r), 40.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_width(r), 120.0, 0.001);
    ASSERT_NEAR(rt_collision_rect_height(r), 120.0, 0.001);
    rt_collision_rect_destroy(r);
}

// ============ Static collision helper tests ============

TEST(rects_overlap) {
    ASSERT(rt_collision_rects_overlap(0, 0, 100, 100, 50, 50, 100, 100) == 1);
    ASSERT(rt_collision_rects_overlap(0, 0, 100, 100, 200, 200, 50, 50) == 0);
    ASSERT(rt_collision_rects_overlap(0, 0, 100, 100, 100, 0, 100, 100) == 0); // Edge touch
}

TEST(point_in_rect) {
    ASSERT(rt_collision_point_in_rect(50, 50, 0, 0, 100, 100) == 1);
    ASSERT(rt_collision_point_in_rect(150, 50, 0, 0, 100, 100) == 0);
}

TEST(circles_overlap) {
    ASSERT(rt_collision_circles_overlap(0, 0, 50, 75, 0, 50) == 1);  // Touching
    ASSERT(rt_collision_circles_overlap(0, 0, 50, 200, 0, 50) == 0); // Far apart
}

TEST(point_in_circle) {
    ASSERT(rt_collision_point_in_circle(50, 50, 50, 50, 10) == 1);  // Center
    ASSERT(rt_collision_point_in_circle(50, 50, 50, 50, 1) == 1);   // Center
    ASSERT(rt_collision_point_in_circle(100, 50, 50, 50, 10) == 0); // Outside
}

TEST(circle_rect) {
    // Circle at (50,50) radius 30, rect at (60,60) 40x40
    ASSERT(rt_collision_circle_rect(50, 50, 30, 60, 60, 40, 40) == 1);
    // Circle far from rect
    ASSERT(rt_collision_circle_rect(0, 0, 10, 100, 100, 20, 20) == 0);
}

TEST(distance) {
    ASSERT_NEAR(rt_collision_distance(0, 0, 3, 4), 5.0, 0.001);
    ASSERT_NEAR(rt_collision_distance(0, 0, 0, 0), 0.0, 0.001);
}

TEST(distance_squared) {
    ASSERT_NEAR(rt_collision_distance_squared(0, 0, 3, 4), 25.0, 0.001);
}

int main() {
    printf("RTCollisionTests:\n");

    printf(" CollisionRect:\n");
    RUN_TEST(rect_create_destroy);
    RUN_TEST(rect_right_bottom);
    RUN_TEST(rect_center);
    RUN_TEST(rect_set_position);
    RUN_TEST(rect_set_center);
    RUN_TEST(rect_move);
    RUN_TEST(rect_contains_point);
    RUN_TEST(rect_overlaps);
    RUN_TEST(rect_overlap_depth);
    RUN_TEST(rect_expand);

    printf(" Static Collision Helpers:\n");
    RUN_TEST(rects_overlap);
    RUN_TEST(point_in_rect);
    RUN_TEST(circles_overlap);
    RUN_TEST(point_in_circle);
    RUN_TEST(circle_rect);
    RUN_TEST(distance);
    RUN_TEST(distance_squared);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
