//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestRaycast2D.cpp
// Purpose: Tests for the 2D raycast runtime.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <limits>

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

TEST(Raycast, LineRectRejectsInvalidInput) {
    EXPECT_FALSE(
        rt_collision_line_rect(std::numeric_limits<double>::infinity(), 0, 10, 10, 0, 0, 10, 10));
    EXPECT_FALSE(rt_collision_line_rect(0, 0, 10, 10, 0, 0, -1, 10));
    EXPECT_FALSE(rt_collision_line_rect(0, 0, 10, 10, 0, 0, 10, -1));
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

TEST(Raycast, TilemapDiagonalCornerChecksSideTouchedTile) {
    void *tm = rt_tilemap_new(2, 2, 10, 10);
    rt_tilemap_set_tile(tm, 1, 0, 7);
    rt_tilemap_set_collision(tm, 7, RT_TILE_COLLISION_SOLID);

    int64_t hit_x = -1;
    int64_t hit_y = -1;
    EXPECT_TRUE(rt_raycast_tilemap(tm, 0, 0, 20, 20, &hit_x, &hit_y));
    EXPECT_TRUE(hit_x >= 10 && hit_x <= 19);
    EXPECT_TRUE(hit_y >= 0 && hit_y <= 9);
}

// VDOC-242: the tile grid is half-open [0, map_w) x [0, map_h). A ray lying on
// the excluded maximum boundary (x == map_w or y == map_h) must not collide with
// the last in-bounds tile. With a fully solid 1x1 map (16x16 px), the interior
// and the inclusive min edges block, while the excluded max edges are clear.
TEST(Raycast, TilemapMaxBoundaryRayIsClear) {
    void *tm = rt_tilemap_new(1, 1, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 7);
    rt_tilemap_set_collision(tm, 7, RT_TILE_COLLISION_SOLID);

    int64_t hx = -1;
    int64_t hy = -1;

    // Interior vertical ray hits the solid tile.
    EXPECT_TRUE(rt_raycast_tilemap(tm, 8, 0, 8, 16, &hx, &hy));

    // Right boundary (x == map_w) is excluded: a vertical ray there is clear.
    EXPECT_FALSE(rt_raycast_tilemap(tm, 16, 0, 16, 16, &hx, &hy));
    // One pixel farther outside is likewise clear (no asymmetric collision).
    EXPECT_FALSE(rt_raycast_tilemap(tm, 17, 0, 17, 16, &hx, &hy));

    // Bottom boundary (y == map_h) is excluded: a horizontal ray there is clear.
    EXPECT_FALSE(rt_raycast_tilemap(tm, 0, 16, 16, 16, &hx, &hy));

    // Left/top min edges are inclusive, so a ray on them still hits.
    EXPECT_TRUE(rt_raycast_tilemap(tm, 0, 0, 0, 16, &hx, &hy));
    EXPECT_TRUE(rt_raycast_tilemap(tm, 0, 0, 16, 0, &hx, &hy));
}

int main() {
    return viper_test::run_all_tests();
}
