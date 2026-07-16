//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestLighting2D.cpp
// Purpose: Tests for the 2D lighting runtime.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/internals/codemap.md
//
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

// VDOC-271: a base radius of zero disables the player light. SetPlayerLight maps
// non-positive radii to the zero sentinel, and Draw now skips every player-light
// pass (including the +40 outer glow) when the base radius is zero. PlayerRadius
// exposes the sentinel so the disabled state is observable.
TEST(Lighting2D, ZeroPlayerRadiusDisablesPlayerLight) {
    rt_lighting2d lit = rt_lighting2d_new(4);

    // Default constructor gives a lit player light.
    EXPECT_EQ(rt_lighting2d_get_player_radius(lit), 180);

    // Zero and negative radii both collapse to the disabled sentinel.
    rt_lighting2d_set_player_light(lit, 0, 0x808080);
    EXPECT_EQ(rt_lighting2d_get_player_radius(lit), 0);
    rt_lighting2d_set_player_light(lit, -50, 0x808080);
    EXPECT_EQ(rt_lighting2d_get_player_radius(lit), 0);

    // A positive radius re-enables it.
    rt_lighting2d_set_player_light(lit, 150, 0x808080);
    EXPECT_EQ(rt_lighting2d_get_player_radius(lit), 150);

    rt_lighting2d_destroy(lit);
}

int main() {
    return viper_test::run_all_tests();
}
