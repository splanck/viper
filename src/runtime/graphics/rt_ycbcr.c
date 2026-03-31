//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_ycbcr.c
// Purpose: YCbCr 4:2:0 to RGBA conversion using BT.601 coefficients.
//   Integer-only arithmetic for performance (no floating point per pixel).
//
// Key invariants:
//   - BT.601 matrix (ITU-R BT.601, standard for Theora/SD video):
//     R = 1.164*(Y-16) + 1.596*(Cr-128)
//     G = 1.164*(Y-16) - 0.392*(Cb-128) - 0.813*(Cr-128)
//     B = 1.164*(Y-16) + 2.017*(Cb-128)
//   - Fixed-point: multiply by 298/256, 409/256, etc. (8-bit shift).
//   - Chroma upsampling: nearest-neighbor (each Cb/Cr sample covers 2x2 luma).
//
// Links: rt_ycbcr.h
//
//===----------------------------------------------------------------------===//

#include "rt_ycbcr.h"

static inline int32_t clamp255(int32_t v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

void ycbcr420_to_rgba(const uint8_t *y_plane, const uint8_t *cb_plane,
                       const uint8_t *cr_plane, int32_t width, int32_t height,
                       int32_t y_stride, int32_t c_stride, uint32_t *rgba_out) {
    if (!y_plane || !cb_plane || !cr_plane || !rgba_out)
        return;
    if (width <= 0 || height <= 0)
        return;

    for (int32_t row = 0; row < height; row++) {
        const uint8_t *y_row = y_plane + row * y_stride;
        const uint8_t *cb_row = cb_plane + (row / 2) * c_stride;
        const uint8_t *cr_row = cr_plane + (row / 2) * c_stride;
        uint32_t *out_row = rgba_out + row * width;

        for (int32_t col = 0; col < width; col++) {
            int32_t y = (int32_t)y_row[col] - 16;
            int32_t cb = (int32_t)cb_row[col / 2] - 128;
            int32_t cr = (int32_t)cr_row[col / 2] - 128;

            /* BT.601 fixed-point conversion (×256, then >>8) */
            int32_t r = (298 * y + 409 * cr + 128) >> 8;
            int32_t g = (298 * y - 100 * cb - 208 * cr + 128) >> 8;
            int32_t b = (298 * y + 516 * cb + 128) >> 8;

            r = clamp255(r);
            g = clamp255(g);
            b = clamp255(b);

            /* Pack as 0xRRGGBBAA (Viper Pixels format) */
            out_row[col] = ((uint32_t)r << 24) | ((uint32_t)g << 16) |
                           ((uint32_t)b << 8) | 0xFF;
        }
    }
}
