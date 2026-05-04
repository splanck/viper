//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_pixels_transform.c
// Purpose: Geometric transforms and image processing effects for
//   Viper.Graphics.Pixels. Includes flips, rotations (90/180/arbitrary),
//   scaling, color inversion, grayscale conversion, tinting, Gaussian blur,
//   and high-quality bilinear resize.
//
// Key invariants:
//   - All transforms return a NEW Pixels object; the source is never modified.
//   - Arbitrary rotation uses bilinear interpolation for quality.
//   - Gaussian blur uses separable convolution (horizontal then vertical).
//   - Resize uses bilinear interpolation with proper subpixel addressing.
//   - Pixel format is 32-bit RGBA: 0xRRGGBBAA in row-major order.
//
// Ownership/Lifetime:
//   - Returned Pixels objects are GC-managed via pixels_alloc().
//   - Temporary buffers (blur kernel, etc.) are malloc/freed within each function.
//
// Links: src/runtime/graphics/rt_pixels_internal.h (shared struct),
//        src/runtime/graphics/rt_pixels.c (core operations),
//        src/runtime/graphics/rt_pixels.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_pixels.h"
#include "rt_pixels_internal.h"

#include "rt_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Image Transforms
//=============================================================================

/// @brief Clamp an int64_t to the [0, 255] range and return as uint8_t.
/// @details Used when converting premultiplied-alpha intermediate values back to
///   straight-alpha 8-bit channels after bilinear interpolation or blur passes.
static uint8_t pixels_clamp_u8_i64(int64_t value) {
    if (value <= 0)
        return 0;
    if (value >= 255)
        return 255;
    return (uint8_t)value;
}

/// @brief Pack premultiplied RGB channels and a straight alpha into a 0xRRGGBBAA pixel.
/// @details Unpremultiplies r/g/b by dividing by @p a (with rounding) before packing.
///   Returns 0 (transparent black) when @p a == 0 to avoid divide-by-zero and because
///   a fully transparent pixel should carry no colour information.
/// @param premul_r  Red channel already multiplied by alpha (pre-alpha value).
/// @param premul_g  Green channel already multiplied by alpha.
/// @param premul_b  Blue channel already multiplied by alpha.
/// @param a         Straight alpha value in [0, 255].
/// @return          Packed 0xRRGGBBAA pixel with straight-alpha channels.
static uint32_t pixels_pack_rgba_pm(int64_t premul_r,
                                    int64_t premul_g,
                                    int64_t premul_b,
                                    int64_t a) {
    uint8_t a8 = pixels_clamp_u8_i64(a);
    if (a8 == 0)
        return 0;

    int64_t r = (premul_r + a / 2) / a;
    int64_t g = (premul_g + a / 2) / a;
    int64_t b = (premul_b + a / 2) / a;
    return ((uint32_t)pixels_clamp_u8_i64(r) << 24) | ((uint32_t)pixels_clamp_u8_i64(g) << 16) |
           ((uint32_t)pixels_clamp_u8_i64(b) << 8) | (uint32_t)a8;
}

/// @brief Bilinear interpolation of four RGBA pixels in premultiplied-alpha space (fixed-point).
/// @details Weights are expressed as fixed-point fractions in [0, 256]; the
///   four bilinear weights sum to 256*256.  Interpolation is done in premultiplied-alpha
///   space to avoid dark-fringe artefacts at transparent boundaries, then the result is
///   unpremultiplied via pixels_pack_rgba_pm.
/// @param c00     Top-left pixel (0xRRGGBBAA).
/// @param c10     Top-right pixel.
/// @param c01     Bottom-left pixel.
/// @param c11     Bottom-right pixel.
/// @param frac_x  Fractional X in [0, 256] (256 = fully right).
/// @param frac_y  Fractional Y in [0, 256] (256 = fully bottom).
/// @return        Interpolated 0xRRGGBBAA pixel.
static uint32_t pixels_bilerp_rgba_premul_fixed(
    uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, int64_t frac_x, int64_t frac_y) {
    int64_t inv_frac_x = 256 - frac_x;
    int64_t inv_frac_y = 256 - frac_y;

    int64_t a00 = c00 & 0xFF;
    int64_t a10 = c10 & 0xFF;
    int64_t a01 = c01 & 0xFF;
    int64_t a11 = c11 & 0xFF;

    int64_t a = (a00 * inv_frac_x * inv_frac_y + a10 * frac_x * inv_frac_y +
                 a01 * inv_frac_x * frac_y + a11 * frac_x * frac_y) >>
                16;
    if (a <= 0)
        return 0;

    int64_t premul_r = ((((c00 >> 24) & 0xFF) * a00) * inv_frac_x * inv_frac_y +
                        (((c10 >> 24) & 0xFF) * a10) * frac_x * inv_frac_y +
                        (((c01 >> 24) & 0xFF) * a01) * inv_frac_x * frac_y +
                        (((c11 >> 24) & 0xFF) * a11) * frac_x * frac_y) >>
                       16;
    int64_t premul_g = ((((c00 >> 16) & 0xFF) * a00) * inv_frac_x * inv_frac_y +
                        (((c10 >> 16) & 0xFF) * a10) * frac_x * inv_frac_y +
                        (((c01 >> 16) & 0xFF) * a01) * inv_frac_x * frac_y +
                        (((c11 >> 16) & 0xFF) * a11) * frac_x * frac_y) >>
                       16;
    int64_t premul_b = ((((c00 >> 8) & 0xFF) * a00) * inv_frac_x * inv_frac_y +
                        (((c10 >> 8) & 0xFF) * a10) * frac_x * inv_frac_y +
                        (((c01 >> 8) & 0xFF) * a01) * inv_frac_x * frac_y +
                        (((c11 >> 8) & 0xFF) * a11) * frac_x * frac_y) >>
                       16;

    return pixels_pack_rgba_pm(premul_r, premul_g, premul_b, a);
}

/// @brief Bilinear interpolation of four RGBA pixels in premultiplied-alpha space (floating-point).
/// @details Double-precision variant of pixels_bilerp_rgba_premul_fixed, used for the
///   arbitrary-angle rotation pass where the source coordinates are already expressed in
///   double-precision floats.  Higher precision than the fixed-point variant at the cost of
///   slightly slower arithmetic.
/// @param c00  Top-left pixel (0xRRGGBBAA).
/// @param c10  Top-right pixel.
/// @param c01  Bottom-left pixel.
/// @param c11  Bottom-right pixel.
/// @param fx   Fractional X in [0.0, 1.0).
/// @param fy   Fractional Y in [0.0, 1.0).
/// @return     Interpolated 0xRRGGBBAA pixel.
static uint32_t pixels_bilerp_rgba_premul_double(
    uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, double fx, double fy) {
    double wx0 = 1.0 - fx;
    double wy0 = 1.0 - fy;
    double w00 = wx0 * wy0;
    double w10 = fx * wy0;
    double w01 = wx0 * fy;
    double w11 = fx * fy;

    double a00 = (double)(c00 & 0xFF);
    double a10 = (double)(c10 & 0xFF);
    double a01 = (double)(c01 & 0xFF);
    double a11 = (double)(c11 & 0xFF);

    double a = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
    if (a <= 0.0)
        return 0;

    double premul_r =
        (double)((c00 >> 24) & 0xFF) * a00 * w00 + (double)((c10 >> 24) & 0xFF) * a10 * w10 +
        (double)((c01 >> 24) & 0xFF) * a01 * w01 + (double)((c11 >> 24) & 0xFF) * a11 * w11;
    double premul_g =
        (double)((c00 >> 16) & 0xFF) * a00 * w00 + (double)((c10 >> 16) & 0xFF) * a10 * w10 +
        (double)((c01 >> 16) & 0xFF) * a01 * w01 + (double)((c11 >> 16) & 0xFF) * a11 * w11;
    double premul_b =
        (double)((c00 >> 8) & 0xFF) * a00 * w00 + (double)((c10 >> 8) & 0xFF) * a10 * w10 +
        (double)((c01 >> 8) & 0xFF) * a01 * w01 + (double)((c11 >> 8) & 0xFF) * a11 * w11;

    return pixels_pack_rgba_pm((int64_t)(premul_r + 0.5),
                               (int64_t)(premul_g + 0.5),
                               (int64_t)(premul_b + 0.5),
                               (int64_t)(a + 0.5));
}

/// @brief Average @p count premultiplied-alpha samples and return as a 0xRRGGBBAA pixel.
/// @details Used by the separable box blur to combine the running sums accumulated over
///   the kernel window.  Rounds all channels using count/2 before dividing to reduce
///   systematic darkening from truncation.  Returns transparent black when count == 0
///   or the alpha average rounds to zero.
/// @param sum_premul_r  Sum of (R * A) values across the kernel window.
/// @param sum_premul_g  Sum of (G * A) values.
/// @param sum_premul_b  Sum of (B * A) values.
/// @param sum_a         Sum of alpha values.
/// @param count         Number of samples summed; must be > 0.
/// @return              Averaged 0xRRGGBBAA pixel.
static uint32_t pixels_average_rgba_premul(int64_t sum_premul_r,
                                           int64_t sum_premul_g,
                                           int64_t sum_premul_b,
                                           int64_t sum_a,
                                           int64_t count) {
    if (count <= 0)
        return 0;

    int64_t a = (sum_a + count / 2) / count;
    if (a <= 0)
        return 0;

    int64_t premul_r = (sum_premul_r + count / 2) / count;
    int64_t premul_g = (sum_premul_g + count / 2) / count;
    int64_t premul_b = (sum_premul_b + count / 2) / count;
    return pixels_pack_rgba_pm(premul_r, premul_g, premul_b, a);
}

/// @brief Map a destination pixel index to the corresponding nearest-neighbor source index.
/// @details Computes floor(dst * (src_size - 1) / (dst_size - 1)), clamped to
///   [0, src_size - 1], so the first and last destination pixels exactly sample
///   the first and last source pixels.
///   Used by the nearest-neighbor scale pass (rt_pixels_scale).  Long double arithmetic
///   avoids integer rounding errors on large images.
/// @param dst       Destination pixel coordinate (0-based).
/// @param src_size  Source dimension (width or height).
/// @param dst_size  Destination dimension.
/// @return          Source coordinate in [0, src_size - 1].
static int64_t pixels_map_index(int64_t dst, int64_t src_size, int64_t dst_size) {
    if (src_size <= 1 || dst_size <= 1)
        return 0;
    long double mapped =
        ((long double)dst * (long double)(src_size - 1)) / (long double)(dst_size - 1);
    if (mapped >= (long double)(src_size - 1))
        return src_size - 1;
    if (mapped <= 0.0L)
        return 0;
    return (int64_t)(mapped + 0.5L);
}

/// @brief Map a destination pixel index to a 8.8 fixed-point source position for bilinear resize.
/// @details Returns dst * (src_size - 1) * 256 / (dst_size - 1), i.e. the source coordinate expressed in
///   units of 1/256 of a pixel.  The caller shifts right by 8 to get the integer part and masks
///   with 0xFF to get the fractional weight for pixels_bilerp_rgba_premul_fixed.
/// @param dst       Destination pixel coordinate (0-based).
/// @param src_size  Source dimension.
/// @param dst_size  Destination dimension.
/// @return          256× magnified source coordinate, clamped to [0, INT64_MAX].
static int64_t pixels_map_fixed_256(int64_t dst, int64_t src_size, int64_t dst_size) {
    if (src_size <= 1 || dst_size <= 1)
        return 0;
    long double mapped = ((long double)dst * (long double)(src_size - 1) * 256.0L) /
                         (long double)(dst_size - 1);
    if (mapped >= (long double)INT64_MAX)
        return INT64_MAX;
    if (mapped <= 0.0L)
        return 0;
    return (int64_t)mapped;
}

static int8_t pixels_long_double_extent_to_i64(long double extent, int64_t *out) {
    if (!out || !isfinite((double)extent) || extent < 0.0L)
        return 0;

    long double rounded = ceill(extent);
    if (rounded < 1.0L)
        rounded = 1.0L;
    if (rounded > (long double)INT64_MAX)
        return 0;

    *out = (int64_t)rounded;
    return 1;
}

/// @brief Mirror the pixel buffer horizontally (left↔right). Returns a new Pixels.
void *rt_pixels_flip_h(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.FlipH: null pixels");
    if (!p)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    for (int64_t y = 0; y < p->height; y++) {
        for (int64_t x = 0; x < p->width; x++) {
            result->data[y * p->width + (p->width - 1 - x)] = p->data[y * p->width + x];
        }
    }

    return result;
}

/// @brief Mirror the pixel buffer vertically (top↔bottom). Returns a new Pixels.
void *rt_pixels_flip_v(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.FlipV: null pixels");
    if (!p)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    for (int64_t y = 0; y < p->height; y++) {
        memcpy(&result->data[(p->height - 1 - y) * p->width],
               &p->data[y * p->width],
               (size_t)p->width * sizeof(uint32_t));
    }

    return result;
}

/// @brief Rotate 90° clockwise. Returns a NEW Pixels (dimensions swap: w×h → h×w).
void *rt_pixels_rotate_cw(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.RotateCW: null pixels");
    if (!p)
        return NULL;

    // New dimensions: width becomes height, height becomes width
    int64_t new_width = p->height;
    int64_t new_height = p->width;

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Rotate 90 CW: src[x,y] -> dst[height-1-y, x]
    // In terms of new coords: dst[x',y'] = src[y', width-1-x']
    for (int64_t y = 0; y < p->height; y++) {
        for (int64_t x = 0; x < p->width; x++) {
            uint32_t pixel = p->data[y * p->width + x];
            // New position: (height-1-y, x) in new coordinate system
            int64_t new_x = p->height - 1 - y;
            int64_t new_y = x;
            result->data[new_y * new_width + new_x] = pixel;
        }
    }

    return result;
}

/// @brief Rotate 90° counter-clockwise. Returns a NEW Pixels (dimensions swap).
void *rt_pixels_rotate_ccw(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.RotateCCW: null pixels");
    if (!p)
        return NULL;

    // New dimensions: width becomes height, height becomes width
    int64_t new_width = p->height;
    int64_t new_height = p->width;

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Rotate 90 CCW: src[x,y] -> dst[y, width-1-x]
    for (int64_t y = 0; y < p->height; y++) {
        for (int64_t x = 0; x < p->width; x++) {
            uint32_t pixel = p->data[y * p->width + x];
            // New position: (y, width-1-x) in new coordinate system
            int64_t new_x = y;
            int64_t new_y = p->width - 1 - x;
            result->data[new_y * new_width + new_x] = pixel;
        }
    }

    return result;
}

/// @brief Rotate 180° (equivalent to flip-h then flip-v). Returns a NEW Pixels.
void *rt_pixels_rotate_180(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Rotate180: null pixels");
    if (!p)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    // Rotate 180: src[x,y] -> dst[width-1-x, height-1-y]
    int64_t total = p->width * p->height;
    for (int64_t i = 0; i < total; i++) {
        result->data[total - 1 - i] = p->data[i];
    }

    return result;
}

/// @brief Rotate by an arbitrary `angle_degrees` (positive = clockwise). Output Pixels
/// is sized to fit the rotated rectangle; corners outside become transparent. Bilinear sampling
/// for smooth interpolation. Returns a NEW Pixels.
void *rt_pixels_rotate(void *pixels, double angle_degrees) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Rotate: null pixels");
    if (!p)
        return NULL;

    if (p->width <= 0 || p->height <= 0)
        return pixels_alloc(0, 0);

    if (!isfinite(angle_degrees))
        return rt_pixels_clone(pixels);

    // Normalize angle to [0, 360) without unbounded loops for huge inputs.
    angle_degrees = fmod(angle_degrees, 360.0);
    if (angle_degrees < 0.0)
        angle_degrees += 360.0;

    // Fast paths for common angles
    if (fabs(angle_degrees) < 0.001 || fabs(angle_degrees - 360.0) < 0.001) {
        // No rotation - return a copy
        rt_pixels_impl *result = pixels_alloc(p->width, p->height);
        if (!result)
            return NULL;
        memcpy(result->data, p->data, (size_t)(p->width * p->height) * sizeof(uint32_t));
        return result;
    }
    if (fabs(angle_degrees - 90.0) < 0.001)
        return rt_pixels_rotate_cw(pixels);
    if (fabs(angle_degrees - 180.0) < 0.001)
        return rt_pixels_rotate_180(pixels);
    if (fabs(angle_degrees - 270.0) < 0.001)
        return rt_pixels_rotate_ccw(pixels);

    // Convert to radians
    double rad = angle_degrees * (3.14159265358979323846 / 180.0);
    double cos_a = cos(rad);
    double sin_a = sin(rad);

    // Calculate new bounding box dimensions
    // The four corners of the original image rotated about center
    double hw = p->width / 2.0;
    double hh = p->height / 2.0;

    // Rotated corner positions (relative to center)
    double corners[4][2] = {
        {-hw * cos_a + hh * sin_a, -hw * sin_a - hh * cos_a}, // top-left
        {hw * cos_a + hh * sin_a, hw * sin_a - hh * cos_a},   // top-right
        {hw * cos_a - hh * sin_a, hw * sin_a + hh * cos_a},   // bottom-right
        {-hw * cos_a - hh * sin_a, -hw * sin_a + hh * cos_a}  // bottom-left
    };

    double min_x = corners[0][0], max_x = corners[0][0];
    double min_y = corners[0][1], max_y = corners[0][1];
    for (int i = 1; i < 4; i++) {
        if (corners[i][0] < min_x)
            min_x = corners[i][0];
        if (corners[i][0] > max_x)
            max_x = corners[i][0];
        if (corners[i][1] < min_y)
            min_y = corners[i][1];
        if (corners[i][1] > max_y)
            max_y = corners[i][1];
    }

    int64_t new_width = 0;
    int64_t new_height = 0;
    if (!pixels_long_double_extent_to_i64((long double)max_x - (long double)min_x,
                                          &new_width) ||
        !pixels_long_double_extent_to_i64((long double)max_y - (long double)min_y,
                                          &new_height)) {
        rt_trap("Pixels.Rotate: dimensions too large");
        return NULL;
    }

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // New center
    double new_hw = new_width / 2.0;
    double new_hh = new_height / 2.0;

    // For each destination pixel, find source pixel using inverse rotation
    for (int64_t dy = 0; dy < new_height; dy++) {
        for (int64_t dx = 0; dx < new_width; dx++) {
            // Destination position relative to new center
            double dx_c = dx - new_hw;
            double dy_c = dy - new_hh;

            // Inverse rotation to find source position
            double sx_c = dx_c * cos_a + dy_c * sin_a;
            double sy_c = -dx_c * sin_a + dy_c * cos_a;

            // Source position in original image coordinates
            double sx = sx_c + hw;
            double sy = sy_c + hh;

            // Bilinear interpolation
            int64_t x0 = (int64_t)floor(sx);
            int64_t y0 = (int64_t)floor(sy);
            int64_t x1 = x0 + 1;
            int64_t y1 = y0 + 1;

            // Skip if completely outside source
            if (x1 < 0 || x0 >= p->width || y1 < 0 || y0 >= p->height)
                continue;

            // Fractional parts
            double fx = sx - x0;
            double fy = sy - y0;

            // Get the four surrounding pixels (with bounds checking)
            uint32_t c00 = 0, c10 = 0, c01 = 0, c11 = 0;

            if (x0 >= 0 && x0 < p->width && y0 >= 0 && y0 < p->height)
                c00 = p->data[y0 * p->width + x0];
            if (x1 >= 0 && x1 < p->width && y0 >= 0 && y0 < p->height)
                c10 = p->data[y0 * p->width + x1];
            if (x0 >= 0 && x0 < p->width && y1 >= 0 && y1 < p->height)
                c01 = p->data[y1 * p->width + x0];
            if (x1 >= 0 && x1 < p->width && y1 >= 0 && y1 < p->height)
                c11 = p->data[y1 * p->width + x1];

            result->data[dy * new_width + dx] =
                pixels_bilerp_rgba_premul_double(c00, c10, c01, c11, fx, fy);
        }
    }

    return result;
}

/// @brief Resize via nearest-neighbor sampling to (`new_width`, `new_height`). Returns a NEW Pixels.
/// Use `_resize` for bilinear filtering.
void *rt_pixels_scale(void *pixels, int64_t new_width, int64_t new_height) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Scale: null pixels");
    if (!p)
        return NULL;

    if (new_width <= 0)
        new_width = 1;
    if (new_height <= 0)
        new_height = 1;

    // Handle empty source
    if (p->width <= 0 || p->height <= 0) {
        return pixels_alloc(new_width, new_height);
    }

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Nearest-neighbor scaling
    for (int64_t y = 0; y < new_height; y++) {
        // Map destination y to source y
        int64_t src_y = pixels_map_index(y, p->height, new_height);

        uint32_t *src_row = p->data + src_y * p->width;
        uint32_t *dst_row = result->data + y * new_width;

        for (int64_t x = 0; x < new_width; x++) {
            // Map destination x to source x
            int64_t src_x = pixels_map_index(x, p->width, new_width);

            dst_row[x] = src_row[src_x];
        }
    }

    return result;
}

//=============================================================================
// Image Processing
//=============================================================================

/// @brief Negate every RGB channel (255 - r, 255 - g, 255 - b). Alpha preserved.
/// Returns a NEW Pixels.
void *rt_pixels_invert(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Invert: null pixels");
    if (!p)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++) {
        uint32_t px = p->data[i];
        // Format is 0xRRGGBBAA - invert RGB, keep alpha
        uint8_t r = (px >> 24) & 0xFF;
        uint8_t g = (px >> 16) & 0xFF;
        uint8_t b = (px >> 8) & 0xFF;
        uint8_t a = px & 0xFF;
        result->data[i] = ((uint32_t)(255 - r) << 24) | ((uint32_t)(255 - g) << 16) |
                          ((uint32_t)(255 - b) << 8) | a;
    }

    return result;
}

/// @brief Convert to grayscale using ITU-R BT.601 luma weights (0.299 R + 0.587 G + 0.114 B).
/// Returns a NEW Pixels with the luma value replicated to R/G/B; alpha preserved.
void *rt_pixels_grayscale(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Grayscale: null pixels");
    if (!p)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++) {
        uint32_t px = p->data[i];
        // Format is 0xRRGGBBAA
        uint8_t r = (px >> 24) & 0xFF;
        uint8_t g = (px >> 16) & 0xFF;
        uint8_t b = (px >> 8) & 0xFF;
        uint8_t a = px & 0xFF;

        // Standard grayscale formula: 0.299*R + 0.587*G + 0.114*B
        uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
        result->data[i] =
            ((uint32_t)gray << 24) | ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | a;
    }

    return result;
}

/// @brief Multiply each pixel by `color` (per-channel modulation, 8-bit normalized). Useful for
/// hue shifts, color-coded variants. Alpha is multiplied when `color` carries an alpha channel.
/// Returns a NEW Pixels.
void *rt_pixels_tint(void *pixels, int64_t color) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Tint: null pixels");
    if (!p)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t tr = 0;
    int64_t tg = 0;
    int64_t tb = 0;
    int64_t ta = 255;
    uint64_t c = (uint64_t)color;
    if ((c & (uint64_t)RT_PIXELS_COLOR_EXPLICIT_ALPHA_FLAG) != 0 || c > 0x00FFFFFFu) {
        uint32_t tint_rgba = rt_pixels_rgba_or_tagged_color_to_rgba(color);
        tr = (tint_rgba >> 24) & 0xFF;
        tg = (tint_rgba >> 16) & 0xFF;
        tb = (tint_rgba >> 8) & 0xFF;
        ta = tint_rgba & 0xFF;
    } else {
        tr = (color >> 16) & 0xFF;
        tg = (color >> 8) & 0xFF;
        tb = color & 0xFF;
    }

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++) {
        uint32_t px = p->data[i];
        // Format is 0xRRGGBBAA
        int64_t r = (px >> 24) & 0xFF;
        int64_t g = (px >> 16) & 0xFF;
        int64_t b = (px >> 8) & 0xFF;
        int64_t a = px & 0xFF;

        // Multiply blend: result = (pixel * tint) / 255
        r = (r * tr) / 255;
        g = (g * tg) / 255;
        b = (b * tb) / 255;
        a = (a * ta) / 255;

        result->data[i] = ((uint32_t)(r & 0xFF) << 24) | ((uint32_t)(g & 0xFF) << 16) |
                          ((uint32_t)(b & 0xFF) << 8) | (uint32_t)(a & 0xFF);
    }

    return result;
}

/// @brief Box blur with a (2*radius+1)-wide kernel applied separately on X then Y for
/// O(n*radius) cost. Larger radius = softer image; radius 0 returns a copy. Returns a NEW Pixels.
void *rt_pixels_blur(void *pixels, int64_t radius) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Blur: null pixels");
    if (!p)
        return NULL;

    if (radius <= 0)
        return rt_pixels_clone(pixels);
    if (radius > 10)
        radius = 10;

    int64_t w = p->width;
    int64_t h = p->height;
    if (w <= 0 || h <= 0)
        return pixels_alloc(w, h);
    if (w > INT64_MAX / h || (uint64_t)(w * h) > SIZE_MAX / sizeof(uint32_t)) {
        rt_trap("Pixels.Blur: dimensions too large");
        return NULL;
    }

    // Separable box blur: horizontal pass → temp, then vertical pass → result.
    // Reduces O(w×h×(2r+1)²) to O(w×h×(2r+1)×2).  Format: 0xRRGGBBAA.
    uint32_t *tmp = (uint32_t *)malloc((size_t)(w * h) * sizeof(uint32_t));
    if (!tmp)
        return NULL;

    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result) {
        free(tmp);
        return NULL;
    }

    // Horizontal pass: blur each row independently into tmp
    for (int64_t y = 0; y < h; y++) {
        for (int64_t x = 0; x < w; x++) {
            int64_t sum_premul_r = 0, sum_premul_g = 0, sum_premul_b = 0, sum_a = 0;
            int64_t count = 0;
            for (int64_t kdx = -radius; kdx <= radius; kdx++) {
                int64_t sx = x + kdx;
                if (sx >= 0 && sx < w) {
                    uint32_t pixel = p->data[y * w + sx];
                    int64_t a = pixel & 0xFF;
                    sum_premul_r += ((pixel >> 24) & 0xFF) * a;
                    sum_premul_g += ((pixel >> 16) & 0xFF) * a;
                    sum_premul_b += ((pixel >> 8) & 0xFF) * a;
                    sum_a += a;
                    count++;
                }
            }
            if (count > 0)
                tmp[y * w + x] = pixels_average_rgba_premul(
                    sum_premul_r, sum_premul_g, sum_premul_b, sum_a, count);
        }
    }

    // Vertical pass: blur each column from tmp into result (y-outer for cache locality)
    for (int64_t y = 0; y < h; y++) {
        for (int64_t x = 0; x < w; x++) {
            int64_t sum_premul_r = 0, sum_premul_g = 0, sum_premul_b = 0, sum_a = 0;
            int64_t count = 0;
            for (int64_t kdy = -radius; kdy <= radius; kdy++) {
                int64_t sy = y + kdy;
                if (sy >= 0 && sy < h) {
                    uint32_t pixel = tmp[sy * w + x];
                    int64_t a = pixel & 0xFF;
                    sum_premul_r += ((pixel >> 24) & 0xFF) * a;
                    sum_premul_g += ((pixel >> 16) & 0xFF) * a;
                    sum_premul_b += ((pixel >> 8) & 0xFF) * a;
                    sum_a += a;
                    count++;
                }
            }
            if (count > 0)
                result->data[y * w + x] = pixels_average_rgba_premul(
                    sum_premul_r, sum_premul_g, sum_premul_b, sum_a, count);
        }
    }

    free(tmp);
    return result;
}

/// @brief Resize via bilinear filtering. Use `_scale` for nearest-neighbor sampling when
/// preserving hard pixel edges. Returns a NEW Pixels.
void *rt_pixels_resize(void *pixels, int64_t new_width, int64_t new_height) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Pixels.Resize: null pixels");
    if (!p)
        return NULL;

    if (new_width <= 0)
        new_width = 1;
    if (new_height <= 0)
        new_height = 1;

    // Handle empty source
    if (p->width <= 0 || p->height <= 0) {
        return pixels_alloc(new_width, new_height);
    }

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Bilinear interpolation scaling
    for (int64_t y = 0; y < new_height; y++) {
        // Map destination y to source y (with fractional part)
        int64_t src_y_256 = pixels_map_fixed_256(y, p->height, new_height);
        int64_t src_y = src_y_256 >> 8;
        int64_t frac_y = src_y_256 & 0xFF;

        if (src_y >= p->height)
            src_y = p->height - 1;
        if (src_y < 0)
            src_y = 0;
        int64_t sy1 = (src_y + 1 < p->height) ? src_y + 1 : src_y;
        if (src_y >= p->height - 1)
            frac_y = 255;

        for (int64_t x = 0; x < new_width; x++) {
            // Map destination x to source x (with fractional part)
            int64_t src_x_256 = pixels_map_fixed_256(x, p->width, new_width);
            int64_t src_x = src_x_256 >> 8;
            int64_t frac_x = src_x_256 & 0xFF;

            if (src_x >= p->width)
                src_x = p->width - 1;
            if (src_x < 0)
                src_x = 0;
            int64_t sx1 = (src_x + 1 < p->width) ? src_x + 1 : src_x;
            if (src_x >= p->width - 1)
                frac_x = 255;

            // Get four neighboring pixels
            uint32_t p00 = p->data[src_y * p->width + src_x];
            uint32_t p10 = p->data[src_y * p->width + sx1];
            uint32_t p01 = p->data[sy1 * p->width + src_x];
            uint32_t p11 = p->data[sy1 * p->width + sx1];

            result->data[y * new_width + x] =
                pixels_bilerp_rgba_premul_fixed(p00, p10, p01, p11, frac_x, frac_y);
        }
    }

    return result;
}
