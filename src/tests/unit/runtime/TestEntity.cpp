//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestEntity.cpp
// Purpose: Tests for Viper.Game.Entity — physics, collision flags, helpers.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_entity.h"
}

TEST(Entity, CreateAndGetPos) {
    void *e = rt_entity_new(500, 300, 16, 24);
    ASSERT_TRUE(e != nullptr);
    EXPECT_EQ(rt_entity_get_x(e), 500);
    EXPECT_EQ(rt_entity_get_y(e), 300);
    EXPECT_EQ(rt_entity_get_width(e), 16);
    EXPECT_EQ(rt_entity_get_height(e), 24);
    EXPECT_EQ(rt_entity_get_dir(e), 1);
    EXPECT_TRUE(rt_entity_get_active(e));
}

TEST(Entity, SetVelocity) {
    void *e = rt_entity_new(0, 0, 10, 10);
    rt_entity_set_vx(e, 100);
    rt_entity_set_vy(e, -50);
    EXPECT_EQ(rt_entity_get_vx(e), 100);
    EXPECT_EQ(rt_entity_get_vy(e), -50);
}

TEST(Entity, ApplyGravityCapsMaxFall) {
    void *e = rt_entity_new(0, 0, 10, 10);
    rt_entity_set_vy(e, 0);
    // Apply gravity=78, max_fall=1350, dt=16
    for (int i = 0; i < 100; i++)
        rt_entity_apply_gravity(e, 78, 1350, 16);
    EXPECT_EQ(rt_entity_get_vy(e), 1350); // Capped at max_fall
}

TEST(Entity, MoveWithoutTilemap) {
    void *e = rt_entity_new(10000, 20000, 16, 16); // (100, 200) in pixels
    rt_entity_set_vx(e, 500);                      // 5 px/frame in centipixels
    rt_entity_set_vy(e, -200);                     // -2 px/frame
    rt_entity_move_and_collide(e, nullptr, 16);    // dt = DT_BASE
    EXPECT_EQ(rt_entity_get_x(e), 10000 + 500);    // moved by vx
    EXPECT_EQ(rt_entity_get_y(e), 20000 - 200);    // moved by vy
}

TEST(Entity, HPProperties) {
    void *e = rt_entity_new(0, 0, 10, 10);
    rt_entity_set_hp(e, 5);
    rt_entity_set_max_hp(e, 10);
    EXPECT_EQ(rt_entity_get_hp(e), 5);
    EXPECT_EQ(rt_entity_get_max_hp(e), 10);
}

TEST(Entity, TypeAndActive) {
    void *e = rt_entity_new(0, 0, 10, 10);
    rt_entity_set_type(e, 42);
    EXPECT_EQ(rt_entity_get_type(e), 42);
    rt_entity_set_active(e, 0);
    EXPECT_FALSE(rt_entity_get_active(e));
}

TEST(Entity, OverlapsAABB) {
    void *a = rt_entity_new(0, 0, 10, 10);     // (0,0) in pixels
    void *b = rt_entity_new(500, 500, 10, 10); // (5,5) in pixels — overlaps
    EXPECT_TRUE(rt_entity_overlaps(a, b));

    void *c = rt_entity_new(2000, 2000, 10, 10); // (20,20) — no overlap
    EXPECT_FALSE(rt_entity_overlaps(a, c));
}

TEST(Entity, NullSafe) {
    EXPECT_EQ(rt_entity_get_x(nullptr), 0);
    EXPECT_EQ(rt_entity_get_vy(nullptr), 0);
    EXPECT_FALSE(rt_entity_on_ground(nullptr));
    rt_entity_apply_gravity(nullptr, 78, 1350, 16);   // no crash
    rt_entity_move_and_collide(nullptr, nullptr, 16); // no crash
    EXPECT_FALSE(rt_entity_overlaps(nullptr, nullptr));
}

int main() {
    return viper_test::run_all_tests();
}
