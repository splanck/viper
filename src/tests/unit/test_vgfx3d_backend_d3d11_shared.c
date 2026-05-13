#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend_d3d11_shared.h"

#include <limits.h>
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

static void test_pack_bone_palette_identity_pads_unused_bones(void) {
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
    EXPECT_NEAR(dst[16], 1.0f, 1e-6f, "Bone palette identity-pads the second bone");
    EXPECT_NEAR(dst[21], 1.0f, 1e-6f, "Bone palette identity-pads the second bone diagonal");
    EXPECT_NEAR(dst[31], 1.0f, 1e-6f, "Bone palette identity-pads the second bone tail diagonal");
    EXPECT_NEAR(dst[17], 0.0f, 1e-6f, "Bone palette clears off-diagonal padding");
    EXPECT_NEAR(dst[sizeof(dst) / sizeof(dst[0]) - 1], 1.0f, 1e-6f,
                "Bone palette identity-pads the tail of the upload buffer");
}

static void test_pack_bone_palette_identity_pads_empty_source(void) {
    float dst[VGFX3D_D3D11_MAX_BONES * 16];

    memset(dst, 0xCD, sizeof(dst));
    vgfx3d_d3d11_pack_bone_palette(dst, NULL, 0);

    EXPECT_NEAR(dst[0], 1.0f, 1e-6f, "Empty bone palette starts with identity");
    EXPECT_NEAR(dst[5], 1.0f, 1e-6f, "Empty bone palette fills identity diagonal");
    EXPECT_NEAR(dst[10], 1.0f, 1e-6f, "Empty bone palette fills third diagonal");
    EXPECT_NEAR(dst[15], 1.0f, 1e-6f, "Empty bone palette fills fourth diagonal");
    EXPECT_NEAR(dst[1], 0.0f, 1e-6f, "Empty bone palette clears off-diagonal values");
}

static void test_pack_bone_palette_keeps_highest_supported_bone(void) {
    float src[VGFX3D_D3D11_BONE_PALETTE_FLOATS];
    float dst[VGFX3D_D3D11_BONE_PALETTE_FLOATS];
    size_t tail = (size_t)(VGFX3D_D3D11_MAX_BONES - 1) * 16u;

    for (size_t i = 0; i < sizeof(src) / sizeof(src[0]); i++)
        src[i] = (float)i;
    memset(dst, 0, sizeof(dst));
    vgfx3d_d3d11_pack_bone_palette(dst, src, VGFX3D_D3D11_MAX_BONES);

    EXPECT_NEAR(dst[tail + 0], src[tail + 0], 1e-6f, "Bone packing preserves the final supported bone");
    EXPECT_NEAR(dst[tail + 15], src[tail + 15], 1e-6f,
                "Bone packing preserves the tail matrix element of the final supported bone");
}

static void test_bone_palette_upload_size_covers_supported_bone_count(void) {
    EXPECT_TRUE(VGFX3D_D3D11_BONE_PALETTE_FLOATS == VGFX3D_D3D11_MAX_BONES * 16u,
                "Bone palette upload has one 4x4 matrix per supported bone");
    EXPECT_TRUE(VGFX3D_D3D11_BONE_PALETTE_BYTES ==
                    sizeof(float) * VGFX3D_D3D11_MAX_BONES * 16u,
                "Bone palette upload byte size matches the packed float payload");
    EXPECT_TRUE(VGFX3D_D3D11_BONE_PALETTE_BYTES == 16384u,
                "D3D11 bone cbuffer covers all 256 shader palette entries");
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
    EXPECT_TRUE(sizeof(vgfx3d_d3d11_per_material_t) == 432u,
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
    EXPECT_TRUE(vgfx3d_d3d11_should_load_existing_color(VGFX3D_D3D11_TARGET_SCENE, 1, 0) == 1,
                "Scene passes honor requested color preservation");
    EXPECT_TRUE(vgfx3d_d3d11_should_load_existing_color(VGFX3D_D3D11_TARGET_OVERLAY, 1, 0) == 0,
                "First overlay pass clears the overlay target");
    EXPECT_TRUE(vgfx3d_d3d11_should_load_existing_color(VGFX3D_D3D11_TARGET_OVERLAY, 1, 1) == 1,
                "Later overlay passes preserve prior overlay contents");

    memset(&cmd, 0, sizeof(cmd));
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_ALPHA,
                "PBR blend materials use alpha blending");
    EXPECT_TRUE(vgfx3d_d3d11_choose_motion_attachment_mode(
                    VGFX3D_D3D11_TARGET_SCENE, &cmd) ==
                    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY,
                "Alpha-blended scene draws disable the motion attachment");
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_d3d11_choose_blend_mode(&cmd) == VGFX3D_D3D11_BLEND_OPAQUE,
                "PBR mask materials keep opaque render-target writes");
    EXPECT_TRUE(vgfx3d_d3d11_choose_motion_attachment_mode(
                    VGFX3D_D3D11_TARGET_SCENE, &cmd) ==
                    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_AND_MOTION,
                "Opaque scene draws keep the motion attachment enabled");
    EXPECT_TRUE(vgfx3d_d3d11_choose_motion_attachment_mode(
                    VGFX3D_D3D11_TARGET_SWAPCHAIN, &cmd) ==
                    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY,
                "Swapchain draws never target a scene-motion attachment");
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
    EXPECT_TRUE(vgfx3d_d3d11_has_complete_splat(1, 1, 1, 1, 1, 1) == 1,
                "Splatting is enabled when the weight map and all four layers are bound");
    EXPECT_TRUE(vgfx3d_d3d11_has_complete_splat(1, 1, 1, 0, 1, 1) == 0,
                "Splatting is disabled when any layer texture is missing");
    EXPECT_TRUE(vgfx3d_d3d11_has_complete_splat(0, 1, 1, 1, 1, 1) == 0,
                "Splatting respects the draw command enable flag");
}

static void test_capacity_and_mip_helpers(void) {
    size_t bytes = 0;

    EXPECT_TRUE(vgfx3d_d3d11_compute_mip_count(1, 1) == 1,
                "1x1 textures use a single mip level");
    EXPECT_TRUE(vgfx3d_d3d11_compute_mip_count(4, 2) == 3,
                "Mip-count helper follows the full downsample chain");
    EXPECT_TRUE(vgfx3d_d3d11_compute_mip_count(0, 2) == 1,
                "Invalid texture dimensions still produce a safe single mip");
    EXPECT_TRUE(vgfx3d_d3d11_next_capacity(0, 65, 64) == 128,
                "Capacity helper grows fixed caches beyond the old hard cap");
    EXPECT_TRUE(vgfx3d_d3d11_next_capacity(16, 8, 16) == 16,
                "Capacity helper keeps existing storage when it is already large enough");
    EXPECT_TRUE(vgfx3d_d3d11_next_capacity(0, 0, 0) == 1,
                "Capacity helper never returns a non-positive capacity");
    EXPECT_TRUE(vgfx3d_d3d11_next_capacity(INT_MAX / 2 + 1, INT_MAX - 1, 64) ==
                    INT_MAX - 1,
                "Capacity helper saturates without overflowing signed int");
    EXPECT_TRUE(vgfx3d_d3d11_compute_row_bytes(7, 4, &bytes) == 1 && bytes == 28u,
                "Row-byte helper computes tightly packed RGBA8 rows");
    EXPECT_TRUE(vgfx3d_d3d11_compute_row_bytes(7, 8, &bytes) == 1 && bytes == 56u,
                "Row-byte helper computes tightly packed RGBA16F rows");
    EXPECT_TRUE(vgfx3d_d3d11_compute_row_bytes(0, 4, &bytes) == 0 && bytes == 0u,
                "Row-byte helper rejects non-positive widths");
    EXPECT_TRUE(vgfx3d_d3d11_compute_instance_upload_bytes(
                    3, sizeof(vgfx3d_d3d11_instance_data_t), &bytes) == 1 &&
                    bytes == 3u * sizeof(vgfx3d_d3d11_instance_data_t),
                "Instance upload helper computes the D3D11 vertex-buffer byte span");
    EXPECT_TRUE(vgfx3d_d3d11_compute_instance_upload_bytes(0,
                                                           sizeof(vgfx3d_d3d11_instance_data_t),
                                                           &bytes) == 0 &&
                    bytes == 0u,
                "Instance upload helper rejects non-positive instance counts");
    EXPECT_TRUE(vgfx3d_d3d11_compute_instance_upload_bytes(
                    INT_MAX, sizeof(vgfx3d_d3d11_instance_data_t), &bytes) == 0 &&
                    bytes == 0u,
                "Instance upload helper rejects spans larger than a D3D11 UINT ByteWidth");
    EXPECT_TRUE(vgfx3d_d3d11_compute_float_srv_update_bytes(3, 8, &bytes) == 1 &&
                    bytes == 12u,
                "Float-SRV update helper covers only live elements, not total capacity");
    EXPECT_TRUE(vgfx3d_d3d11_compute_float_srv_update_bytes(9, 8, &bytes) == 0 &&
                    bytes == 0u,
                "Float-SRV update helper rejects spans beyond allocated capacity");
    EXPECT_TRUE(vgfx3d_d3d11_compute_float_srv_update_bytes(SIZE_MAX / sizeof(float) + 1u,
                                                            SIZE_MAX,
                                                            &bytes) == 0 &&
                    bytes == 0u,
                "Float-SRV update helper rejects byte-size overflow");
    EXPECT_TRUE(vgfx3d_d3d11_validate_rgba8_destination(3, 2, 12, &bytes) == 1 &&
                    bytes == 24u,
                "RGBA8 destination validation returns the full writable span");
    EXPECT_TRUE(vgfx3d_d3d11_validate_rgba8_destination(3, 2, 12, NULL) == 1,
                "RGBA8 destination validation still checks spans without an out parameter");
    EXPECT_TRUE(vgfx3d_d3d11_validate_rgba8_destination(3, 2, 8, &bytes) == 0,
                "RGBA8 destination validation rejects short strides");
}

static void test_d3d11_limits_and_prune_helpers(void) {
    EXPECT_TRUE(vgfx3d_d3d11_is_valid_texture2d_extent(1, 1) == 1,
                "Texture extent helper accepts the smallest valid texture");
    EXPECT_TRUE(vgfx3d_d3d11_is_valid_texture2d_extent(
                    VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION,
                    VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION) == 1,
                "Texture extent helper accepts the D3D11 maximum");
    EXPECT_TRUE(vgfx3d_d3d11_is_valid_texture2d_extent(
                    VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION + 1, 16) == 0,
                "Texture extent helper rejects oversized widths before D3D allocation");
    EXPECT_TRUE(vgfx3d_d3d11_is_valid_cubemap_extent(0) == 0,
                "Cubemap extent helper rejects non-positive face sizes");
    EXPECT_TRUE(vgfx3d_d3d11_is_valid_cubemap_extent(
                    VGFX3D_D3D11_MAX_CUBEMAP_DIMENSION + 1) == 0,
                "Cubemap extent helper rejects oversized faces");

    EXPECT_TRUE(vgfx3d_d3d11_clamp_morph_shape_count(1024u, 64) ==
                    VGFX3D_D3D11_MAX_MORPH_SHAPES,
                "Morph shape helper applies the backend shape cap");
    EXPECT_TRUE(vgfx3d_d3d11_clamp_morph_shape_count((uint32_t)INT_MAX, 1) == 0,
                "Morph shape helper prevents signed shader-index overflow");
    EXPECT_TRUE(vgfx3d_d3d11_clamp_morph_shape_count(1u << 28, 8) == 2,
                "Morph shape helper clamps by the maximum HLSL int buffer index");

    EXPECT_TRUE(vgfx3d_d3d11_should_prune_cache_entry(100, 0, 0, 1000, 64, 240) == 1,
                "Cache prune helper evicts aged entries while above the resident floor");
    EXPECT_TRUE(vgfx3d_d3d11_should_prune_cache_entry(100, 0, 36, 1000, 64, 240) == 0,
                "Cache prune helper keeps enough entries to preserve the resident floor");
    EXPECT_TRUE(vgfx3d_d3d11_should_prune_cache_entry(100, 0, 0, 12, 64, 240) == 0,
                "Cache prune helper keeps recently used entries");
}

static void test_target_fallback_helper(void) {
    EXPECT_TRUE(vgfx3d_d3d11_resolve_available_target(
                    VGFX3D_D3D11_TARGET_SCENE, 1, 0, 0) == VGFX3D_D3D11_TARGET_SCENE,
                "Scene target stays selected when scene resources exist");
    EXPECT_TRUE(vgfx3d_d3d11_resolve_available_target(
                    VGFX3D_D3D11_TARGET_SCENE, 0, 0, 0) == VGFX3D_D3D11_TARGET_SWAPCHAIN,
                "Scene target falls back to swapchain when allocation failed");
    EXPECT_TRUE(vgfx3d_d3d11_resolve_available_target(
                    VGFX3D_D3D11_TARGET_OVERLAY, 1, 0, 0) == VGFX3D_D3D11_TARGET_SCENE,
                "Missing overlay target preserves the existing scene color target");
    EXPECT_TRUE(vgfx3d_d3d11_resolve_available_target(
                    VGFX3D_D3D11_TARGET_OVERLAY, 0, 0, 0) == VGFX3D_D3D11_TARGET_SWAPCHAIN,
                "Missing overlay and scene targets fall back to swapchain");
    EXPECT_TRUE(vgfx3d_d3d11_resolve_available_target(
                    VGFX3D_D3D11_TARGET_RTT, 1, 1, 0) == VGFX3D_D3D11_TARGET_SWAPCHAIN,
                "Missing RTT resources do not leave the backend targeting stale RTVs");
    EXPECT_TRUE(vgfx3d_d3d11_resolve_available_target(
                    (vgfx3d_d3d11_target_kind_t)99, 1, 1, 1) ==
                    VGFX3D_D3D11_TARGET_SWAPCHAIN,
                "Invalid target kinds fall back to the swapchain");
}

static void test_morph_cache_reuse_helper(void) {
    vgfx3d_draw_cmd_t cmd;
    float deltas[9] = {0};
    float weights[3] = {0};
    float normal_deltas[9] = {0};

    memset(&cmd, 0, sizeof(cmd));
    cmd.morph_key = &cmd;
    cmd.morph_revision = 7;
    cmd.morph_deltas = deltas;
    cmd.morph_weights = weights;
    cmd.morph_shape_count = 3;
    cmd.vertex_count = 1;

    EXPECT_TRUE(vgfx3d_d3d11_should_reuse_morph_cache(
                    cmd.morph_key, 7, 3, 1, 0, &cmd) == 1,
                "Morph cache reuse accepts matching position-only payloads");
    EXPECT_TRUE(vgfx3d_d3d11_should_reuse_morph_cache(
                    cmd.morph_key, 6, 3, 1, 0, &cmd) == 0,
                "Morph cache reuse rejects stale revisions");
    EXPECT_TRUE(vgfx3d_d3d11_should_reuse_morph_cache(
                    cmd.morph_key, 7, 2, 1, 0, &cmd) == 0,
                "Morph cache reuse rejects mismatched shape counts");
    cmd.morph_normal_deltas = normal_deltas;
    EXPECT_TRUE(vgfx3d_d3d11_should_reuse_morph_cache(
                    cmd.morph_key, 7, 3, 1, 0, &cmd) == 0,
                "Morph cache reuse includes normal-delta presence");
    EXPECT_TRUE(vgfx3d_d3d11_should_reuse_morph_cache(
                    cmd.morph_key, 7, 3, 1, 1, &cmd) == 1,
                "Morph cache reuse accepts matching normal-delta payloads");
    cmd.morph_shape_count = VGFX3D_D3D11_MAX_MORPH_SHAPES + 4;
    EXPECT_TRUE(vgfx3d_d3d11_should_reuse_morph_cache(
                    cmd.morph_key, 7, VGFX3D_D3D11_MAX_MORPH_SHAPES, 1, 1, &cmd) == 1,
                "Morph cache reuse compares against the clamped D3D11 shape count");
}

static void test_shadow_and_rtt_policy_helpers(void) {
    int complete_none[VGFX3D_MAX_SHADOW_LIGHTS] = {0};
    int complete_first[VGFX3D_MAX_SHADOW_LIGHTS] = {0};
    int complete_sparse[VGFX3D_MAX_SHADOW_LIGHTS] = {0};
    int complete_all[VGFX3D_MAX_SHADOW_LIGHTS] = {0};

    complete_first[0] = 1;
    if (VGFX3D_MAX_SHADOW_LIGHTS > 1)
        complete_sparse[1] = 1;
    for (int i = 0; i < VGFX3D_MAX_SHADOW_LIGHTS; i++)
        complete_all[i] = 1;

    EXPECT_TRUE(vgfx3d_d3d11_compute_shadow_count(
                    VGFX3D_MAX_SHADOW_LIGHTS, complete_none) == 0,
                "Shadow count helper reports no advertised slots when slot zero is absent");
    EXPECT_TRUE(vgfx3d_d3d11_compute_shadow_count(
                    VGFX3D_MAX_SHADOW_LIGHTS, complete_first) == 1,
                "Shadow count helper advertises the first complete slot");
    EXPECT_TRUE(vgfx3d_d3d11_compute_shadow_count(
                    VGFX3D_MAX_SHADOW_LIGHTS, complete_sparse) == 0,
                "Shadow count helper rejects sparse shadow slots");
    EXPECT_TRUE(vgfx3d_d3d11_compute_shadow_count(
                    VGFX3D_MAX_SHADOW_LIGHTS, complete_all) == VGFX3D_MAX_SHADOW_LIGHTS,
                "Shadow count helper advertises only a contiguous complete prefix");

    EXPECT_TRUE(vgfx3d_d3d11_sanitize_shadow_index(0, 1) == 0,
                "Shadow index sanitizer preserves valid advertised slots");
    EXPECT_TRUE(vgfx3d_d3d11_sanitize_shadow_index(1, 1) == -1,
                "Shadow index sanitizer disables slots outside the advertised range");
    EXPECT_TRUE(vgfx3d_d3d11_sanitize_shadow_index(-1, 1) == -1,
                "Shadow index sanitizer keeps negative slots unshadowed");

    EXPECT_TRUE(vgfx3d_d3d11_should_mark_rtt_dirty(1, 1, 1, 1, 1, 1, 1) == 1,
                "RTT dirty helper accepts complete active RTT state");
    EXPECT_TRUE(vgfx3d_d3d11_should_mark_rtt_dirty(1, 1, 1, 0, 1, 1, 1) == 0,
                "RTT dirty helper rejects partial color target state");
    EXPECT_TRUE(vgfx3d_d3d11_should_mark_rtt_dirty(0, 1, 1, 1, 1, 1, 1) == 0,
                "RTT dirty helper ignores inactive RTT state");
}

int main(void) {
    test_pack_bone_palette_identity_pads_unused_bones();
    test_pack_bone_palette_identity_pads_empty_source();
    test_pack_bone_palette_keeps_highest_supported_bone();
    test_bone_palette_upload_size_covers_supported_bone_count();
    test_pack_scalar_array4_matches_hlsl_layout();
    test_constant_buffer_struct_sizes_match_expected_layout();
    test_fill_instance_data_uses_previous_or_current_matrices();
    test_frame_history_preserves_scene_state_across_overlay_passes();
    test_upload_status_helpers_drop_stale_state();
    test_target_kind_blend_and_color_format_helpers();
    test_capacity_and_mip_helpers();
    test_d3d11_limits_and_prune_helpers();
    test_target_fallback_helper();
    test_morph_cache_reuse_helper();
    test_shadow_and_rtt_policy_helpers();

    printf("vgfx3d d3d11 shared tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
