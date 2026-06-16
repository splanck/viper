//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_vgfx3d_backend_opengl_shared.c
// Purpose: Unit tests for OpenGL backend shared helper functions.
//
// Key invariants:
//   - Shared helper math and cache policies are deterministic without an OpenGL context.
//   - Render-target, blend, and readback policies stay consistent with backend code.
//   - Sampler-state cache indices stay clamped to their fixed array bounds.
//
// Ownership/Lifetime:
//   - Tests allocate only stack-local fixtures and process-local scratch buffers.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_shared.c
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend_opengl_shared.h"

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

static void test_frame_history_preserves_scene_state_across_overlay_passes(void) {
    vgfx3d_opengl_frame_history_t history;
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

    vgfx3d_opengl_update_frame_history(&history, scene_vp0, inv0, cam0, 0);
    EXPECT_TRUE(history.scene_history_valid == 1,
                "Main-pass history becomes valid after the first scene");
    EXPECT_NEAR(history.scene_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "First scene seeds prevViewProjection from the current scene");
    EXPECT_NEAR(history.draw_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "First scene uses the current VP as draw-time history");

    vgfx3d_opengl_update_frame_history(&history, scene_vp1, inv1, cam1, 0);
    EXPECT_NEAR(history.scene_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "Second scene preserves the previous scene VP");
    EXPECT_NEAR(
        history.scene_vp[0], scene_vp1[0], 1e-6f, "Second scene updates the current scene VP");
    EXPECT_NEAR(history.draw_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "Second scene draws against the prior scene VP");
    EXPECT_NEAR(
        history.scene_inv_vp[0], inv1[0], 1e-6f, "Second scene updates the scene inverse VP");
    EXPECT_NEAR(
        history.scene_cam_pos[0], cam1[0], 1e-6f, "Second scene updates the scene camera position");

    vgfx3d_opengl_update_frame_history(&history, overlay_vp, overlay_inv, cam0, 1);
    EXPECT_NEAR(history.scene_vp[0],
                scene_vp1[0],
                1e-6f,
                "Overlay passes preserve the main-scene VP for later postfx");
    EXPECT_NEAR(history.scene_prev_vp[0],
                scene_vp0[0],
                1e-6f,
                "Overlay passes preserve the previous main-scene VP");
    EXPECT_NEAR(history.scene_inv_vp[0],
                inv1[0],
                1e-6f,
                "Overlay passes preserve the main-scene inverse VP");
    EXPECT_NEAR(history.draw_prev_vp[0],
                overlay_vp[0],
                1e-6f,
                "Overlay passes use their own VP for draw-time history");
}

static void test_target_blend_motion_and_readback_helpers(void) {
    vgfx3d_draw_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    EXPECT_TRUE(vgfx3d_opengl_choose_target_kind(1, 1) == VGFX3D_OPENGL_TARGET_RTT,
                "RTT rendering always chooses the RTT target kind");
    EXPECT_TRUE(vgfx3d_opengl_choose_target_kind(0, 0) == VGFX3D_OPENGL_TARGET_SWAPCHAIN,
                "Without GPU postfx the backend renders directly to the swapchain");
    EXPECT_TRUE(vgfx3d_opengl_choose_target_kind(0, 1) == VGFX3D_OPENGL_TARGET_SCENE,
                "GPU postfx main passes render into the scene target");

    EXPECT_TRUE(vgfx3d_opengl_choose_color_format(VGFX3D_OPENGL_TARGET_SCENE) ==
                    VGFX3D_OPENGL_COLOR_FORMAT_HDR16F,
                "Scene rendering uses the HDR color target");
    EXPECT_TRUE(vgfx3d_opengl_choose_color_format(VGFX3D_OPENGL_TARGET_SWAPCHAIN) ==
                        VGFX3D_OPENGL_COLOR_FORMAT_UNORM8 &&
                    vgfx3d_opengl_choose_color_format(VGFX3D_OPENGL_TARGET_RTT) ==
                        VGFX3D_OPENGL_COLOR_FORMAT_UNORM8,
                "Swapchain and RTT targets use UNORM8 output");

    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_opengl_choose_blend_mode(&cmd) == VGFX3D_OPENGL_BLEND_ALPHA,
                "Blend materials use alpha blending");
    EXPECT_TRUE(vgfx3d_opengl_choose_motion_attachment_mode(VGFX3D_OPENGL_TARGET_SCENE, &cmd) ==
                    VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY,
                "Alpha-blended scene draws disable the motion attachment");
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_opengl_choose_blend_mode(&cmd) == VGFX3D_OPENGL_BLEND_OPAQUE,
                "Mask materials keep opaque render-target writes");
    EXPECT_TRUE(vgfx3d_opengl_choose_motion_attachment_mode(VGFX3D_OPENGL_TARGET_SCENE, &cmd) ==
                    VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_AND_MOTION,
                "Opaque scene draws keep the motion attachment enabled");
    EXPECT_TRUE(vgfx3d_opengl_choose_motion_attachment_mode(VGFX3D_OPENGL_TARGET_SWAPCHAIN, &cmd) ==
                    VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY,
                "Swapchain draws never target a scene-motion attachment");

    memset(&cmd, 0, sizeof(cmd));
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    cmd.alpha = 0.5f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    EXPECT_TRUE(vgfx3d_opengl_choose_blend_mode(&cmd) == VGFX3D_OPENGL_BLEND_ALPHA,
                "Legacy translucent materials use alpha blending");
    cmd.alpha = 1.0f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_opengl_choose_blend_mode(&cmd) == VGFX3D_OPENGL_BLEND_ALPHA,
                "Legacy explicit blend materials use alpha blending");
    cmd.alpha = 0.25f;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(vgfx3d_opengl_choose_blend_mode(&cmd) == VGFX3D_OPENGL_BLEND_OPAQUE,
                "Legacy masked materials stay on the opaque render path");

    EXPECT_TRUE(vgfx3d_opengl_choose_readback_kind(0) == VGFX3D_OPENGL_READBACK_BACKBUFFER,
                "Direct rendering reads back the backbuffer");
    EXPECT_TRUE(vgfx3d_opengl_choose_readback_kind(1) == VGFX3D_OPENGL_READBACK_POSTFX_COMPOSITE,
                "GPU postfx readback uses the composited postfx path");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_shadow_index(1, 2) == 1,
                "Shadow index helper preserves completed shadow slots");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_shadow_index(2, 2) == -1,
                "Shadow index helper rejects slots beyond the completed count");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_shadow_index(0, 0) == -1,
                "Shadow index helper rejects all slots when no shadow maps completed");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_shadow_index(3, 99) == -1,
                "Shadow index helper still clamps to the backend maximum slot count");
}

static void test_capacity_and_cache_helpers(void) {
    vgfx3d_draw_cmd_t cmd;
    size_t bytes = 0;
    size_t capacity = 0;

    EXPECT_TRUE(vgfx3d_opengl_compute_mip_count(1, 1) == 1, "1x1 textures use a single mip level");
    EXPECT_TRUE(vgfx3d_opengl_compute_mip_count(4, 2) == 3,
                "Mip-count helper follows the full downsample chain");
    EXPECT_TRUE(vgfx3d_opengl_next_capacity(0, 65, 64) == 128,
                "Capacity helper grows beyond the old fixed cache size");
    EXPECT_TRUE(vgfx3d_opengl_next_capacity(16, 8, 16) == 16,
                "Capacity helper keeps existing storage when it is already large enough");
    EXPECT_TRUE(vgfx3d_opengl_compute_buffer_capacity(0, 65, 64, &capacity) == 1 && capacity == 128,
                "Size_t capacity helper grows without narrowing through int32_t");
    EXPECT_TRUE(vgfx3d_opengl_compute_buffer_capacity(SIZE_MAX / 2 + 1, SIZE_MAX, 64, &capacity) ==
                        1 &&
                    capacity == SIZE_MAX,
                "Size_t capacity helper reaches exact oversized needs without doubling overflow");
    EXPECT_TRUE(vgfx3d_opengl_validate_rgba8_destination(3, 2, 12, &bytes) == 1 && bytes == 24,
                "RGBA8 destination validation accepts a tight destination");
    EXPECT_TRUE(vgfx3d_opengl_validate_rgba8_destination(3, 2, 8, &bytes) == 0,
                "RGBA8 destination validation rejects short strides");
    EXPECT_TRUE(vgfx3d_opengl_clamp_morph_shape_count(1024u, 64) == VGFX3D_OPENGL_MAX_MORPH_SHAPES,
                "Morph count helper clamps to the shader-visible shape limit");
    EXPECT_TRUE(vgfx3d_opengl_clamp_morph_shape_count((uint32_t)INT_MAX, 1) == 0,
                "Morph count helper rejects vertex counts that would overflow int indexing");
    EXPECT_TRUE(vgfx3d_opengl_clamp_morph_shape_count(1u << 28, 8) == 2,
                "Morph count helper clamps to the signed shader index range");
    EXPECT_TRUE(vgfx3d_opengl_texture_buffer_accepts_r32f_payload(64u, 16) == 1,
                "R32F texture-buffer payload accepts exact texel-limit fits");
    EXPECT_TRUE(vgfx3d_opengl_texture_buffer_accepts_r32f_payload(68u, 16) == 0,
                "R32F texture-buffer payload rejects values beyond the texel limit");
    EXPECT_TRUE(vgfx3d_opengl_texture_buffer_accepts_r32f_payload(66u, 64) == 0,
                "R32F texture-buffer payload rejects non-float-aligned byte sizes");
    EXPECT_TRUE(vgfx3d_opengl_texture_buffer_accepts_r32f_payload(64u, 0) == 1,
                "R32F texture-buffer payload treats unknown GL limits as unbounded");
    EXPECT_TRUE(vgfx3d_opengl_has_complete_splat(1, 1, 1, 1, 1, 1) == 1,
                "Terrain splat helper accepts a complete control map and four layers");
    EXPECT_TRUE(vgfx3d_opengl_has_complete_splat(1, 1, 1, 0, 1, 1) == 0,
                "Terrain splat helper rejects partial layer bindings");
    EXPECT_TRUE(vgfx3d_opengl_should_prune_cache_entry(300, 10, 240) == 1,
                "Old cache entries become prune candidates");
    EXPECT_TRUE(vgfx3d_opengl_should_prune_cache_entry(200, 10, 240) == 0,
                "Recently used cache entries stay resident");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_anisotropy(0) == 1,
                "OpenGL sampler anisotropy clamps zero to one");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_anisotropy(64) == 16,
                "OpenGL sampler anisotropy clamps high values to sixteen");
    EXPECT_TRUE(vgfx3d_opengl_sanitize_anisotropy(8) == 8,
                "OpenGL sampler anisotropy preserves valid values");
    EXPECT_TRUE(vgfx3d_opengl_sampler_anisotropy_index(1) == 0,
                "OpenGL sampler anisotropy index starts at zero");
    EXPECT_TRUE(vgfx3d_opengl_sampler_anisotropy_index(16) == 15,
                "OpenGL sampler anisotropy index covers the final cache slot");

    memset(&cmd, 0, sizeof(cmd));
    cmd.morph_key = &cmd;
    cmd.morph_revision = 4;
    cmd.morph_deltas = (const float *)&cmd;
    cmd.morph_weights = (const float *)&cmd;
    cmd.morph_shape_count = 3;
    cmd.vertex_count = 128;
    cmd.morph_normal_deltas = NULL;
    EXPECT_TRUE(vgfx3d_opengl_should_reuse_morph_cache(
                    cmd.morph_key, cmd.morph_revision, 3, 128, 0, &cmd) == 1,
                "Morph cache entries reuse matching key/revision payloads");
    cmd.morph_revision = 5;
    EXPECT_TRUE(vgfx3d_opengl_should_reuse_morph_cache(cmd.morph_key, 4, 3, 128, 0, &cmd) == 0,
                "Morph cache entries reject stale revisions");
    cmd.morph_revision = 4;
    cmd.morph_normal_deltas = (const float *)&tests_run;
    EXPECT_TRUE(vgfx3d_opengl_should_reuse_morph_cache(cmd.morph_key, 4, 3, 128, 0, &cmd) == 0,
                "Morph cache entries include normal-delta presence in the cache key");
    cmd.morph_normal_deltas = (const float *)&tests_run;
    cmd.morph_shape_count = VGFX3D_OPENGL_MAX_MORPH_SHAPES + 4;
    EXPECT_TRUE(vgfx3d_opengl_should_reuse_morph_cache(
                    cmd.morph_key, 4, VGFX3D_OPENGL_MAX_MORPH_SHAPES, 128, 1, &cmd) == 1,
                "Morph cache reuse compares against the clamped OpenGL shape count");
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
    EXPECT_TRUE(vgfx3d_opengl_project_shadow_coord(
                    m, VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC, world, uv_depth) == 1,
                "OpenGL orthographic shadow projection accepts finite coordinates");
    EXPECT_NEAR(uv_depth[0], 0.625f, 1e-6f, "OpenGL orthographic shadow UV does not divide by W");
    EXPECT_NEAR(uv_depth[1], 0.375f, 1e-6f, "OpenGL orthographic shadow UV preserves GL Y");

    memset(m, 0, sizeof(m));
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 0.5f;
    m[14] = 1.0f;
    EXPECT_TRUE(vgfx3d_opengl_project_shadow_coord(
                    m, VGFX3D_SHADOW_PROJECTION_PERSPECTIVE, world, uv_depth) == 1,
                "OpenGL perspective shadow projection accepts positive W");
    EXPECT_NEAR(uv_depth[0], 0.5625f, 1e-6f, "OpenGL perspective shadow UV divides by W");
    EXPECT_NEAR(uv_depth[1], 0.4375f, 1e-6f, "OpenGL perspective shadow UV preserves GL Y");
    EXPECT_NEAR(uv_depth[2], 0.75f, 1e-6f, "OpenGL perspective shadow depth divides by W");

    world[2] = 0.0f;
    EXPECT_TRUE(vgfx3d_opengl_project_shadow_coord(
                    m, VGFX3D_SHADOW_PROJECTION_PERSPECTIVE, world, uv_depth) == 0,
                "OpenGL perspective shadow projection rejects non-positive W");
}

int main(void) {
    test_frame_history_preserves_scene_state_across_overlay_passes();
    test_target_blend_motion_and_readback_helpers();
    test_capacity_and_cache_helpers();
    test_shadow_projection_helper_handles_orthographic_and_perspective();

    printf("vgfx3d opengl shared tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
