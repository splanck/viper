//===----------------------------------------------------------------------===//
// Tests for 2D Raycast + LineOfSight (Plan 05).
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_tilemap.h"
int8_t rt_collision_line_rect(
    double x1, double y1, double x2, double y2, double rx, double ry, double rw, double rh);
int8_t rt_collision_line_circle(
    double x1, double y1, double x2, double y2, double cx, double cy, double r);
int8_t rt_raycast_tilemap(
    void *tilemap, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t *hit_x, int64_t *hit_y);
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

TEST(Raycast, DegenerateLineCircleUsesPointTest) {
    EXPECT_TRUE(rt_collision_line_circle(5, 5, 5, 5, 5, 5, 1));
    EXPECT_FALSE(rt_collision_line_circle(8, 5, 8, 5, 5, 5, 1));
}

TEST(Raycast, TilemapLongRayFindsDistantTile) {
    void *tm = rt_tilemap_new(300, 1, 16, 16);
    rt_tilemap_set_tile(tm, 250, 0, 7);
    rt_tilemap_set_collision(tm, 7, RT_TILE_COLLISION_SOLID);

    int64_t hit_x = -1;
    int64_t hit_y = -1;
    EXPECT_TRUE(rt_raycast_tilemap(tm, 0, 8, 5000, 8, &hit_x, &hit_y));
    EXPECT_TRUE(hit_x >= 250 * 16);
    EXPECT_TRUE(hit_y == 8);
}

int main() {
    return viper_test::run_all_tests();
}
