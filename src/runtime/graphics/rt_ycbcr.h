//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_ycbcr.h
// Purpose: YCbCr 4:2:0 → RGBA conversion for video decoders (Theora, etc.)
//   Uses BT.601 matrix coefficients (standard for SD video).
//
// Key invariants:
//   - Input: separate Y, Cb, Cr planes. Y is full resolution, Cb/Cr are
//     half-width and half-height (4:2:0 chroma subsampling).
//   - Output: 0xRRGGBBAA packed uint32_t array (Viper Pixels format).
//   - Clamping: output values clamped to [0, 255].
//
// Links: rt_theora.h, rt_videoplayer.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Convert YCbCr 4:2:0 planes to RGBA pixel array.
/// @param y_plane   Luma plane (width × height).
/// @param cb_plane  Cb chroma plane (width/2 × height/2).
/// @param cr_plane  Cr chroma plane (width/2 × height/2).
/// @param width     Frame width in pixels (must be even).
/// @param height    Frame height in pixels (must be even).
/// @param y_stride  Bytes per row in Y plane.
/// @param c_stride  Bytes per row in Cb/Cr planes.
/// @param rgba_out  Output array (width × height uint32_t, 0xRRGGBBAA).
void ycbcr420_to_rgba(const uint8_t *y_plane, const uint8_t *cb_plane,
                       const uint8_t *cr_plane, int32_t width, int32_t height,
                       int32_t y_stride, int32_t c_stride, uint32_t *rgba_out);

#ifdef __cplusplus
}
#endif
