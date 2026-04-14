//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_pixels_draw.c
// Purpose: Drawing primitives for Viper.Graphics.Pixels. Provides Canvas-
//   compatible drawing operations (line, box, disc, ring, ellipse, triangle,
//   Bezier, flood fill) that operate on a Pixels buffer using integer
//   coordinates and 0x00RRGGBB color format.
//
// Key invariants:
//   - Color format is 0x00RRGGBB (Canvas-compatible), NOT 0xRRGGBBAA.
//     Internally converts via rgb_to_rgba() before writing to the pixel buffer.
//   - All drawing is clipped to buffer bounds (no out-of-bounds writes).
//   - Flood fill uses an iterative scanline algorithm with a malloc'd stack.
//   - Thick lines are drawn as a series of filled discs along the line.
//   - Triangle fill uses scanline rasterization with sorted vertices.
//   - Alpha blending uses Porter-Duff "over" compositing.
//
// Ownership/Lifetime:
//   - Drawing functions modify the target Pixels in place (no new allocations).
//   - Flood fill temporarily allocates a work stack (freed before return).
//
// Links: src/runtime/graphics/rt_pixels_internal.h (shared struct and helpers),
//        src/runtime/graphics/rt_pixels.c (core operations),
//        src/runtime/graphics/rt_pixels.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_pixels.h"
#include "rt_pixels_internal.h"

#include "rt_internal.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Drawing Primitives  (color format: 0x00RRGGBB — Canvas-compatible)
//=============================================================================

/// @brief Write an opaque RGB pixel (color in 0x00RRGGBB format, alpha forced to 0xFF).
/// Out-of-bounds is a silent no-op (delegates to `rt_pixels_set`).
void rt_pixels_set_rgb(void *pixels, int64_t x, int64_t y, int64_t color) {
    rt_pixels_set(pixels, x, y, (color << 8) | 0xFF);
}

/// @brief Read an RGB pixel (returns 0x00RRGGBB, dropping the alpha channel).
int64_t rt_pixels_get_rgb(void *pixels, int64_t x, int64_t y) {
    return rt_pixels_get(pixels, x, y) >> 8;
}

/// @brief Draw a 1-pixel-wide line between (x1,y1) and (x2,y2) using Bresenham.
/// Color is 0x00RRGGBB. Out-of-bounds pixels along the line are clipped silently.
void rt_pixels_draw_line(
    void *pixels, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawLine: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    int64_t adx = dx < 0 ? -dx : dx;
    int64_t ady = dy < 0 ? -dy : dy;
    int64_t sx = dx >= 0 ? 1 : -1;
    int64_t sy = dy >= 0 ? 1 : -1;

    int64_t err = adx - ady;
    int64_t x = x1;
    int64_t y = y1;

    pixels_touch(p);

    for (;;) {
        set_pixel_raw(p, x, y, rgba);
        if (x == x2 && y == y2)
            break;
        int64_t e2 = err * 2;
        if (e2 > -ady) {
            err -= ady;
            x += sx;
        }
        if (e2 < adx) {
            err += adx;
            y += sy;
        }
    }
}

/// @brief Fill an axis-aligned rectangle with @p color (0x00RRGGBB).
/// Auto-clipped to buffer bounds; rectangles entirely outside are no-ops.
void rt_pixels_draw_box(void *pixels, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawBox: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    // Clip to buffer bounds
    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < 0 ? 0 : y;
    int64_t x1 = x + w;
    int64_t y1 = y + h;
    if (x1 > p->width)
        x1 = p->width;
    if (y1 > p->height)
        y1 = p->height;
    if (x0 >= x1 || y0 >= y1)
        return;

    pixels_touch(p);

    for (int64_t row = y0; row < y1; row++)
        for (int64_t col = x0; col < x1; col++)
            p->data[row * p->width + col] = rgba;
}

/// @brief Draw a 1-pixel-wide rectangle outline (top/bottom rows + side columns).
/// Inverse of `_draw_box`. Color is 0x00RRGGBB.
void rt_pixels_draw_frame(void *pixels, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawFrame: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (w <= 0 || h <= 0)
        return;

    pixels_touch(p);

    // Top and bottom rows
    for (int64_t col = x; col < x + w; col++) {
        set_pixel_raw(p, col, y, rgba);
        set_pixel_raw(p, col, y + h - 1, rgba);
    }
    // Left and right columns (skip corners already drawn)
    for (int64_t row = y + 1; row < y + h - 1; row++) {
        set_pixel_raw(p, x, row, rgba);
        set_pixel_raw(p, x + w - 1, row, rgba);
    }
}

/// @brief Fill a circle of radius @p r centered at (cx,cy) with @p color (0x00RRGGBB).
/// Uses integer square root for span widths — no floating point.
void rt_pixels_draw_disc(void *pixels, int64_t cx, int64_t cy, int64_t r, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawDisc: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (r < 0)
        r = 0;

    pixels_touch(p);

    for (int64_t dy = -r; dy <= r; dy++) {
        int64_t dx = isqrt64(r * r - dy * dy);
        for (int64_t fx = cx - dx; fx <= cx + dx; fx++)
            set_pixel_raw(p, fx, cy + dy, rgba);
    }
}

/// @brief Draw a 1-pixel-wide circle outline (Midpoint algorithm with 8-way symmetry).
/// Inverse of `_draw_disc`. Color is 0x00RRGGBB.
void rt_pixels_draw_ring(void *pixels, int64_t cx, int64_t cy, int64_t r, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawRing: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (r < 0)
        return;
    pixels_touch(p);
    if (r == 0) {
        set_pixel_raw(p, cx, cy, rgba);
        return;
    }

    // Midpoint circle: 8-way symmetry
    int64_t mx = r;
    int64_t my = 0;
    int64_t err = 0;

    while (mx >= my) {
        set_pixel_raw(p, cx + mx, cy + my, rgba);
        set_pixel_raw(p, cx + my, cy + mx, rgba);
        set_pixel_raw(p, cx - my, cy + mx, rgba);
        set_pixel_raw(p, cx - mx, cy + my, rgba);
        set_pixel_raw(p, cx - mx, cy - my, rgba);
        set_pixel_raw(p, cx - my, cy - mx, rgba);
        set_pixel_raw(p, cx + my, cy - mx, rgba);
        set_pixel_raw(p, cx + mx, cy - my, rgba);

        my++;
        if (err <= 0) {
            err += 2 * my + 1;
        } else {
            mx--;
            err += 2 * (my - mx) + 1;
        }
    }
}

/// @brief Fill an ellipse with X/Y radii (rx, ry) centered at (cx, cy).
/// Scanline rasterization in pure integer arithmetic.
void rt_pixels_draw_ellipse(
    void *pixels, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawEllipse: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (rx <= 0 || ry <= 0) {
        pixels_touch(p);
        set_pixel_raw(p, cx, cy, rgba);
        return;
    }

    pixels_touch(p);

    // Scanline fill: for each row dy, fill span [cx-dx .. cx+dx]
    // dx = rx * isqrt(ry^2 - dy^2) / ry  (integer arithmetic, no float)
    int64_t ry2 = ry * ry;
    for (int64_t dy = -ry; dy <= ry; dy++) {
        int64_t rem = ry2 - dy * dy;
        if (rem < 0)
            rem = 0;
        int64_t dx = rx * isqrt64(rem) / ry;
        for (int64_t fx = cx - dx; fx <= cx + dx; fx++)
            set_pixel_raw(p, fx, cy + dy, rgba);
    }
}

/// @brief Draw a 1-pixel-wide ellipse outline using midpoint algorithm with 4-quadrant symmetry.
void rt_pixels_draw_ellipse_frame(
    void *pixels, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawEllipseFrame: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    if (rx <= 0 || ry <= 0) {
        pixels_touch(p);
        set_pixel_raw(p, cx, cy, rgba);
        return;
    }

    pixels_touch(p);

    // Midpoint ellipse algorithm — 4-quadrant symmetry
    int64_t rx2 = rx * rx;
    int64_t ry2 = ry * ry;
    int64_t two_rx2 = 2 * rx2;
    int64_t two_ry2 = 2 * ry2;
    int64_t ex = 0;
    int64_t ey = ry;
    int64_t px_val = 0;
    int64_t py_val = two_rx2 * ey;

    // Region 1 (slope magnitude < 1)
    int64_t d1 = ry2 - rx2 * ry + rx2 / 4;
    while (px_val < py_val) {
        set_pixel_raw(p, cx + ex, cy + ey, rgba);
        set_pixel_raw(p, cx - ex, cy + ey, rgba);
        set_pixel_raw(p, cx + ex, cy - ey, rgba);
        set_pixel_raw(p, cx - ex, cy - ey, rgba);
        ex++;
        px_val += two_ry2;
        if (d1 < 0) {
            d1 += ry2 + px_val;
        } else {
            ey--;
            py_val -= two_rx2;
            d1 += ry2 + px_val - py_val;
        }
    }

    // Region 2 (slope magnitude >= 1)
    int64_t d2 = ry2 * ex * ex + rx2 * (ey - 1) * (ey - 1) - rx2 * ry2;
    while (ey >= 0) {
        set_pixel_raw(p, cx + ex, cy + ey, rgba);
        set_pixel_raw(p, cx - ex, cy + ey, rgba);
        set_pixel_raw(p, cx + ex, cy - ey, rgba);
        set_pixel_raw(p, cx - ex, cy - ey, rgba);
        ey--;
        py_val -= two_rx2;
        if (d2 > 0) {
            d2 += rx2 - py_val;
        } else {
            ex++;
            px_val += two_ry2;
            d2 += rx2 - py_val + px_val;
        }
    }
}

/// @brief Replace the connected region of pixels matching the seed color with @p color.
/// Uses an iterative scanline algorithm with a malloc'd worklist (no recursion,
/// no stack overflow risk on large images). Aborts silently on allocation failure
/// or capacity overflow rather than partially filling.
void rt_pixels_flood_fill(void *pixels, int64_t x, int64_t y, int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.FloodFill: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;

    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return;

    uint32_t target = p->data[y * p->width + x];
    uint32_t fill_c = rgb_to_rgba(color);

    if (target == fill_c)
        return;

    pixels_touch(p);

    // Iterative scanline flood fill — no recursion, no stack overflow risk
    typedef struct {
        int64_t x;
        int64_t y;
    } FillSeed;

    int64_t cap = 4096;
    FillSeed *stack = (FillSeed *)malloc((size_t)cap * sizeof(FillSeed));
    if (!stack)
        return;

    int64_t top = 0;
    stack[top].x = x;
    stack[top].y = y;
    top++;

    while (top > 0) {
        top--;
        int64_t sx = stack[top].x;
        int64_t sy = stack[top].y;

        if (sy < 0 || sy >= p->height || sx < 0 || sx >= p->width)
            continue;
        if (p->data[sy * p->width + sx] != target)
            continue;

        // Scan left to find span start
        int64_t lx = sx;
        while (lx > 0 && p->data[sy * p->width + (lx - 1)] == target)
            lx--;

        // Scan right to find span end
        int64_t rx = sx;
        while (rx + 1 < p->width && p->data[sy * p->width + (rx + 1)] == target)
            rx++;

        // Fill the span
        for (int64_t fx = lx; fx <= rx; fx++)
            p->data[sy * p->width + fx] = fill_c;

        // Push seed pixels for rows above and below this span
        for (int64_t row_off = -1; row_off <= 1; row_off += 2) {
            int64_t ny = sy + row_off;
            if (ny < 0 || ny >= p->height)
                continue;

            int64_t in_span = 0;
            for (int64_t fx = lx; fx <= rx; fx++) {
                if (p->data[ny * p->width + fx] == target) {
                    if (!in_span) {
                        if (top >= cap) {
                            if (cap > INT64_MAX / 2) {
                                free(stack);
                                return; // Abort flood fill on capacity overflow
                            }
                            int64_t new_cap = cap * 2;
                            FillSeed *ns =
                                (FillSeed *)realloc(stack, (size_t)new_cap * sizeof(FillSeed));
                            if (!ns) {
                                free(stack);
                                return;
                            }
                            stack = ns;
                            cap = new_cap;
                        }
                        stack[top].x = fx;
                        stack[top].y = ny;
                        top++;
                        in_span = 1;
                    }
                } else {
                    in_span = 0;
                }
            }
        }
    }

    free(stack);
}

/// @brief Draw a line of arbitrary thickness by stamping filled discs along the path.
/// Falls back to the 1-pixel `_draw_line` when @p thickness <= 1.
void rt_pixels_draw_thick_line(void *pixels,
                               int64_t x1,
                               int64_t y1,
                               int64_t x2,
                               int64_t y2,
                               int64_t thickness,
                               int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawThickLine: null pixels");
        return;
    }
    if (thickness <= 1) {
        rt_pixels_draw_line(pixels, x1, y1, x2, y2, color);
        return;
    }

    int64_t radius = thickness / 2;

    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    int64_t adx = dx < 0 ? -dx : dx;
    int64_t ady = dy < 0 ? -dy : dy;
    int64_t sx = dx >= 0 ? 1 : -1;
    int64_t sy = dy >= 0 ? 1 : -1;

    int64_t err = adx - ady;
    int64_t x = x1;
    int64_t y = y1;

    for (;;) {
        rt_pixels_draw_disc(pixels, x, y, radius, color);
        if (x == x2 && y == y2)
            break;
        int64_t e2 = err * 2;
        if (e2 > -ady) {
            err -= ady;
            x += sx;
        }
        if (e2 < adx) {
            err += adx;
            y += sy;
        }
    }
}

/// @brief Fill a solid triangle defined by three vertices using scanline rasterization.
/// Vertices are sorted top-to-bottom internally; degenerate (collinear) triangles
/// produce no output.
void rt_pixels_draw_triangle(void *pixels,
                             int64_t x1,
                             int64_t y1,
                             int64_t x2,
                             int64_t y2,
                             int64_t x3,
                             int64_t y3,
                             int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawTriangle: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    // Sort vertices by y ascending (bubble sort 3 elements)
    if (y1 > y2) {
        int64_t tx = x1;
        x1 = x2;
        x2 = tx;
        int64_t ty = y1;
        y1 = y2;
        y2 = ty;
    }
    if (y1 > y3) {
        int64_t tx = x1;
        x1 = x3;
        x3 = tx;
        int64_t ty = y1;
        y1 = y3;
        y3 = ty;
    }
    if (y2 > y3) {
        int64_t tx = x2;
        x2 = x3;
        x3 = tx;
        int64_t ty = y2;
        y2 = y3;
        y3 = ty;
    }

    int64_t total_h = y3 - y1;
    if (total_h == 0)
        return;

    pixels_touch(p);

    // Upper half: y1 .. y2
    int64_t upper_h = y2 - y1;
    for (int64_t row = 0; row <= upper_h; row++) {
        int64_t scan_y = y1 + row;
        int64_t ax = x1 + (x3 - x1) * row / total_h;
        int64_t bx = x1 + (x2 - x1) * row / (upper_h > 0 ? upper_h : 1);
        if (ax > bx) {
            int64_t tmp = ax;
            ax = bx;
            bx = tmp;
        }
        for (int64_t col = ax; col <= bx; col++)
            set_pixel_raw(p, col, scan_y, rgba);
    }

    // Lower half: y2 .. y3
    int64_t lower_h = y3 - y2;
    for (int64_t row = 0; row <= lower_h; row++) {
        int64_t scan_y = y2 + row;
        int64_t ax = x1 + (x3 - x1) * (upper_h + row) / total_h;
        int64_t bx = x2 + (x3 - x2) * row / (lower_h > 0 ? lower_h : 1);
        if (ax > bx) {
            int64_t tmp = ax;
            ax = bx;
            bx = tmp;
        }
        for (int64_t col = ax; col <= bx; col++)
            set_pixel_raw(p, col, scan_y, rgba);
    }
}

/// @brief Draw a quadratic Bézier curve from (x1,y1) to (x2,y2) via control point.
/// Step count is adaptive (capped at 10000) using integer de Casteljau evaluation.
void rt_pixels_draw_bezier(void *pixels,
                           int64_t x1,
                           int64_t y1,
                           int64_t cx_ctrl,
                           int64_t cy_ctrl,
                           int64_t x2,
                           int64_t y2,
                           int64_t color) {
    if (!pixels) {
        rt_trap("Pixels.DrawBezier: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    uint32_t rgba = rgb_to_rgba(color);

    // Adaptive step count: enough steps to avoid gaps
    int64_t adx = x2 - x1;
    if (adx < 0)
        adx = -adx;
    int64_t ady = y2 - y1;
    if (ady < 0)
        ady = -ady;
    int64_t acx = cx_ctrl - x1;
    if (acx < 0)
        acx = -acx;
    int64_t acy = cy_ctrl - y1;
    if (acy < 0)
        acy = -acy;
    int64_t steps = adx > ady ? adx : ady;
    if (acx > steps)
        steps = acx;
    if (acy > steps)
        steps = acy;
    steps = steps * 2 + 1;
    if (steps < 2)
        steps = 2;
    if (steps > 10000)
        steps = 10000; // Cap to prevent excessive loops

    pixels_touch(p);

    // Integer de Casteljau: P(t) via linear interpolation at t = i/steps
    for (int64_t i = 0; i <= steps; i++) {
        int64_t lx0 = x1 + (cx_ctrl - x1) * i / steps;
        int64_t ly0 = y1 + (cy_ctrl - y1) * i / steps;
        int64_t lx1 = cx_ctrl + (x2 - cx_ctrl) * i / steps;
        int64_t ly1 = cy_ctrl + (y2 - cy_ctrl) * i / steps;
        int64_t bx = lx0 + (lx1 - lx0) * i / steps;
        int64_t by = ly0 + (ly1 - ly0) * i / steps;
        set_pixel_raw(p, bx, by, rgba);
    }
}

/// @brief Composite an RGB pixel onto the buffer using Porter-Duff "over" with @p alpha [0..255].
/// Fast paths: alpha==0 → no-op, alpha==255 → opaque write. Otherwise blends src over dst.
void rt_pixels_blend_pixel(void *pixels, int64_t x, int64_t y, int64_t color, int64_t alpha) {
    if (!pixels) {
        rt_trap("Pixels.BlendPixel: null pixels");
        return;
    }
    rt_pixels_impl *p = (rt_pixels_impl *)pixels;
    if (x < 0 || x >= p->width || y < 0 || y >= p->height)
        return;

    // Clamp alpha to [0, 255]
    if (alpha <= 0)
        return; // fully transparent — no-op
    if (alpha > 255)
        alpha = 255;

    // Fully opaque fast path — same as set_rgb
    if (alpha == 255) {
        p->data[y * p->width + x] = (uint32_t)((color << 8) | 0xFF);
        pixels_touch(p);
        return;
    }

    // Extract source channels from 0x00RRGGBB
    uint32_t sr = (uint32_t)((color >> 16) & 0xFF);
    uint32_t sg = (uint32_t)((color >> 8) & 0xFF);
    uint32_t sb = (uint32_t)(color & 0xFF);
    uint32_t sa = (uint32_t)alpha;

    // Extract destination channels from 0xRRGGBBAA
    uint32_t dst = p->data[y * p->width + x];
    uint32_t dr = (dst >> 24) & 0xFF;
    uint32_t dg = (dst >> 16) & 0xFF;
    uint32_t db = (dst >> 8) & 0xFF;
    uint32_t da = (dst) & 0xFF;

    // Porter-Duff "over": out = src * sa/255 + dst * da/255 * (255 - sa)/255
    // Simplified (pre-multiplied integer arithmetic, +127 for rounding):
    uint32_t inv = 255 - sa;
    uint32_t or_ = (sr * sa + dr * inv + 127) / 255;
    uint32_t og = (sg * sa + dg * inv + 127) / 255;
    uint32_t ob = (sb * sa + db * inv + 127) / 255;
    uint32_t oa = sa + (da * inv + 127) / 255;

    p->data[y * p->width + x] = (or_ << 24) | (og << 16) | (ob << 8) | oa;
    pixels_touch(p);
}
