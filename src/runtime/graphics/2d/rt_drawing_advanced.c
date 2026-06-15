//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_drawing_advanced.c
// Purpose: Advanced 2D canvas drawing: thick/round strokes, flood fill, triangles,
//   ellipses, arcs, beziers, polylines/polygons (incl. Path2D), and linear
//   gradients. Color math lives in rt_color.c.
//
// Links: rt_graphics2d.h, rt_graphics_internal.h (canvas API),
//        rt_color.c (color utilities used by gradients)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics2d.h"
#include "rt_graphics_internal.h"
#include "rt_heap.h"

#include <limits.h>

#ifdef VIPER_ENABLE_GRAPHICS


enum {
    RTG_CORNER_TOP_LEFT = 0,
    RTG_CORNER_TOP_RIGHT = 1,
    RTG_CORNER_BOTTOM_LEFT = 2,
    RTG_CORNER_BOTTOM_RIGHT = 3,
};

/// @brief Validate a points array passed to Polyline/Polygon and expose its raw element pointer.
/// @details Polyline/Polygon take an Integer-typed Viper array of length
///          `count * 2` (interleaved x/y pairs). This helper rejects: NULL
///          array, non-positive count, count > INT64_MAX/2 (so the multiply
///          can't overflow), arrays whose heap header is missing or wrong
///          kind (must be RT_HEAP_ARRAY of RT_ELEM_I64), and arrays shorter
///          than `count * 2` elements.
/// @param points_ptr Opaque pointer to the heap-allocated array.
/// @param count      Number of (x, y) pairs the caller intends to read.
/// @param points_out Out: raw int64_t* into the array on success, NULL on failure.
/// @return 1 if the array is safe to walk for `count` pairs, 0 otherwise (no draw).
static int8_t rt_canvas_points_checked(void *points_ptr,
                                       int64_t count,
                                       const int64_t **points_out) {
    if (points_out)
        *points_out = NULL;
    if (!points_ptr || count <= 0 || count > INT64_MAX / 2)
        return 0;

    rt_heap_hdr_t *hdr = NULL;
    if (!rt_heap_try_get_header(points_ptr, &hdr) || !hdr)
        return 0;
    if ((rt_heap_kind_t)hdr->kind != RT_HEAP_ARRAY || (rt_elem_kind_t)hdr->elem_kind != RT_ELEM_I64)
        return 0;

    uint64_t required = (uint64_t)count * 2u;
    if (required > hdr->len)
        return 0;
    if (points_out)
        *points_out = (const int64_t *)points_ptr;
    return 1;
}

/// @brief Floor a long double to int64, saturating at INT64_MIN/MAX instead of overflowing.
static int64_t rt_canvas_adv_floor_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)floorl(value);
}

/// @brief Ceil a long double to int64, saturating at INT64_MIN/MAX instead of overflowing.
static int64_t rt_canvas_adv_ceil_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)ceill(value);
}

/// @brief Linearly interpolate the x value of a line segment at scanline @p y.
/// @details Used by triangle/polygon scan-conversion to find the left/right
///          edge x at a given y. Long-double arithmetic keeps precision when
///          int64 endpoints are widely separated; the result floors to int64
///          with saturation. Degenerate horizontal segments (y1 == y0) return x0.
/// @param x0,y0 First endpoint.
/// @param x1,y1 Second endpoint.
/// @param y     Scanline to sample.
/// @return Interpolated x at scanline @p y, saturated to int64.
static int64_t rt_canvas_adv_interp_x(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t y) {
    if (y1 == y0)
        return x0;
    long double t = ((long double)y - (long double)y0) / ((long double)y1 - (long double)y0);
    long double x = (long double)x0 + ((long double)x1 - (long double)x0) * t;
    return rt_canvas_adv_floor_ld_to_i64_sat(x);
}

/// @brief Convert a Viper packed color to a 24-bit ViperGFX RGB value.
/// @details Normalizes through rt_pixels_color_to_rgba (0xRRGGBBAA) then drops
///          the alpha byte, since the advanced canvas primitives draw opaque.
static vgfx_color_t rt_canvas_adv_color_to_vgfx_rgb(int64_t color) {
    return (vgfx_color_t)((rt_pixels_color_to_rgba(color) >> 8) & 0x00FFFFFFu);
}

/// @brief Squared distance between two points in long double (no sqrt, no
///        overflow) — used only to compare relative edge lengths.
static long double rt_canvas_adv_dist2_ld(int64_t x1, int64_t y1, int64_t x2, int64_t y2) {
    long double dx = (long double)x2 - (long double)x1;
    long double dy = (long double)y2 - (long double)y1;
    return dx * dx + dy * dy;
}

/// @brief Degenerate-triangle fallback: when the three vertices are collinear
///        (zero area), draw just the longest of the three edges so the shape
///        still renders as the line it visually is.
static void rt_canvas_adv_degenerate_triangle_line(void *canvas_ptr,
                                                   int64_t x1,
                                                   int64_t y1,
                                                   int64_t x2,
                                                   int64_t y2,
                                                   int64_t x3,
                                                   int64_t y3,
                                                   int64_t color) {
    long double d12 = rt_canvas_adv_dist2_ld(x1, y1, x2, y2);
    long double d23 = rt_canvas_adv_dist2_ld(x2, y2, x3, y3);
    long double d31 = rt_canvas_adv_dist2_ld(x3, y3, x1, y1);
    if (d12 >= d23 && d12 >= d31) {
        rt_canvas_line(canvas_ptr, x1, y1, x2, y2, color);
    } else if (d23 >= d31) {
        rt_canvas_line(canvas_ptr, x2, y2, x3, y3, color);
    } else {
        rt_canvas_line(canvas_ptr, x3, y3, x1, y1, color);
    }
}

/// @brief Plot the two clip-respecting points of one octant of a circle, mirrored into one of four
/// corners.
/// @details For a Bresenham circle stepper at offset (x, y) inside the
///          first octant (x >= y >= 0), this writes the two pixels that
///          fall in the requested @p corner — the (x, y) and (y, x)
///          reflections within that quadrant. Used by round_box / round_frame
///          to draw only the pixels that belong to a specific corner.
/// @param canvas Canvas to draw into. NULL → no-op.
/// @param cx,cy  Circle center in logical coordinates.
/// @param x,y    Bresenham octant offset (caller advances these).
/// @param corner One of RTG_CORNER_TOP_LEFT / TOP_RIGHT / BOTTOM_LEFT / BOTTOM_RIGHT.
/// @param color  Pixel color (0xAARRGGBB packed).
static void rt_canvas_plot_quarter_circle(rt_canvas *canvas,
                                          int64_t cx,
                                          int64_t cy,
                                          int64_t x,
                                          int64_t y,
                                          int corner,
                                          vgfx_color_t color) {
    if (!canvas || !canvas->gfx_win)
        return;

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w);
    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h);
#define RT_CANVAS_PLOT_CLIPPED(px, py)                                                             \
    do {                                                                                           \
        int64_t qx = (px);                                                                         \
        int64_t qy = (py);                                                                         \
        if (qx >= clip_x && qx < clip_x1 && qy >= clip_y && qy < clip_y1 &&                        \
            rtg_i64_fits_i32(qx) && rtg_i64_fits_i32(qy))                                          \
            vgfx_pset(canvas->gfx_win, (int32_t)qx, (int32_t)qy, color);                           \
    } while (0)

    switch (corner) {
        case RTG_CORNER_TOP_LEFT:
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, -x), rtg_add_sat64(cy, -y));
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, -y), rtg_add_sat64(cy, -x));
            break;
        case RTG_CORNER_TOP_RIGHT:
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, x), rtg_add_sat64(cy, -y));
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, y), rtg_add_sat64(cy, -x));
            break;
        case RTG_CORNER_BOTTOM_LEFT:
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, -x), rtg_add_sat64(cy, y));
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, -y), rtg_add_sat64(cy, x));
            break;
        case RTG_CORNER_BOTTOM_RIGHT:
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, x), rtg_add_sat64(cy, y));
            RT_CANVAS_PLOT_CLIPPED(rtg_add_sat64(cx, y), rtg_add_sat64(cy, x));
            break;
    }
#undef RT_CANVAS_PLOT_CLIPPED
}

/// @brief Bresenham-step a circle of @p radius and emit only the pixels in the chosen @p corner.
/// @details Standard mid-point circle rasterizer (8-way symmetry collapsed to
///          one corner via rt_canvas_plot_quarter_circle). Used by round_box
///          and round_frame for the four corner arcs. radius == 0 plots a
///          single pixel at the center; negative radii are no-ops.
/// @param canvas Canvas. NULL → no-op.
/// @param cx,cy  Arc center in logical coordinates.
/// @param radius Arc radius in pixels.
/// @param corner Quadrant selector (see RTG_CORNER_* enum).
/// @param color  Stroke color (0xAARRGGBB packed).
static void rt_canvas_draw_quarter_circle(
    rt_canvas *canvas, int64_t cx, int64_t cy, int64_t radius, int corner, vgfx_color_t color) {
    if (!canvas || !canvas->gfx_win || radius < 0)
        return;

    if (radius == 0) {
        rt_canvas_plot(canvas, cx, cy, (int64_t)color);
        return;
    }

    int64_t x = radius;
    int64_t y = 0;
    int64_t err = 1 - radius;
    while (x >= y) {
        rt_canvas_plot_quarter_circle(canvas, cx, cy, x, y, corner, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/// @brief Return the canvas clip rect converted from logical to physical pixels.
/// @details The canvas tracks a logical clip rect (in logical pixels), but
///          some primitives (notably gradient_h/gradient_v's per-pixel inner
///          loops) want to walk physical pixels directly. This helper
///          multiplies through by the window's HiDPI scale factor and returns
///          both the scale and the [px0, px1) × [py0, py1) physical-pixel
///          bounds. NULL out-pointers are individually skipped.
/// @return 1 on success (clip rect available), 0 if the canvas has no clip set.
static int8_t rt_canvas_get_scaled_clip_bounds(
    rt_canvas *canvas, float *scale_out, int64_t *px0, int64_t *py0, int64_t *px1, int64_t *py1) {
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return 0;

    float scale = vgfx_window_get_scale(canvas->gfx_win);
    if (scale_out)
        *scale_out = scale;
    if (px0)
        *px0 = rtg_scale_up_i64(clip_x, scale);
    if (py0)
        *py0 = rtg_scale_up_i64(clip_y, scale);
    if (px1)
        *px1 = rtg_scale_up_i64(rtg_add_sat64(clip_x, clip_w), scale);
    if (py1)
        *py1 = rtg_scale_up_i64(rtg_add_sat64(clip_y, clip_h), scale);
    return 1;
}

//=============================================================================
// Extended Drawing Primitives
//=============================================================================

/// @brief Draw a thick line segment with rounded endcaps.
///
/// `thickness == 1` falls through to the fast hairline `vgfx_line`.
/// Wider lines are tessellated as a filled parallelogram (the body
/// of the line, perpendicular to the segment) plus two filled
/// circles at the endpoints to round off the caps. Color is the
/// standard `0xRRGGBBAA` packed format.
void rt_canvas_thick_line(void *canvas_ptr,
                          int64_t x1,
                          int64_t y1,
                          int64_t x2,
                          int64_t y2,
                          int64_t thickness,
                          int64_t color) {
    if (!canvas_ptr || thickness <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    vgfx_color_t col = rt_canvas_adv_color_to_vgfx_rgb(color);

    if (thickness == 1) {
        rt_canvas_line(canvas, x1, y1, x2, y2, color);
        return;
    }

    // Draw thick line as a filled parallelogram body + two round endcap circles.
    // This is O((length + r) * r) vs O(length * r^2) for circle-per-step,
    // which is a factor-of-r speedup for large thickness values.
    int64_t half = thickness / 2;

    if (x1 == x2 && y1 == y2) {
        rt_canvas_disc(canvas, x1, y1, half, color);
        return;
    }

    rt_canvas_disc(canvas, x1, y1, half, color);
    rt_canvas_disc(canvas, x2, y2, half, color);

    // Parallelogram body: four corners offset by perpendicular half-width.
    long double ldx = (long double)x2 - (long double)x1;
    long double ldy = (long double)y2 - (long double)y1;
    long double len = sqrtl(ldx * ldx + ldy * ldy);
    if (len <= 0.0L || !isfinite((double)len))
        return;
    // Perpendicular unit vector (rotated 90 degrees)
    long double px = (-ldy / len) * (long double)half;
    long double py = (ldx / len) * (long double)half;

    // Four corners of the parallelogram
    long double ax = (long double)x1 + px, ay = (long double)y1 + py;
    long double bx = (long double)x1 - px, by = (long double)y1 - py;
    long double cx = (long double)x2 + px, cy = (long double)y2 + py;
    long double dx = (long double)x2 - px, dy_c = (long double)y2 - py;

    // Scanline fill the parallelogram (convex 4-vertex polygon).
    int64_t y_lo = rt_canvas_adv_floor_ld_to_i64_sat(fminl(fminl(ay, by), fminl(cy, dy_c)));
    int64_t y_hi = rt_canvas_adv_ceil_ld_to_i64_sat(fmaxl(fmaxl(ay, by), fmaxl(cy, dy_c)));
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    int64_t clip_last_y = rtg_add_sat64(clip_y, clip_h - 1);
    if (y_lo < clip_y)
        y_lo = clip_y;
    if (y_hi > clip_last_y)
        y_hi = clip_last_y;

    for (int64_t scan_y = y_lo; scan_y <= y_hi; scan_y++) {
        long double sv = (long double)scan_y;
        long double x_min = 1e300L, x_max = -1e300L;
        long double xi;

        // Edge A->C
        if (fminl(ay, cy) <= sv && sv <= fmaxl(ay, cy) && ay != cy) {
            xi = ax + (cx - ax) * (sv - ay) / (cy - ay);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }
        // Edge C->D
        if (fminl(cy, dy_c) <= sv && sv <= fmaxl(cy, dy_c) && cy != dy_c) {
            xi = cx + (dx - cx) * (sv - cy) / (dy_c - cy);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }
        // Edge D->B
        if (fminl(dy_c, by) <= sv && sv <= fmaxl(dy_c, by) && dy_c != by) {
            xi = dx + (bx - dx) * (sv - dy_c) / (by - dy_c);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }
        // Edge B->A
        if (fminl(by, ay) <= sv && sv <= fmaxl(by, ay) && by != ay) {
            xi = bx + (ax - bx) * (sv - by) / (ay - by);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }

        if (x_max >= x_min) {
            int64_t lx = rt_canvas_adv_floor_ld_to_i64_sat(x_min);
            int64_t rx = rt_canvas_adv_ceil_ld_to_i64_sat(x_max);
            int64_t clip_last_x = rtg_add_sat64(clip_x, clip_w - 1);
            if (rx < clip_x || lx > clip_last_x)
                continue;
            if (lx < clip_x)
                lx = clip_x;
            if (rx > clip_last_x)
                rx = clip_last_x;
            vgfx_line(canvas->gfx_win,
                      rtg_clamp_i64_to_i32(lx),
                      rtg_clamp_i64_to_i32(scan_y),
                      rtg_clamp_i64_to_i32(rx),
                      rtg_clamp_i64_to_i32(scan_y),
                      col);
        }
    }
}

/// @brief Fill a rectangle whose corners are quarter-circles of radius `radius`.
///
/// Composed of one center rectangle + two side rectangles + four
/// quarter-circle fills at the corners. Setting `radius >= min(w,h)/2`
/// produces a stadium / capsule shape; `radius == 0` degenerates
/// to a regular filled box.
void rt_canvas_round_box(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color) {
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    // Clamp radius to half of smallest dimension
    int64_t max_radius = rtg_min64(w, h) / 2;
    if (radius > max_radius)
        radius = max_radius;
    if (radius < 0)
        radius = 0;

    if (radius == 0) {
        rt_canvas_box(canvas, x, y, w, h, color);
        return;
    }

    int64_t middle_h = h - 2 * radius;
    if (middle_h > 0)
        rt_canvas_box(canvas, x, rtg_add_sat64(y, radius), w, middle_h, color);

    int64_t cx_left = rtg_add_sat64(x, radius);
    int64_t cx_right = rtg_add_sat64(x, w - radius - 1);
    int64_t cy_top = rtg_add_sat64(y, radius);
    int64_t cy_bottom = rtg_add_sat64(y, h - radius - 1);
    long double r2 = (long double)radius * (long double)radius;

    for (int64_t dy = -radius; dy < 0; dy++) {
        long double rem = r2 - (long double)dy * (long double)dy;
        int64_t span = rem <= 0.0L ? 0 : rt_canvas_adv_floor_ld_to_i64_sat(sqrtl(rem));
        int64_t row = rtg_add_sat64(cy_top, dy);
        rt_canvas_line(
            canvas, rtg_add_sat64(cx_left, -span), row, rtg_add_sat64(cx_right, span), row, color);
    }
    for (int64_t dy = 1; dy <= radius; dy++) {
        long double rem = r2 - (long double)dy * (long double)dy;
        int64_t span = rem <= 0.0L ? 0 : rt_canvas_adv_floor_ld_to_i64_sat(sqrtl(rem));
        int64_t row = rtg_add_sat64(cy_bottom, dy);
        rt_canvas_line(
            canvas, rtg_add_sat64(cx_left, -span), row, rtg_add_sat64(cx_right, span), row, color);
    }
}

/// @brief Stroke (outline only) the same rounded-rectangle shape as `rt_canvas_round_box`.
///
/// Renders four straight edge segments + four quarter-circle arcs.
void rt_canvas_round_frame(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color) {
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    vgfx_color_t col = rt_canvas_adv_color_to_vgfx_rgb(color);

    // Clamp radius
    int64_t max_radius = rtg_min64(w, h) / 2;
    if (radius > max_radius)
        radius = max_radius;
    if (radius < 0)
        radius = 0;

    if (radius == 0) {
        rt_canvas_frame(canvas, x, y, w, h, color);
        return;
    }

    rt_canvas_line(canvas, rtg_add_sat64(x, radius), y, rtg_add_sat64(x, w - radius - 1), y, color);
    rt_canvas_line(canvas,
                   rtg_add_sat64(x, radius),
                   rtg_add_sat64(y, h - 1),
                   rtg_add_sat64(x, w - radius - 1),
                   rtg_add_sat64(y, h - 1),
                   color);

    rt_canvas_line(canvas, x, rtg_add_sat64(y, radius), x, rtg_add_sat64(y, h - radius - 1), color);
    rt_canvas_line(canvas,
                   rtg_add_sat64(x, w - 1),
                   rtg_add_sat64(y, radius),
                   rtg_add_sat64(x, w - 1),
                   rtg_add_sat64(y, h - radius - 1),
                   color);

    // Draw corner arcs as true quarter circles so the outline stays hollow.
    rt_canvas_draw_quarter_circle(canvas,
                                  rtg_add_sat64(x, radius),
                                  rtg_add_sat64(y, radius),
                                  radius,
                                  RTG_CORNER_TOP_LEFT,
                                  col);
    rt_canvas_draw_quarter_circle(canvas,
                                  rtg_add_sat64(x, w - radius - 1),
                                  rtg_add_sat64(y, radius),
                                  radius,
                                  RTG_CORNER_TOP_RIGHT,
                                  col);
    rt_canvas_draw_quarter_circle(canvas,
                                  rtg_add_sat64(x, radius),
                                  rtg_add_sat64(y, h - radius - 1),
                                  radius,
                                  RTG_CORNER_BOTTOM_LEFT,
                                  col);
    rt_canvas_draw_quarter_circle(canvas,
                                  rtg_add_sat64(x, w - radius - 1),
                                  rtg_add_sat64(y, h - radius - 1),
                                  radius,
                                  RTG_CORNER_BOTTOM_RIGHT,
                                  col);
}

/// @brief Fill the flood.
void rt_canvas_flood_fill(void *canvas_ptr, int64_t start_x, int64_t start_y, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w);
    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h);
    if (start_x < clip_x || start_x >= clip_x1 || start_y < clip_y || start_y >= clip_y1)
        return;

    float scale = 1.0f;
    int64_t clip_px0 = 0;
    int64_t clip_py0 = 0;
    int64_t clip_px1 = 0;
    int64_t clip_py1 = 0;
    if (!rt_canvas_get_scaled_clip_bounds(
            canvas, &scale, &clip_px0, &clip_py0, &clip_px1, &clip_py1))
        return;

    start_x = rtg_scale_up_i64(start_x, scale);
    start_y = rtg_scale_up_i64(start_y, scale);

    // Bounds check (physical, constrained to the active clip)
    if (start_x < clip_px0 || start_x >= clip_px1 || start_y < clip_py0 || start_y >= clip_py1 ||
        start_x < 0 || start_x >= fb.width || start_y < 0 || start_y >= fb.height)
        return;

    // Get the target color (color to replace)
    uint8_t *start_pixel = &fb.pixels[start_y * fb.stride + start_x * 4];
    uint32_t target_r = start_pixel[0];
    uint32_t target_g = start_pixel[1];
    uint32_t target_b = start_pixel[2];
    uint32_t target_a = start_pixel[3];

    // Get fill color components
    uint32_t fill_rgba = rt_pixels_color_to_rgba(color);
    uint8_t fill_r = (uint8_t)((fill_rgba >> 24) & 0xFFu);
    uint8_t fill_g = (uint8_t)((fill_rgba >> 16) & 0xFFu);
    uint8_t fill_b = (uint8_t)((fill_rgba >> 8) & 0xFFu);
    uint8_t fill_a = (uint8_t)(fill_rgba & 0xFFu);

    // Don't fill if target color is the same as fill color
    if (target_r == fill_r && target_g == fill_g && target_b == fill_b && target_a == fill_a)
        return;

    /* O-03: Use a dynamically-growing stack starting at 4096 entries
     * instead of pre-allocating the worst-case (width * height) upfront.
     * This avoids O(r^2) allocations for small fill regions. */
    int64_t stack_cap = 4096;
    int64_t *stack_x = (int64_t *)malloc((size_t)stack_cap * sizeof(int64_t));
    int64_t *stack_y = (int64_t *)malloc((size_t)stack_cap * sizeof(int64_t));
    if (!stack_x || !stack_y) {
        free(stack_x);
        free(stack_y);
        return;
    }

    int64_t stack_top = 0;
    stack_x[stack_top] = start_x;
    stack_y[stack_top] = start_y;
    stack_top++;

    while (stack_top > 0) {
        stack_top--;
        int64_t x = stack_x[stack_top];
        int64_t y = stack_y[stack_top];

        // Skip if out of bounds
        if (x < clip_px0 || x >= clip_px1 || y < clip_py0 || y >= clip_py1 || x < 0 ||
            x >= fb.width || y < 0 || y >= fb.height)
            continue;

        uint8_t *pixel = &fb.pixels[y * fb.stride + x * 4];

        // Skip if not target color
        if (pixel[0] != target_r || pixel[1] != target_g || pixel[2] != target_b ||
            pixel[3] != target_a)
            continue;

        // Fill this pixel
        pixel[0] = fill_r;
        pixel[1] = fill_g;
        pixel[2] = fill_b;
        pixel[3] = fill_a;

        // Grow stack if needed before pushing 4 neighbors
        if (stack_top > stack_cap - 4) {
            if (stack_top > INT64_MAX - 4)
                break;
            int64_t required = stack_top + 4;
            int64_t new_cap = stack_cap;
            while (new_cap < required) {
                if (new_cap > INT64_MAX / 2) {
                    new_cap = required;
                    break;
                }
                new_cap *= 2;
            }
            if (new_cap > INT64_MAX / (int64_t)sizeof(int64_t))
                break;
            int64_t *nx = (int64_t *)realloc(stack_x, (size_t)new_cap * sizeof(int64_t));
            if (!nx) {
                free(stack_x);
                free(stack_y);
                return;
            }
            stack_x = nx;
            int64_t *ny = (int64_t *)realloc(stack_y, (size_t)new_cap * sizeof(int64_t));
            if (!ny) {
                free(stack_x);
                free(stack_y);
                return;
            }
            stack_y = ny;
            stack_cap = new_cap;
        }

        // Push neighbors (4-connected)
        stack_x[stack_top] = x + 1;
        stack_y[stack_top] = y;
        stack_top++;
        stack_x[stack_top] = x - 1;
        stack_y[stack_top] = y;
        stack_top++;
        stack_x[stack_top] = x;
        stack_y[stack_top] = y + 1;
        stack_top++;
        stack_x[stack_top] = x;
        stack_y[stack_top] = y - 1;
        stack_top++;
    }

    free(stack_x);
    free(stack_y);
}

/// @brief Filled triangle defined by three points (any winding order).
///
/// Uses scanline rasterisation: sorts vertices by Y, then walks each
/// horizontal line filling between the active edges. Sub-pixel
/// vertices are not antialiased — this is the fast solid-fill path.
void rt_canvas_triangle(void *canvas_ptr,
                        int64_t x1,
                        int64_t y1,
                        int64_t x2,
                        int64_t y2,
                        int64_t x3,
                        int64_t y3,
                        int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    long double area = ((long double)x2 - (long double)x1) * ((long double)y3 - (long double)y1) -
                       ((long double)y2 - (long double)y1) * ((long double)x3 - (long double)x1);
    if (area == 0.0L) {
        rt_canvas_adv_degenerate_triangle_line(canvas_ptr, x1, y1, x2, y2, x3, y3, color);
        return;
    }

    // Sort vertices by y-coordinate (y1 <= y2 <= y3)
    if (y1 > y2) {
        int64_t tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    if (y2 > y3) {
        int64_t tmp = x2;
        x2 = x3;
        x3 = tmp;
        tmp = y2;
        y2 = y3;
        y3 = tmp;
    }
    if (y1 > y2) {
        int64_t tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    // Handle degenerate cases
    if (y1 == y3) {
        // Horizontal line
        int64_t min_x = rtg_min64(rtg_min64(x1, x2), x3);
        int64_t max_x = rtg_max64(rtg_max64(x1, x2), x3);
        rt_canvas_line(canvas, min_x, y1, max_x, y1, color);
        return;
    }

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    (void)clip_x;
    (void)clip_w;
    int64_t scan_start = rtg_max64(y1, clip_y);
    int64_t scan_end = rtg_min64(y3, rtg_add_sat64(clip_y, clip_h) - 1);
    if (scan_end < scan_start)
        return;

    // Fill triangle using scanline algorithm
    for (int64_t y = scan_start; y <= scan_end; y++) {
        int64_t xa, xb;

        if (y < y2) {
            // Upper part of triangle
            xa = rt_canvas_adv_interp_x(x1, y1, x2, y2, y);
        } else {
            // Lower part of triangle
            xa = rt_canvas_adv_interp_x(x2, y2, x3, y3, y);
        }

        // Long edge from y1 to y3
        xb = rt_canvas_adv_interp_x(x1, y1, x3, y3, y);

        if (xa > xb) {
            int64_t tmp = xa;
            xa = xb;
            xb = tmp;
        }

        rt_canvas_line(canvas, xa, y, xb, y, color);
    }
}

/// @brief Outline-only triangle — three line segments connecting the vertices.
void rt_canvas_triangle_frame(void *canvas_ptr,
                              int64_t x1,
                              int64_t y1,
                              int64_t x2,
                              int64_t y2,
                              int64_t x3,
                              int64_t y3,
                              int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas_line(canvas_ptr, x1, y1, x2, y2, color);
    rt_canvas_line(canvas_ptr, x2, y2, x3, y3, color);
    rt_canvas_line(canvas_ptr, x3, y3, x1, y1, color);
}

/// @brief Filled axis-aligned ellipse centered at `(cx, cy)` with radii `(rx, ry)`.
///
/// Uses the scanline algorithm: for each y from -ry to +ry, compute
/// the x extent from the ellipse equation and fill that horizontal
/// span. Avoids the trig calls of polar tessellation.
void rt_canvas_ellipse(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color) {
    if (!canvas_ptr || rx <= 0 || ry <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    // If rx == ry, it's a circle
    if (rx == ry) {
        rt_canvas_disc(canvas_ptr, cx, cy, rx, color);
        return;
    }

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;

    int64_t y0 = rtg_sub_nonneg_sat64(cy, ry);
    int64_t y1 = rtg_add_sat64(cy, ry);
    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h) - 1;
    if (y0 < clip_y)
        y0 = clip_y;
    if (y1 > clip_y1)
        y1 = clip_y1;
    if (y1 < y0)
        return;

    long double rx_ld = (long double)rx;
    long double ry_ld = (long double)ry;
    int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w) - 1;
    vgfx_color_t col = rt_canvas_adv_color_to_vgfx_rgb(color);
    for (int64_t py = y0; py <= y1; ++py) {
        long double dy = (long double)py - (long double)cy;
        long double norm = 1.0L - (dy * dy) / (ry_ld * ry_ld);
        if (norm < 0.0L)
            continue;
        long double span = rx_ld * sqrtl(norm);
        int64_t x0 = rt_canvas_adv_floor_ld_to_i64_sat((long double)cx - span);
        int64_t x1 = rt_canvas_adv_ceil_ld_to_i64_sat((long double)cx + span);
        if (x0 < clip_x)
            x0 = clip_x;
        if (x1 > clip_x1)
            x1 = clip_x1;
        if (x1 >= x0)
            vgfx_line(canvas->gfx_win,
                      rtg_clamp_i64_to_i32(x0),
                      rtg_clamp_i64_to_i32(py),
                      rtg_clamp_i64_to_i32(x1),
                      rtg_clamp_i64_to_i32(py),
                      col);
    }
}

/// @brief Stroke (outline only) an ellipse using a polyline approximation.
///
/// Tessellates the ellipse boundary into short line segments — segment
/// count scales with the larger radius so small ellipses don't pay
/// for unnecessary detail.
void rt_canvas_ellipse_frame(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color) {
    if (!canvas_ptr || rx <= 0 || ry <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    // If rx == ry, it's a circle
    if (rx == ry) {
        rt_canvas_ring(canvas_ptr, cx, cy, rx, color);
        return;
    }

    int64_t max_radius = rx > ry ? rx : ry;
    int64_t steps = max_radius > 1024 ? 4096 : max_radius * 4;
    if (steps < 24)
        steps = 24;
    if (steps > 4096)
        steps = 4096;

    long double rx_ld = (long double)rx;
    long double ry_ld = (long double)ry;
    int64_t prev_x = 0;
    int64_t prev_y = 0;
    for (int64_t i = 0; i <= steps; ++i) {
        long double angle = (2.0L * 3.14159265358979323846L * (long double)i) / (long double)steps;
        int64_t px = rt_canvas_adv_floor_ld_to_i64_sat((long double)cx + cosl(angle) * rx_ld);
        int64_t py = rt_canvas_adv_floor_ld_to_i64_sat((long double)cy + sinl(angle) * ry_ld);
        if (i > 0)
            rt_canvas_line(canvas_ptr, prev_x, prev_y, px, py, color);
        prev_x = px;
        prev_y = py;
    }
}

/// @brief Filled ellipse with per-pixel alpha blending against the existing canvas.
///
/// Slower than `rt_canvas_ellipse` because each pixel goes through
/// `vgfx_blend_pixel` instead of a fast solid fill — use only when
/// translucency is needed.
void rt_canvas_ellipse_alpha(void *canvas_ptr,
                             int64_t cx,
                             int64_t cy,
                             int64_t rx,
                             int64_t ry,
                             int64_t color,
                             int64_t alpha) {
    if (!canvas_ptr || rx <= 0 || ry <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win || alpha <= 0)
        return;

    if (alpha >= 255) {
        rt_canvas_ellipse(canvas_ptr, cx, cy, rx, ry, color);
        return;
    }

    if (rx == ry) {
        rt_canvas_disc_alpha(canvas_ptr, cx, cy, rx, color, alpha);
        return;
    }

    uint32_t argb =
        ((uint32_t)(alpha & 0xFF) << 24) | ((rt_pixels_color_to_rgba(color) >> 8) & 0x00FFFFFFu);

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    int64_t y0 = rtg_sub_nonneg_sat64(cy, ry);
    int64_t y1 = rtg_add_sat64(cy, ry);
    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h) - 1;
    if (y0 < clip_y)
        y0 = clip_y;
    if (y1 > clip_y1)
        y1 = clip_y1;
    if (y1 < y0)
        return;

    long double rx_ld = (long double)rx;
    long double ry_ld = (long double)ry;
    int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w) - 1;
    for (int64_t py = y0; py <= y1; ++py) {
        long double dy = (long double)py - (long double)cy;
        long double norm = 1.0L - (dy * dy) / (ry_ld * ry_ld);
        if (norm < 0.0L)
            continue;

        long double span = rx_ld * sqrtl(norm);
        int64_t x0 = rt_canvas_adv_floor_ld_to_i64_sat((long double)cx - span);
        int64_t x1 = rt_canvas_adv_ceil_ld_to_i64_sat((long double)cx + span);
        if (x0 < clip_x)
            x0 = clip_x;
        if (x1 > clip_x1)
            x1 = clip_x1;
        for (int64_t px = x0; px <= x1; ++px)
            vgfx_pset_alpha(canvas->gfx_win, (int32_t)px, (int32_t)py, argb);
    }
}

//=============================================================================
// Advanced Curves & Shapes
//=============================================================================

/// @brief Filled circular arc (pie slice) from `start_deg` to `end_deg`.
///
/// Drawn as a fan of triangles from the center to points along the
/// arc. Angles are in degrees, measured clockwise from the positive
/// X axis.
void rt_canvas_arc(void *canvas_ptr,
                   int64_t cx,
                   int64_t cy,
                   int64_t radius,
                   int64_t start_angle,
                   int64_t end_angle,
                   int64_t color) {
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    vgfx_color_t col = rt_canvas_adv_color_to_vgfx_rgb(color);

    // Normalize angles (modulo avoids near-infinite loop for extreme values)
    start_angle = ((start_angle % 360) + 360) % 360;
    end_angle = ((end_angle % 360) + 360) % 360;

    if (end_angle <= start_angle)
        end_angle += 360;

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    int64_t y0 = rtg_sub_nonneg_sat64(cy, radius);
    int64_t y1 = rtg_add_sat64(cy, radius);
    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h) - 1;
    if (y0 < clip_y)
        y0 = clip_y;
    if (y1 > clip_y1)
        y1 = clip_y1;
    if (y1 < y0)
        return;

    long double r2 = (long double)radius * (long double)radius;
    int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w) - 1;
    for (int64_t py = y0; py <= y1; py++) {
        long double y_math = (long double)py - (long double)cy;
        long double rem = r2 - y_math * y_math;
        if (rem < 0.0L)
            continue;
        long double span = sqrtl(rem);
        int64_t x0 = rt_canvas_adv_floor_ld_to_i64_sat((long double)cx - span);
        int64_t x1 = rt_canvas_adv_ceil_ld_to_i64_sat((long double)cx + span);
        if (x0 < clip_x)
            x0 = clip_x;
        if (x1 > clip_x1)
            x1 = clip_x1;
        for (int64_t px = x0; px <= x1; px++) {
            long double x_math = (long double)px - (long double)cx;
            if (x_math == 0.0L && y_math == 0.0L) {
                vgfx_pset(canvas->gfx_win, (int32_t)px, (int32_t)py, col);
                continue;
            }

            long double angle = atan2l(y_math, x_math) * (180.0L / 3.14159265358979323846L);
            if (angle < 0.0L)
                angle += 360.0L;
            long double check_angle = angle;
            if (check_angle < (long double)start_angle)
                check_angle += 360.0L;
            if (check_angle >= (long double)start_angle && check_angle <= (long double)end_angle)
                vgfx_pset(canvas->gfx_win, (int32_t)px, (int32_t)py, col);
        }
    }
}

/// @brief Stroke (outline only) a circular arc segment — just the curve, no radii.
void rt_canvas_arc_frame(void *canvas_ptr,
                         int64_t cx,
                         int64_t cy,
                         int64_t radius,
                         int64_t start_angle,
                         int64_t end_angle,
                         int64_t color) {
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    // Normalize angles (modulo avoids near-infinite loop for extreme values)
    start_angle = ((start_angle % 360) + 360) % 360;
    end_angle = ((end_angle % 360) + 360) % 360;

    if (end_angle <= start_angle)
        end_angle += 360;

    // Draw arc outline by stepping through angles.
    int64_t span = end_angle - start_angle;
    int64_t steps = radius > INT64_MAX / span ? 4096 : (span * radius) / 30;
    if (steps < 10)
        steps = 10;
    if (steps > 4096)
        steps = 4096;

    int64_t prev_x = 0;
    int64_t prev_y = 0;
    long double radius_ld = (long double)radius;
    for (int64_t i = 0; i <= steps; i++) {
        long double angle_deg =
            (long double)start_angle + ((long double)span * (long double)i) / (long double)steps;
        long double angle = angle_deg * (3.14159265358979323846L / 180.0L);
        int64_t px = rt_canvas_adv_floor_ld_to_i64_sat((long double)cx + cosl(angle) * radius_ld);
        int64_t py = rt_canvas_adv_floor_ld_to_i64_sat((long double)cy + sinl(angle) * radius_ld);
        if (i > 0)
            rt_canvas_line(canvas_ptr, prev_x, prev_y, px, py, color);
        prev_x = px;
        prev_y = py;
    }
}

/// @brief Stroke a cubic Bezier curve from `(x1,y1)` to `(x4,y4)` with two control points.
///
/// Tessellates the curve via De Casteljau subdivision and renders
/// the resulting polyline as a thick line. Step count is chosen
/// adaptively from the chord/curve length ratio so straight curves
/// don't over-tessellate.
void rt_canvas_bezier(void *canvas_ptr,
                      int64_t x1,
                      int64_t y1,
                      int64_t cx,
                      int64_t cy,
                      int64_t x2,
                      int64_t y2,
                      int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    // Quadratic Bezier: B(t) = (1-t)^2*P1 + 2(1-t)t*C + t^2*P2.
    int64_t steps = 50;
    int64_t px = x1, py = y1;

    for (int64_t i = 1; i <= steps; i++) {
        long double t = (long double)i / (long double)steps;
        long double mt = 1.0L - t;
        int64_t nx = rt_canvas_adv_floor_ld_to_i64_sat(
            mt * mt * (long double)x1 + 2.0L * mt * t * (long double)cx + t * t * (long double)x2);
        int64_t ny = rt_canvas_adv_floor_ld_to_i64_sat(
            mt * mt * (long double)y1 + 2.0L * mt * t * (long double)cy + t * t * (long double)y2);

        rt_canvas_line(canvas_ptr, px, py, nx, ny, color);
        px = nx;
        py = ny;
    }
}

/// @brief Draw an open polyline through @p count (x,y) point pairs.
/// @details Connects consecutive vertices with rt_canvas_line; does not close
///          the path. No-op when fewer than 2 points are supplied.
static void rt_canvas_polyline_points(void *canvas_ptr,
                                      const int64_t *points,
                                      int64_t count,
                                      int64_t color) {
    if (!canvas_ptr || !points || count < 2)
        return;

    for (int64_t i = 0; i < count - 1; i++) {
        int64_t x1 = points[i * 2];
        int64_t y1 = points[i * 2 + 1];
        int64_t x2 = points[(i + 1) * 2];
        int64_t y2 = points[(i + 1) * 2 + 1];
        rt_canvas_line(canvas_ptr, x1, y1, x2, y2, color);
    }
}

/// @brief Draw a filled polygon through @p count (x,y) point pairs.
/// @details Uses a scanline fill: for each clipped scanline, computes edge
///          intersections (closing the last->first edge), insertion-sorts
///          them, and fills horizontal spans between intersection pairs.
///          Clipped to the canvas logical clip bounds. No-op for < 3 points.
static void rt_canvas_polygon_points(void *canvas_ptr,
                                     const int64_t *points,
                                     int64_t count,
                                     int64_t color) {
    if (!canvas_ptr || !points || count < 3)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;

    // Find bounding box
    int64_t min_y = points[1], max_y = points[1];
    for (int64_t i = 1; i < count; i++) {
        int64_t y = points[i * 2 + 1];
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
    }

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;

    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h) - 1;
    if (min_y < clip_y)
        min_y = clip_y;
    if (max_y > clip_y1)
        max_y = clip_y1;
    if (max_y < min_y)
        return;

    if ((uint64_t)count > SIZE_MAX / sizeof(int64_t))
        return;
    int64_t *intersections = (int64_t *)malloc((size_t)count * sizeof(int64_t));
    if (!intersections)
        return;

    // Scanline fill algorithm
    for (int64_t y = min_y; y <= max_y; y++) {
        // Find all edge intersections with this scanline
        int64_t num_intersections = 0;

        for (int64_t i = 0; i < count; i++) {
            int64_t j = (i + 1) % count;
            int64_t y1 = points[i * 2 + 1];
            int64_t y2 = points[j * 2 + 1];

            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y)) {
                int64_t x1 = points[i * 2];
                int64_t x2 = points[j * 2];
                int64_t x = rt_canvas_adv_interp_x(x1, y1, x2, y2, y);
                intersections[num_intersections++] = x;
            }
        }

        // Sort intersections
        for (int64_t i = 0; i < num_intersections - 1; i++) {
            for (int64_t j = i + 1; j < num_intersections; j++) {
                if (intersections[j] < intersections[i]) {
                    int64_t tmp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = tmp;
                }
            }
        }

        // Fill between pairs of intersections
        for (int64_t i = 0; i + 1 < num_intersections; i += 2) {
            rt_canvas_line(canvas_ptr, intersections[i], y, intersections[i + 1], y, color);
        }
    }

    free(intersections);
}

/// @brief Draw the outline (frame) of a polygon through @p count point pairs.
/// @details Connects all vertices and closes the last->first edge, without
///          filling. No-op for fewer than 3 points.
static void rt_canvas_polygon_frame_points(void *canvas_ptr,
                                           const int64_t *points,
                                           int64_t count,
                                           int64_t color) {
    if (!canvas_ptr || !points || count < 3)
        return;

    // Draw lines connecting all vertices, including back to start
    for (int64_t i = 0; i < count; i++) {
        int64_t j = (i + 1) % count;
        int64_t x1 = points[i * 2];
        int64_t y1 = points[i * 2 + 1];
        int64_t x2 = points[j * 2];
        int64_t y2 = points[j * 2 + 1];
        rt_canvas_line(canvas_ptr, x1, y1, x2, y2, color);
    }
}

/// @brief Polyline operation.
void rt_canvas_polyline(void *canvas_ptr, void *points_ptr, int64_t count, int64_t color) {
    const int64_t *points = NULL;
    if (!rt_canvas_points_checked(points_ptr, count, &points))
        return;
    rt_canvas_polyline_points(canvas_ptr, points, count, color);
}

/// @brief Polygon operation.
void rt_canvas_polygon(void *canvas_ptr, void *points_ptr, int64_t count, int64_t color) {
    const int64_t *points = NULL;
    if (!rt_canvas_points_checked(points_ptr, count, &points))
        return;
    rt_canvas_polygon_points(canvas_ptr, points, count, color);
}

/// @brief Frame the polygon.
void rt_canvas_polygon_frame(void *canvas_ptr, void *points_ptr, int64_t count, int64_t color) {
    const int64_t *points = NULL;
    if (!rt_canvas_points_checked(points_ptr, count, &points))
        return;
    rt_canvas_polygon_frame_points(canvas_ptr, points, count, color);
}

#ifndef RT_DRAWING_ADVANCED_NO_PATH2D
/// @brief Flatten a Path2D into a freshly-allocated interleaved (x,y) array.
/// @details Reads rt_path2d_count/get_x/get_y into a heap buffer the caller
///          must free(). Fails (returns 0, outputs cleared) when the path has
///          fewer than @p min_count points or the size would overflow.
/// @param path       Path2D handle.
/// @param min_count  Minimum required point count (2 for polyline, 3 polygon).
/// @param points_out Out: malloc'd count*2 int64 array (caller frees).
/// @param count_out  Out: number of points written.
/// @return 1 on success, 0 on failure.
static int8_t rt_canvas_path_points(void *path,
                                    int64_t min_count,
                                    int64_t **points_out,
                                    int64_t *count_out) {
    if (points_out)
        *points_out = NULL;
    if (count_out)
        *count_out = 0;
    if (!path)
        return 0;

    int64_t count = rt_path2d_count(path);
    if (count < min_count || count > INT64_MAX / 2)
        return 0;
    if ((uint64_t)count > SIZE_MAX / (2u * sizeof(int64_t)))
        return 0;

    size_t point_values = (size_t)count * 2u;
    int64_t *points = (int64_t *)malloc(point_values * sizeof(int64_t));
    if (!points)
        return 0;
    for (int64_t i = 0; i < count; ++i) {
        points[i * 2] = rt_path2d_get_x(path, i);
        points[i * 2 + 1] = rt_path2d_get_y(path, i);
    }
    if (points_out)
        *points_out = points;
    if (count_out)
        *count_out = count;
    return 1;
}

void rt_canvas_polyline_path(void *canvas_ptr, void *path, int64_t color) {
    int64_t *points = NULL;
    int64_t count = 0;
    if (!rt_canvas_path_points(path, 2, &points, &count))
        return;
    rt_canvas_polyline_points(canvas_ptr, points, count, color);
    free(points);
}

void rt_canvas_polygon_path(void *canvas_ptr, void *path, int64_t color) {
    int64_t *points = NULL;
    int64_t count = 0;
    if (!rt_canvas_path_points(path, 3, &points, &count))
        return;
    rt_canvas_polygon_points(canvas_ptr, points, count, color);
    free(points);
}

void rt_canvas_polygon_frame_path(void *canvas_ptr, void *path, int64_t color) {
    int64_t *points = NULL;
    int64_t count = 0;
    if (!rt_canvas_path_points(path, 3, &points, &count))
        return;
    rt_canvas_polygon_frame_points(canvas_ptr, points, count, color);
    free(points);
}
#else
void rt_canvas_polyline_path(void *canvas_ptr, void *path, int64_t color) {
    (void)canvas_ptr;
    (void)path;
    (void)color;
}

void rt_canvas_polygon_path(void *canvas_ptr, void *path, int64_t color) {
    (void)canvas_ptr;
    (void)path;
    (void)color;
}

void rt_canvas_polygon_frame_path(void *canvas_ptr, void *path, int64_t color) {
    (void)canvas_ptr;
    (void)path;
    (void)color;
}
#endif

//=============================================================================
// Gradients
//=============================================================================

/// @brief Fill a rectangle with a horizontal linear gradient between two colors.
///
/// Each column of pixels is a linear interpolation between
/// `color_left` and `color_right`. The blend is per-channel in
/// 0xRRGGBBAA format (alpha included).
void rt_canvas_gradient_h(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2) {
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    int64_t orig_x = x;
    int64_t orig_w = w;
    if (!rt_canvas_clip_intersect_logical(canvas, &x, &y, &w, &h))
        return;

    // Precompute gradient colours for each column, then blit each row of height h
    // with a single memcpy-equivalent pass — avoids w*vgfx_line() call overhead.
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb)) {
        // Fallback: per-column vgfx_line for mock/headless contexts.
        // vgfx_line auto-scales via coord_scale, so pass logical coords.
        int64_t w_minus1 = orig_w > 1 ? orig_w - 1 : 1;
        for (int64_t col = 0; col < w; col++) {
            int64_t logical_x = rtg_add_sat64(x, col);
            int64_t gradient_col = rtg_add_sat64(logical_x, -orig_x);
            int64_t color = rt_color_lerp(c1, c2, rtg_mul_sat64(gradient_col, 100) / w_minus1);
            vgfx_line(canvas->gfx_win,
                      rtg_clamp_i64_to_i32(logical_x),
                      rtg_clamp_i64_to_i32(y),
                      rtg_clamp_i64_to_i32(logical_x),
                      rtg_clamp_i64_to_i32(rtg_add_sat64(y, h - 1)),
                      rt_canvas_adv_color_to_vgfx_rgb(color));
        }
        return;
    }

    float scale = vgfx_window_get_scale(canvas->gfx_win);
    int64_t px0 = rtg_scale_up_i64(x, scale);
    int64_t px1 = rtg_scale_up_i64(rtg_add_sat64(x, w), scale);
    int64_t py0 = rtg_scale_up_i64(y, scale);
    int64_t py1 = rtg_scale_up_i64(rtg_add_sat64(y, h), scale);
    if (px0 < 0)
        px0 = 0;
    if (py0 < 0)
        py0 = 0;
    if (px1 > fb.width)
        px1 = fb.width;
    if (py1 > fb.height)
        py1 = fb.height;
    if (px1 <= px0 || py1 <= py0)
        return;

    int64_t draw_w = px1 - px0;
    if ((uint64_t)draw_w > SIZE_MAX / 4u)
        return;
    size_t row_bytes = (size_t)draw_w * 4u;
    uint8_t *row_buf = (uint8_t *)malloc(row_bytes);
    if (!row_buf)
        return;

    int64_t w_minus1 = orig_w > 1 ? orig_w - 1 : 1;
    memset(row_buf, 0, row_bytes);
    for (int64_t col = 0; col < w; col++) {
        int64_t logical_x = rtg_add_sat64(x, col);
        int64_t col_px0 = rtg_scale_up_i64(logical_x, scale);
        int64_t col_px1 = rtg_scale_up_i64(rtg_add_sat64(logical_x, 1), scale);
        if (col_px1 <= col_px0)
            col_px1 = col_px0 + 1;
        if (col_px0 < px0)
            col_px0 = px0;
        if (col_px1 > px1)
            col_px1 = px1;
        if (col_px1 <= col_px0)
            continue;

        int64_t gradient_col = rtg_add_sat64(logical_x, -orig_x);
        uint32_t rgba = rt_pixels_color_to_rgba(
            rt_color_lerp(c1, c2, rtg_mul_sat64(gradient_col, 100) / w_minus1));
        uint8_t cr = (uint8_t)((rgba >> 24) & 0xFF);
        uint8_t cg = (uint8_t)((rgba >> 16) & 0xFF);
        uint8_t cb = (uint8_t)((rgba >> 8) & 0xFF);
        uint8_t ca = (uint8_t)(rgba & 0xFF);
        for (int64_t px = col_px0; px < col_px1; px++) {
            size_t idx = (size_t)(px - px0) * 4u;
            row_buf[idx + 0u] = cr;
            row_buf[idx + 1u] = cg;
            row_buf[idx + 2u] = cb;
            row_buf[idx + 3u] = ca;
        }
    }

    for (int64_t row = py0; row < py1; row++)
        memcpy(&fb.pixels[(size_t)row * (size_t)fb.stride + (size_t)px0 * 4u],
               row_buf,
               (size_t)draw_w * 4u);

    free(row_buf);
}

/// @brief Fill a rectangle with a vertical linear gradient between two colors.
/// @see rt_canvas_gradient_h — same idea, rows instead of columns.
void rt_canvas_gradient_v(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2) {
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    int64_t orig_y = y;
    int64_t orig_h = h;
    if (!rt_canvas_clip_intersect_logical(canvas, &x, &y, &w, &h))
        return;

    // Write each row directly into the framebuffer — one colour per row, no per-row
    // vgfx_line() overhead.
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb)) {
        // Fallback: vgfx_line auto-scales via coord_scale, so pass logical coords.
        int64_t h_minus1 = orig_h > 1 ? orig_h - 1 : 1;
        for (int64_t row = 0; row < h; row++) {
            int64_t logical_y = rtg_add_sat64(y, row);
            int64_t gradient_row = rtg_add_sat64(logical_y, -orig_y);
            int64_t color = rt_color_lerp(c1, c2, rtg_mul_sat64(gradient_row, 100) / h_minus1);
            vgfx_line(canvas->gfx_win,
                      rtg_clamp_i64_to_i32(x),
                      rtg_clamp_i64_to_i32(logical_y),
                      rtg_clamp_i64_to_i32(rtg_add_sat64(x, w - 1)),
                      rtg_clamp_i64_to_i32(logical_y),
                      rt_canvas_adv_color_to_vgfx_rgb(color));
        }
        return;
    }

    float scale = vgfx_window_get_scale(canvas->gfx_win);
    int64_t px0 = rtg_scale_up_i64(x, scale);
    int64_t px1 = rtg_scale_up_i64(rtg_add_sat64(x, w), scale);
    int64_t py0 = rtg_scale_up_i64(y, scale);
    int64_t py1 = rtg_scale_up_i64(rtg_add_sat64(y, h), scale);
    if (px0 < 0)
        px0 = 0;
    if (py0 < 0)
        py0 = 0;
    if (px1 > fb.width)
        px1 = fb.width;
    if (py1 > fb.height)
        py1 = fb.height;
    if (px1 <= px0 || py1 <= py0)
        return;

    int64_t draw_w = px1 - px0;
    int64_t h_minus1 = orig_h > 1 ? orig_h - 1 : 1;

    for (int64_t row = 0; row < h; row++) {
        int64_t logical_y = rtg_add_sat64(y, row);
        int64_t row_py0 = rtg_scale_up_i64(logical_y, scale);
        int64_t row_py1 = rtg_scale_up_i64(rtg_add_sat64(logical_y, 1), scale);
        if (row_py1 <= row_py0)
            row_py1 = row_py0 + 1;
        if (row_py0 < py0)
            row_py0 = py0;
        if (row_py1 > py1)
            row_py1 = py1;
        if (row_py1 <= row_py0)
            continue;

        int64_t gradient_row = rtg_add_sat64(logical_y, -orig_y);
        uint32_t rgba = rt_pixels_color_to_rgba(
            rt_color_lerp(c1, c2, rtg_mul_sat64(gradient_row, 100) / h_minus1));
        uint8_t cr = (uint8_t)((rgba >> 24) & 0xFF);
        uint8_t cg = (uint8_t)((rgba >> 16) & 0xFF);
        uint8_t cb = (uint8_t)((rgba >> 8) & 0xFF);
        uint8_t ca = (uint8_t)(rgba & 0xFF);
        for (int64_t py = row_py0; py < row_py1; py++) {
            uint8_t *dst = &fb.pixels[(size_t)py * (size_t)fb.stride + (size_t)px0 * 4u];
            for (int64_t i = 0; i < draw_w; i++) {
                dst[i * 4 + 0] = cr;
                dst[i * 4 + 1] = cg;
                dst[i * 4 + 2] = cb;
                dst[i * 4 + 3] = ca;
            }
        }
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
