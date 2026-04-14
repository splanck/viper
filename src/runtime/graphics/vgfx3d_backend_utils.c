//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_utils.c
// Purpose: Cross-backend utility helpers shared between the Metal/OpenGL/D3D11
//   3D backends — pixel/cubemap unpack to RGBA8, generation tracking,
//   row-flip, normal-matrix derivation from a model matrix, and 4×4 inverse.
//
// Key invariants:
//   - Pixels payloads are 0xRRGGBBAA in `uint32_t`, row-major, top-left origin.
//   - Normal matrix is the inverse-transpose of the model matrix's upper 3×3,
//     stored in the upper-left 3×3 of the 4×4 output (M[15] = 1, rest 0).
//   - Cubemap generation is a 64-bit FNV-style hash of all six face generations,
//     enabling cheap "did anything change?" checks for backend caches.
//
// Links: vgfx3d_backend_utils.h, vgfx3d_backend_*.c (per-API implementations)
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t w;
    int64_t h;
    uint32_t *data;
    uint64_t generation;
} vgfx3d_pixels_view_t;

typedef struct {
    void *vptr;
    void *faces[6];
    int64_t face_size;
} vgfx3d_cubemap_view_t;

/// @brief Read the monotonic generation counter on a Pixels object.
/// Returns 0 for null. Backends compare against last-seen generation to detect
/// when a GPU texture upload is required.
uint64_t vgfx3d_get_pixels_generation(const void *pixels_ptr) {
    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    if (!pv)
        return 0;
    return pv->generation;
}

/// @brief Decode a Pixels object into a freshly malloc'd RGBA8 byte array.
/// Caller owns and frees the returned buffer. Returns 0 on success, -1 on
/// invalid dimensions or allocation failure. Out-params are unmodified on error.
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

/// @brief Decode all six cubemap faces into separate RGBA8 byte arrays.
/// All faces must be square and the same size. Caller owns and frees each
/// face buffer. On error any partially-allocated faces are freed automatically.
int vgfx3d_unpack_cubemap_faces_rgba(const void *cubemap_ptr,
                                     int32_t *out_face_size,
                                     uint8_t *out_faces[6]) {
    int32_t face_size = 0;
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;

    if (!cubemap || !out_face_size || !out_faces || cubemap->face_size <= 0 ||
        cubemap->face_size > INT32_MAX)
        return -1;

    for (int face = 0; face < 6; face++)
        out_faces[face] = NULL;

    face_size = (int32_t)cubemap->face_size;
    for (int face = 0; face < 6; face++) {
        int32_t w = 0;
        int32_t h = 0;
        if (vgfx3d_unpack_pixels_rgba(cubemap->faces[face], &w, &h, &out_faces[face]) != 0 ||
            w != face_size || h != face_size) {
            for (int cleanup = 0; cleanup < 6; cleanup++) {
                free(out_faces[cleanup]);
                out_faces[cleanup] = NULL;
            }
            return -1;
        }
    }

    *out_face_size = face_size;
    return 0;
}

/// @brief Hash all six face generations into a single cubemap-level signature.
/// Uses an FNV-prime mixing scheme so per-face changes propagate. Returns 0
/// when no faces are bound.
uint64_t vgfx3d_get_cubemap_generation(const void *cubemap_ptr) {
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;
    uint64_t signature = 1469598103934665603ull;
    int any_face = 0;

    if (!cubemap)
        return 0;

    for (int face = 0; face < 6; face++) {
        uint64_t generation = vgfx3d_get_pixels_generation(cubemap->faces[face]);
        signature ^= generation + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);
        any_face |= (cubemap->faces[face] != NULL);
    }

    return any_face ? signature : 0;
}

/// @brief Flip an RGBA8 image vertically in place (top<->bottom row swap).
/// Used to convert between Pixels' top-left origin and APIs that expect
/// bottom-left (e.g., OpenGL textures).
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

/// @brief Compute the normal matrix (inverse-transpose of the upper 3×3 of
/// @p model_matrix) and place it in the upper-left 3×3 of @p out_matrix.
/// Falls back to copying the upper 3×3 directly when the matrix is singular,
/// avoiding NaN/Inf propagation in shaders.
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

/// @brief Invert a 4×4 row-major matrix using cofactor expansion.
/// @return 0 on success, -1 if @p matrix is null or singular (|det| < 1e-12).
/// Out-buffer is unmodified on failure.
int vgfx3d_invert_matrix4(const float *matrix, float *out_matrix) {
    float inv[16];
    float det;

    if (!matrix || !out_matrix)
        return -1;

    inv[0] = matrix[5] * matrix[10] * matrix[15] - matrix[5] * matrix[11] * matrix[14] -
             matrix[9] * matrix[6] * matrix[15] + matrix[9] * matrix[7] * matrix[14] +
             matrix[13] * matrix[6] * matrix[11] - matrix[13] * matrix[7] * matrix[10];
    inv[4] = -matrix[4] * matrix[10] * matrix[15] + matrix[4] * matrix[11] * matrix[14] +
             matrix[8] * matrix[6] * matrix[15] - matrix[8] * matrix[7] * matrix[14] -
             matrix[12] * matrix[6] * matrix[11] + matrix[12] * matrix[7] * matrix[10];
    inv[8] = matrix[4] * matrix[9] * matrix[15] - matrix[4] * matrix[11] * matrix[13] -
             matrix[8] * matrix[5] * matrix[15] + matrix[8] * matrix[7] * matrix[13] +
             matrix[12] * matrix[5] * matrix[11] - matrix[12] * matrix[7] * matrix[9];
    inv[12] = -matrix[4] * matrix[9] * matrix[14] + matrix[4] * matrix[10] * matrix[13] +
              matrix[8] * matrix[5] * matrix[14] - matrix[8] * matrix[6] * matrix[13] -
              matrix[12] * matrix[5] * matrix[10] + matrix[12] * matrix[6] * matrix[9];
    inv[1] = -matrix[1] * matrix[10] * matrix[15] + matrix[1] * matrix[11] * matrix[14] +
             matrix[9] * matrix[2] * matrix[15] - matrix[9] * matrix[3] * matrix[14] -
             matrix[13] * matrix[2] * matrix[11] + matrix[13] * matrix[3] * matrix[10];
    inv[5] = matrix[0] * matrix[10] * matrix[15] - matrix[0] * matrix[11] * matrix[14] -
             matrix[8] * matrix[2] * matrix[15] + matrix[8] * matrix[3] * matrix[14] +
             matrix[12] * matrix[2] * matrix[11] - matrix[12] * matrix[3] * matrix[10];
    inv[9] = -matrix[0] * matrix[9] * matrix[15] + matrix[0] * matrix[11] * matrix[13] +
             matrix[8] * matrix[1] * matrix[15] - matrix[8] * matrix[3] * matrix[13] -
             matrix[12] * matrix[1] * matrix[11] + matrix[12] * matrix[3] * matrix[9];
    inv[13] = matrix[0] * matrix[9] * matrix[14] - matrix[0] * matrix[10] * matrix[13] -
              matrix[8] * matrix[1] * matrix[14] + matrix[8] * matrix[2] * matrix[13] +
              matrix[12] * matrix[1] * matrix[10] - matrix[12] * matrix[2] * matrix[9];
    inv[2] = matrix[1] * matrix[6] * matrix[15] - matrix[1] * matrix[7] * matrix[14] -
             matrix[5] * matrix[2] * matrix[15] + matrix[5] * matrix[3] * matrix[14] +
             matrix[13] * matrix[2] * matrix[7] - matrix[13] * matrix[3] * matrix[6];
    inv[6] = -matrix[0] * matrix[6] * matrix[15] + matrix[0] * matrix[7] * matrix[14] +
             matrix[4] * matrix[2] * matrix[15] - matrix[4] * matrix[3] * matrix[14] -
             matrix[12] * matrix[2] * matrix[7] + matrix[12] * matrix[3] * matrix[6];
    inv[10] = matrix[0] * matrix[5] * matrix[15] - matrix[0] * matrix[7] * matrix[13] -
              matrix[4] * matrix[1] * matrix[15] + matrix[4] * matrix[3] * matrix[13] +
              matrix[12] * matrix[1] * matrix[7] - matrix[12] * matrix[3] * matrix[5];
    inv[14] = -matrix[0] * matrix[5] * matrix[14] + matrix[0] * matrix[6] * matrix[13] +
              matrix[4] * matrix[1] * matrix[14] - matrix[4] * matrix[2] * matrix[13] -
              matrix[12] * matrix[1] * matrix[6] + matrix[12] * matrix[2] * matrix[5];
    inv[3] = -matrix[1] * matrix[6] * matrix[11] + matrix[1] * matrix[7] * matrix[10] +
             matrix[5] * matrix[2] * matrix[11] - matrix[5] * matrix[3] * matrix[10] -
             matrix[9] * matrix[2] * matrix[7] + matrix[9] * matrix[3] * matrix[6];
    inv[7] = matrix[0] * matrix[6] * matrix[11] - matrix[0] * matrix[7] * matrix[10] -
             matrix[4] * matrix[2] * matrix[11] + matrix[4] * matrix[3] * matrix[10] +
             matrix[8] * matrix[2] * matrix[7] - matrix[8] * matrix[3] * matrix[6];
    inv[11] = -matrix[0] * matrix[5] * matrix[11] + matrix[0] * matrix[7] * matrix[9] +
              matrix[4] * matrix[1] * matrix[11] - matrix[4] * matrix[3] * matrix[9] -
              matrix[8] * matrix[1] * matrix[7] + matrix[8] * matrix[3] * matrix[5];
    inv[15] = matrix[0] * matrix[5] * matrix[10] - matrix[0] * matrix[6] * matrix[9] -
              matrix[4] * matrix[1] * matrix[10] + matrix[4] * matrix[2] * matrix[9] +
              matrix[8] * matrix[1] * matrix[6] - matrix[8] * matrix[2] * matrix[5];

    det = matrix[0] * inv[0] + matrix[1] * inv[4] + matrix[2] * inv[8] + matrix[3] * inv[12];
    if (fabsf(det) < 1e-12f)
        return -1;

    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out_matrix[i] = inv[i] * det;
    return 0;
}
