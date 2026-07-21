//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_utils.h
// Purpose: Backend-agnostic helper declarations shared by the vgfx3d render
//   backends — Pixels-to-RGBA8 decoding and other small format/utility
//   routines used when uploading CPU images to GPU textures or sizing
//   window-backed scene targets.
//
// Key invariants:
//   - Helpers are pure/stateless; outputs are caller-owned heap buffers.
//
// Ownership/Lifetime:
//   - Functions that return/allocate buffers transfer ownership to the
//     caller (caller frees); inputs are borrowed and not retained.
//
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *data;
    uint64_t bytes;
    int32_t width;
    int32_t height;
    int32_t block_width;
    int32_t block_height;
    int32_t block_bytes;
    int32_t format_id;
} vgfx3d_native_texture_mip_t;

struct vgfx3d_draw_cmd;
struct vgfx3d_camera_params;
struct vgfx3d_light_params;
struct vgfx3d_cluster_table;
struct vgfx3d_postfx_chain;
struct vgfx3d_postfx_snapshot;

#define VGFX3D_BACKEND_MATRIX_COMPONENT_ABS_MAX 1000000000000.0f

/// @brief Return non-zero when every float is finite and within the absolute bound.
int vgfx3d_float_array_is_bounded(const float *values, size_t count, float abs_max);
/// @brief Copy finite floats, substituting a finite fallback for invalid lanes.
void vgfx3d_copy_float_array_finite_or(float *dst, const float *src, size_t count, float fallback);
/// @brief Copy a bounded matrix, or write identity when the source is unusable.
void vgfx3d_copy_mat4_finite_or_identity(float *dst, const float *src);
/// @brief Copy a bounded matrix, falling back to another bounded matrix or identity.
void vgfx3d_copy_mat4_finite_or(float *dst, const float *src, const float *fallback);
/// @brief Validate a bounded shadow matrix with at least one useful component.
int vgfx3d_shadow_matrix_is_usable(const float *matrix);
/// @brief Copy and normalize a direction, using the fallback/default when unusable.
void vgfx3d_copy_vec3_direction_or(float *dst, const float *src, const float fallback[3]);
/// @brief Return the requested finite value, or a finite fallback (ultimately zero).
float vgfx3d_finite_or(float requested, float fallback);
/// @brief Clamp a finite value, substituting the fallback and tolerating inverted bounds.
float vgfx3d_clamp_float_param(float requested, float min_value, float max_value, float fallback);
/// @brief Normalize material workflow constants to legacy or PBR.
int32_t vgfx3d_sanitize_material_workflow(int32_t requested);
/// @brief Normalize alpha-mode constants to the public opaque/mask/blend range.
int32_t vgfx3d_sanitize_alpha_mode(int32_t requested);
/// @brief Normalize Game3D shading-model constants to the shader-visible range.
int32_t vgfx3d_sanitize_shading_model(int32_t requested);
/// @brief Normalize material shadow-mode constants to auto/none/cast.
int32_t vgfx3d_sanitize_shadow_mode(int32_t requested);
/// @brief Normalize texture-wrap constants, defaulting malformed values to repeat.
int32_t vgfx3d_sanitize_texture_wrap(int32_t requested);
/// @brief Normalize texture-filter constants, defaulting malformed values to linear.
int32_t vgfx3d_sanitize_texture_filter(int32_t requested);
/// @brief Normalize texture mip-filter constants, defaulting malformed values to none.
int32_t vgfx3d_sanitize_texture_mip_filter(int32_t requested);
/// @brief Normalize a texture UV-set selector to the shader-visible uv0/uv1 range.
int32_t vgfx3d_sanitize_texture_uv_set(int32_t requested);
/// @brief Copy a draw command while normalizing backend-visible material state.
void vgfx3d_sanitize_draw_command(const struct vgfx3d_draw_cmd *src, struct vgfx3d_draw_cmd *dst);
/// @brief Copy per-frame camera state while normalizing every backend-visible value.
void vgfx3d_sanitize_camera_params(const struct vgfx3d_camera_params *src,
                                   struct vgfx3d_camera_params *dst);
/// @brief Copy one light while normalizing all shader-visible scalar/vector state.
void vgfx3d_sanitize_light_params(const struct vgfx3d_light_params *src,
                                  struct vgfx3d_light_params *dst);
/// @brief Copy a bounded light array and return the number of initialized outputs.
int32_t vgfx3d_sanitize_light_array(const struct vgfx3d_light_params *src,
                                    int32_t count,
                                    struct vgfx3d_light_params *dst,
                                    int32_t dst_capacity);
/// @brief Clamp one light's shadow base/span to the contiguous advertised slot range.
void vgfx3d_sanitize_light_shadow_span(struct vgfx3d_light_params *light, int32_t shadow_count);
/// @brief Copy non-negative bounded ambient RGB, treating a NULL source as black.
void vgfx3d_sanitize_ambient_rgb(const float *src, float dst[3]);
/// @brief Validate a clustered-light table before any fixed-size GPU upload/indexing.
int vgfx3d_cluster_table_is_usable(const struct vgfx3d_cluster_table *table,
                                   uint32_t expected_revision,
                                   int32_t light_count);
/// @brief Validate an enabled post-FX chain before indexed effect traversal.
int vgfx3d_postfx_chain_is_usable(const struct vgfx3d_postfx_chain *chain);
/// @brief Copy one post-FX snapshot while clamping shader loops and numeric parameters.
void vgfx3d_sanitize_postfx_snapshot(const struct vgfx3d_postfx_snapshot *src,
                                     struct vgfx3d_postfx_snapshot *dst);
/// @brief Convert reversed-Z storage to canonical depth, returning -1 for invalid samples.
float vgfx3d_sanitize_reversed_depth_probe_result(float reversed_depth);

/// @brief Compute the normative scene-target extent for a logical output and render scale.
/// @details Each result is `floor(output_dimension * scale)`, clamped to one pixel. A scale of
///          exactly 1 preserves the input extent. The accepted scale range is the closed interval
///          `[0.25, 1]`; accepting only that range keeps every backend's direct hook behavior
///          consistent with `Canvas3D.TrySetRenderScale`.
/// @param output_width Logical presentation width in pixels; must be positive.
/// @param output_height Logical presentation height in pixels; must be positive.
/// @param scale Finite scene scale in the closed interval `[0.25, 1]`.
/// @param out_scene_width Receives the scaled scene width; cleared before validation.
/// @param out_scene_height Receives the scaled scene height; cleared before validation.
/// @return 1 when both outputs contain a valid extent, otherwise 0. Any non-null output pointer is
///         cleared on failure.
int vgfx3d_compute_scaled_scene_extent(int32_t output_width,
                                       int32_t output_height,
                                       float scale,
                                       int32_t *out_scene_width,
                                       int32_t *out_scene_height);

/// @brief Compute a valid square texture mip extent from a positive base extent.
int vgfx3d_expected_square_mip_extent(int32_t base_extent, int32_t mip_level, int32_t *out_extent);
/// @brief Validate a prefiltered IBL chain against a cubemap's complete mip pyramid.
int vgfx3d_validate_cubemap_ibl_layout(int32_t face_size,
                                       int32_t ibl_base_size,
                                       int32_t ibl_mip_count,
                                       int32_t max_ibl_mips,
                                       int32_t *out_level_base);
/// @brief Check whether a whole-resource upload fits the remaining frame budget.
int vgfx3d_upload_budget_allows(uint64_t budget, uint64_t used, uint64_t requested);

/// @brief Decode a Pixels object into a freshly malloc'd RGBA8 byte array (caller frees).
int vgfx3d_unpack_pixels_rgba(const void *pixels_ptr,
                              int32_t *out_w,
                              int32_t *out_h,
                              uint8_t **out_rgba);
/// @brief Return the valid extent of a Pixels object without allocating.
int vgfx3d_get_pixels_extent(const void *pixels_ptr, int32_t *out_w, int32_t *out_h);
/// @brief Decode a row slice from a Pixels object into a freshly malloc'd RGBA8 array.
int vgfx3d_unpack_pixels_rgba_rows(const void *pixels_ptr,
                                   int32_t start_row,
                                   int32_t row_count,
                                   int flip_y,
                                   int32_t *out_w,
                                   int32_t *out_rows,
                                   uint8_t **out_rgba);
/// @brief Compute the RGBA8 byte count uploaded for one Pixels texture.
int vgfx3d_estimate_pixels_rgba_upload_bytes(const void *pixels_ptr, uint64_t *out_bytes);
/// @brief Return how many texture rows may be uploaded under a per-frame byte budget.
int32_t vgfx3d_upload_rows_for_budget(
    int32_t width, int32_t height, int32_t next_row, uint64_t budget, uint64_t used);
/// @brief Compute remaining RGBA8 row bytes for one in-progress 2D texture upload.
uint64_t vgfx3d_pending_rgba_upload_bytes(int32_t width,
                                          int32_t height,
                                          int32_t next_row,
                                          int upload_in_progress);
/// @brief Compute remaining RGBA8 row bytes for one in-progress six-face cubemap upload.
uint64_t vgfx3d_pending_cubemap_rgba_upload_bytes(int32_t face_size,
                                                  int32_t upload_face,
                                                  int32_t upload_next_row,
                                                  int upload_in_progress);
/// @brief Return how many compressed block rows may upload under a per-frame byte budget.
int32_t vgfx3d_upload_block_rows_for_budget(int32_t width,
                                            int32_t height,
                                            int32_t block_width,
                                            int32_t block_height,
                                            int32_t block_bytes,
                                            int32_t next_block_row,
                                            uint64_t budget,
                                            uint64_t used);
/// @brief Compute remaining compressed block-row bytes for one in-progress texture upload.
uint64_t vgfx3d_pending_block_upload_bytes(int32_t width,
                                           int32_t height,
                                           int32_t block_width,
                                           int32_t block_height,
                                           int32_t block_bytes,
                                           int32_t next_block_row,
                                           int upload_in_progress);
/// @brief Read the Pixels generation counter (used to detect when a GPU upload is required).
uint64_t vgfx3d_get_pixels_generation(const void *pixels_ptr);
/// @brief Stable cache signature for a Pixels object (identity + generation).
uint64_t vgfx3d_get_pixels_cache_key(const void *pixels_ptr);
/// @brief True when a TextureAsset3D can use native compressed upload under @p caps.
int vgfx3d_textureasset_native_supported(void *asset, int64_t native_caps);
/// @brief Borrow one resident native mip from a TextureAsset3D by relative mip index.
int vgfx3d_textureasset_get_native_resident_mip(void *asset,
                                                int64_t relative_mip,
                                                vgfx3d_native_texture_mip_t *out_mip);
/// @brief Borrow one native mip from a draw-time TextureAsset3D resident-window snapshot.
/// @details @p first_mip and @p mip_count are captured when the draw command is queued, so a
///          later TextureAsset3D residency change cannot alter which native payload a deferred
///          draw uploads. This helper does not mutate the asset or its current resident window.
int vgfx3d_textureasset_get_native_snapshot_mip(void *asset,
                                                int64_t first_mip,
                                                int64_t mip_count,
                                                int64_t relative_mip,
                                                vgfx3d_native_texture_mip_t *out_mip);
/// @brief True when a native mip declares the canonical block footprint for its format.
int vgfx3d_native_texture_block_layout_is_valid(const vgfx3d_native_texture_mip_t *mip);
/// @brief Validate one native compressed mip against its captured chain and block layout.
int vgfx3d_validate_native_texture_mip(const vgfx3d_native_texture_mip_t *mip,
                                       int32_t base_width,
                                       int32_t base_height,
                                       int64_t relative_mip,
                                       int32_t expected_format_id,
                                       int32_t expected_block_width,
                                       int32_t expected_block_height,
                                       int32_t expected_block_bytes,
                                       uint64_t max_payload_bytes,
                                       uint64_t *out_required_bytes);
/// @brief Sum native mip payload bytes still pending from @p next_relative_mip/block-row cursor.
uint64_t vgfx3d_textureasset_pending_native_bytes(void *asset,
                                                  int64_t next_relative_mip,
                                                  int32_t next_block_row,
                                                  int upload_in_progress);
/// @brief Sum pending bytes for a draw-time TextureAsset3D resident-window snapshot.
uint64_t vgfx3d_textureasset_pending_native_snapshot_bytes(void *asset,
                                                           int64_t first_mip,
                                                           int64_t mip_count,
                                                           int64_t next_relative_mip,
                                                           int32_t next_block_row,
                                                           int upload_in_progress);
/// @brief Decode all six cubemap faces into separate RGBA8 byte arrays (caller frees each).
int vgfx3d_unpack_cubemap_faces_rgba(const void *cubemap_ptr,
                                     int32_t *out_face_size,
                                     uint8_t *out_faces[6]);
/// @brief Return the valid square face size for a cubemap without allocating.
int vgfx3d_get_cubemap_face_size(const void *cubemap_ptr, int32_t *out_face_size);
/// @brief Decode a cubemap face row slice into a freshly malloc'd RGBA8 array.
int vgfx3d_unpack_cubemap_rgba_rows(const void *cubemap_ptr,
                                    int32_t face_index,
                                    int32_t start_row,
                                    int32_t row_count,
                                    int flip_y,
                                    int32_t *out_face_size,
                                    int32_t *out_rows,
                                    uint8_t **out_rgba);
/// @brief Compute the RGBA8 byte count uploaded for one six-face cubemap.
int vgfx3d_estimate_cubemap_rgba_upload_bytes(const void *cubemap_ptr, uint64_t *out_bytes);
/// @brief Combined generation hash across all six cubemap faces.
uint64_t vgfx3d_get_cubemap_generation(const void *cubemap_ptr);
/// @brief Flip an RGBA8 image vertically in place (top<->bottom row swap, OpenGL Y-flip).
void vgfx3d_flip_rgba_rows(uint8_t *rgba, int32_t w, int32_t h);
/// @brief Convert IEEE-754 binary16 to float32.
float vgfx3d_half_to_float(uint16_t bits);
/// @brief Clamp a linear float to [0,1] and quantize to 8-bit UNORM.
uint8_t vgfx3d_float_to_unorm8(float value);
/// @brief Tonemap an HDR linear float with Reinhard and quantize to 8-bit UNORM.
uint8_t vgfx3d_hdr_to_unorm8(float value);
/// @brief Convert linear RGBA16F pixels to displayable RGBA8 (RGB tonemapped, alpha clamped).
void vgfx3d_copy_linear_rgba16f_to_rgba8(uint8_t *dst_rgba,
                                         int32_t dst_stride,
                                         int32_t copy_w,
                                         int32_t copy_h,
                                         const uint16_t *src_rgba16f,
                                         int32_t src_stride_bytes);
/// @brief Convert linear RGBA16F pixels to linear RGBA32F.
void vgfx3d_copy_linear_rgba16f_to_rgba32f(float *dst_rgba32f,
                                           int32_t dst_stride_floats,
                                           int32_t copy_w,
                                           int32_t copy_h,
                                           const uint16_t *src_rgba16f,
                                           int32_t src_stride_bytes);
/// @brief Convert linear RGBA32F pixels to displayable RGBA8 (RGB tonemapped, alpha clamped).
void vgfx3d_copy_linear_rgba32f_to_rgba8(uint8_t *dst_rgba,
                                         int32_t dst_stride,
                                         int32_t copy_w,
                                         int32_t copy_h,
                                         const float *src_rgba32f,
                                         int32_t src_stride_bytes);
/// @brief Compute the normal matrix (inverse-transpose of model 3×3) into a 4×4 output.
void vgfx3d_compute_normal_matrix4(const float *model_matrix, float *out_matrix);
/// @brief Invert a row-major 4×4 matrix. Returns 0 on success, -1 if the matrix is singular.
int vgfx3d_invert_matrix4(const float *matrix, float *out_matrix);

#ifdef __cplusplus
}
#endif
