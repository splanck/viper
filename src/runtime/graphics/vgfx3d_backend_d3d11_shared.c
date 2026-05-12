//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_d3d11_shared.c
// Purpose: D3D11 backend helpers shared with other backends — constant-buffer
//   packing (HLSL float4 alignment), instance/bone palette upload prep,
//   per-frame TAA history, mip count math, capacity growth, and policy
//   choices for render-target / blend / color-format selection.
//
// Key invariants:
//   - HLSL constant buffers require float4 alignment, so scalar arrays must be
//     packed into vec4 slots (one scalar per .x, padding zeros in .yzw).
//   - Bone palette is a fixed VGFX3D_D3D11_MAX_BONES × mat4 (16 floats); the
//     upload is zero-padded and clamped to that supported cap to keep the
//     cbuffer size constant.
//   - Frame history tracks scene VP and previous-frame VP separately from
//     overlay/draw VP so motion vectors stay correct across overlay passes.
//
// Links: vgfx3d_backend_d3d11_shared.h, vgfx3d_backend_d3d11.c
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_d3d11_shared.h"

#include "vgfx3d_backend_utils.h"

#include <limits.h>
#include <string.h>

/// @brief Pack a flat scalar array into HLSL-aligned float4 slots, four scalars per vector.
/// Remaining vector lanes are zeroed. Truncates if @p src exceeds @p dst capacity.
void vgfx3d_d3d11_pack_scalar_array4(float (*dst)[4],
                                     int32_t dst_vec_count,
                                     const float *src,
                                     int32_t src_scalar_count) {
    int32_t scalar_capacity;

    if (!dst || dst_vec_count <= 0)
        return;

    memset(dst, 0, (size_t)dst_vec_count * sizeof(dst[0]));
    if (!src || src_scalar_count <= 0)
        return;

    scalar_capacity = dst_vec_count > INT_MAX / 4 ? INT_MAX : dst_vec_count * 4;
    if (src_scalar_count > scalar_capacity)
        src_scalar_count = scalar_capacity;
    for (int32_t i = 0; i < src_scalar_count; i++)
        dst[i / 4][i % 4] = src[i];
}

static void vgfx3d_d3d11_store_identity4x4(float *dst) {
    memset(dst, 0, sizeof(float) * 16u);
    dst[0] = 1.0f;
    dst[5] = 1.0f;
    dst[10] = 1.0f;
    dst[15] = 1.0f;
}

/// @brief Copy a bone palette (mat4 per bone) into a fixed-size cbuffer slot.
/// Fills unused slots with identity so out-of-range indices do not collapse vertices.
/// If @p bone_count exceeds `VGFX3D_D3D11_MAX_BONES`, the upload is clamped to
/// the largest supported palette size for this backend.
void vgfx3d_d3d11_pack_bone_palette(float *dst, const float *src, int32_t bone_count) {
    size_t copy_count;
    int32_t first_unused = 0;

    if (!dst)
        return;

    if (src && bone_count > 0) {
        if (bone_count > VGFX3D_D3D11_MAX_BONES)
            bone_count = VGFX3D_D3D11_MAX_BONES;
        copy_count = (size_t)bone_count * 16u;
        memcpy(dst, src, copy_count * sizeof(float));
        first_unused = bone_count;
    }
    for (int32_t i = first_unused; i < VGFX3D_D3D11_MAX_BONES; i++)
        vgfx3d_d3d11_store_identity4x4(&dst[(size_t)i * 16u]);
}

/// @brief Build per-instance cbuffer entries (model + normal + prev_model) for instanced draws.
/// When previous-frame matrices are missing, prev_model is filled with the current model so
/// motion-vector shaders compute zero displacement (no false motion).
void vgfx3d_d3d11_fill_instance_data(vgfx3d_d3d11_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices) {
    if (!dst || instance_count <= 0 || !instance_matrices)
        return;

    for (int32_t i = 0; i < instance_count; i++) {
        const float *model = &instance_matrices[(size_t)i * 16u];
        memcpy(dst[i].model, model, sizeof(dst[i].model));
        vgfx3d_compute_normal_matrix4(model, dst[i].normal);
        if (has_prev_instance_matrices && prev_instance_matrices) {
            memcpy(dst[i].prev_model,
                   &prev_instance_matrices[(size_t)i * 16u],
                   sizeof(dst[i].prev_model));
        } else {
            memcpy(dst[i].prev_model, model, sizeof(dst[i].prev_model));
        }
    }
}

/// @brief Roll the per-frame VP/inv-VP/cam-pos history forward by one frame.
/// Scene VP and overlay VP are tracked separately because overlay passes use
/// the current VP for both "current" and "previous" (no temporal coherence).
void vgfx3d_d3d11_update_frame_history(vgfx3d_d3d11_frame_history_t *history,
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

/// @brief Reconcile bone-buffer upload outcomes into the per-object draw flags.
/// If the current upload failed, both skinning flags are cleared so the shader
/// falls back to the unskinned path. If only the prev-frame upload failed, motion
/// vectors degrade gracefully to "no skinning history".
void vgfx3d_d3d11_resolve_bone_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                             int current_upload_ok,
                                             int prev_upload_ok) {
    if (!object_data)
        return;
    if (!current_upload_ok) {
        object_data->has_skinning = 0;
        object_data->has_prev_skinning = 0;
        return;
    }
    if (!prev_upload_ok)
        object_data->has_prev_skinning = 0;
}

/// @brief Reconcile morph-target upload outcomes (positions and normals) into draw flags.
/// On failure the shape count drops to 0 (mesh renders un-morphed). If only normal
/// deltas fail, the position morph still applies but normals will be re-derived from
/// the morphed positions.
void vgfx3d_d3d11_resolve_morph_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                              int morph_upload_ok,
                                              int morph_normal_upload_ok) {
    if (!object_data)
        return;
    if (!morph_upload_ok) {
        object_data->morph_shape_count = 0;
        object_data->vertex_count = 0;
        object_data->has_prev_morph_weights = 0;
        object_data->has_morph_normal_deltas = 0;
        return;
    }
    if (!morph_normal_upload_ok)
        object_data->has_morph_normal_deltas = 0;
}

/// @brief Compute the number of mipmap levels required to reach 1×1 from (width × height).
/// Returns 1 for invalid dimensions. Used when creating textures with full mip chains.
int32_t vgfx3d_d3d11_compute_mip_count(int32_t width, int32_t height) {
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

/// @brief Capacity-doubling growth helper: returns the next power-of-2 capacity >= @p needed.
/// Saturates at @p needed when doubling would overflow INT_MAX.
int32_t vgfx3d_d3d11_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity) {
    int32_t next_capacity;

    if (needed <= 0) {
        if (current_capacity > 0)
            return current_capacity;
        return minimum_capacity > 0 ? minimum_capacity : 1;
    }
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

int vgfx3d_d3d11_is_valid_texture2d_extent(int32_t width, int32_t height) {
    return width > 0 && height > 0 && width <= VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION &&
           height <= VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION;
}

int vgfx3d_d3d11_is_valid_cubemap_extent(int32_t face_size) {
    return face_size > 0 && face_size <= VGFX3D_D3D11_MAX_CUBEMAP_DIMENSION;
}

int32_t vgfx3d_d3d11_clamp_morph_shape_count(uint32_t vertex_count,
                                             int32_t requested_shape_count) {
    int32_t shape_count;
    uint32_t max_indexed_vertices;
    uint32_t max_shapes_by_index;

    if (vertex_count == 0 || requested_shape_count <= 0)
        return 0;
    shape_count = requested_shape_count;
    if (shape_count > VGFX3D_D3D11_MAX_MORPH_SHAPES)
        shape_count = VGFX3D_D3D11_MAX_MORPH_SHAPES;
    max_indexed_vertices = (uint32_t)((INT_MAX - 2) / 3);
    max_shapes_by_index = max_indexed_vertices / vertex_count;
    if (max_shapes_by_index == 0)
        return 0;
    if ((uint32_t)shape_count > max_shapes_by_index)
        shape_count = (int32_t)max_shapes_by_index;
    return shape_count;
}

int vgfx3d_d3d11_should_prune_cache_entry(int32_t total_count,
                                          int32_t kept_count,
                                          int32_t scan_index,
                                          uint64_t age,
                                          int32_t max_resident,
                                          uint64_t prune_age) {
    int32_t remaining_after_current;

    if (total_count <= 0 || kept_count < 0 || scan_index < 0 || scan_index >= total_count)
        return 0;
    if (max_resident < 0)
        max_resident = 0;
    if (total_count <= max_resident || age <= prune_age)
        return 0;
    remaining_after_current = total_count - scan_index - 1;
    return kept_count + remaining_after_current >= max_resident;
}

/// @brief Pick the right render-target classification for the current draw context.
/// Order of priority: explicit RTT > swapchain (no postfx) > overlay (loading existing
/// color) > scene (HDR intermediate that postfx will tonemap).
vgfx3d_d3d11_target_kind_t vgfx3d_d3d11_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color) {
    if (rtt_active)
        return VGFX3D_D3D11_TARGET_RTT;
    if (!gpu_postfx_enabled)
        return VGFX3D_D3D11_TARGET_SWAPCHAIN;
    if (load_existing_color)
        return VGFX3D_D3D11_TARGET_OVERLAY;
    return VGFX3D_D3D11_TARGET_SCENE;
}

int8_t vgfx3d_d3d11_should_load_existing_color(vgfx3d_d3d11_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame) {
    if (!requested_load_existing_color)
        return 0;
    if (target_kind != VGFX3D_D3D11_TARGET_OVERLAY)
        return 1;
    return overlay_used_this_frame ? 1 : 0;
}

/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_d3d11_blend_mode_t
vgfx3d_d3d11_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd) {
    if (cmd && cmd->additive_blend)
        return VGFX3D_D3D11_BLEND_ADDITIVE;
    return vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? VGFX3D_D3D11_BLEND_ALPHA
                                                 : VGFX3D_D3D11_BLEND_OPAQUE;
}

/// @brief Pick the color format for a render target — HDR16F for the scene pass
/// (so post-FX tonemapping has headroom), UNORM8 for everything else.
vgfx3d_d3d11_color_format_t
vgfx3d_d3d11_choose_color_format(vgfx3d_d3d11_target_kind_t target_kind) {
    return target_kind == VGFX3D_D3D11_TARGET_SCENE ? VGFX3D_D3D11_COLOR_FORMAT_HDR16F
                                                    : VGFX3D_D3D11_COLOR_FORMAT_UNORM8;
}

vgfx3d_d3d11_motion_attachment_mode_t
vgfx3d_d3d11_choose_motion_attachment_mode(vgfx3d_d3d11_target_kind_t target_kind,
                                           const vgfx3d_draw_cmd_t *cmd) {
    if (target_kind != VGFX3D_D3D11_TARGET_SCENE)
        return VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY;
    return vgfx3d_d3d11_choose_blend_mode(cmd) == VGFX3D_D3D11_BLEND_OPAQUE
               ? VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_AND_MOTION
               : VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY;
}

int vgfx3d_d3d11_has_complete_splat(int8_t cmd_has_splat,
                                    int has_splat_map,
                                    int has_layer0,
                                    int has_layer1,
                                    int has_layer2,
                                    int has_layer3) {
    return cmd_has_splat && has_splat_map && has_layer0 && has_layer1 && has_layer2 &&
           has_layer3;
}
