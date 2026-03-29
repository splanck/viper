#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vgfx3d_unpack_pixels_rgba(const void *pixels_ptr,
                              int32_t *out_w,
                              int32_t *out_h,
                              uint8_t **out_rgba);
void vgfx3d_flip_rgba_rows(uint8_t *rgba, int32_t w, int32_t h);
void vgfx3d_compute_normal_matrix4(const float *model_matrix, float *out_matrix);

#ifdef __cplusplus
}
#endif
