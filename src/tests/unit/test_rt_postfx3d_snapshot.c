#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_postfx3d.h"

#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

static void test_snapshot_includes_advanced_effects(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_snapshot_t snapshot;

    rt_postfx3d_add_ssao(fx, 0.8, 1.5, 6);
    rt_postfx3d_add_dof(fx, 12.0, 2.5, 4.0);
    rt_postfx3d_add_motion_blur(fx, 0.7, 5);

    EXPECT_TRUE(vgfx3d_postfx_get_snapshot(fx, &snapshot) == 1,
                "Snapshot export succeeds when advanced effects are present");
    EXPECT_TRUE(snapshot.ssao_enabled == 1, "Snapshot includes SSAO enable flag");
    EXPECT_TRUE(snapshot.ssao_radius == 0.8f && snapshot.ssao_intensity == 1.5f &&
                    snapshot.ssao_samples == 6,
                "Snapshot includes SSAO parameters");
    EXPECT_TRUE(snapshot.dof_enabled == 1, "Snapshot includes DOF enable flag");
    EXPECT_TRUE(snapshot.dof_focus_distance == 12.0f && snapshot.dof_aperture == 2.5f &&
                    snapshot.dof_max_blur == 4.0f,
                "Snapshot includes DOF parameters");
    EXPECT_TRUE(snapshot.motion_blur_enabled == 1,
                "Snapshot includes motion-blur enable flag");
    EXPECT_TRUE(snapshot.motion_blur_intensity == 0.7f && snapshot.motion_blur_samples == 5,
                "Snapshot includes motion-blur parameters");
}

static void test_snapshot_disabled_returns_zero(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_snapshot_t snapshot;

    rt_postfx3d_add_ssao(fx, 1.0, 1.0, 4);
    rt_postfx3d_set_enabled(fx, 0);

    EXPECT_TRUE(vgfx3d_postfx_get_snapshot(fx, &snapshot) == 0,
                "Snapshot export returns zero when PostFX is disabled");
}

int main(void) {
    test_snapshot_includes_advanced_effects();
    test_snapshot_disabled_returns_zero();

    printf("rt_postfx3d snapshot tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
