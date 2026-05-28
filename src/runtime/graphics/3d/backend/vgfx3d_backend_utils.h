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

/// @brief Decode a Pixels object into a freshly malloc'd RGBA8 byte array (caller frees).
int vgfx3d_unpack_pixels_rgba(const void *pixels_ptr,
                              int32_t *out_w,
                              int32_t *out_h,
                              uint8_t **out_rgba);
/// @brief Read the Pixels generation counter (used to detect when a GPU upload is required).
uint64_t vgfx3d_get_pixels_generation(const void *pixels_ptr);
/// @brief Stable cache signature for a Pixels object (identity + generation).
uint64_t vgfx3d_get_pixels_cache_key(const void *pixels_ptr);
/// @brief Decode all six cubemap faces into separate RGBA8 byte arrays (caller frees each).
int vgfx3d_unpack_cubemap_faces_rgba(const void *cubemap_ptr,
                                     int32_t *out_face_size,
                                     uint8_t *out_faces[6]);
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
