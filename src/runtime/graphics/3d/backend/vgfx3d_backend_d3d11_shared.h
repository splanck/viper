//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_shared.h
// Purpose: Shared declarations/constants for the Direct3D 11 vgfx3d backend —
//   bone-palette limits, shared constant-buffer layouts, and helper routines
//   used by both the main D3D11 backend and its shared support unit.
//
// Key invariants:
//   - Layouts here must stay in lock-step with the HLSL shaders and the
//     equivalent Metal/OpenGL shared layouts (same bone/light limits).
//   - Internal to the D3D11 backend; not part of the public Viper API.
//
// Ownership/Lifetime:
//   - Declarations only; no allocation or ownership semantics here.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c,
//        src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_shared.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VGFX3D_D3D11_MAX_BONES 256
#define VGFX3D_D3D11_BONE_PALETTE_FLOATS (VGFX3D_D3D11_MAX_BONES * 16u)
#define VGFX3D_D3D11_BONE_PALETTE_BYTES (sizeof(float) * VGFX3D_D3D11_BONE_PALETTE_FLOATS)
#define VGFX3D_D3D11_MAX_MORPH_SHAPES 64
#define VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS (VGFX3D_D3D11_MAX_MORPH_SHAPES / 4)
#define VGFX3D_D3D11_PACKED_CUSTOM_PARAM_VECS 3
#define VGFX3D_D3D11_TEXTURE_SLOT_COUNT RT_MATERIAL3D_TEXTURE_SLOT_COUNT
#define VGFX3D_D3D11_MAX_TEXTURE2D_DIMENSION 16384
#define VGFX3D_D3D11_MAX_CUBEMAP_DIMENSION 16384
#define VGFX3D_D3D11_MAX_TEXTURE_ANISOTROPY 16
#define VGFX3D_D3D11_ANISOTROPY_LEVEL_COUNT VGFX3D_D3D11_MAX_TEXTURE_ANISOTROPY
#define VGFX3D_D3D11_MAX_CONSTANT_BUFFER_BYTES (64u * 1024u)
#define VGFX3D_D3D11_MAX_SLOPE_SCALED_DEPTH_BIAS 16.0f
#define VGFX3D_D3D11_MATERIAL_SHININESS_MAX 1000000.0f
#define VGFX3D_D3D11_MATERIAL_SPLAT_SCALE_MAX 1000000.0f
#define VGFX3D_D3D11_LIGHT_INTENSITY_MAX 1000000.0f
#define VGFX3D_D3D11_LIGHT_ATTENUATION_MAX 1000000.0f
#define VGFX3D_D3D11_POSTFX_SCALAR_MAX 1000000.0f
#define VGFX3D_D3D11_FOG_DISTANCE_MAX 1000000000.0f
#define VGFX3D_D3D11_SHADOW_BIAS_MAX 1000000.0f
#define VGFX3D_D3D11_SHADING_MODEL_MAX 5
#define VGFX3D_D3D11_TONEMAP_MODE_MAX 2
#define VGFX3D_D3D11_BLOOM_MIP_COUNT_MAX 6
#define VGFX3D_D3D11_BLOOM_MIN_DOWNSAMPLE_EXTENT 8
#define VGFX3D_D3D11_CLUSTER_ZNEAR_MIN 0.0001f
#define VGFX3D_D3D11_CLUSTER_ZNEAR_FALLBACK 0.1f
#define VGFX3D_D3D11_CLUSTER_ZFAR_FALLBACK 1000.0f
#define VGFX3D_D3D11_CLUSTER_ZFAR_MAX 1000000000.0f

/// @brief Blend state required by a draw: opaque, standard alpha, or additive.
typedef enum {
    VGFX3D_D3D11_BLEND_OPAQUE = 0,
    VGFX3D_D3D11_BLEND_ALPHA = 1,
    VGFX3D_D3D11_BLEND_ADDITIVE = 2,
} vgfx3d_d3d11_blend_mode_t;

/// @brief Render-target classification: the swapchain back buffer, the offscreen HDR
///   scene target, a user render-to-texture target, or the overlay target.
typedef enum {
    VGFX3D_D3D11_TARGET_SWAPCHAIN = 0,
    VGFX3D_D3D11_TARGET_SCENE = 1,
    VGFX3D_D3D11_TARGET_RTT = 2,
    VGFX3D_D3D11_TARGET_OVERLAY = 3,
} vgfx3d_d3d11_target_kind_t;

/// @brief Color format of a target: 8-bit UNORM (display) or 16-bit float (HDR scene).
typedef enum {
    VGFX3D_D3D11_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_D3D11_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_d3d11_color_format_t;

/// @brief Whether a pass attaches only color or also the motion-vector target.
typedef enum {
    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY = 0,
    VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_AND_MOTION = 1,
} vgfx3d_d3d11_motion_attachment_mode_t;

/// @brief Source texture/surface class for a canvas readback request.
typedef enum {
    VGFX3D_D3D11_READBACK_BACKBUFFER = 0,
    VGFX3D_D3D11_READBACK_POSTFX_COMPOSITE = 1,
    VGFX3D_D3D11_READBACK_SCENE_COLOR = 2,
    VGFX3D_D3D11_READBACK_PRESENTED_SNAPSHOT = 3,
} vgfx3d_d3d11_readback_kind_t;

/// @brief Per-object HLSL constant buffer: model/prev-model/normal matrices plus skinning,
///   morph, and instancing flags and packed morph weights.
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
    /* R18 per-instance bone-palette stride (0 = shared palette); padded to a
     * full float4 row so the HLSL cbuffer layout stays 16-byte aligned. */
    int32_t instance_bone_stride;
    int32_t _pad_stride[3];
    float morph_weights_packed[VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS][4];
    float prev_morph_weights_packed[VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS][4];
} vgfx3d_d3d11_per_object_t;

/// @brief Per-material HLSL constant buffer: colors, scalar/PBR factor banks, flag banks,
///   terrain-splat scales, shading model, packed custom params, and per-slot UV set +
///   transform data.
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

/// @brief One per-instance vertex-buffer entry for instanced draws: model, normal, and
///   previous-frame model matrices.
typedef struct {
    float model[16];
    float normal[16];
    float prev_model[16];
} vgfx3d_d3d11_instance_data_t;

/// @brief Per-frame view/projection history for motion vectors: current/previous/inverse
///   scene VP, the draw's previous VP, camera position, and scene/overlay validity flags.
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
/// @brief Decide whether instanced motion-history attributes are actually available.
int vgfx3d_d3d11_should_use_previous_instance_matrices(const float *prev_instance_matrices,
                                                       int8_t has_prev_instance_matrices);
/// @brief Roll per-frame VP/inv-VP/cam-pos history forward by one frame (scene + overlay tracked
/// separately).
void vgfx3d_d3d11_update_frame_history(vgfx3d_d3d11_frame_history_t *history,
                                       const float *vp,
                                       const float *inv_vp,
                                       const float *cam_pos,
                                       int8_t is_overlay_pass,
                                       int8_t uses_separate_overlay_target);
/// @brief Reconcile bone-buffer upload outcome into per-object draw flags (clears skinning on
/// failure).
void vgfx3d_d3d11_resolve_bone_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                             int current_upload_ok,
                                             int prev_upload_ok);
/// @brief Decide if shader skinning can run; palette uploads clamp to the shader maximum.
int vgfx3d_d3d11_should_enable_skinning(const float *bone_palette, int32_t bone_count);
/// @brief Reconcile morph-target upload outcome (positions + normals) into draw flags.
void vgfx3d_d3d11_resolve_morph_upload_status(vgfx3d_d3d11_per_object_t *object_data,
                                              int morph_upload_ok,
                                              int morph_normal_upload_ok);
/// @brief Number of mipmap levels needed to reach 1×1 from (width × height).
int32_t vgfx3d_d3d11_compute_mip_count(int32_t width, int32_t height);
/// @brief Clamp material sampler anisotropy into the backend cacheable [1,16] range.
int32_t vgfx3d_d3d11_sanitize_anisotropy(int32_t requested);
/// @brief Convert sanitized anisotropy to a compact cache index [0,15].
int32_t vgfx3d_d3d11_sampler_anisotropy_index(int32_t requested);
/// @brief Normalize texture UV-set selectors to the two shader-visible channels.
int32_t vgfx3d_d3d11_sanitize_texture_uv_set(int32_t requested);
/// @brief Clamp integer post-FX sample/pass counts before cbuffer upload.
int32_t vgfx3d_d3d11_clamp_int_param(int32_t requested, int32_t min_value, int32_t max_value);
/// @brief Replace non-finite float parameters before D3D11 cbuffer/state upload.
float vgfx3d_d3d11_finite_or(float requested, float fallback);
/// @brief Clamp finite float parameters, using @p fallback for NaN/Inf values.
float vgfx3d_d3d11_clamp_float_param(float requested,
                                     float min_value,
                                     float max_value,
                                     float fallback);
/// @brief Normalize arbitrary integer flags to shader-facing 0/1 values.
int32_t vgfx3d_d3d11_sanitize_bool_flag(int32_t requested);
/// @brief Normalize light type constants to the shader-visible range.
int32_t vgfx3d_d3d11_sanitize_light_type(int32_t requested);
/// @brief Normalize shadow projection type, disabling perspective for unshadowed lights.
int32_t vgfx3d_d3d11_sanitize_shadow_projection_type(int32_t sanitized_shadow_index,
                                                     int32_t requested_projection_type);
/// @brief Clamp and order spot-light cone cosines before shader upload.
void vgfx3d_d3d11_sanitize_spot_cone(float requested_inner,
                                      float requested_outer,
                                      float *out_inner,
                                      float *out_outer);
/// @brief Sanitize shadow cascade split distances into a finite nondecreasing sequence.
void vgfx3d_d3d11_sanitize_shadow_cascade_splits(float *dst, const float *src, size_t count);
/// @brief Clamp a clustered-light global prefix to the uploaded light-array range.
int32_t vgfx3d_d3d11_sanitize_cluster_global_count(int32_t requested, int32_t light_count);
/// @brief Sanitize the clustered-light logarithmic Z range before shader upload.
void vgfx3d_d3d11_sanitize_cluster_depth_range(float requested_near,
                                               float requested_far,
                                               float *out_near,
                                               float *out_far);
/// @brief Return non-zero only when every value in @p values is finite.
int vgfx3d_d3d11_float_array_is_finite(const float *values, size_t count);
/// @brief Copy float constants while replacing NaN/Inf lanes with @p fallback.
void vgfx3d_d3d11_copy_float_array_finite_or(float *dst,
                                             const float *src,
                                             size_t count,
                                             float fallback);
/// @brief Validate a finite, non-degenerate direction vector for CPU/shader upload.
int vgfx3d_d3d11_vec3_direction_is_usable(const float *values);
/// @brief Copy a direction vector or a stable fallback when the source is unusable.
void vgfx3d_d3d11_copy_vec3_direction_or(float *dst,
                                         const float *src,
                                         const float fallback[3]);
/// @brief Copy a matrix when finite, otherwise write the identity matrix.
void vgfx3d_d3d11_copy_mat4_finite_or_identity(float *dst, const float *src);
/// @brief Copy a matrix when finite, otherwise copy @p fallback or identity.
void vgfx3d_d3d11_copy_mat4_finite_or(float *dst, const float *src, const float *fallback);
/// @brief Sanitize D3D11 rasterizer slope-scaled depth bias.
float vgfx3d_d3d11_sanitize_slope_scaled_depth_bias(float requested);
/// @brief Normalize material workflow constants before shader upload.
int32_t vgfx3d_d3d11_sanitize_material_workflow(int32_t requested);
/// @brief Normalize material alpha-mode constants before shader upload.
int32_t vgfx3d_d3d11_sanitize_alpha_mode(int32_t requested);
/// @brief Normalize Game3D shading-model constants before shader upload.
int32_t vgfx3d_d3d11_sanitize_shading_model(int32_t requested);
/// @brief Normalize tonemap mode constants before post-FX shader upload.
int32_t vgfx3d_d3d11_sanitize_tonemap_mode(int32_t requested);
/// @brief Sanitize fog near/far distances before scene constant upload.
void vgfx3d_d3d11_sanitize_fog_range(float requested_near,
                                      float requested_far,
                                      float *out_near,
                                      float *out_far);
/// @brief Sanitize D3D11 shader-facing shadow depth bias.
float vgfx3d_d3d11_sanitize_shadow_bias(float requested);
/// @brief Validate a backend-facing post-FX chain before indexed iteration.
int vgfx3d_d3d11_postfx_chain_is_usable(const vgfx3d_postfx_chain_t *chain);
/// @brief Return non-zero when one PostFX effect descriptor actually changes rendering.
int vgfx3d_d3d11_postfx_effect_is_active(const vgfx3d_postfx_effect_desc_t *effect);
/// @brief Return non-zero when a usable chain contains an active effect of @p type_value.
int vgfx3d_d3d11_postfx_chain_has_active_effect(const vgfx3d_postfx_chain_t *chain,
                                                int32_t type_value);
/// @brief Return non-zero when a usable chain contains any active effect.
int vgfx3d_d3d11_postfx_chain_has_active_effects(const vgfx3d_postfx_chain_t *chain);
/// @brief Decide whether the bone constant buffers need a per-draw upload.
int vgfx3d_d3d11_should_upload_bone_palette(int has_skinning, int has_prev_skinning);
/// @brief Saturating unsigned 64-bit addition for upload counters.
uint64_t vgfx3d_d3d11_saturating_add_u64(uint64_t a, uint64_t b);
/// @brief Capacity-doubling growth helper (saturates at INT_MAX).
int32_t vgfx3d_d3d11_next_capacity(int32_t current_capacity,
                                   int32_t needed,
                                   int32_t minimum_capacity);
/// @brief Overflow-safe row byte computation for tightly packed readback rows.
int vgfx3d_d3d11_compute_row_bytes(int32_t width, int32_t bytes_per_pixel, size_t *out_bytes);
/// @brief Compute a valid non-zero D3D11 buffer ByteWidth.
int vgfx3d_d3d11_compute_buffer_byte_width(size_t size, uint32_t *out_width);
/// @brief Compute a 16-byte-aligned D3D11 constant-buffer ByteWidth.
int vgfx3d_d3d11_compute_constant_buffer_byte_width(size_t size, uint32_t *out_width);
/// @brief Compute a D3D11 UpdateSubresource pitch for tightly packed RGBA8 rows.
int vgfx3d_d3d11_compute_rgba8_upload_pitch(int32_t width, uint32_t *out_pitch);
/// @brief Compute one mip level extent for a square D3D11 texture chain.
int vgfx3d_d3d11_expected_square_mip_extent(int32_t base_extent,
                                            int32_t mip_level,
                                            int32_t *out_extent);
/// @brief Compute one bloom mip extent for a scene-sized D3D11 post-FX chain.
int vgfx3d_d3d11_compute_bloom_mip_extent(int32_t width,
                                          int32_t height,
                                          int32_t mip_level,
                                          int32_t *out_width,
                                          int32_t *out_height);
/// @brief Validate an IBL mip payload extent against the destination cubemap mip.
int vgfx3d_d3d11_validate_ibl_mip_extent(int32_t face_size,
                                         int32_t mip_level,
                                         int32_t width,
                                         int32_t height);
/// @brief Compute a bounded morph-delta float count for D3D11 float SRV uploads.
int vgfx3d_d3d11_compute_morph_float_count(uint32_t vertex_count,
                                           int32_t requested_shape_count,
                                           size_t *out_elements);
/// @brief Compute a D3D11 vertex-buffer upload span for per-instance data.
int vgfx3d_d3d11_compute_instance_upload_bytes(int32_t instance_count,
                                               size_t instance_stride,
                                               size_t *out_bytes);
/// @brief Compute the byte span for a partial float-SRV buffer update.
int vgfx3d_d3d11_compute_float_srv_update_bytes(size_t element_count,
                                                size_t capacity,
                                                size_t *out_bytes);
/// @brief Validate an RGBA8 readback destination span.
int vgfx3d_d3d11_validate_rgba8_destination(int32_t width,
                                            int32_t height,
                                            int32_t stride,
                                            size_t *out_bytes);
/// @brief Validate a row range before building a D3D11_BOX.
int vgfx3d_d3d11_validate_row_span(int32_t extent, int32_t start, int32_t count);
/// @brief Check dimensions against D3D11 feature-level 11 texture limits.
int vgfx3d_d3d11_is_valid_texture2d_extent(int32_t width, int32_t height);
/// @brief Check a square cubemap face dimension against D3D11 limits.
int vgfx3d_d3d11_is_valid_cubemap_extent(int32_t face_size);
/// @brief Validate a mapped texture row span before copying it into an RGBA8 destination.
int vgfx3d_d3d11_validate_mapped_texture_copy(int32_t width,
                                              int32_t dst_stride,
                                              uint32_t src_row_pitch,
                                              int32_t src_bytes_per_pixel,
                                              size_t *out_src_row_bytes,
                                              size_t *out_dst_row_bytes);
/// @brief Bytes in one compressed/native texture block row.
uint64_t vgfx3d_d3d11_native_mip_row_bytes(const vgfx3d_native_texture_mip_t *mip);
/// @brief Number of compressed/native block rows in a mip.
uint64_t vgfx3d_d3d11_native_mip_block_rows(const vgfx3d_native_texture_mip_t *mip);
/// @brief Minimum bytes needed for the complete compressed/native mip payload.
uint64_t vgfx3d_d3d11_native_mip_required_bytes(const vgfx3d_native_texture_mip_t *mip);
/// @brief Expected block layout for a D3D11-native compressed texture format.
int vgfx3d_d3d11_native_format_block_layout(int32_t format_id,
                                            int32_t *out_block_width,
                                            int32_t *out_block_height,
                                            int32_t *out_block_bytes);
/// @brief Check that a native compressed mip is valid for a D3D11 texture mip chain.
int vgfx3d_d3d11_validate_native_mip_desc(const vgfx3d_native_texture_mip_t *mip,
                                          const vgfx3d_native_texture_mip_t *previous_mip,
                                          int32_t expected_format_id,
                                          int32_t expected_block_width,
                                          int32_t expected_block_height,
                                          int32_t expected_block_bytes);
/// @brief Check that a native mip count can fit in the D3D11 texture descriptor.
int vgfx3d_d3d11_is_valid_native_mip_count(int32_t base_width,
                                           int32_t base_height,
                                           int64_t mip_count);
/// @brief Clamp morph shapes so shader-side int indexing cannot overflow.
int32_t vgfx3d_d3d11_clamp_morph_shape_count(uint32_t vertex_count, int32_t requested_shape_count);
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
vgfx3d_d3d11_target_kind_t vgfx3d_d3d11_resolve_available_target(
    vgfx3d_d3d11_target_kind_t requested,
    int scene_available,
    int overlay_available,
    int rtt_available);
/// @brief Decide whether a cached morph payload still matches a draw command.
int vgfx3d_d3d11_should_reuse_morph_cache(const void *cached_key,
                                          uint64_t cached_revision,
                                          int32_t cached_shape_count,
                                          uint32_t cached_vertex_count,
                                          int8_t cached_has_normal_deltas,
                                          const vgfx3d_draw_cmd_t *cmd);
/// @brief Count the contiguous complete shadow slots that are safe to advertise to HLSL.
int32_t vgfx3d_d3d11_compute_shadow_count(int32_t slot_count, const int *slot_complete);
/// @brief Clamp or disable a light's shadow slot against the advertised slot range.
int32_t vgfx3d_d3d11_sanitize_shadow_index(int32_t requested_shadow_index,
                                           int32_t advertised_shadow_count);
/// @brief Clamp a light's cascade count so it cannot address beyond advertised shadow slots.
int32_t vgfx3d_d3d11_sanitize_shadow_cascade_count(int32_t requested_cascade_count,
                                                   int32_t sanitized_shadow_index,
                                                   int32_t advertised_shadow_count);
/// @brief Clamp a shadow-count value to the D3D11 shader-visible shadow slots.
int32_t vgfx3d_d3d11_clamp_shadow_count(int32_t advertised_shadow_count);
/// @brief Project a world-space point through a shadow matrix to HLSL UV/depth coordinates.
int vgfx3d_d3d11_project_shadow_coord(const float *shadow_vp,
                                      int32_t projection_type,
                                      const float world_pos[3],
                                      float out_uv_depth[3]);
/// @brief Decide whether an RTT frame has enough state to mark its CPU mirror dirty.
int vgfx3d_d3d11_should_mark_rtt_dirty(int8_t rtt_active,
                                       int has_target,
                                       int has_color_tex,
                                       int has_color_rtv,
                                       int has_depth_tex,
                                       int has_depth_dsv,
                                       int has_staging);
/// @brief Map a draw command to its required blend state (alpha vs opaque).
vgfx3d_d3d11_blend_mode_t vgfx3d_d3d11_choose_blend_mode(const vgfx3d_draw_cmd_t *cmd);
/// @brief Pick the color format — HDR16F for the scene pass, UNORM8 elsewhere.
vgfx3d_d3d11_color_format_t vgfx3d_d3d11_choose_color_format(
    vgfx3d_d3d11_target_kind_t target_kind);
/// @brief Decide whether the next pass should preserve existing color contents.
int8_t vgfx3d_d3d11_should_load_existing_color(vgfx3d_d3d11_target_kind_t target_kind,
                                               int8_t requested_load_existing_color,
                                               int8_t overlay_used_this_frame);
/// @brief Decide whether to attach the motion-vector target.
vgfx3d_d3d11_motion_attachment_mode_t vgfx3d_d3d11_choose_motion_attachment_mode(
    vgfx3d_d3d11_target_kind_t target_kind, const vgfx3d_draw_cmd_t *cmd);
/// @brief Decide whether terrain splatting has all required textures bound.
int vgfx3d_d3d11_has_complete_splat(int8_t cmd_has_splat,
                                    int has_splat_map,
                                    int has_layer0,
                                    int has_layer1,
                                    int has_layer2,
                                    int has_layer3);
/// @brief Decide whether scene color should be composited to the swapchain now.
int vgfx3d_d3d11_should_composite_to_swapchain(int8_t rtt_active,
                                               int8_t gpu_postfx_enabled,
                                               int has_scene_targets,
                                               int8_t scene_composited_to_swapchain);
/// @brief Decide whether a new begin-frame invalidates a prior swapchain composite.
int vgfx3d_d3d11_should_reset_composited_swapchain_for_frame(int8_t rtt_active,
                                                             int8_t load_existing_color);
/// @brief Decide whether a post-FX enable update invalidates a prior swapchain composite.
int vgfx3d_d3d11_should_reset_composited_swapchain_for_postfx_update(int8_t current_enabled,
                                                                     int8_t requested_enabled);
/// @brief Decide whether a begin-frame should preserve scene temporal history as an overlay pass.
int vgfx3d_d3d11_should_treat_begin_frame_as_overlay(
    vgfx3d_d3d11_target_kind_t resolved_target_kind, int8_t requested_load_existing_color);
/// @brief Decide whether the overlay pass is rendering into the separate overlay target.
int vgfx3d_d3d11_uses_separate_overlay_target(vgfx3d_d3d11_target_kind_t resolved_target_kind,
                                              int has_overlay_target);
/// @brief Decide whether readback should source a present snapshot, backbuffer, post-FX, or scene.
vgfx3d_d3d11_readback_kind_t vgfx3d_d3d11_choose_readback_kind(
    int8_t presented_snapshot_valid,
    int presented_snapshot_has_texture,
    int8_t scene_composited_to_swapchain,
    int8_t gpu_postfx_enabled,
    int8_t postfx_chain_valid,
    int8_t postfx_chain_enabled,
    int32_t postfx_effect_count,
    int postfx_has_effects,
    int has_scene_targets,
    vgfx3d_d3d11_target_kind_t current_target_kind);
/// @brief Decide whether a pre-present snapshot remains valid after Present returns.
int vgfx3d_d3d11_should_keep_presented_snapshot(int snapshot_ok, int present_ok);

#ifdef __cplusplus
}
#endif
