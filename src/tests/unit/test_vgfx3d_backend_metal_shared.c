#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend_metal_shared.h"

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
    float dst[VGFX3D_METAL_MAX_BONES * 16];

    for (int i = 0; i < 16; i++)
        src[i] = (float)(i + 1);
    memset(dst, 0xCD, sizeof(dst));
    vgfx3d_metal_pack_bone_palette(dst, src, 1);

    for (int i = 0; i < 16; i++) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Bone palette preserves matrix element %d", i);
        EXPECT_NEAR(dst[i], src[i], 1e-6f, msg);
    }
    EXPECT_NEAR(dst[16], 0.0f, 1e-6f, "Bone palette zero-pads the second bone");
    EXPECT_NEAR(dst[sizeof(dst) / sizeof(dst[0]) - 1], 0.0f, 1e-6f,
                "Bone palette zero-pads the tail of the upload buffer");
}

static void test_fill_instance_data_transposes_and_tracks_history(void) {
    vgfx3d_metal_instance_data_t instances[2];
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
    vgfx3d_metal_fill_instance_data(instances, 2, models, prev_models, 1);
    EXPECT_NEAR(instances[0].model[12], 2.0f, 1e-6f,
                "Instance staging transposes model matrices to Metal column-major layout");
    EXPECT_NEAR(instances[0].prev_model[12], 1.0f, 1e-6f,
                "Instance staging preserves previous model matrices");
    EXPECT_NEAR(instances[1].prev_model[12], -6.0f, 1e-6f,
                "Instance staging preserves the second previous model matrix");

    memset(instances, 0, sizeof(instances));
    vgfx3d_metal_fill_instance_data(instances, 2, models, NULL, 0);
    EXPECT_NEAR(instances[1].prev_model[12], instances[1].model[12], 1e-6f,
                "Instance staging falls back to the current matrix when no history exists");
}

static void test_frame_history_preserves_scene_state_across_overlay_passes(void) {
    vgfx3d_metal_frame_history_t history;
    float scene_vp0[16];
    float scene_vp1[16];
    float overlay_vp[16];
    float inv0[16];
    float inv1[16];
    float overlay_inv[16];
    float cam0[3] = {1.0f, 2.0f, 3.0f};
    float cam1[3] = {4.0f, 5.0f, 6.0f};

    memset(&history, 0, sizeof(history));
    for (int i = 0; i < 16; i++) {
        scene_vp0[i] = (float)(i + 1);
        scene_vp1[i] = (float)(i + 21);
        overlay_vp[i] = (float)(i + 41);
        inv0[i] = (float)(i + 61);
        inv1[i] = (float)(i + 81);
        overlay_inv[i] = (float)(i + 101);
    }

    vgfx3d_metal_update_frame_history(&history, scene_vp0, inv0, cam0, 0, 0);
    EXPECT_TRUE(history.scene_history_valid == 1, "Main-pass history becomes valid on first scene");
    EXPECT_NEAR(history.scene_prev_vp[0], scene_vp0[0], 1e-6f,
                "First main pass seeds prevViewProjection from the current scene");
    EXPECT_NEAR(history.draw_prev_vp[0], scene_vp0[0], 1e-6f,
                "First main pass uses the current VP as draw-time history");

    vgfx3d_metal_update_frame_history(&history, scene_vp1, inv1, cam1, 0, 0);
    EXPECT_NEAR(history.scene_prev_vp[0], scene_vp0[0], 1e-6f,
                "Second main pass preserves the previous scene VP");
    EXPECT_NEAR(history.scene_vp[0], scene_vp1[0], 1e-6f, "Second main pass updates the current scene VP");
    EXPECT_NEAR(history.draw_prev_vp[0], scene_vp0[0], 1e-6f,
                "Second main pass draws against the prior scene VP");
    EXPECT_NEAR(history.scene_inv_vp[0], inv1[0], 1e-6f,
                "Second main pass updates the scene inverse VP");
    EXPECT_NEAR(history.scene_cam_pos[0], cam1[0], 1e-6f,
                "Second main pass updates the scene camera position");

    vgfx3d_metal_update_frame_history(&history, overlay_vp, overlay_inv, cam0, 1, 1);
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

static void test_target_kind_blend_motion_and_readback_helpers(void) {
    vgfx3d_draw_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    EXPECT_TRUE(vgfx3d_metal_choose_target_kind(1, 1, 0) == VGFX3D_METAL_TARGET_RTT,
                "RTT rendering always chooses the RTT target kind");
    EXPECT_TRUE(vgfx3d_metal_choose_target_kind(0, 0, 0) == VGFX3D_METAL_TARGET_SWAPCHAIN,
                "Without GPU postfx the backend renders directly to the swapchain");
    EXPECT_TRUE(vgfx3d_metal_choose_target_kind(0, 1, 0) == VGFX3D_METAL_TARGET_SCENE,
                "GPU postfx main passes render into the HDR scene target");
    EXPECT_TRUE(vgfx3d_metal_choose_target_kind(0, 1, 1) == VGFX3D_METAL_TARGET_OVERLAY,
                "GPU postfx overlay passes render into the overlay target");
    EXPECT_TRUE(vgfx3d_metal_should_load_existing_color(VGFX3D_METAL_TARGET_OVERLAY, 1, 0) == 0,
                "The first overlay pass in a frame clears stale overlay contents");
    EXPECT_TRUE(vgfx3d_metal_should_load_existing_color(VGFX3D_METAL_TARGET_OVERLAY, 1, 1) == 1,
                "Later overlay passes in the same frame preserve prior overlay draws");
    EXPECT_TRUE(vgfx3d_metal_should_load_existing_color(VGFX3D_METAL_TARGET_SCENE, 1, 0) == 1,
                "Non-overlay passes keep explicit load-existing requests");

    EXPECT_TRUE(vgfx3d_metal_choose_color_format(VGFX3D_METAL_TARGET_SCENE) ==
                    VGFX3D_METAL_COLOR_FORMAT_HDR16F,
                "Scene rendering uses the HDR color target");
    EXPECT_TRUE(vgfx3d_metal_choose_color_format(VGFX3D_METAL_TARGET_SWAPCHAIN) ==
                        VGFX3D_METAL_COLOR_FORMAT_UNORM8 &&
                    vgfx3d_metal_choose_color_format(VGFX3D_METAL_TARGET_OVERLAY) ==
                        VGFX3D_METAL_COLOR_FORMAT_UNORM8,
                "Swapchain and overlay targets use UNORM8 output");

    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_ALPHA,
                "Blend materials use alpha blending");
    EXPECT_TRUE(vgfx3d_metal_choose_motion_attachment_mode(VGFX3D_METAL_TARGET_SCENE, &cmd) ==
                    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_ONLY,
                "Alpha-blended scene draws disable the motion attachment");
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_OPAQUE,
                "Mask materials keep opaque render-target writes");
    EXPECT_TRUE(vgfx3d_metal_choose_motion_attachment_mode(VGFX3D_METAL_TARGET_SCENE, &cmd) ==
                    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_AND_MOTION,
                "Opaque scene draws keep the motion attachment enabled");
    EXPECT_TRUE(vgfx3d_metal_choose_motion_attachment_mode(VGFX3D_METAL_TARGET_SWAPCHAIN, &cmd) ==
                    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_ONLY,
                "Swapchain draws never target a scene-motion attachment");

    memset(&cmd, 0, sizeof(cmd));
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_ALPHA,
                "Legacy explicit blend materials use alpha blending");
    cmd.alpha = 0.25f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_OPAQUE,
                "Legacy masked materials stay on the opaque render path");

    EXPECT_TRUE(vgfx3d_metal_choose_readback_kind(0) == VGFX3D_METAL_READBACK_BACKBUFFER,
                "Direct rendering reads back the backbuffer");
    EXPECT_TRUE(vgfx3d_metal_choose_readback_kind(1) ==
                    VGFX3D_METAL_READBACK_POSTFX_COMPOSITE,
                "GPU postfx readback uses the composited postfx path");
}

static void test_capacity_mip_and_morph_cache_helpers(void) {
    vgfx3d_draw_cmd_t cmd;

    EXPECT_TRUE(vgfx3d_metal_compute_mip_count(1, 1) == 1,
                "1x1 textures use a single mip level");
    EXPECT_TRUE(vgfx3d_metal_compute_mip_count(4, 2) == 3,
                "Mip-count helper follows the full downsample chain");
    EXPECT_TRUE(vgfx3d_metal_next_capacity(0, 65, 64) == 128,
                "Capacity helper grows beyond the old fixed cache size");
    EXPECT_TRUE(vgfx3d_metal_next_capacity(16, 8, 16) == 16,
                "Capacity helper keeps existing storage when it is already large enough");
    EXPECT_TRUE(vgfx3d_metal_should_prune_cache_entry(300, 10, 240) == 1,
                "Old cache entries become prune candidates");
    EXPECT_TRUE(vgfx3d_metal_should_prune_cache_entry(200, 10, 240) == 0,
                "Recently used cache entries stay resident");

    memset(&cmd, 0, sizeof(cmd));
    cmd.morph_key = &cmd;
    cmd.morph_revision = 4;
    cmd.morph_deltas = (const float *)&cmd;
    cmd.morph_weights = (const float *)&cmd;
    cmd.morph_shape_count = 3;
    cmd.vertex_count = 128;
    EXPECT_TRUE(vgfx3d_metal_should_reuse_morph_cache(cmd.morph_key, 4, 3, 128, 0, &cmd) == 1,
                "Morph cache entries reuse matching key/revision payloads");
    cmd.morph_revision = 5;
    EXPECT_TRUE(vgfx3d_metal_should_reuse_morph_cache(cmd.morph_key, 4, 3, 128, 0, &cmd) == 0,
                "Morph cache entries reject stale revisions");
    cmd.morph_revision = 4;
    cmd.morph_normal_deltas = (const float *)&tests_run;
    EXPECT_TRUE(vgfx3d_metal_should_reuse_morph_cache(cmd.morph_key, 4, 3, 128, 0, &cmd) == 0,
                "Morph cache entries include normal-delta presence in the cache key");
}

int main(void) {
    test_pack_bone_palette_zero_pads_unused_bones();
    test_fill_instance_data_transposes_and_tracks_history();
    test_frame_history_preserves_scene_state_across_overlay_passes();
    test_target_kind_blend_motion_and_readback_helpers();
    test_capacity_mip_and_morph_cache_helpers();

    printf("vgfx3d metal shared tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
