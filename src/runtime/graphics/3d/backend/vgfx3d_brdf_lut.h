//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_brdf_lut.h
// Purpose: Shared split-sum environment-BRDF lookup table for image-based
//   lighting. One deterministic CPU precomputation feeds every backend: the
//   GPU backends upload the table as a small RG texture, the software
//   rasterizer samples it directly.
// Key invariants:
//   - The table is a pure function of (NdotV, roughness), built from a fixed
//     Hammersley sample sequence — bit-stable per platform, VM == native.
//   - vgfx3d_brdf_lut_ensure() must be called from a single-threaded seam
//     (backend init / frame begin) before any concurrent sampling.
// Ownership/Lifetime:
//   - The table is process-lifetime static storage; callers never free it.
// Links: vgfx3d_backend_sw_raster.inc, vgfx3d_backend_metal.m,
//   vgfx3d_backend_opengl.c, vgfx3d_backend_d3d11.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Table edge size (NdotV on X, roughness on Y), two floats per texel.
#define VGFX3D_BRDF_LUT_SIZE 64

/// @brief Build the table if it has not been built yet. Not thread-safe by
///   itself: call from backend init or frame begin before parallel sampling.
void vgfx3d_brdf_lut_ensure(void);

/// @brief Borrow the table data: VGFX3D_BRDF_LUT_SIZE^2 texels of (A, B) float
///   pairs, row-major with NdotV along X and roughness along Y. Builds the
///   table on first use.
const float *vgfx3d_brdf_lut_data(void);

/// @brief Bilinear CPU sample of the table; writes scale (A) and bias (B) for
///   the split-sum specular term F0 * A + B.
void vgfx3d_brdf_lut_sample(float ndotv, float roughness, float *out_ab);

#ifdef __cplusplus
}
#endif
