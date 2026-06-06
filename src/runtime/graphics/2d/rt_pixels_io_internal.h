//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_pixels_io_internal.h
// Purpose: Shared includes, 64-bit file-seek macros, and size-overflow helpers
//   for the image codec translation units (rt_pixels_io.c BMP/GIF/dispatch,
//   rt_pixels_png.c, rt_pixels_jpeg.c).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_pixels.h"
#include "rt_pixels_internal.h"

#include "rt_bytes.h"
#include "rt_compress.h"
#include "rt_crc32.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Use 64-bit seek/tell to support files larger than 2 GB on Windows
// where `long` (and thus ftell/fseek) is only 32 bits even on 64-bit builds.
#if defined(_WIN32)
#define px_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define px_ftell(fp) _ftelli64((fp))
#else
#define px_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define px_ftell(fp) ftello((fp))
#endif

/// @brief Multiply two size_t values, returning 0 on overflow.
/// @details Writes the product to @p out only on success.  Used by the PNG

///   decoder to compute row-stride and total buffer sizes without wrapping.
static inline int px_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    if (out)
        *out = a * b;
    return 1;
}

/// @brief Add two size_t values, returning 0 on overflow.
/// @details Writes the sum to @p out only on success.  Companion to px_mul_size
///   for computing PNG row strides that involve both multiplication and addition.
static inline int px_add_size(size_t a, size_t b, size_t *out) {
    if (b > SIZE_MAX - a)
        return 0;
    if (out)
        *out = a + b;
    return 1;
}

/// @brief Compute the unfiltered byte stride for a PNG scanline, guarding overflow.
/// @details Handles both sub-8-bit depths (1/2/4 bpp — bits packed across bytes) and
///   >= 8-bit depths (bytes per sample * samples).  All intermediate multiplications use
///   px_mul_size so a pathological (width=UINT32_MAX, bpp=8) input cannot overflow size_t.
///   Returns 0 and does not write @p stride_out if any overflow would occur.
/// @param width             Scanline width in pixels.
/// @param bit_depth         PNG bit depth (1, 2, 4, 8, or 16).
/// @param samples_per_pixel Color channels per pixel (1=greyscale, 2=GA, 3=RGB, 4=RGBA).
/// @param stride_out        Output: number of unfiltered data bytes per row on success.
/// @return 1 on success, 0 on overflow or invalid inputs.
