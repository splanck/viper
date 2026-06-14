//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_metal_shared.c
// Purpose: Metal backend helpers shared with sister backends — bone palette /
//   instance-buffer packing (with column-major transpose for MSL), frame
//   history, and policy choices for target/blend/format selection.
//
// Key invariants:
//   - MSL expects column-major matrices, so all model/normal/prev_model
//     payloads are transposed from Viper's row-major form before upload.
//
// Links: vgfx3d_backend_metal_shared.h, vgfx3d_backend_metal.c
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_metal_shared.h"

#include "vgfx3d_backend_utils.h"

#include <limits.h>
#include <math.h>
#include <string.h>

/// @brief Transpose a 4x4 matrix from one row/column major layout to the other.
/// @details The runtime stores matrices in row-major order (`m[r*4+c]`)
///   while Metal shading language reads uniform matrices in column-major
///   by default (`m[c*4+r]`). This helper is the conversion point on the
///   CPU→GPU boundary — every matrix bound to a Metal argument goes
///   through here first so shader code can continue to use natural
///   column-major syntax without the upload site caring.
static void transpose4x4_local(const float *src, float *dst) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst[c * 4 + r] = src[r * 4 + c];
}

/// @brief Store a row-major identity matrix into one fixed bone-palette slot.
static void vgfx3d_metal_store_identity4x4(float *dst) {
    memset(dst, 0, sizeof(float) * 16u);
    dst[0] = 1.0f;
    dst[5] = 1.0f;
    dst[10] = 1.0f;
    dst[15] = 1.0f;
}

/// @brief Copy a bone palette into a fixed-size MTLBuffer slot (identity-pads unused bones).
/// Matches `vgfx3d_d3d11_pack_bone_palette` semantics; oversized inputs are
/// clamped to the largest palette this backend exposes.
void vgfx3d_metal_pack_bone_palette(float *dst, const float *src, int32_t bone_count) {
    size_t copy_count;
    int32_t first_unused = 0;

    if (!dst)
        return;

    if (src && bone_count > 0) {
        if (bone_count > VGFX3D_METAL_MAX_BONES)
            bone_count = VGFX3D_METAL_MAX_BONES;
        copy_count = (size_t)bone_count * 16u;
        memcpy(dst, src, copy_count * sizeof(float));
        first_unused = bone_count;
    }
    for (int32_t i = first_unused; i < VGFX3D_METAL_MAX_BONES; i++)
        vgfx3d_metal_store_identity4x4(&dst[(size_t)i * 16u]);
}

/// @brief Build per-instance Metal buffer entries with column-major transpose for MSL.
/// Computes the normal matrix from each model matrix; absent prev-frame data falls back
/// to the current model so motion-vector shaders see zero displacement.
void vgfx3d_metal_fill_instance_data(vgfx3d_metal_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices) {
    if (!dst || instance_count <= 0 || !instance_matrices)
        return;

    for (int32_t i = 0; i < instance_count; i++) {
        const float *model = &instance_matrices[(size_t)i * 16u];
        float normal[16];

        transpose4x4_local(model, dst[i].model);
        vgfx3d_compute_normal_matrix4(model, normal);
        transpose4x4_local(normal, dst[i].normal);
        if (has_prev_instance_matrices && prev_instance_matrices) {
            transpose4x4_local(&prev_instance_matrices[(size_t)i * 16u], dst[i].prev_model);
        } else {
            memcpy(dst[i].prev_model, dst[i].model, sizeof(dst[i].prev_model));
        }
    }
}

/// @brief Roll the Metal backend's per-frame VP/inv-VP/cam-pos history forward.
/// Mirrors the D3D11 / OpenGL helpers; see vgfx3d_d3d11_update_frame_history for semantics.
void vgfx3d_metal_update_frame_history(vgfx3d_metal_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target) {
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
        history->overlay_used_this_frame = 0;
        return;
    }

    memcpy(history->draw_prev_vp, vp, sizeof(history->draw_prev_vp));
    history->overlay_used_this_frame = uses_separate_overlay_target ? 1 : 0;
}

/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_metal_compute_mip_count(int32_t width, int32_t height) {
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

/// @brief Clamp material sampler anisotropy into the backend cacheable [1,16] range.
int32_t vgfx3d_metal_sanitize_anisotropy(int32_t requested) {
    if (requested < 1)
        return 1;
    if (requested > VGFX3D_METAL_MAX_TEXTURE_ANISOTROPY)
        return VGFX3D_METAL_MAX_TEXTURE_ANISOTROPY;
    return requested;
}

/// @brief Convert sanitized anisotropy to a compact cache index [0,15].
int32_t vgfx3d_metal_sampler_anisotropy_index(int32_t requested) {
    return vgfx3d_metal_sanitize_anisotropy(requested) - 1;
}

/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_metal_next_capacity(int32_t current_capacity,
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

/// @brief Clamp morph shape count to shader and index-range limits.
int32_t vgfx3d_metal_clamp_morph_shape_count(uint32_t vertex_count, int32_t requested_shape_count) {
    int32_t shape_count;
    uint32_t max_indexed_vertices;
    uint32_t max_shapes_by_index;

    if (vertex_count == 0 || requested_shape_count <= 0)
        return 0;
    shape_count = requested_shape_count;
    if (shape_count > VGFX3D_METAL_MAX_MORPH_SHAPES)
        shape_count = VGFX3D_METAL_MAX_MORPH_SHAPES;
    max_indexed_vertices = (uint32_t)((INT_MAX - 2) / 3);
    max_shapes_by_index = max_indexed_vertices / vertex_count;
    if (max_shapes_by_index == 0)
        return 0;
    if ((uint32_t)shape_count > max_shapes_by_index)
        shape_count = (int32_t)max_shapes_by_index;
    return shape_count;
}

/// @brief Pick the right render-target classification for the Metal backend.
vgfx3d_metal_target_kind_t vgfx3d_metal_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color) {
    if (rtt_active)
        return VGFX3D_METAL_TARGET_RTT;
    if (!gpu_postfx_enabled)
        return VGFX3D_METAL_TARGET_SWAPCHAIN;
    if (load_existing_color)
        return VGFX3D_METAL_TARGET_OVERLAY;
    return VGFX3D_METAL_TARGET_SCENE;
}

/// @brief Decide whether the next pass should preserve existing color contents.
/// Overlay targets only load when this frame already used them; otherwise the
/// requested-load flag is honored. Used to avoid bandwidth-wasting Clear→Load cycles.
int8_t vgfx3d_metal_should_load_existing_color(vgfx3d_metal_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame) {
    if (!requested_load_existing_color)
        return 0;
    if (target_kind != VGFX3D_METAL_TARGET_OVERLAY)
        return 1;
    return overlay_used_this_frame ? 1 : 0;
}

/// @brief Pick the color format — HDR16F for the scene pass, UNORM8 elsewhere.
vgfx3d_metal_color_format_t vgfx3d_metal_choose_color_format(
    vgfx3d_metal_target_kind_t target_kind) {
    return target_kind == VGFX3D_METAL_TARGET_SCENE ? VGFX3D_METAL_COLOR_FORMAT_HDR16F
                                                    : VGFX3D_METAL_COLOR_FORMAT_UNORM8;
}

/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_metal_blend_mode_t vgfx3d_metal_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd) {
    if (cmd && cmd->additive_blend)
        return VGFX3D_METAL_BLEND_ADDITIVE;
    return vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? VGFX3D_METAL_BLEND_ALPHA
                                                 : VGFX3D_METAL_BLEND_OPAQUE;
}

/// @brief Decide whether terrain splatting has every required texture bound.
int vgfx3d_metal_has_complete_splat(int8_t cmd_has_splat,
                                    int has_splat_map,
                                    int has_layer0,
                                    int has_layer1,
                                    int has_layer2,
                                    int has_layer3) {
    return cmd_has_splat && has_splat_map && has_layer0 && has_layer1 && has_layer2 && has_layer3;
}

/// @brief Decide whether to attach a motion-vector buffer to the current pass.
/// Only the scene pass with opaque draws gets a motion attachment; alpha-blended
/// draws and non-scene targets drop motion (TAA can't disambiguate transparency).
vgfx3d_metal_motion_attachment_mode_t vgfx3d_metal_choose_motion_attachment_mode(
    vgfx3d_metal_target_kind_t target_kind, const vgfx3d_draw_cmd_t *cmd) {
    if (target_kind != VGFX3D_METAL_TARGET_SCENE)
        return VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_ONLY;
    return vgfx3d_metal_choose_blend_mode(cmd) == VGFX3D_METAL_BLEND_OPAQUE
               ? VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_AND_MOTION
               : VGFX3D_METAL_MOTION_ATTACHMENTS_COLOR_ONLY;
}

/// @brief Decide whether canvas readback should source the backbuffer or postfx target.
vgfx3d_metal_readback_kind_t vgfx3d_metal_choose_readback_kind(int8_t gpu_postfx_enabled) {
    return gpu_postfx_enabled ? VGFX3D_METAL_READBACK_POSTFX_COMPOSITE
                              : VGFX3D_METAL_READBACK_BACKBUFFER;
}

/// @brief Clamp light shadow indices to completed contiguous shadow-map slots.
int32_t vgfx3d_metal_sanitize_shadow_index(int32_t shadow_index, int32_t shadow_count) {
    if (shadow_count <= 0 || shadow_count > VGFX3D_MAX_SHADOW_LIGHTS)
        return -1;
    return (shadow_index >= 0 && shadow_index < shadow_count) ? shadow_index : -1;
}

/// @brief Project a world-space point through a shadow VP matrix using MSL sampling rules.
int vgfx3d_metal_project_shadow_coord(const float *shadow_vp,
                                      int32_t projection_type,
                                      const float world_pos[3],
                                      float out_uv_depth[3]) {
    float lx;
    float ly;
    float lz;
    float lw;
    float ndc_x;
    float ndc_y;
    float ndc_z;

    if (!shadow_vp || !world_pos || !out_uv_depth)
        return 0;
    lx = world_pos[0] * shadow_vp[0] + world_pos[1] * shadow_vp[1] + world_pos[2] * shadow_vp[2] +
         shadow_vp[3];
    ly = world_pos[0] * shadow_vp[4] + world_pos[1] * shadow_vp[5] + world_pos[2] * shadow_vp[6] +
         shadow_vp[7];
    lz = world_pos[0] * shadow_vp[8] + world_pos[1] * shadow_vp[9] + world_pos[2] * shadow_vp[10] +
         shadow_vp[11];
    lw = world_pos[0] * shadow_vp[12] + world_pos[1] * shadow_vp[13] +
         world_pos[2] * shadow_vp[14] + shadow_vp[15];
    if (projection_type == VGFX3D_SHADOW_PROJECTION_PERSPECTIVE) {
        if (!isfinite(lw) || lw <= 0.0001f)
            return 0;
        ndc_x = lx / lw;
        ndc_y = ly / lw;
        ndc_z = lz / lw;
    } else {
        ndc_x = lx;
        ndc_y = ly;
        ndc_z = lz;
    }
    if (!isfinite(ndc_x) || !isfinite(ndc_y) || !isfinite(ndc_z))
        return 0;
    out_uv_depth[0] = ndc_x * 0.5f + 0.5f;
    out_uv_depth[1] = 0.5f - ndc_y * 0.5f;
    out_uv_depth[2] = ndc_z * 0.5f + 0.5f;
    return 1;
}

/// @brief Decide whether to reuse a cached morph-target Metal buffer.
/// Returns 1 if the cached payload (key + revision + shape/vertex counts +
/// normal-deltas flag) still matches the draw command; 0 otherwise.
int vgfx3d_metal_should_reuse_morph_cache(const void *cached_key,
                                          uint64_t cached_revision,
                                          int32_t cached_shape_count,
                                          uint32_t cached_vertex_count,
                                          int8_t cached_has_normal_deltas,
                                          const vgfx3d_draw_cmd_t *cmd) {
    int32_t shape_count;
    int8_t has_normal_deltas;

    if (!cmd || !cmd->morph_key || cmd->morph_revision == 0 || !cmd->morph_deltas ||
        !cmd->morph_weights || cmd->morph_shape_count <= 0 || cmd->vertex_count == 0) {
        return 0;
    }

    shape_count = vgfx3d_metal_clamp_morph_shape_count(cmd->vertex_count, cmd->morph_shape_count);
    if (shape_count <= 0)
        return 0;
    has_normal_deltas = cmd->morph_normal_deltas ? 1 : 0;
    return cached_key == cmd->morph_key && cached_revision == cmd->morph_revision &&
           cached_shape_count == shape_count && cached_vertex_count == cmd->vertex_count &&
           cached_has_normal_deltas == has_normal_deltas;
}

/// @brief Decide whether a GPU-cached resource has gone cold enough to evict.
/// @details Metal's texture/buffer caches track the last frame on which each
///   entry was used. An entry is eligible for eviction when `current_frame -
///   last_used_frame > max_age`. Two special cases: `max_age == 0` disables
///   pruning entirely (treats every entry as immortal — useful for debug
///   builds where texture churn should stay diagnosable), and
///   `current_frame <= last_used_frame` is treated as "don't prune" because
///   it indicates a counter wraparound or entry-from-the-future state that
///   shouldn't trigger a mass eviction.
/// @return Non-zero when the entry should be dropped.
int vgfx3d_metal_should_prune_cache_entry(uint64_t current_frame,
                                          uint64_t last_used_frame,
                                          uint64_t max_age) {
    if (max_age == 0 || current_frame <= last_used_frame)
        return 0;
    return (current_frame - last_used_frame) > max_age;
}
