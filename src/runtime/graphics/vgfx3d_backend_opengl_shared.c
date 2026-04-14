//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_opengl_shared.c
// Purpose: OpenGL backend helpers shared with sister backends — frame history,
//   mip math, capacity growth, and policy choices for render-target,
//   readback, and morph-cache reuse.
//
// Links: vgfx3d_backend_opengl_shared.h, vgfx3d_backend_opengl.c
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_opengl_shared.h"

#include <limits.h>
#include <string.h>

/// @brief Roll the OpenGL backend's per-frame VP/inv-VP/cam-pos history forward by one frame.
/// Mirrors the D3D11 helper; see vgfx3d_d3d11_update_frame_history for semantics.
void vgfx3d_opengl_update_frame_history(vgfx3d_opengl_frame_history_t *history,
                                        const float *vp,
                                        const float *inv_vp,
                                        const float *cam_pos,
                                        int8_t is_overlay_pass) {
    if (!history || !vp || !inv_vp)
        return;

    if (!is_overlay_pass) {
        if (history->scene_history_valid) {
            memcpy(history->scene_prev_vp, history->scene_vp, sizeof(history->scene_prev_vp));
        } else {
            memcpy(history->scene_prev_vp, vp, sizeof(history->scene_prev_vp));
            history->scene_history_valid = 1;
        }
        memcpy(history->scene_vp, vp, sizeof(history->scene_vp));
        memcpy(history->scene_inv_vp, inv_vp, sizeof(history->scene_inv_vp));
        memcpy(history->draw_prev_vp, history->scene_prev_vp, sizeof(history->draw_prev_vp));
        if (cam_pos)
            memcpy(history->scene_cam_pos, cam_pos, sizeof(history->scene_cam_pos));
        return;
    }

    memcpy(history->draw_prev_vp, vp, sizeof(history->draw_prev_vp));
}

/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_opengl_compute_mip_count(int32_t width, int32_t height) {
    int32_t mip_count = 1;

    if (width <= 0 || height <= 0)
        return 1;
    while (width > 1 || height > 1) {
        if (width > 1)
            width >>= 1;
        if (height > 1)
            height >>= 1;
        mip_count++;
    }
    return mip_count;
}

/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_opengl_next_capacity(int32_t current_capacity,
                                    int32_t needed,
                                    int32_t minimum_capacity) {
    int32_t next_capacity;

    if (needed <= 0)
        return current_capacity > 0 ? current_capacity : minimum_capacity;
    next_capacity = current_capacity > 0 ? current_capacity : minimum_capacity;
    if (next_capacity < 1)
        next_capacity = 1;
    while (next_capacity < needed) {
        if (next_capacity > INT_MAX / 2)
            return needed;
        next_capacity *= 2;
    }
    return next_capacity;
}

/// @brief Pick the right render-target classification for the OpenGL backend.
/// See vgfx3d_d3d11_choose_target_kind for the policy semantics.
vgfx3d_opengl_target_kind_t vgfx3d_opengl_choose_target_kind(int8_t rtt_active,
                                                             int8_t gpu_postfx_enabled) {
    if (rtt_active)
        return VGFX3D_OPENGL_TARGET_RTT;
    return gpu_postfx_enabled ? VGFX3D_OPENGL_TARGET_SCENE
                              : VGFX3D_OPENGL_TARGET_SWAPCHAIN;
}

vgfx3d_opengl_color_format_t
vgfx3d_opengl_choose_color_format(vgfx3d_opengl_target_kind_t target_kind) {
    return target_kind == VGFX3D_OPENGL_TARGET_SCENE ? VGFX3D_OPENGL_COLOR_FORMAT_HDR16F
                                                     : VGFX3D_OPENGL_COLOR_FORMAT_UNORM8;
}

vgfx3d_opengl_blend_mode_t
vgfx3d_opengl_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd) {
    return vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? VGFX3D_OPENGL_BLEND_ALPHA
                                                 : VGFX3D_OPENGL_BLEND_OPAQUE;
}

vgfx3d_opengl_motion_attachment_mode_t
vgfx3d_opengl_choose_motion_attachment_mode(vgfx3d_opengl_target_kind_t target_kind,
                                            const vgfx3d_draw_cmd_t *cmd) {
    if (target_kind != VGFX3D_OPENGL_TARGET_SCENE)
        return VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY;
    return vgfx3d_opengl_choose_blend_mode(cmd) == VGFX3D_OPENGL_BLEND_ALPHA
               ? VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_ONLY
               : VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_AND_MOTION;
}

/// @brief Decide whether canvas readback should source the swapchain or a postfx target.
vgfx3d_opengl_readback_kind_t vgfx3d_opengl_choose_readback_kind(int8_t gpu_postfx_enabled) {
    return gpu_postfx_enabled ? VGFX3D_OPENGL_READBACK_POSTFX_COMPOSITE
                              : VGFX3D_OPENGL_READBACK_BACKBUFFER;
}

/// @brief Decide whether to reuse a cached morph-target GPU buffer.
/// Returns 1 if the cached payload is still valid (same key + matching shape/vertex count),
/// 0 if the buffer must be re-uploaded.
int vgfx3d_opengl_should_reuse_morph_cache(const void *cached_key,
                                           uint64_t cached_revision,
                                           int32_t cached_shape_count,
                                           uint32_t cached_vertex_count,
                                           int8_t cached_has_normal_deltas,
                                           const vgfx3d_draw_cmd_t *cmd) {
    int8_t has_normal_deltas;

    if (!cmd || !cmd->morph_key || cmd->morph_revision == 0 || !cmd->morph_deltas ||
        !cmd->morph_weights || cmd->morph_shape_count <= 0 || cmd->vertex_count == 0) {
        return 0;
    }

    has_normal_deltas = cmd->morph_normal_deltas ? 1 : 0;
    return cached_key == cmd->morph_key && cached_revision == cmd->morph_revision &&
           cached_shape_count == cmd->morph_shape_count &&
           cached_vertex_count == cmd->vertex_count &&
           cached_has_normal_deltas == has_normal_deltas;
}

/// @brief Decide whether a per-mesh GPU cache entry should be evicted this frame.
/// Returns 1 when the entry has been unused for more than @p ttl_frames frames.
int vgfx3d_opengl_should_prune_cache_entry(uint64_t current_frame,
                                           uint64_t last_used_frame,
                                           uint64_t max_age) {
    if (max_age == 0 || current_frame <= last_used_frame)
        return 0;
    return (current_frame - last_used_frame) > max_age;
}
