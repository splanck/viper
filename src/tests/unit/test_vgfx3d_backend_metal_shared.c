//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_vgfx3d_backend_metal_shared.c
// Purpose: Unit tests for Metal backend shared helper functions.
//
// Key invariants:
//   - Shared helper math and cache policies are deterministic without a Metal device.
//   - Matrix staging transposes row-major runtime data for MSL expectations.
//   - Sampler-state cache indices stay clamped to their fixed array bounds.
//
// Ownership/Lifetime:
//   - Tests allocate only stack-local fixtures and transient file buffers.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_metal_shared.c
//
//===----------------------------------------------------------------------===//

#ifndef ZANNA_ENABLE_GRAPHICS
#define ZANNA_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend_metal_shared.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ZANNA_SOURCE_DIR
#define ZANNA_SOURCE_DIR "."
#endif

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
    EXPECT_NEAR(dst[16], 1.0f, 1e-6f, "Bone palette identity-pads the second bone X scale");
    EXPECT_NEAR(dst[21], 1.0f, 1e-6f, "Bone palette identity-pads the second bone Y scale");
    EXPECT_NEAR(dst[26], 1.0f, 1e-6f, "Bone palette identity-pads the second bone Z scale");
    EXPECT_NEAR(dst[31], 1.0f, 1e-6f, "Bone palette identity-pads the second bone W scale");
    EXPECT_NEAR(dst[17], 0.0f, 1e-6f, "Bone palette clears off-diagonal identity padding");
}

static void test_pack_bone_palette_identity_pads_empty_source(void) {
    float dst[VGFX3D_METAL_MAX_BONES * 16];

    memset(dst, 0xCD, sizeof(dst));
    vgfx3d_metal_pack_bone_palette(dst, NULL, 0);

    EXPECT_NEAR(dst[0], 1.0f, 1e-6f, "Empty bone palette identity-pads the first bone");
    EXPECT_NEAR(dst[5], 1.0f, 1e-6f, "Empty bone palette identity-pads the first bone");
    EXPECT_NEAR(dst[10], 1.0f, 1e-6f, "Empty bone palette identity-pads the first bone");
    EXPECT_NEAR(dst[15], 1.0f, 1e-6f, "Empty bone palette identity-pads the first bone");
    EXPECT_NEAR(dst[1], 0.0f, 1e-6f, "Empty bone palette clears off-diagonal slots");
}

static void test_pack_bone_palette_keeps_highest_supported_bone(void) {
    float src[VGFX3D_METAL_MAX_BONES * 16];
    float dst[VGFX3D_METAL_MAX_BONES * 16];
    size_t tail = (size_t)(VGFX3D_METAL_MAX_BONES - 1) * 16u;

    for (size_t i = 0; i < sizeof(src) / sizeof(src[0]); i++)
        src[i] = (float)i;
    memset(dst, 0, sizeof(dst));
    vgfx3d_metal_pack_bone_palette(dst, src, VGFX3D_METAL_MAX_BONES);

    EXPECT_NEAR(dst[tail + 0],
                src[tail + 0],
                1e-6f,
                "Metal bone packing preserves the final supported bone");
    EXPECT_NEAR(dst[tail + 15],
                src[tail + 15],
                1e-6f,
                "Metal bone packing preserves the tail matrix element of the final supported bone");
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
    EXPECT_NEAR(instances[0].model[12],
                2.0f,
                1e-6f,
                "Instance staging transposes model matrices to Metal column-major layout");
    EXPECT_NEAR(instances[0].prev_model[12],
                1.0f,
                1e-6f,
                "Instance staging preserves previous model matrices");
    EXPECT_NEAR(instances[1].prev_model[12],
                -6.0f,
                1e-6f,
                "Instance staging preserves the second previous model matrix");

    memset(instances, 0, sizeof(instances));
    vgfx3d_metal_fill_instance_data(instances, 2, models, NULL, 0);
    EXPECT_NEAR(instances[1].prev_model[12],
                instances[1].model[12],
                1e-6f,
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
    EXPECT_NEAR(history.scene_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "First main pass seeds prevViewProjection from the current scene");
    EXPECT_NEAR(history.draw_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "First main pass uses the current VP as draw-time history");

    vgfx3d_metal_update_frame_history(&history, scene_vp1, inv1, cam1, 0, 0);
    EXPECT_NEAR(history.scene_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "Second main pass preserves the previous scene VP");
    EXPECT_NEAR(
        history.scene_vp[0], scene_vp1[0], 1e-6f, "Second main pass updates the current scene VP");
    EXPECT_NEAR(history.draw_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "Second main pass draws against the prior scene VP");
    EXPECT_NEAR(
        history.scene_inv_vp[0], inv1[0], 1e-6f, "Second main pass updates the scene inverse VP");
    EXPECT_NEAR(history.scene_cam_pos[0],
                cam1[0],
                1e-6f,
                "Second main pass updates the scene camera position");

    vgfx3d_metal_update_frame_history(&history, overlay_vp, overlay_inv, cam0, 1, 1);
    EXPECT_NEAR(history.scene_vp[0],
                scene_vp1[0],
                1e-6f,
                "Overlay pass preserves the scene VP for later postfx");
    EXPECT_NEAR(history.scene_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "Overlay pass preserves the previous scene VP");
    EXPECT_NEAR(
        history.scene_inv_vp[0], inv1[0], 1e-6f, "Overlay pass preserves the scene inverse VP");
    EXPECT_NEAR(history.scene_cam_pos[0],
                cam1[0],
                1e-6f,
                "Overlay pass preserves the scene camera position");
    EXPECT_NEAR(history.draw_prev_vp[0],
                overlay_vp[0],
                1e-6f,
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
    cmd.alpha = 0.5f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_ALPHA,
                "Legacy translucent materials use alpha blending");
    cmd.alpha = 1.0f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_ALPHA,
                "Legacy explicit blend materials use alpha blending");
    cmd.alpha = 0.25f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_metal_choose_blend_mode(&cmd) == VGFX3D_METAL_BLEND_OPAQUE,
                "Legacy masked materials stay on the opaque render path");

    EXPECT_TRUE(vgfx3d_metal_choose_readback_kind(0) == VGFX3D_METAL_READBACK_BACKBUFFER,
                "Direct rendering reads back the backbuffer");
    EXPECT_TRUE(vgfx3d_metal_choose_readback_kind(1) == VGFX3D_METAL_READBACK_POSTFX_COMPOSITE,
                "GPU postfx readback uses the composited postfx path");
}

static void test_capacity_mip_and_morph_cache_helpers(void) {
    vgfx3d_draw_cmd_t cmd;

    EXPECT_TRUE(vgfx3d_metal_compute_mip_count(1, 1) == 1, "1x1 textures use a single mip level");
    EXPECT_TRUE(vgfx3d_metal_compute_mip_count(4, 2) == 3,
                "Mip-count helper follows the full downsample chain");
    EXPECT_TRUE(vgfx3d_metal_next_capacity(0, 65, 64) == 128,
                "Capacity helper grows beyond the old fixed cache size");
    EXPECT_TRUE(vgfx3d_metal_next_capacity(16, 8, 16) == 16,
                "Capacity helper keeps existing storage when it is already large enough");
    EXPECT_TRUE(vgfx3d_metal_clamp_morph_shape_count(1024u, 64) == VGFX3D_METAL_MAX_MORPH_SHAPES,
                "Morph count helper clamps to the shader-visible shape limit");
    EXPECT_TRUE(vgfx3d_metal_clamp_morph_shape_count((uint32_t)INT_MAX, 1) == 0,
                "Morph count helper rejects vertex counts that would overflow int indexing");
    EXPECT_TRUE(vgfx3d_metal_clamp_morph_shape_count(1u << 28, 8) == 2,
                "Morph count helper clamps to the signed shader index range");
    EXPECT_TRUE(vgfx3d_metal_has_complete_splat(1, 1, 1, 1, 1, 1) == 1,
                "Terrain splat helper accepts a complete control map and four layers");
    EXPECT_TRUE(vgfx3d_metal_has_complete_splat(1, 1, 1, 0, 1, 1) == 0,
                "Terrain splat helper rejects partial layer bindings");
    EXPECT_TRUE(vgfx3d_metal_should_prune_cache_entry(300, 10, 240) == 1,
                "Old cache entries become prune candidates");
    EXPECT_TRUE(vgfx3d_metal_should_prune_cache_entry(200, 10, 240) == 0,
                "Recently used cache entries stay resident");
    EXPECT_TRUE(vgfx3d_metal_sanitize_anisotropy(0) == 1,
                "Metal sampler anisotropy clamps zero to one");
    EXPECT_TRUE(vgfx3d_metal_sanitize_anisotropy(64) == 16,
                "Metal sampler anisotropy clamps high values to sixteen");
    EXPECT_TRUE(vgfx3d_metal_sanitize_anisotropy(8) == 8,
                "Metal sampler anisotropy preserves valid values");
    EXPECT_TRUE(vgfx3d_metal_sampler_anisotropy_index(1) == 0,
                "Metal sampler anisotropy index starts at zero");
    EXPECT_TRUE(vgfx3d_metal_sampler_anisotropy_index(16) == 15,
                "Metal sampler anisotropy index covers the final cache slot");
    EXPECT_TRUE(vgfx3d_metal_sanitize_shadow_index(1, 2) == 1,
                "Shadow index helper preserves completed shadow slots");
    EXPECT_TRUE(vgfx3d_metal_sanitize_shadow_index(2, 2) == -1,
                "Shadow index helper rejects slots beyond the completed count");
    EXPECT_TRUE(vgfx3d_metal_sanitize_shadow_index(0, 0) == -1,
                "Shadow index helper rejects all slots when no shadow maps completed");
    EXPECT_TRUE(vgfx3d_metal_sanitize_shadow_index(3, 99) == -1,
                "Shadow index helper still clamps to the backend maximum slot count");

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
    cmd.morph_shape_count = VGFX3D_METAL_MAX_MORPH_SHAPES + 4;
    EXPECT_TRUE(vgfx3d_metal_should_reuse_morph_cache(
                    cmd.morph_key, 4, VGFX3D_METAL_MAX_MORPH_SHAPES, 128, 1, &cmd) == 1,
                "Morph cache reuse compares against the clamped Metal shape count");
}

static void test_shadow_projection_helper_handles_orthographic_and_perspective(void) {
    float m[16];
    float world[3] = {0.25f, -0.25f, 2.0f};
    float uv_depth[3];

    memset(m, 0, sizeof(m));
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 2.0f;
    EXPECT_TRUE(vgfx3d_metal_project_shadow_coord(
                    m, VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC, world, uv_depth) == 1,
                "Metal orthographic shadow projection accepts finite coordinates");
    EXPECT_NEAR(uv_depth[0], 0.625f, 1e-6f, "Metal orthographic shadow UV does not divide by W");
    EXPECT_NEAR(uv_depth[1], 0.625f, 1e-6f, "Metal orthographic shadow UV flips Y");

    memset(m, 0, sizeof(m));
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 0.5f;
    m[14] = 1.0f;
    EXPECT_TRUE(vgfx3d_metal_project_shadow_coord(
                    m, VGFX3D_SHADOW_PROJECTION_PERSPECTIVE, world, uv_depth) == 1,
                "Metal perspective shadow projection accepts positive W");
    EXPECT_NEAR(uv_depth[0], 0.5625f, 1e-6f, "Metal perspective shadow UV divides by W");
    EXPECT_NEAR(uv_depth[1], 0.5625f, 1e-6f, "Metal perspective shadow UV flips Y after divide");
    EXPECT_NEAR(uv_depth[2], 0.75f, 1e-6f, "Metal perspective shadow depth divides by W");

    world[2] = 0.0f;
    EXPECT_TRUE(vgfx3d_metal_project_shadow_coord(
                    m, VGFX3D_SHADOW_PROJECTION_PERSPECTIVE, world, uv_depth) == 0,
                "Metal perspective shadow projection rejects non-positive W");
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long size;
    char *text;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    text = (char *)malloc((size_t)size + 1u);
    if (!text) {
        fclose(f);
        return NULL;
    }
    if (fread(text, 1u, (size_t)size, f) != (size_t)size) {
        free(text);
        fclose(f);
        return NULL;
    }
    text[size] = '\0';
    fclose(f);
    return text;
}

/// @brief Read and concatenate the Metal backend's translation unit and its .inc chunks.
/// @details The backend is one TU split into sequential textual chunks; shader-source
///   regression checks must scan the whole set (the MSL library lives in the shaders
///   chunk, the skybox/post-FX sources in the context chunk).
static char *read_metal_backend_sources(void) {
    static const char *k_parts[] = {
        "vgfx3d_backend_metal.m",
        "vgfx3d_backend_metal_shaders.inc",
        "vgfx3d_backend_metal_targets.inc",
        "vgfx3d_backend_metal_present.inc",
        "vgfx3d_backend_metal_context.inc",
        "vgfx3d_backend_metal_texture.inc",
        "vgfx3d_backend_metal_draw.inc",
    };
    char path[1024];
    char *combined = NULL;
    size_t combined_len = 0;

    for (size_t i = 0; i < sizeof(k_parts) / sizeof(k_parts[0]); i++) {
        char *part;
        size_t part_len;
        char *grown;
        snprintf(path,
                 sizeof(path),
                 "%s/src/runtime/graphics/3d/backend/%s",
                 ZANNA_SOURCE_DIR,
                 k_parts[i]);
        part = read_text_file(path);
        if (!part) {
            free(combined);
            return NULL;
        }
        part_len = strlen(part);
        grown = (char *)realloc(combined, combined_len + part_len + 1u);
        if (!grown) {
            free(part);
            free(combined);
            return NULL;
        }
        combined = grown;
        memcpy(combined + combined_len, part, part_len + 1u);
        combined_len += part_len;
        free(part);
    }
    return combined;
}

static void test_metal_shader_source_uses_safe_normalization(void) {
    char *source = read_metal_backend_sources();
    EXPECT_TRUE(source != NULL,
                "Metal backend source chunks are readable for shader-source regression checks");
    if (!source)
        return;

    EXPECT_TRUE(
        strstr(source, "return (len2 > 1e-12 && len2 < 1e20) ? v * rsqrt(len2) : fallback;") !=
            NULL,
        "Metal safe_normalize3 rejects non-finite and huge vector lengths");
    EXPECT_TRUE(strstr(source, "skybox_safe_normalize3") != NULL,
                "Metal skybox shader defines a safe normalization helper");
    EXPECT_TRUE(strstr(source, "worldDir = skybox_safe_normalize3") != NULL,
                "Metal skybox camera-forward path uses safe normalization");
    EXPECT_TRUE(strstr(source, "float3 viewDir = skybox_safe_normalize3") != NULL,
                "Metal skybox inverse-projection path uses safe normalization");
    EXPECT_TRUE(strstr(source, "float3(0.0, 0.0, -1.0)") != NULL,
                "Metal skybox zero-vector fallback follows the Canvas3D camera -Z convention");
    EXPECT_TRUE(strstr(source, "eval_native_light") != NULL &&
                    strstr(source, "native_light_decay") != NULL,
                "Metal shader retains native area/volume evaluation and FBX decay");
    EXPECT_TRUE(strstr(source, "float4 basisU") != NULL &&
                    strstr(source, "float4 basisV") != NULL &&
                    strstr(source, "float4 shape") != NULL,
                "Metal light layout retains emitter basis and dimensions");
    EXPECT_TRUE(strstr(source, " normalize(") == NULL,
                "Metal shader source avoids raw normalize calls");

    free(source);
}

static void test_metal_mipmap_generation_never_waits_on_cpu(void) {
    char *source = read_metal_backend_sources();
    const char *begin;
    const char *end;
    const char *commit;
    const char *wait;

    EXPECT_TRUE(source != NULL,
                "Metal backend source chunks are readable for mipmap synchronization checks");
    if (!source)
        return;
    begin = strstr(source, "static void metal_generate_mipmaps");
    end = begin ? strstr(begin, "\n}\n") : NULL;
    EXPECT_TRUE(begin != NULL && end != NULL, "Metal mipmap helper body is present");
    if (begin && end) {
        commit = strstr(begin, "[cmd_buf commit]");
        wait = strstr(begin, "waitUntilCompleted");
        EXPECT_TRUE(commit != NULL && commit < end, "Metal mipmap helper commits its blit work");
        EXPECT_TRUE(wait == NULL || wait >= end,
                    "Metal mipmap helper never blocks the CPU for GPU completion");
    }
    free(source);
}

int main(void) {
    test_pack_bone_palette_identity_pads_unused_bones();
    test_pack_bone_palette_identity_pads_empty_source();
    test_pack_bone_palette_keeps_highest_supported_bone();
    test_fill_instance_data_transposes_and_tracks_history();
    test_frame_history_preserves_scene_state_across_overlay_passes();
    test_target_kind_blend_motion_and_readback_helpers();
    test_capacity_mip_and_morph_cache_helpers();
    test_shadow_projection_helper_handles_orthographic_and_perspective();
    test_metal_shader_source_uses_safe_normalization();
    test_metal_mipmap_generation_never_waits_on_cpu();

    printf("vgfx3d metal shared tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
