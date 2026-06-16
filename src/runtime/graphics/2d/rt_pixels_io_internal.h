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
#include "rt_file_stdio.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Use the shared UTF-8 stdio wrapper's 64-bit seek/tell helpers so image codecs
// do not carry their own platform-specific branches.
#define px_fseek(fp, off, whence) rt_file_stdio_seek64((fp), (off), (whence))
#define px_ftell(fp) rt_file_stdio_tell64((fp))

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
