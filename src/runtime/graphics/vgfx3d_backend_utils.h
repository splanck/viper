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
/// @brief Compute the normal matrix (inverse-transpose of model 3×3) into a 4×4 output.
void vgfx3d_compute_normal_matrix4(const float *model_matrix, float *out_matrix);
/// @brief Invert a row-major 4×4 matrix. Returns 0 on success, -1 if the matrix is singular.
int vgfx3d_invert_matrix4(const float *matrix, float *out_matrix);

#ifdef __cplusplus
}
#endif
