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
    EXPECT_TRUE(snapshot.motion_blur_enabled == 1, "Snapshot includes motion-blur enable flag");
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

static void test_snapshot_preserves_documented_tonemap_and_grade_params(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_snapshot_t snapshot;

    rt_postfx3d_add_tonemap(fx, 0, 1.0);
    rt_postfx3d_add_color_grade(fx, 0.02, 1.08, 1.04);

    EXPECT_TRUE(vgfx3d_postfx_get_snapshot(fx, &snapshot) == 1,
                "Snapshot export succeeds for tonemap and color-grade settings");
    EXPECT_TRUE(snapshot.tonemap_mode == 0 && snapshot.tonemap_exposure == 1.0f,
                "Snapshot preserves tonemap mode 0 as disabled");
    EXPECT_TRUE(snapshot.color_grade_enabled == 1, "Snapshot includes color-grade enable flag");
    EXPECT_TRUE(snapshot.cg_brightness == 0.02f && snapshot.cg_contrast == 1.08f &&
                    snapshot.cg_saturation == 1.04f,
                "Snapshot preserves additive color-grade parameters");
}

static void test_effect_chain_grows_past_legacy_cap(void) {
    void *fx = rt_postfx3d_new();

    for (int i = 0; i < 12; i++)
        rt_postfx3d_add_fxaa(fx);

    EXPECT_TRUE(rt_postfx3d_get_effect_count(fx) == 12,
                "PostFX3D preserves effects appended past the old 8-entry cap");
}

static void test_chain_export_preserves_effect_order_and_duplicates(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_chain_t chain = {0};

    rt_postfx3d_add_bloom(fx, 0.8, 1.5, 2);
    rt_postfx3d_add_vignette(fx, 0.6, 0.25);
    rt_postfx3d_add_bloom(fx, 0.9, 0.4, 1);

    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1,
                "Chain export succeeds for ordered GPU postfx data");
    EXPECT_TRUE(chain.enabled == 1 && chain.effect_count == 3,
                "Chain export preserves every enabled effect entry");
    EXPECT_TRUE(chain.effects[0].type == VGFX3D_POSTFX_EFFECT_BLOOM &&
                    chain.effects[0].snapshot.bloom_threshold == 0.8f &&
                    chain.effects[0].snapshot.bloom_intensity == 1.5f,
                "Chain export preserves the first bloom pass");
    EXPECT_TRUE(chain.effects[1].type == VGFX3D_POSTFX_EFFECT_VIGNETTE &&
                    chain.effects[1].snapshot.vignette_enabled == 1 &&
                    chain.effects[1].snapshot.vignette_radius == 0.6f,
                "Chain export preserves middle-pass ordering");
    EXPECT_TRUE(chain.effects[2].type == VGFX3D_POSTFX_EFFECT_BLOOM &&
                    chain.effects[2].snapshot.bloom_threshold == 0.9f &&
                    chain.effects[2].snapshot.bloom_intensity == 0.4f,
                "Chain export preserves duplicate effect types as separate ordered passes");

    vgfx3d_postfx_chain_free(&chain);
}

int main(void) {
    test_snapshot_includes_advanced_effects();
    test_snapshot_disabled_returns_zero();
    test_snapshot_preserves_documented_tonemap_and_grade_params();
    test_effect_chain_grows_past_legacy_cap();
    test_chain_export_preserves_effect_order_and_duplicates();

    printf("rt_postfx3d snapshot tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
