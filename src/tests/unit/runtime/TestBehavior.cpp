//===----------------------------------------------------------------------===//
// Tests for Behavior composition system (Plan 06).
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_behavior.h"
#include "rt_entity.h"
}

TEST(Behavior, PatrolMovesEntity) {
    void *e = rt_entity_new(10000, 10000, 16, 16);
    void *b = rt_behavior_new();
    rt_behavior_add_patrol(b, 200);
    rt_entity_set_dir(e, 1); // face right
    rt_behavior_update(b, e, nullptr, 0, 0, 16);
    EXPECT_TRUE(rt_entity_get_x(e) > 10000); // moved right
}

TEST(Behavior, GravityApplied) {
    void *e = rt_entity_new(10000, 10000, 16, 16);
    rt_entity_set_vy(e, 0);
    void *b = rt_behavior_new();
    rt_behavior_add_gravity(b, 78, 1350);
    for (int i = 0; i < 5; i++)
        rt_behavior_update(b, e, nullptr, 0, 0, 16);
    EXPECT_TRUE(rt_entity_get_vy(e) > 0); // falling
}

TEST(Behavior, ShootCooldown) {
    void *e = rt_entity_new(10000, 10000, 16, 16);
    void *b = rt_behavior_new();
    rt_behavior_add_shoot(b, 100); // 100ms cooldown
    // Should not be ready immediately
    rt_behavior_update(b, e, nullptr, 0, 0, 16);
    EXPECT_FALSE(rt_behavior_shoot_ready(b));
    // After enough time, should be ready
    for (int i = 0; i < 10; i++)
        rt_behavior_update(b, e, nullptr, 0, 0, 16);
    EXPECT_TRUE(rt_behavior_shoot_ready(b));
    // Should auto-clear
    EXPECT_FALSE(rt_behavior_shoot_ready(b));
}

TEST(Behavior, AnimLoopAdvances) {
    void *e = rt_entity_new(10000, 10000, 16, 16);
    void *b = rt_behavior_new();
    rt_behavior_add_anim_loop(b, 4, 50); // 4 frames, 50ms each
    EXPECT_EQ(rt_behavior_anim_frame(b), 0);
    rt_behavior_update(b, e, nullptr, 0, 0, 50);
    EXPECT_EQ(rt_behavior_anim_frame(b), 1);
    rt_behavior_update(b, e, nullptr, 0, 0, 50);
    EXPECT_EQ(rt_behavior_anim_frame(b), 2);
}

TEST(Behavior, CombinedPatrolGravity) {
    void *e = rt_entity_new(10000, 10000, 16, 16);
    rt_entity_set_dir(e, 1);
    void *b = rt_behavior_new();
    rt_behavior_add_patrol(b, 200);
    rt_behavior_add_gravity(b, 78, 1350);
    rt_behavior_update(b, e, nullptr, 0, 0, 16);
    // Should have moved right AND fallen
    EXPECT_TRUE(rt_entity_get_x(e) > 10000);
    EXPECT_TRUE(rt_entity_get_y(e) > 10000);
}

TEST(Behavior, NullSafe) {
    rt_behavior_update(nullptr, nullptr, nullptr, 0, 0, 16);
    EXPECT_FALSE(rt_behavior_shoot_ready(nullptr));
    EXPECT_EQ(rt_behavior_anim_frame(nullptr), 0);
}

int main() {
    return viper_test::run_all_tests();
}
