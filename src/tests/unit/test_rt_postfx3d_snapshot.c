//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_postfx3d_snapshot.c
// Purpose: Snapshot tests for the 3D post-processing pipeline output.
// Key invariants:
//   - Standalone translation unit; no cross-layer dependencies.
// Ownership/Lifetime:
//   - No long-lived state; all allocations are scoped to the run.
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_postfx3d.h"

#include <math.h>
#include <stdint.h>
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

#define EXPECT_NEARF(a, b, eps, msg) EXPECT_TRUE(fabsf((float)(a) - (float)(b)) <= (eps), msg)

static int snapshot_float_fields_are_finite(const vgfx3d_postfx_snapshot_t *snapshot) {
    return snapshot && isfinite(snapshot->bloom_threshold) && isfinite(snapshot->bloom_intensity) &&
           isfinite(snapshot->tonemap_exposure) && isfinite(snapshot->cg_brightness) &&
           isfinite(snapshot->cg_contrast) && isfinite(snapshot->cg_saturation) &&
           isfinite(snapshot->vignette_radius) && isfinite(snapshot->vignette_softness) &&
           isfinite(snapshot->ssao_radius) && isfinite(snapshot->ssao_intensity) &&
           isfinite(snapshot->dof_focus_distance) && isfinite(snapshot->dof_aperture) &&
           isfinite(snapshot->dof_max_blur) && isfinite(snapshot->motion_blur_intensity);
}

typedef struct {
    void *vptr;
    void *effects;
    int32_t effect_count;
    int32_t effect_capacity;
    int8_t enabled;
} PostFX3DTestLayout;

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
                    chain.effects[0].snapshot.bloom_intensity == 1.5f &&
                    chain.effects[0].snapshot.bloom_passes == 2,
                "Chain export preserves the first bloom pass");
    EXPECT_TRUE(chain.effects[1].type == VGFX3D_POSTFX_EFFECT_VIGNETTE &&
                    chain.effects[1].snapshot.vignette_enabled == 1 &&
                    chain.effects[1].snapshot.vignette_radius == 0.6f,
                "Chain export preserves middle-pass ordering");
    EXPECT_TRUE(chain.effects[2].type == VGFX3D_POSTFX_EFFECT_BLOOM &&
                    chain.effects[2].snapshot.bloom_threshold == 0.9f &&
                    chain.effects[2].snapshot.bloom_intensity == 0.4f &&
                    chain.effects[2].snapshot.bloom_passes == 1,
                "Chain export preserves duplicate effect types as separate ordered passes");

    vgfx3d_postfx_chain_free(&chain);
}

static void test_effect_parameters_are_sanitized_for_backend_chain(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_chain_t chain = {0};

    rt_postfx3d_add_bloom(fx, NAN, INFINITY, 999);
    rt_postfx3d_add_tonemap(fx, 999, NAN);
    rt_postfx3d_add_color_grade(fx, INFINITY, NAN, -INFINITY);
    rt_postfx3d_add_vignette(fx, NAN, -1.0);
    rt_postfx3d_add_ssao(fx, NAN, INFINITY, 999);
    rt_postfx3d_add_dof(fx, NAN, INFINITY, 9999.0);
    rt_postfx3d_add_motion_blur(fx, INFINITY, 999);

    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1,
                "Chain export succeeds for sanitized parameters");
    EXPECT_TRUE(chain.effect_count == 7, "Chain export preserves sanitized effects");

    EXPECT_TRUE(chain.effects[0].type == VGFX3D_POSTFX_EFFECT_BLOOM,
                "Sanitized chain stores bloom first");
    EXPECT_NEARF(chain.effects[0].snapshot.bloom_threshold,
                 0.8f,
                 0.0001f,
                 "Bloom threshold falls back from NaN");
    EXPECT_NEARF(chain.effects[0].snapshot.bloom_intensity,
                 1.0f,
                 0.0001f,
                 "Bloom intensity falls back from infinity");
    EXPECT_TRUE(chain.effects[0].snapshot.bloom_passes == 32,
                "Bloom blur passes clamp to the quality cap");

    EXPECT_TRUE(chain.effects[1].type == VGFX3D_POSTFX_EFFECT_TONEMAP,
                "Sanitized chain stores tonemap second");
    EXPECT_TRUE(chain.effects[1].snapshot.tonemap_mode == 2,
                "Tonemap mode clamps to the highest supported mode");
    EXPECT_NEARF(chain.effects[1].snapshot.tonemap_exposure,
                 1.0f,
                 0.0001f,
                 "Tonemap exposure falls back from NaN");

    EXPECT_TRUE(chain.effects[2].type == VGFX3D_POSTFX_EFFECT_COLOR_GRADE,
                "Sanitized chain stores color grade third");
    EXPECT_NEARF(chain.effects[2].snapshot.cg_brightness,
                 0.0f,
                 0.0001f,
                 "Color grade brightness falls back from infinity");
    EXPECT_NEARF(chain.effects[2].snapshot.cg_contrast,
                 1.0f,
                 0.0001f,
                 "Color grade contrast falls back from NaN");
    EXPECT_NEARF(chain.effects[2].snapshot.cg_saturation,
                 1.0f,
                 0.0001f,
                 "Color grade saturation falls back from infinity");

    EXPECT_TRUE(chain.effects[3].type == VGFX3D_POSTFX_EFFECT_VIGNETTE,
                "Sanitized chain stores vignette fourth");
    EXPECT_NEARF(chain.effects[3].snapshot.vignette_radius,
                 0.7f,
                 0.0001f,
                 "Vignette radius falls back from NaN");
    EXPECT_NEARF(chain.effects[3].snapshot.vignette_softness,
                 0.001f,
                 0.0001f,
                 "Vignette softness clamps to a non-zero floor");

    EXPECT_TRUE(chain.effects[4].type == VGFX3D_POSTFX_EFFECT_SSAO,
                "Sanitized chain stores SSAO fifth");
    EXPECT_NEARF(
        chain.effects[4].snapshot.ssao_radius, 0.5f, 0.0001f, "SSAO radius falls back from NaN");
    EXPECT_NEARF(chain.effects[4].snapshot.ssao_intensity,
                 1.0f,
                 0.0001f,
                 "SSAO intensity falls back from infinity");
    EXPECT_TRUE(chain.effects[4].snapshot.ssao_samples == 128,
                "SSAO samples clamp to the quality cap");

    EXPECT_TRUE(chain.effects[5].type == VGFX3D_POSTFX_EFFECT_DOF,
                "Sanitized chain stores DOF sixth");
    EXPECT_NEARF(chain.effects[5].snapshot.dof_focus_distance,
                 10.0f,
                 0.0001f,
                 "DOF focus distance falls back from NaN");
    EXPECT_NEARF(chain.effects[5].snapshot.dof_aperture,
                 0.0f,
                 0.0001f,
                 "DOF aperture falls back from infinity");
    EXPECT_NEARF(chain.effects[5].snapshot.dof_max_blur,
                 128.0f,
                 0.0001f,
                 "DOF max blur clamps to the quality cap");

    EXPECT_TRUE(chain.effects[6].type == VGFX3D_POSTFX_EFFECT_MOTION_BLUR,
                "Sanitized chain stores motion blur seventh");
    EXPECT_NEARF(chain.effects[6].snapshot.motion_blur_intensity,
                 0.0f,
                 0.0001f,
                 "Motion-blur intensity falls back from infinity");
    EXPECT_TRUE(chain.effects[6].snapshot.motion_blur_samples == 64,
                "Motion-blur samples clamp to the quality cap");

    vgfx3d_postfx_chain_free(&chain);
}

static void test_extreme_finite_effect_parameters_are_capped(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_chain_t chain = {0};
    const double huge = 1.0e300;

    rt_postfx3d_add_bloom(fx, huge, huge, INT64_MAX);
    rt_postfx3d_add_tonemap(fx, INT64_MAX, huge);
    rt_postfx3d_add_color_grade(fx, huge, huge, huge);
    rt_postfx3d_add_vignette(fx, huge, huge);
    rt_postfx3d_add_ssao(fx, huge, huge, INT64_MAX);
    rt_postfx3d_add_dof(fx, huge, huge, huge);
    rt_postfx3d_add_motion_blur(fx, huge, INT64_MAX);

    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1,
                "Chain export succeeds for huge finite parameters");
    EXPECT_TRUE(chain.effect_count == 7, "Huge finite parameters preserve the authored chain");
    for (int32_t i = 0; i < chain.effect_count; i++)
        EXPECT_TRUE(snapshot_float_fields_are_finite(&chain.effects[i].snapshot),
                    "Huge finite parameters export only finite snapshot floats");

    EXPECT_NEARF(chain.effects[0].snapshot.bloom_threshold,
                 64.0f,
                 0.0001f,
                 "Huge bloom threshold clamps to the runtime cap");
    EXPECT_NEARF(chain.effects[0].snapshot.bloom_intensity,
                 64.0f,
                 0.0001f,
                 "Huge bloom intensity clamps to the runtime cap");
    EXPECT_TRUE(chain.effects[0].snapshot.bloom_passes == 32,
                "Huge bloom blur passes clamp to the quality cap");

    EXPECT_NEARF(chain.effects[1].snapshot.tonemap_exposure,
                 64.0f,
                 0.0001f,
                 "Huge tonemap exposure clamps to the runtime cap");
    EXPECT_NEARF(chain.effects[2].snapshot.cg_brightness,
                 1.0f,
                 0.0001f,
                 "Huge color-grade brightness clamps to +1");
    EXPECT_NEARF(chain.effects[2].snapshot.cg_contrast,
                 4.0f,
                 0.0001f,
                 "Huge color-grade contrast clamps to +4");
    EXPECT_NEARF(chain.effects[2].snapshot.cg_saturation,
                 4.0f,
                 0.0001f,
                 "Huge color-grade saturation clamps to +4");
    EXPECT_NEARF(
        chain.effects[3].snapshot.vignette_radius, 1.0f, 0.0001f, "Huge vignette radius clamps");
    EXPECT_NEARF(chain.effects[3].snapshot.vignette_softness,
                 1.0f,
                 0.0001f,
                 "Huge vignette softness clamps");
    EXPECT_NEARF(chain.effects[4].snapshot.ssao_radius,
                 1000000.0f,
                 0.5f,
                 "Huge SSAO radius clamps to the scene cap");
    EXPECT_NEARF(chain.effects[4].snapshot.ssao_intensity,
                 64.0f,
                 0.0001f,
                 "Huge SSAO intensity clamps to the runtime cap");
    EXPECT_NEARF(chain.effects[5].snapshot.dof_focus_distance,
                 1000000.0f,
                 0.5f,
                 "Huge DOF focus distance clamps to the scene cap");
    EXPECT_NEARF(chain.effects[5].snapshot.dof_aperture,
                 64.0f,
                 0.0001f,
                 "Huge DOF aperture clamps to the runtime cap");
    EXPECT_NEARF(chain.effects[5].snapshot.dof_max_blur,
                 128.0f,
                 0.0001f,
                 "Huge DOF blur clamps to the quality cap");
    EXPECT_NEARF(chain.effects[6].snapshot.motion_blur_intensity,
                 1.0f,
                 0.0001f,
                 "Huge motion blur intensity clamps to one");

    vgfx3d_postfx_chain_free(&chain);
}

static void test_chain_copy_rejects_inconsistent_metadata(void) {
    vgfx3d_postfx_effect_desc_t one_effect = {0};
    vgfx3d_postfx_chain_t src = {0};
    vgfx3d_postfx_chain_t dst = {0};

    src.enabled = 1;
    src.effect_count = 2;
    src.effect_capacity = 1;
    src.effects = &one_effect;
    dst.enabled = 1;

    EXPECT_TRUE(vgfx3d_postfx_chain_copy(&dst, &src) == 0,
                "Chain copy rejects sources whose count exceeds capacity");
    EXPECT_TRUE(dst.enabled == 0 && dst.effect_count == 0,
                "Rejected chain copy resets the destination to disabled");
}

static void test_private_effect_count_corruption_is_bounded(void) {
    void *fx = rt_postfx3d_new();
    PostFX3DTestLayout *layout = (PostFX3DTestLayout *)fx;
    vgfx3d_postfx_chain_t chain = {0};
    vgfx3d_postfx_snapshot_t snapshot;

    rt_postfx3d_add_bloom(fx, 0.8, 1.0, 2);
    EXPECT_TRUE(layout->effect_capacity > 0, "PostFX3D corruption fixture allocated effects");

    layout->effect_count = layout->effect_capacity + 100;
    EXPECT_TRUE(rt_postfx3d_get_effect_count(fx) == layout->effect_capacity,
                "PostFX3D effect count getter clamps private counts to capacity");
    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1,
                "PostFX3D chain export ignores over-capacity private counts");
    EXPECT_TRUE(chain.effect_count == 1 && chain.effects[0].type == VGFX3D_POSTFX_EFFECT_BLOOM,
                "PostFX3D chain export keeps only valid enabled effects");
    EXPECT_TRUE(vgfx3d_postfx_get_snapshot(fx, &snapshot) == 1 && snapshot.bloom_enabled == 1,
                "PostFX3D snapshot export ignores over-capacity private counts");
    vgfx3d_postfx_chain_free(&chain);

    layout->effect_count = -5;
    rt_postfx3d_add_fxaa(fx);
    EXPECT_TRUE(rt_postfx3d_get_effect_count(fx) == 1,
                "PostFX3D append repairs negative private counts before indexing");
    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1,
                "PostFX3D chain export still succeeds after count repair");
    EXPECT_TRUE(chain.effect_count == 1 && chain.effects[0].type == VGFX3D_POSTFX_EFFECT_FXAA,
                "PostFX3D append writes inside the repaired effect array");
    vgfx3d_postfx_chain_free(&chain);
}

static void test_set_enabled_normalizes_boolean_state(void) {
    void *fx = rt_postfx3d_new();

    rt_postfx3d_set_enabled(fx, -7);
    EXPECT_TRUE(rt_postfx3d_get_enabled(fx) == 1, "Negative enabled values normalize to true");
    rt_postfx3d_set_enabled(fx, 0);
    EXPECT_TRUE(rt_postfx3d_get_enabled(fx) == 0, "Zero enabled values normalize to false");
}

/* Plan 05: TAA chain export, blend clamping, and the explicit-tonemap marker. */
static void test_taa_and_explicit_tonemap_export(void) {
    void *fx = rt_postfx3d_new();
    vgfx3d_postfx_chain_t chain = {0};
    vgfx3d_postfx_snapshot_t snapshot;

    rt_postfx3d_add_bloom(fx, 0.8, 1.0, 4);
    rt_postfx3d_add_taa(fx, 0.9);
    rt_postfx3d_add_tonemap(fx, 0, 1.25);

    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1 && chain.effect_count == 3,
                "TAA chain export keeps all three effects");
    EXPECT_TRUE(chain.effects[1].type == VGFX3D_POSTFX_EFFECT_TAA &&
                    chain.effects[1].snapshot.taa_enabled == 1,
                "TAA entry exports the appended enum value with taa_enabled set");
    EXPECT_NEARF(chain.effects[1].snapshot.taa_blend,
                 0.9f,
                 0.0001f,
                 "TAA blend passes through inside the valid range");
    EXPECT_TRUE(chain.effects[0].snapshot.tonemap_explicit == 0,
                "Non-tonemap entries do not carry the explicit-tonemap marker");
    EXPECT_TRUE(chain.effects[2].snapshot.tonemap_explicit == 1 &&
                    chain.effects[2].snapshot.tonemap_mode == 0,
                "Explicit mode-0 tonemap entries carry the explicit-tonemap marker");
    EXPECT_TRUE(vgfx3d_postfx_requires_gpu_scene_buffers(fx) == 1,
                "TAA requires GPU scene buffers like SSAO/DOF/motion blur");
    EXPECT_TRUE(vgfx3d_postfx_get_snapshot(fx, &snapshot) == 1 && snapshot.taa_enabled == 1,
                "Legacy flat snapshot reports the TAA effect");
    vgfx3d_postfx_chain_free(&chain);

    fx = rt_postfx3d_new();
    rt_postfx3d_add_taa(fx, 5.0);
    rt_postfx3d_add_taa(fx, 0.1);
    rt_postfx3d_add_taa(fx, NAN);
    EXPECT_TRUE(vgfx3d_postfx_get_chain(fx, &chain) == 1 && chain.effect_count == 3,
                "TAA clamp fixture exports three entries");
    EXPECT_NEARF(
        chain.effects[0].snapshot.taa_blend, 0.98f, 0.0001f, "TAA blend clamps above to 0.98");
    EXPECT_NEARF(
        chain.effects[1].snapshot.taa_blend, 0.5f, 0.0001f, "TAA blend clamps below to 0.5");
    EXPECT_NEARF(chain.effects[2].snapshot.taa_blend,
                 0.9f,
                 0.0001f,
                 "Non-finite TAA blend falls back to the 0.9 default");
    vgfx3d_postfx_chain_free(&chain);
}

int main(void) {
    test_snapshot_includes_advanced_effects();
    test_snapshot_disabled_returns_zero();
    test_snapshot_preserves_documented_tonemap_and_grade_params();
    test_effect_chain_grows_past_legacy_cap();
    test_chain_export_preserves_effect_order_and_duplicates();
    test_effect_parameters_are_sanitized_for_backend_chain();
    test_extreme_finite_effect_parameters_are_capped();
    test_chain_copy_rejects_inconsistent_metadata();
    test_private_effect_count_corruption_is_bounded();
    test_set_enabled_normalizes_boolean_state();
    test_taa_and_explicit_tonemap_export();

    printf("rt_postfx3d snapshot tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
