#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t w;
    int64_t h;
    uint32_t *data;
} vgfx3d_pixels_view_t;

int vgfx3d_unpack_pixels_rgba(const void *pixels_ptr,
                              int32_t *out_w,
                              int32_t *out_h,
                              uint8_t **out_rgba) {
    if (!pixels_ptr || !out_w || !out_h || !out_rgba)
        return -1;

    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    if (!pv->data || pv->w <= 0 || pv->h <= 0 || pv->w > INT32_MAX || pv->h > INT32_MAX)
        return -1;

    int32_t w = (int32_t)pv->w;
    int32_t h = (int32_t)pv->h;
    size_t pixel_count = (size_t)w * (size_t)h;
    uint8_t *rgba = (uint8_t *)malloc(pixel_count * 4);
    if (!rgba)
        return -1;

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = pv->data[i]; /* 0xRRGGBBAA */
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    *out_w = w;
    *out_h = h;
    *out_rgba = rgba;
    return 0;
}

void vgfx3d_flip_rgba_rows(uint8_t *rgba, int32_t w, int32_t h) {
    if (!rgba || w <= 0 || h <= 1)
        return;

    size_t row_bytes = (size_t)w * 4;
    uint8_t *tmp = (uint8_t *)malloc(row_bytes);
    if (!tmp)
        return;

    for (int32_t y = 0; y < h / 2; y++) {
        uint8_t *top = rgba + (size_t)y * row_bytes;
        uint8_t *bot = rgba + (size_t)(h - 1 - y) * row_bytes;
        memcpy(tmp, top, row_bytes);
        memcpy(top, bot, row_bytes);
        memcpy(bot, tmp, row_bytes);
    }

    free(tmp);
}

void vgfx3d_compute_normal_matrix4(const float *model_matrix, float *out_matrix) {
    if (!model_matrix || !out_matrix)
        return;

    const float a = model_matrix[0], b = model_matrix[1], c = model_matrix[2];
    const float d = model_matrix[4], e = model_matrix[5], f = model_matrix[6];
    const float g = model_matrix[8], h = model_matrix[9], i = model_matrix[10];

    const float c00 = e * i - f * h;
    const float c01 = -(d * i - f * g);
    const float c02 = d * h - e * g;
    const float c10 = -(b * i - c * h);
    const float c11 = a * i - c * g;
    const float c12 = -(a * h - b * g);
    const float c20 = b * f - c * e;
    const float c21 = -(a * f - c * d);
    const float c22 = a * e - b * d;

    float det = a * c00 + b * c01 + c * c02;
    float inv_det = 0.0f;
    if (fabsf(det) > 1e-8f)
        inv_det = 1.0f / det;

    memset(out_matrix, 0, sizeof(float) * 16);
    out_matrix[15] = 1.0f;

    if (inv_det == 0.0f) {
        out_matrix[0] = a;
        out_matrix[1] = b;
        out_matrix[2] = c;
        out_matrix[4] = d;
        out_matrix[5] = e;
        out_matrix[6] = f;
        out_matrix[8] = g;
        out_matrix[9] = h;
        out_matrix[10] = i;
        return;
    }

    out_matrix[0] = c00 * inv_det;
    out_matrix[1] = c10 * inv_det;
    out_matrix[2] = c20 * inv_det;
    out_matrix[4] = c01 * inv_det;
    out_matrix[5] = c11 * inv_det;
    out_matrix[6] = c21 * inv_det;
    out_matrix[8] = c02 * inv_det;
    out_matrix[9] = c12 * inv_det;
    out_matrix[10] = c22 * inv_det;
}
