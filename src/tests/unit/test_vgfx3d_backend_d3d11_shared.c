#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend_d3d11_shared.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (fabs((double)(a) - (double)(b)) > (eps))                                               \
            fprintf(stderr, "FAIL: %s (got %.6f expected %.6f)\n", msg, (double)(a), (double)(b)); \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

static void set_identity4x4(float *m) {
    memset(m, 0, sizeof(float) * 16u);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void test_pack_bone_palette_zero_pads_unused_bones(void) {
    float src[16];
    float dst[VGFX3D_D3D11_MAX_BONES * 16];

    for (int i = 0; i < 16; i++)
        src[i] = (float)(i + 1);
    memset(dst, 0xCD, sizeof(dst));
    vgfx3d_d3d11_pack_bone_palette(dst, src, 1);

    for (int i = 0; i < 16; i++) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Bone palette preserves matrix element %d", i);
        EXPECT_NEAR(dst[i], src[i], 1e-6f, msg);
    }
    EXPECT_NEAR(dst[16], 0.0f, 1e-6f, "Bone palette zero-pads the second bone");
    EXPECT_NEAR(dst[sizeof(dst) / sizeof(dst[0]) - 1], 0.0f, 1e-6f,
                "Bone palette zero-pads the tail of the upload buffer");
}

static void test_pack_scalar_array4_matches_hlsl_layout(void) {
    float packed[2][4];
    float src[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    vgfx3d_d3d11_pack_scalar_array4(packed, 2, src, 8);
    EXPECT_NEAR(packed[0][0], 1.0f, 1e-6f, "Packed scalar array stores lane 0");
    EXPECT_NEAR(packed[0][3], 4.0f, 1e-6f, "Packed scalar array stores lane 3");
    EXPECT_NEAR(packed[1][0], 5.0f, 1e-6f, "Packed scalar array advances to the second vector");
    EXPECT_NEAR(packed[1][3], 8.0f, 1e-6f, "Packed scalar array stores the final scalar");
}

static void test_constant_buffer_struct_sizes_match_expected_layout(void) {
    EXPECT_TRUE(sizeof(vgfx3d_d3d11_per_object_t) == 480u,
                "PerObject C struct matches the packed HLSL cbuffer size");
    EXPECT_TRUE(sizeof(vgfx3d_d3d11_per_material_t) == 208u,
                "PerMaterial C struct matches the packed HLSL cbuffer size");
}

static void test_fill_instance_data_uses_previous_or_current_matrices(void) {
    vgfx3d_d3d11_instance_data_t instances[2];
    float models[32];
    float prev_models[32];

    set_identity4x4(&models[0]);
    set_identity4x4(&models[16]);
    models[3] = 2.0f;
    models[19] = -4.0f;
    set_identity4x4(&prev_models[0]);
    set_identity4x4(&prev_models[16]);
    prev_models[3] = 1.0f;
    prev_models[19] = -6.0f;

    memset(instances, 0, sizeof(instances));
    vgfx3d_d3d11_fill_instance_data(instances, 2, models, prev_models, 1);
    EXPECT_NEAR(instances[0].model[3], 2.0f, 1e-6f, "Instance data preserves the model matrix");
    EXPECT_NEAR(instances[0].prev_model[3], 1.0f, 1e-6f,
                "Instance data preserves the previous model matrix");
    EXPECT_NEAR(instances[1].prev_model[3], -6.0f, 1e-6f,
                "Instance data preserves the second previous model matrix");

    memset(instances, 0, sizeof(instances));
    vgfx3d_d3d11_fill_instance_data(instances, 2, models, NULL, 0);
    EXPECT_NEAR(instances[1].prev_model[3], instances[1].model[3], 1e-6f,
                "Instance data falls back to the current matrix when no history exists");
}

static void test_frame_history_preserves_scene_state_across_overlay_passes(void) {
    vgfx3d_d3d11_frame_history_t history;
    float scene_vp0[16];
    float scene_vp1[16];
    float overlay_vp[16];
    float inv0[16];
    float inv1[16];
    float overlay_inv[16];
    float cam0[3] = {1.0f, 2.0f, 3.0f};
    float cam1[3] = {4.0f, 5.0f, 6.0f};
    float overlay_cam[3] = {9.0f, 8.0f, 7.0f};

    memset(&history, 0, sizeof(history));
    for (int i = 0; i < 16; i++) {
        scene_vp0[i] = (float)(i + 1);
        scene_vp1[i] = (float)(i + 21);
        overlay_vp[i] = (float)(i + 41);
        inv0[i] = (float)(i + 61);
        inv1[i] = (float)(i + 81);
        overlay_inv[i] = (float)(i + 101);
    }

    vgfx3d_d3d11_update_frame_history(&history, scene_vp0, inv0, cam0, 0, 0);
    EXPECT_TRUE(history.scene_history_valid == 1, "Main-pass history becomes valid on first scene");
    EXPECT_NEAR(history.scene_prev_vp[0], scene_vp0[0], 1e-6f,
                "First main pass seeds prevViewProjection from the current scene");
    EXPECT_NEAR(history.draw_prev_vp[0], scene_vp0[0], 1e-6f,
                "First main pass uses the current VP as draw-time history");

    vgfx3d_d3d11_update_frame_history(&history, scene_vp1, inv1, cam1, 0, 0);
    EXPECT_NEAR(history.scene_prev_vp[0], scene_vp0[0], 1e-6f,
                "Second main pass preserves the previous scene VP");
    EXPECT_NEAR(history.scene_vp[0], scene_vp1[0], 1e-6f, "Second main pass updates scene VP");
    EXPECT_NEAR(history.draw_prev_vp[0], scene_vp0[0], 1e-6f,
                "Second main pass draws against the prior scene VP");
    EXPECT_NEAR(history.scene_inv_vp[0], inv1[0], 1e-6f,
                "Second main pass updates the scene inverse VP");
    EXPECT_NEAR(history.scene_cam_pos[0], cam1[0], 1e-6f,
                "Second main pass updates the scene camera position");

    vgfx3d_d3d11_update_frame_history(&history, overlay_vp, overlay_inv, overlay_cam, 1, 1);
    EXPECT_NEAR(history.scene_vp[0], scene_vp1[0], 1e-6f,
                "Overlay pass preserves the scene VP for later postfx");
    EXPECT_NEAR(history.scene_prev_vp[0], scene_vp0[0], 1e-6f,
                "Overlay pass preserves the previous scene VP");
    EXPECT_NEAR(history.scene_inv_vp[0], inv1[0], 1e-6f,
                "Overlay pass preserves the scene inverse VP");
    EXPECT_NEAR(history.scene_cam_pos[0], cam1[0], 1e-6f,
                "Overlay pass preserves the scene camera position");
    EXPECT_NEAR(history.draw_prev_vp[0], overlay_vp[0], 1e-6f,
                "Overlay pass uses its own VP for draw-time history");
    EXPECT_TRUE(history.overlay_used_this_frame == 1,
                "Overlay pass marks the separate overlay target as used");
}

static void test_upload_status_helpers_drop_stale_state(void) {
    vgfx3d_d3d11_per_object_t object_data;

    memset(&object_data, 0, sizeof(object_data));
    object_data.has_skinning = 1;
    object_data.has_prev_skinning = 1;
    vgfx3d_d3d11_resolve_bone_upload_status(&object_data, 0, 1);
    EXPECT_TRUE(object_data.has_skinning == 0 && object_data.has_prev_skinning == 0,
                "Bone upload failure disables both current and previous skinning flags");

    memset(&object_data, 0, sizeof(object_data));
    object_data.has_skinning = 1;
    object_data.has_prev_skinning = 1;
    object_data.morph_shape_count = 4;
    object_data.vertex_count = 16;
    object_data.has_prev_morph_weights = 1;
    object_data.has_morph_normal_deltas = 1;
    vgfx3d_d3d11_resolve_morph_upload_status(&object_data, 1, 0);
    EXPECT_TRUE(object_data.morph_shape_count == 4 && object_data.vertex_count == 16,
                "Morph normal upload failure keeps the main morph payload active");
    EXPECT_TRUE(object_data.has_morph_normal_deltas == 0,
                "Morph normal upload failure disables only the normal-delta flag");

    vgfx3d_d3d11_resolve_morph_upload_status(&object_data, 0, 0);
    EXPECT_TRUE(object_data.morph_shape_count == 0 && object_data.vertex_count == 0 &&
                    object_data.has_prev_morph_weights == 0 &&
                    object_data.has_morph_normal_deltas == 0,
                "Morph upload failure clears all morph-related state");
}

static void test_target_kind_blend_and_color_format_helpers(void) {
    vgfx3d_draw_cmd_t cmd;

    EXPECT_TRUE(vgfx3d_d3d11_choose_target_kind(1, 1, 0) == VGFX3D_D3D11_TARGET_RTT,
                "RTT rendering always chooses the RTT target kind");
    EXPECT_TRUE(vgfx3d_d3d11_choose_target_kind(0, 0, 1) == VGFX3D_D3D11_TARGET_SWAPCHAIN,
                "Without GPU postfx the backend renders directly to the swapchain");
    EXPECT_TRUE(vgfx3d_d3d11_choose_target_kind(0, 1, 0) == VGFX3D_D3D11_TARGET_SCENE,
                "GPU postfx main passes render into the HDR scene target");
    EXPECT_TRUE(vgfx3d_d3d11_choose_target_kind(0, 1, 1) == VGFX3D_D3D11_TARGET_OVERLAY,
                "GPU postfx overlay passes render into the overlay target");

    memset(&cmd, 0, sizeof(cmd));
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_ALPHA,
                "PBR blend materials use alpha blending");
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_OPAQUE,
                "PBR mask materials keep opaque render-target writes");
    cmd.workflow = 0;
    cmd.alpha = 0.5f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_ALPHA,
                "Legacy translucent materials use alpha blending");
    cmd.alpha = 1.0f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_ALPHA,
                "Legacy explicit blend materials use alpha blending");
    cmd.alpha = 0.25f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_OPAQUE,
                "Legacy masked materials stay on the opaque render path");

    EXPECT_TRUE(vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE) ==
                    VGFX3D_D3D11_COLOR_FORMAT_HDR16F,
                "Scene rendering uses the HDR color format class");
    EXPECT_TRUE(vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SWAPCHAIN) ==
                    VGFX3D_D3D11_COLOR_FORMAT_UNORM8 &&
                    vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_OVERLAY) ==
                        VGFX3D_D3D11_COLOR_FORMAT_UNORM8,
                "Swapchain and overlay targets use UNORM8 output");
}

static void test_capacity_and_mip_helpers(void) {
    EXPECT_TRUE(vgfx3d_d3d11_compute_mip_count(1, 1) == 1,
                "1x1 textures use a single mip level");
    EXPECT_TRUE(vgfx3d_d3d11_compute_mip_count(4, 2) == 3,
                "Mip-count helper follows the full downsample chain");
    EXPECT_TRUE(vgfx3d_d3d11_next_capacity(0, 65, 64) == 128,
                "Capacity helper grows fixed caches beyond the old hard cap");
    EXPECT_TRUE(vgfx3d_d3d11_next_capacity(16, 8, 16) == 16,
                "Capacity helper keeps existing storage when it is already large enough");
}

int main(void) {
    test_pack_bone_palette_zero_pads_unused_bones();
    test_pack_scalar_array4_matches_hlsl_layout();
    test_constant_buffer_struct_sizes_match_expected_layout();
    test_fill_instance_data_uses_previous_or_current_matrices();
    test_frame_history_preserves_scene_state_across_overlay_passes();
    test_upload_status_helpers_drop_stale_state();
    test_target_kind_blend_and_color_format_helpers();
    test_capacity_and_mip_helpers();

    printf("vgfx3d d3d11 shared tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
