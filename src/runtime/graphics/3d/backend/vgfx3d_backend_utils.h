//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_utils.h
// Purpose: Backend-agnostic helper declarations shared by the vgfx3d render
//   backends — Pixels-to-RGBA8 decoding and other small format/utility
//   routines used when uploading CPU images to GPU textures.
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
