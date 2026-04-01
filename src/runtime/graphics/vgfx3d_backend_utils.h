#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vgfx3d_unpack_pixels_rgba(const void *pixels_ptr,
                              int32_t *out_w,
                              int32_t *out_h,
                              uint8_t **out_rgba);
uint64_t vgfx3d_get_pixels_generation(const void *pixels_ptr);
int vgfx3d_unpack_cubemap_faces_rgba(const void *cubemap_ptr,
                                     int32_t *out_face_size,
                                     uint8_t *out_faces[6]);
uint64_t vgfx3d_get_cubemap_generation(const void *cubemap_ptr);
void vgfx3d_flip_rgba_rows(uint8_t *rgba, int32_t w, int32_t h);
void vgfx3d_compute_normal_matrix4(const float *model_matrix, float *out_matrix);
int vgfx3d_invert_matrix4(const float *matrix, float *out_matrix);

#ifdef __cplusplus
}
#endif
