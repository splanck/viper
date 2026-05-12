#pragma once

#include "vgfx3d_backend.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGFX3D_D3D11_MAX_BONES 256
#define VGFX3D_D3D11_BONE_PALETTE_FLOATS (VGFX3D_D3D11_MAX_BONES * 16u)
#define VGFX3D_D3D11_BONE_PALETTE_BYTES (sizeof(float) * VGFX3D_D3D11_BONE_PALETTE_FLOATS)
#define VGFX3D_D3D11_MAX_MORPH_SHAPES 32
#define VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS (VGFX3D_D3D11_MAX_MORPH_SHAPES / 4)
#define VGFX3D_D3D11_PACKED_CUSTOM_PARAM_VECS 2
#define VGFX3D_D3D11_TEXTURE_SLOT_COUNT RT_MATERIAL3D_TEXTURE_SLOT_COUNT
#define VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION 16384
#define VGFX3D_D3D11_MAX_CUBEMAP_DIMENSION 16384

typedef enum {
    VGFX3D_D3D11_BLEND_OPAQUE = 0,
    VGFX3D_D3D11_BLEND_ALPHA = 1,
    VGFX3D_D3D11_BLEND_ADDITIVE = 2,
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

typedef enum {
    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY = 0,
    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_AND_MOTION = 1,
} vgfx3d_d3d11_motion_attachment_mode_t;

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
    int32_t texture_uv_sets0[4];
    int32_t texture_uv_sets1[4];
    float texture_uv_transform0[VGFX3D_D3D11_TEXTURE_SLOT_COUNT][4];
    float texture_uv_transform1[VGFX3D_D3D11_TEXTURE_SLOT_COUNT][4];
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

/// @brief Pack scalar array into HLSL float4-aligned slots (four scalars per float4).
void vgfx3d_d3d11_pack_scalar_array4(float (*dst)[4],
                                     int32_t dst_vec_count,
                                     const float *src,
                                     int32_t src_scalar_count);
/// @brief Copy bone palette into a fixed-size cbuffer slot (identity-pads unused bones).
void vgfx3d_d3d11_pack_bone_palette(float *dst, const float *src, int32_t bone_count);
/// @brief Build per-instance cbuffer entries (model + normal + prev_model) for instanced draws.
void vgfx3d_d3d11_fill_instance_data(vgfx3d_d3d11_instance_data_t *dst,
                                     int32_t instance_count,
                                     const float *instance_matrices,
                                     const float *prev_instance_matrices,
                                     int8_t has_prev_instance_matrices);
/// @brief Roll per-frame VP/inv-VP/cam-pos history forward by one frame (scene + overlay tracked separately).
void vgfx3d_d3d11_update_frame_history(vgfx3d_d3d11_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target);
/// @brief Reconcile bone-buffer upload outcome into per-object draw flags (clears skinning on failure).
void vgfx3d_d3d11_resolve_bone_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                             int current_upload_ok,
                                             int prev_upload_ok);
/// @brief Reconcile morph-target upload outcome (positions + normals) into draw flags.
void vgfx3d_d3d11_resolve_morph_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                              int morph_upload_ok,
                                              int morph_normal_upload_ok);
/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_d3d11_compute_mip_count(int32_t width, int32_t height);
/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_d3d11_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity);
/// @brief Overflow-safe row byte computation for tightly packed readback rows.
int vgfx3d_d3d11_compute_row_bytes(int32_t width,
                                   int32_t bytes_per_pixel,
                                   size_t *out_bytes);
/// @brief Validate an RGBA8 readback destination span.
int vgfx3d_d3d11_validate_rgba8_destination(int32_t width,
                                            int32_t height,
                                            int32_t stride,
                                            size_t *out_bytes);
/// @brief Check dimensions against D3D11 feature-level 11 texture limits.
int vgfx3d_d3d11_is_valid_texture2d_extent(int32_t width, int32_t height);
/// @brief Check a square cubemap face dimension against D3D11 limits.
int vgfx3d_d3d11_is_valid_cubemap_extent(int32_t face_size);
/// @brief Clamp morph shapes so shader-side int indexing cannot overflow.
int32_t vgfx3d_d3d11_clamp_morph_shape_count(uint32_t vertex_count,
                                             int32_t requested_shape_count);
/// @brief Decide whether an aged cache entry can be pruned without dropping below the floor.
int vgfx3d_d3d11_should_prune_cache_entry(int32_t total_count,
                                          int32_t kept_count,
                                          int32_t scan_index,
                                          uint64_t age,
                                          int32_t max_resident,
                                          uint64_t prune_age);
/// @brief Pick the right render-target classification (RTT > swapchain > overlay > scene).
vgfx3d_d3d11_target_kind_t vgfx3d_d3d11_choose_target_kind(int8_t rtt_active,
                                                           int8_t gpu_postfx_enabled,
                                                           int8_t load_existing_color);
/// @brief Downgrade an intended target when allocation failed or the resource is unavailable.
vgfx3d_d3d11_target_kind_t
vgfx3d_d3d11_resolve_available_target(vgfx3d_d3d11_target_kind_t requested,
                                      int scene_available,
                                      int overlay_available,
                                      int rtt_available);
/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_d3d11_blend_mode_t
vgfx3d_d3d11_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd);
/// @brief Pick the color format — HDR16F for the scene pass, UNORM8 elsewhere.
vgfx3d_d3d11_color_format_t
vgfx3d_d3d11_choose_color_format(vgfx3d_d3d11_target_kind_t target_kind);
/// @brief Decide whether the next pass should preserve existing color contents.
int8_t vgfx3d_d3d11_should_load_existing_color(vgfx3d_d3d11_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame);
/// @brief Decide whether to attach the motion-vector target.
vgfx3d_d3d11_motion_attachment_mode_t
vgfx3d_d3d11_choose_motion_attachment_mode(vgfx3d_d3d11_target_kind_t target_kind,
                                           const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether terrain splatting has all required textures bound.
int vgfx3d_d3d11_has_complete_splat(int8_t cmd_has_splat,
                                    int has_splat_map,
                                    int has_layer0,
                                    int has_layer1,
                                    int has_layer2,
                                    int has_layer3);

#ifdef __cplusplus
}
#endif
