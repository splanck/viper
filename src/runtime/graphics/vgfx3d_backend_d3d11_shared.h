#pragma once

#include "vgfx3d_backend.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGFX3D_D3D11_MAX_BONES 128
#define VGFX3D_D3D11_MAX_MORPH_SHAPES 32
#define VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS (VGFX3D_D3D11_MAX_MORPH_SHAPES / 4)
#define VGFX3D_D3D11_PACKED_CUSTOM_PARAM_VECS 2

typedef enum {
    VGFX3D_D3D11_BLEND_OPAQUE = 0,
    VGFX3D_D3D11_BLEND_ALPHA = 1,
} vgfx3d_d3d11_blend_mode_t;

typedef enum {
    VGFX3D_D3D11_TARGET_SWAPCHAIN = 0,
    VGFX3D_D3D11_TARGET_SCENE = 1,
    VGFX3D_D3D11_TARGET_RTT = 2,
    VGFX3D_D3D11_TARGET_OVERLAY = 3,
} vgfx3d_d3d11_target_kind_t;

typedef enum {
    VGFX3D_D3D11_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_D3D11_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_d3d11_color_format_t;

typedef struct {
    float model[16];
    float prev_model[16];
    float normal[16];
    int32_t has_prev_model_matrix;
    int32_t has_skinning;
    int32_t has_prev_skinning;
    int32_t has_morph_normal_deltas;
    int32_t morph_shape_count;
    int32_t vertex_count;
    int32_t has_prev_morph_weights;
    int32_t has_prev_instance_matrices;
    float morph_weights_packed[VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS][4];
    float prev_morph_weights_packed[VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS][4];
} vgfx3d_d3d11_per_object_t;

typedef struct {
    float diffuse[4];
    float specular[4];
    float emissive[4];
    float scalars[4];
    float pbr_scalars0[4];
    float pbr_scalars1[4];
    int32_t flags0[4];
    int32_t flags1[4];
    int32_t pbr_flags[4];
    float splat_scales[4];
    int32_t shading_model;
    float _pad0[3];
    float custom_params_packed[VGFX3D_D3D11_PACKED_CUSTOM_PARAM_VECS][4];
} vgfx3d_d3d11_per_material_t;

typedef struct {
    float model[16];
    float normal[16];
    float prev_model[16];
} vgfx3d_d3d11_instance_data_t;

typedef struct {
    float scene_vp[16];
    float scene_prev_vp[16];
    float scene_inv_vp[16];
    float draw_prev_vp[16];
    float scene_cam_pos[3];
    int8_t scene_history_valid;
    int8_t overlay_used_this_frame;
} vgfx3d_d3d11_frame_history_t;

void vgfx3d_d3d11_pack_scalar_array4(float (*dst)[4],
                                     int32_t dst_vec_count,
                                     const float *src,
                                     int32_t src_scalar_count);
void vgfx3d_d3d11_pack_bone_palette(float *dst, const float *src, int32_t bone_count);
void vgfx3d_d3d11_fill_instance_data(vgfx3d_d3d11_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices);
void vgfx3d_d3d11_update_frame_history(vgfx3d_d3d11_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target);
void vgfx3d_d3d11_resolve_bone_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                             int current_upload_ok,
                                             int prev_upload_ok);
void vgfx3d_d3d11_resolve_morph_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                              int morph_upload_ok,
                                              int morph_normal_upload_ok);
int32_t vgfx3d_d3d11_compute_mip_count(int32_t width, int32_t height);
int32_t vgfx3d_d3d11_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity);
vgfx3d_d3d11_target_kind_t vgfx3d_d3d11_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color);
vgfx3d_d3d11_blend_mode_t
vgfx3d_d3d11_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd);
vgfx3d_d3d11_color_format_t
vgfx3d_d3d11_choose_color_format(vgfx3d_d3d11_target_kind_t target_kind);

#ifdef __cplusplus
}
#endif
