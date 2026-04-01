//===----------------------------------------------------------------------===//
// Tests for 2D Raycast + LineOfSight (Plan 05).
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
int8_t rt_collision_line_rect(double x1, double y1, double x2, double y2,
                              double rx, double ry, double rw, double rh);
int8_t rt_collision_line_circle(double x1, double y1, double x2, double y2,
                                double cx, double cy, double r);
}

TEST(Raycast, LineRectHit) {
    EXPECT_TRUE(rt_collision_line_rect(0, 5, 20, 5, 5, 0, 10, 10));
}

TEST(Raycast, LineRectMiss) {
    EXPECT_FALSE(rt_collision_line_rect(0, 0, 10, 0, 20, 20, 5, 5));
}

TEST(Raycast, LineCircleHit) {
    EXPECT_TRUE(rt_collision_line_circle(0, 5, 20, 5, 10, 5, 3));
}

TEST(Raycast, LineCircleMiss) {
    EXPECT_FALSE(rt_collision_line_circle(0, 0, 10, 0, 5, 20, 3));
}

TEST(Raycast, LineRectEdge) {
    // Line along rect boundary
    EXPECT_TRUE(rt_collision_line_rect(0, 0, 10, 0, 0, 0, 10, 10));
}

TEST(Raycast, LineCircleTangent) {
    // Line passes at distance exactly r — should be tangent (borderline)
    EXPECT_TRUE(rt_collision_line_circle(0, 3, 10, 3, 5, 0, 3));
}

int main() { return viper_test::run_all_tests(); }
