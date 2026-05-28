//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_metal_shared.h
// Purpose: Shared declarations/constants for the Metal vgfx3d backend —
//   bone-palette limits and shared uniform layouts used by the Objective-C
//   Metal backend and its C shared support unit.
//
// Key invariants:
//   - Layouts here must match the Metal shaders and stay consistent with the
//     D3D11/OpenGL shared layouts (same bone/light limits).
//   - Internal to the Metal backend; not part of the public Viper API.
//
// Ownership/Lifetime:
//   - Declarations only; no allocation or ownership semantics here.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_metal_shared.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vgfx3d_backend.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGFX3D_METAL_MAX_BONES 256
#define VGFX3D_METAL_BONE_PALETTE_FLOATS (VGFX3D_METAL_MAX_BONES * 16u)
#define VGFX3D_METAL_BONE_PALETTE_BYTES (sizeof(float) * VGFX3D_METAL_BONE_PALETTE_FLOATS)
#define VGFX3D_METAL_MAX_MORPH_SHAPES 32

/// @brief Blend state required by a draw: opaque, standard alpha, or additive.
typedef enum {
    VGFX3D_METAL_BLEND_OPAQUE = 0,
    VGFX3D_METAL_BLEND_ALPHA = 1,
    VGFX3D_METAL_BLEND_ADDITIVE = 2,
} vgfx3d_metal_blend_mode_t;

/// @brief Render-target classification: swapchain, offscreen HDR scene, RTT, or overlay.
typedef enum {
    VGFX3D_METAL_TARGET_SWAPCHAIN = 0,
    VGFX3D_METAL_TARGET_SCENE = 1,
    VGFX3D_METAL_TARGET_RTT = 2,
    VGFX3D_METAL_TARGET_OVERLAY = 3,
} vgfx3d_metal_target_kind_t;

/// @brief Color format of a target: 8-bit UNORM (display) or 16-bit float (HDR scene).
typedef enum {
    VGFX3D_METAL_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_METAL_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_metal_color_format_t;

/// @brief Whether a pass attaches only color or also the motion-vector target.
typedef enum {
    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_ONLY = 0,
    VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_AND_MOTION = 1,
} vgfx3d_metal_motion_attachment_mode_t;

/// @brief Source for canvas readback: the presented backbuffer or the post-FX composite.
typedef enum {
    VGFX3D_METAL_READBACK_BACKBUFFER = 0,
    VGFX3D_METAL_READBACK_POSTFX_COMPOSITE = 1,
} vgfx3d_metal_readback_kind_t;

/// @brief Per-frame view/projection history for motion vectors (current/previous/inverse
///   scene VP, draw's previous VP, camera position, and scene/overlay validity flags).
typedef struct {
    float scene_vp[16];
    float scene_prev_vp[16];
    float scene_inv_vp[16];
    float draw_prev_vp[16];
    float scene_cam_pos[3];
    int8_t scene_history_valid;
    int8_t overlay_used_this_frame;
} vgfx3d_metal_frame_history_t;

/// @brief One per-instance Metal buffer entry: model, normal, and previous-frame model
///   matrices (column-major-transposed for MSL by fill_instance_data).
typedef struct {
    float model[16];
    float normal[16];
    float prev_model[16];
} vgfx3d_metal_instance_data_t;

/// @brief Copy bone palette into a fixed-size MTLBuffer slot (identity-pads unused bones).
void vgfx3d_metal_pack_bone_palette(float *dst, const float *src, int32_t bone_count);
/// @brief Build per-instance Metal buffer entries with column-major transpose for MSL.
void vgfx3d_metal_fill_instance_data(vgfx3d_metal_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices);
/// @brief Roll per-frame VP/inv-VP/cam-pos history forward by one frame.
void vgfx3d_metal_update_frame_history(vgfx3d_metal_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target);
/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_metal_compute_mip_count(int32_t width, int32_t height);
/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_metal_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity);
/// @brief Clamp morph shapes so shader-side int indexing cannot overflow.
int32_t vgfx3d_metal_clamp_morph_shape_count(uint32_t vertex_count,
                                             int32_t requested_shape_count);
/// @brief Pick the right render-target classification for the Metal backend.
vgfx3d_metal_target_kind_t vgfx3d_metal_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color);
/// @brief Decide whether the next pass should preserve existing color contents.
int8_t vgfx3d_metal_should_load_existing_color(vgfx3d_metal_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame);
/// @brief Pick the color format — HDR16F for the scene pass, UNORM8 elsewhere.
vgfx3d_metal_color_format_t
vgfx3d_metal_choose_color_format(vgfx3d_metal_target_kind_t target_kind);
/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_metal_blend_mode_t
vgfx3d_metal_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether terrain splatting has every required texture bound.
int vgfx3d_metal_has_complete_splat(int8_t cmd_has_splat,
                                    int has_splat_map,
                                    int has_layer0,
                                    int has_layer1,
                                    int has_layer2,
                                    int has_layer3);
/// @brief Decide whether to attach a motion-vector buffer (only for opaque scene draws).
vgfx3d_metal_motion_attachment_mode_t
vgfx3d_metal_choose_motion_attachment_mode(vgfx3d_metal_target_kind_t target_kind,
                                           const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether canvas readback should source the backbuffer or postfx target.
vgfx3d_metal_readback_kind_t vgfx3d_metal_choose_readback_kind(int8_t gpu_postfx_enabled);
/// @brief Clamp light shadow indices to the currently completed contiguous shadow slots.
int32_t vgfx3d_metal_sanitize_shadow_index(int32_t shadow_index, int32_t shadow_count);
/// @brief Decide whether to reuse a cached morph-target Metal buffer (key + revision + counts match).
int vgfx3d_metal_should_reuse_morph_cache(const void *cached_key,
                                          uint64_t cached_revision,
                                          int32_t cached_shape_count,
                                          uint32_t cached_vertex_count,
                                          int8_t cached_has_normal_deltas,
                                          const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether a per-mesh GPU cache entry should be evicted (unused for > max_age frames).
int vgfx3d_metal_should_prune_cache_entry(uint64_t current_frame,
                                          uint64_t last_used_frame,
                                          uint64_t max_age);

#ifdef __cplusplus
}
#endif
