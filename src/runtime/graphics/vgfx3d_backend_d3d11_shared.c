#include "vgfx3d_backend_d3d11_shared.h"

#include "vgfx3d_backend_utils.h"

#include <limits.h>
#include <string.h>

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

    scalar_capacity = dst_vec_count * 4;
    if (src_scalar_count > scalar_capacity)
        src_scalar_count = scalar_capacity;
    for (int32_t i = 0; i < src_scalar_count; i++)
        dst[i / 4][i % 4] = src[i];
}

void vgfx3d_d3d11_pack_bone_palette(float *dst, const float *src, int32_t bone_count) {
    size_t copy_count;

    if (!dst)
        return;

    memset(dst, 0, sizeof(float) * VGFX3D_D3D11_MAX_BONES * 16u);
    if (!src || bone_count <= 0)
        return;

    if (bone_count > VGFX3D_D3D11_MAX_BONES)
        bone_count = VGFX3D_D3D11_MAX_BONES;
    copy_count = (size_t)bone_count * 16u;
    memcpy(dst, src, copy_count * sizeof(float));
}

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

int32_t vgfx3d_d3d11_next_capacity(int32_t current_capacity,
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

vgfx3d_d3d11_blend_mode_t
vgfx3d_d3d11_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd) {
    return vgfx3d_draw_cmd_uses_alpha_blend(cmd) ? VGFX3D_D3D11_BLEND_ALPHA
                                                 : VGFX3D_D3D11_BLEND_OPAQUE;
}

vgfx3d_d3d11_color_format_t
vgfx3d_d3d11_choose_color_format(vgfx3d_d3d11_target_kind_t target_kind) {
    return target_kind == VGFX3D_D3D11_TARGET_SCENE ? VGFX3D_D3D11_COLOR_FORMAT_HDR16F
                                                    : VGFX3D_D3D11_COLOR_FORMAT_UNORM8;
}
