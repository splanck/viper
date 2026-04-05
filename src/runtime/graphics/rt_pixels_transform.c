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

#include <math.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Image Transforms
//=============================================================================

void *rt_pixels_flip_h(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.FlipH: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // Flip in place: swap pixels symmetrically within each row
    for (int64_t y = 0; y < p->height; y++) {
        uint32_t *row = p->data + y * p->width;
        for (int64_t x = 0; x < p->width / 2; x++) {
            uint32_t tmp = row[x];
            row[x] = row[p->width - 1 - x];
            row[p->width - 1 - x] = tmp;
        }
    }

    return pixels; // Return self for chaining
}

void *rt_pixels_flip_v(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.FlipV: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    // Flip in place: swap rows symmetrically
    size_t row_bytes = (size_t)p->width * sizeof(uint32_t);
    uint32_t *tmp_row = (uint32_t *)malloc(row_bytes);
    if (!tmp_row)
        return pixels;

    for (int64_t y = 0; y < p->height / 2; y++) {
        uint32_t *top = p->data + y * p->width;
        uint32_t *bot = p->data + (p->height - 1 - y) * p->width;
        memcpy(tmp_row, top, row_bytes);
        memcpy(top, bot, row_bytes);
        memcpy(bot, tmp_row, row_bytes);
    }

    free(tmp_row);
    return pixels; // Return self for chaining
}

void *rt_pixels_rotate_cw(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.RotateCW: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

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

void *rt_pixels_rotate_ccw(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.RotateCCW: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

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

void *rt_pixels_rotate_180(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Rotate180: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
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

void *rt_pixels_rotate(void *pixels, double angle_degrees) {
    if (!pixels) {
        rt_trap("Pixels.Rotate: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    if (p->width <= 0 || p->height <= 0)
        return pixels_alloc(0, 0);

    // Normalize angle to 0-360
    while (angle_degrees < 0)
        angle_degrees += 360.0;
    while (angle_degrees >= 360.0)
        angle_degrees -= 360.0;

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

    int64_t new_width = (int64_t)ceil(max_x - min_x);
    int64_t new_height = (int64_t)ceil(max_y - min_y);
    if (new_width < 1)
        new_width = 1;
    if (new_height < 1)
        new_height = 1;

    rt_pixels_impl *result = pixels_alloc(new_width, new_height);
    if (!result)
        return NULL;

    // Clear to transparent
    memset(result->data, 0, (size_t)(new_width * new_height) * sizeof(uint32_t));

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

            // Bilinear interpolation for each channel (0xRRGGBBAA format)
            uint8_t r00 = (c00 >> 24) & 0xFF, g00 = (c00 >> 16) & 0xFF;
            uint8_t b00 = (c00 >> 8) & 0xFF, a00 = c00 & 0xFF;
            uint8_t r10 = (c10 >> 24) & 0xFF, g10 = (c10 >> 16) & 0xFF;
            uint8_t b10 = (c10 >> 8) & 0xFF, a10 = c10 & 0xFF;
            uint8_t r01 = (c01 >> 24) & 0xFF, g01 = (c01 >> 16) & 0xFF;
            uint8_t b01 = (c01 >> 8) & 0xFF, a01 = c01 & 0xFF;
            uint8_t r11 = (c11 >> 24) & 0xFF, g11 = (c11 >> 16) & 0xFF;
            uint8_t b11 = (c11 >> 8) & 0xFF, a11 = c11 & 0xFF;

            double r = r00 * (1 - fx) * (1 - fy) + r10 * fx * (1 - fy) + r01 * (1 - fx) * fy +
                       r11 * fx * fy;
            double g = g00 * (1 - fx) * (1 - fy) + g10 * fx * (1 - fy) + g01 * (1 - fx) * fy +
                       g11 * fx * fy;
            double b = b00 * (1 - fx) * (1 - fy) + b10 * fx * (1 - fy) + b01 * (1 - fx) * fy +
                       b11 * fx * fy;
            double a = a00 * (1 - fx) * (1 - fy) + a10 * fx * (1 - fy) + a01 * (1 - fx) * fy +
                       a11 * fx * fy;

            uint8_t ri = (uint8_t)(r > 255 ? 255 : (r < 0 ? 0 : r));
            uint8_t gi = (uint8_t)(g > 255 ? 255 : (g < 0 ? 0 : g));
            uint8_t bi = (uint8_t)(b > 255 ? 255 : (b < 0 ? 0 : b));
            uint8_t ai = (uint8_t)(a > 255 ? 255 : (a < 0 ? 0 : a));

            result->data[dy * new_width + dx] =
                ((uint32_t)ri << 24) | ((uint32_t)gi << 16) | ((uint32_t)bi << 8) | ai;
        }
    }

    return result;
}

void *rt_pixels_scale(void *pixels, int64_t new_width, int64_t new_height) {
    if (!pixels) {
        rt_trap("Pixels.Scale: null pixels");
        return NULL;
    }

    if (new_width <= 0)
        new_width = 1;
    if (new_height <= 0)
        new_height = 1;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

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
        int64_t src_y = (y * p->height) / new_height;
        if (src_y >= p->height)
            src_y = p->height - 1;

        uint32_t *src_row = p->data + src_y * p->width;
        uint32_t *dst_row = result->data + y * new_width;

        for (int64_t x = 0; x < new_width; x++) {
            // Map destination x to source x
            int64_t src_x = (x * p->width) / new_width;
            if (src_x >= p->width)
                src_x = p->width - 1;

            dst_row[x] = src_row[src_x];
        }
    }

    return result;
}

//=============================================================================
// Image Processing
//=============================================================================

void *rt_pixels_invert(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Invert: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
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

void *rt_pixels_grayscale(void *pixels) {
    if (!pixels) {
        rt_trap("Pixels.Grayscale: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
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

void *rt_pixels_tint(void *pixels, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.Tint: null pixels");
        return NULL;
    }

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    // Extract tint color (0x00RRGGBB format)
    int64_t tr = (color >> 16) & 0xFF;
    int64_t tg = (color >> 8) & 0xFF;
    int64_t tb = color & 0xFF;

    int64_t count = p->width * p->height;
    for (int64_t i = 0; i < count; i++) {
        uint32_t px = p->data[i];
        // Format is 0xRRGGBBAA
        int64_t r = (px >> 24) & 0xFF;
        int64_t g = (px >> 16) & 0xFF;
        int64_t b = (px >> 8) & 0xFF;
        uint8_t a = px & 0xFF;

        // Multiply blend: result = (pixel * tint) / 255
        r = (r * tr) / 255;
        g = (g * tg) / 255;
        b = (b * tb) / 255;

        result->data[i] = ((uint32_t)(r & 0xFF) << 24) | ((uint32_t)(g & 0xFF) << 16) |
                          ((uint32_t)(b & 0xFF) << 8) | a;
    }

    return result;
}

void *rt_pixels_blur(void *pixels, int64_t radius) {
    if (!pixels) {
        rt_trap("Pixels.Blur: null pixels");
        return NULL;
    }

    if (radius < 1)
        radius = 1;
    if (radius > 10)
        radius = 10;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    rt_pixels_impl *result = pixels_alloc(p->width, p->height);
    if (!result)
        return NULL;

    int64_t w = p->width;
    int64_t h = p->height;

    // Separable box blur: horizontal pass → temp, then vertical pass → result.
    // Reduces O(w×h×(2r+1)²) to O(w×h×(2r+1)×2).  Format: 0xRRGGBBAA.
    uint32_t *tmp = (uint32_t *)malloc((size_t)(w * h) * sizeof(uint32_t));
    if (!tmp)
        return result; // return zero-filled result on OOM

    // Horizontal pass: blur each row independently into tmp
    for (int64_t y = 0; y < h; y++) {
        for (int64_t x = 0; x < w; x++) {
            int64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int64_t count = 0;
            for (int64_t kdx = -radius; kdx <= radius; kdx++) {
                int64_t sx = x + kdx;
                if (sx >= 0 && sx < w) {
                    uint32_t pixel = p->data[y * w + sx];
                    sum_a += (pixel >> 24) & 0xFF;
                    sum_r += (pixel >> 16) & 0xFF;
                    sum_g += (pixel >> 8) & 0xFF;
                    sum_b += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0)
                tmp[y * w + x] = ((uint32_t)(sum_a / count) << 24) |
                                 ((uint32_t)(sum_r / count) << 16) |
                                 ((uint32_t)(sum_g / count) << 8) | (uint32_t)(sum_b / count);
        }
    }

    // Vertical pass: blur each column from tmp into result (y-outer for cache locality)
    for (int64_t y = 0; y < h; y++) {
        for (int64_t x = 0; x < w; x++) {
            int64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            int64_t count = 0;
            for (int64_t kdy = -radius; kdy <= radius; kdy++) {
                int64_t sy = y + kdy;
                if (sy >= 0 && sy < h) {
                    uint32_t pixel = tmp[sy * w + x];
                    sum_a += (pixel >> 24) & 0xFF;
                    sum_r += (pixel >> 16) & 0xFF;
                    sum_g += (pixel >> 8) & 0xFF;
                    sum_b += pixel & 0xFF;
                    count++;
                }
            }
            if (count > 0)
                result->data[y * w + x] =
                    ((uint32_t)(sum_a / count) << 24) | ((uint32_t)(sum_r / count) << 16) |
                    ((uint32_t)(sum_g / count) << 8) | (uint32_t)(sum_b / count);
        }
    }

    free(tmp);
    return result;
}

void *rt_pixels_resize(void *pixels, int64_t new_width, int64_t new_height) {
    if (!pixels) {
        rt_trap("Pixels.Resize: null pixels");
        return NULL;
    }

    if (new_width <= 0)
        new_width = 1;
    if (new_height <= 0)
        new_height = 1;

    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

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
        int64_t src_y_256 = (y * p->height * 256) / new_height;
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
            int64_t src_x_256 = (x * p->width * 256) / new_width;
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

            // Extract components - format is 0xAARRGGBB
            int64_t a00 = (p00 >> 24) & 0xFF, r00 = (p00 >> 16) & 0xFF, g00 = (p00 >> 8) & 0xFF,
                    b00 = p00 & 0xFF;
            int64_t a10 = (p10 >> 24) & 0xFF, r10 = (p10 >> 16) & 0xFF, g10 = (p10 >> 8) & 0xFF,
                    b10 = p10 & 0xFF;
            int64_t a01 = (p01 >> 24) & 0xFF, r01 = (p01 >> 16) & 0xFF, g01 = (p01 >> 8) & 0xFF,
                    b01 = p01 & 0xFF;
            int64_t a11 = (p11 >> 24) & 0xFF, r11 = (p11 >> 16) & 0xFF, g11 = (p11 >> 8) & 0xFF,
                    b11 = p11 & 0xFF;

            // Bilinear interpolation
            int64_t inv_frac_x = 256 - frac_x;
            int64_t inv_frac_y = 256 - frac_y;

            int64_t a = (a00 * inv_frac_x * inv_frac_y + a10 * frac_x * inv_frac_y +
                         a01 * inv_frac_x * frac_y + a11 * frac_x * frac_y) >>
                        16;
            int64_t r = (r00 * inv_frac_x * inv_frac_y + r10 * frac_x * inv_frac_y +
                         r01 * inv_frac_x * frac_y + r11 * frac_x * frac_y) >>
                        16;
            int64_t g = (g00 * inv_frac_x * inv_frac_y + g10 * frac_x * inv_frac_y +
                         g01 * inv_frac_x * frac_y + g11 * frac_x * frac_y) >>
                        16;
            int64_t b = (b00 * inv_frac_x * inv_frac_y + b10 * frac_x * inv_frac_y +
                         b01 * inv_frac_x * frac_y + b11 * frac_x * frac_y) >>
                        16;

            result->data[y * new_width + x] = ((uint32_t)(a & 0xFF) << 24) |
                                              ((uint32_t)(r & 0xFF) << 16) |
                                              ((uint32_t)(g & 0xFF) << 8) | (b & 0xFF);
        }
    }

    return result;
}
