//===----------------------------------------------------------------------===//
// Tests for Viper.Game.Lighting2D edge cases.
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_lighting2d.h"
}

TEST(Lighting2D, ClampsMaxLightsAndKeepsPermanentLights) {
    rt_lighting2d lit = rt_lighting2d_new(200);

    for (int i = 0; i < 140; i++)
        rt_lighting2d_add_light(lit, i, 0, 5, 0x12345678, 0);

    EXPECT_EQ(rt_lighting2d_get_light_count(lit), 128);
    for (int i = 0; i < 10; i++)
        rt_lighting2d_update(lit);
    EXPECT_EQ(rt_lighting2d_get_light_count(lit), 128);

    rt_lighting2d_destroy(lit);
}

TEST(Lighting2D, ClampsStyleInputsAndRejectsInvalidRadii) {
    rt_lighting2d lit = rt_lighting2d_new(4);

    rt_lighting2d_set_darkness(lit, -20);
    EXPECT_EQ(rt_lighting2d_get_darkness(lit), 0);
    rt_lighting2d_set_darkness(lit, 999);
    EXPECT_EQ(rt_lighting2d_get_darkness(lit), 255);

    rt_lighting2d_set_tint_color(lit, 0x12345678);
    EXPECT_EQ(rt_lighting2d_get_tint_color(lit), 0x345678);

    rt_lighting2d_add_light(lit, 0, 0, 0, 0xFFFFFF, 1);
    rt_lighting2d_add_light(lit, 0, 0, -1, 0xFFFFFF, 1);
    EXPECT_EQ(rt_lighting2d_get_light_count(lit), 0);

    rt_lighting2d_add_light(lit, 0, 0, 4, 0xFFFFFF, 1);
    EXPECT_EQ(rt_lighting2d_get_light_count(lit), 1);
    rt_lighting2d_update(lit);
    EXPECT_EQ(rt_lighting2d_get_light_count(lit), 0);

    rt_lighting2d_destroy(lit);
}

int main() {
    return viper_test::run_all_tests();
}
