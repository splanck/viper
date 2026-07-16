//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestTilemapAnim.cpp
// Purpose: Tests for tilemap tile-animation state.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
void *rt_tilemap_new(int64_t w, int64_t h, int64_t tw, int64_t th);
void rt_tilemap_set_tile(void *tm, int64_t x, int64_t y, int64_t tile);
void rt_tilemap_set_collision(void *tm, int64_t tile, int64_t coll_type);
int64_t rt_tilemap_get_collision(void *tm, int64_t tile);
void rt_tilemap_set_tile_anim(void *tm, int64_t base, int64_t count, int64_t ms);
void rt_tilemap_set_tile_anim_frame(void *tm, int64_t base, int64_t idx, int64_t tid);
void rt_tilemap_update_anims(void *tm, int64_t dt);
int64_t rt_tilemap_resolve_anim_tile(void *tm, int64_t tile);
int8_t rt_tilemap_is_solid_at(void *tm, int64_t pixel_x, int64_t pixel_y);
}

TEST(TilemapAnim, RegisterAndAdvance) {
    void *tm = rt_tilemap_new(10, 10, 32, 32);
    rt_tilemap_set_tile_anim(tm, 5, 3, 100);           // tile 5 → 3 frames, 100ms each
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 5), 5); // frame 0 = base
    rt_tilemap_update_anims(tm, 100);                  // advance 1 frame
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 5), 6); // frame 1
    rt_tilemap_update_anims(tm, 100);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 5), 7); // frame 2
}

TEST(TilemapAnim, FrameWraps) {
    void *tm = rt_tilemap_new(10, 10, 32, 32);
    rt_tilemap_set_tile_anim(tm, 10, 2, 50);
    rt_tilemap_update_anims(tm, 50); // frame 1
    rt_tilemap_update_anims(tm, 50); // wraps to frame 0
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 10), 10);
}

TEST(TilemapAnim, CustomFrameIds) {
    void *tm = rt_tilemap_new(10, 10, 32, 32);
    rt_tilemap_set_tile_anim(tm, 20, 3, 100);
    rt_tilemap_set_tile_anim_frame(tm, 20, 0, 100);
    rt_tilemap_set_tile_anim_frame(tm, 20, 1, 200);
    rt_tilemap_set_tile_anim_frame(tm, 20, 2, 300);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 20), 100);
    rt_tilemap_update_anims(tm, 100);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 20), 200);
}

TEST(TilemapAnim, NonAnimatedUnchanged) {
    void *tm = rt_tilemap_new(10, 10, 32, 32);
    rt_tilemap_set_tile_anim(tm, 5, 2, 100);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 99), 99); // not animated
}

TEST(TilemapAnim, InvalidFrameDurationIgnored) {
    void *tm = rt_tilemap_new(10, 10, 32, 32);
    rt_tilemap_set_tile_anim(tm, 7, 2, 0);
    rt_tilemap_update_anims(tm, 500);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 7), 7);
}

TEST(TilemapAnim, AnimatedTilesUseBaseCollision) {
    void *tm = rt_tilemap_new(2, 2, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 5);
    rt_tilemap_set_collision(tm, 5, 1);
    rt_tilemap_set_collision(tm, 6, 0);
    rt_tilemap_set_collision(tm, 7, 0);
    rt_tilemap_set_tile_anim(tm, 5, 3, 100);
    rt_tilemap_set_tile_anim_frame(tm, 5, 0, 5);
    rt_tilemap_set_tile_anim_frame(tm, 5, 1, 6);
    rt_tilemap_set_tile_anim_frame(tm, 5, 2, 7);

    EXPECT_EQ(rt_tilemap_is_solid_at(tm, 8, 8), 1);

    rt_tilemap_update_anims(tm, 100);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 5), 6);
    EXPECT_EQ(rt_tilemap_is_solid_at(tm, 8, 8), 1);

    rt_tilemap_update_anims(tm, 100);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 5), 7);
    EXPECT_EQ(rt_tilemap_is_solid_at(tm, 8, 8), 1);
}

TEST(TilemapAnim, TileZeroCollisionIsAlwaysEmpty) {
    void *tm = rt_tilemap_new(1, 1, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 0);
    rt_tilemap_set_collision(tm, 0, 1);

    EXPECT_EQ(rt_tilemap_get_collision(tm, 0), 0);
    EXPECT_EQ(rt_tilemap_is_solid_at(tm, 8, 8), 0);
}

TEST(TilemapAnim, TileZeroCannotBeAnimated) {
    void *tm = rt_tilemap_new(1, 1, 16, 16);
    rt_tilemap_set_tile_anim(tm, 0, 2, 100);
    rt_tilemap_set_tile_anim_frame(tm, 0, 1, 99);
    rt_tilemap_update_anims(tm, 100);

    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, 0), 0);
    EXPECT_EQ(rt_tilemap_resolve_anim_tile(tm, -1), -1);
}

int main() {
    return viper_test::run_all_tests();
}
