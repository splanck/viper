//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_drawing_advanced.c
// Purpose: Advanced drawing primitives and color functions for the Canvas
//   runtime. Includes thick_line, round_box, round_frame, flood_fill, triangle,
//   triangle_frame, ellipse, ellipse_frame, arc, arc_frame, bezier, polyline,
//   polygon, polygon_frame, gradient_h, gradient_v, and all rt_color_*
//   functions (HSL conversion, lerp, brighten, darken, hex, etc.).
//
// Key invariants:
//   - All functions guard against NULL canvas_ptr and NULL gfx_win.
//   - Colors use 0x00RRGGBB format; HSL uses H=0-360, S=0-100, L=0-100.
//   - flood_fill uses a dynamically-growing stack (O(1) initial alloc).
//
// Ownership/Lifetime:
//   - flood_fill allocates and frees its own stack buffers.
//   - No other functions allocate; they operate on existing canvas handles.
//
// Links: rt_graphics_internal.h, rt_graphics.h (public API),
//        vgfx.h (ViperGFX C API)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_internal.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// Extended Drawing Primitives
//=============================================================================

void rt_canvas_thick_line(void *canvas_ptr,
                          int64_t x1,
                          int64_t y1,
                          int64_t x2,
                          int64_t y2,
                          int64_t thickness,
                          int64_t color)
{
    if (!canvas_ptr || thickness <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    if (thickness == 1)
    {
        // Use standard line drawing
        vgfx_line(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)x2, (int32_t)y2, col);
        return;
    }

    // Draw thick line as a filled parallelogram body + two round endcap circles.
    // This is O((length + r) * r) vs O(length * r^2) for circle-per-step,
    // which is a factor-of-r speedup for large thickness values.
    int64_t half = thickness / 2;

    if (x1 == x2 && y1 == y2)
    {
        vgfx_fill_circle(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)half, col);
        return;
    }

    // Round endcaps
    vgfx_fill_circle(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)half, col);
    vgfx_fill_circle(canvas->gfx_win, (int32_t)x2, (int32_t)y2, (int32_t)half, col);

    // Parallelogram body: four corners offset by perpendicular half-width.
    double ldx = (double)(x2 - x1);
    double ldy = (double)(y2 - y1);
    double len = sqrt(ldx * ldx + ldy * ldy);
    // Perpendicular unit vector (rotated 90 degrees)
    double px = (-ldy / len) * (double)half;
    double py = (ldx / len) * (double)half;

    // Four corners of the parallelogram
    double ax = (double)x1 + px, ay = (double)y1 + py;
    double bx = (double)x1 - px, by = (double)y1 - py;
    double cx = (double)x2 + px, cy = (double)y2 + py;
    double dx = (double)x2 - px, dy_c = (double)y2 - py;

    // Scanline fill the parallelogram (convex 4-vertex polygon).
    int32_t y_lo = (int32_t)floor(fmin(fmin(ay, by), fmin(cy, dy_c)));
    int32_t y_hi = (int32_t)ceil(fmax(fmax(ay, by), fmax(cy, dy_c)));

    for (int32_t scan_y = y_lo; scan_y <= y_hi; scan_y++)
    {
        double sv = (double)scan_y;
        double x_min = 1e18, x_max = -1e18;
        double xi;

        // Edge A->C
        if (fmin(ay, cy) <= sv && sv <= fmax(ay, cy) && ay != cy)
        {
            xi = ax + (cx - ax) * (sv - ay) / (cy - ay);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }
        // Edge C->D
        if (fmin(cy, dy_c) <= sv && sv <= fmax(cy, dy_c) && cy != dy_c)
        {
            xi = cx + (dx - cx) * (sv - cy) / (dy_c - cy);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }
        // Edge D->B
        if (fmin(dy_c, by) <= sv && sv <= fmax(dy_c, by) && dy_c != by)
        {
            xi = dx + (bx - dx) * (sv - dy_c) / (by - dy_c);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }
        // Edge B->A
        if (fmin(by, ay) <= sv && sv <= fmax(by, ay) && by != ay)
        {
            xi = bx + (ax - bx) * (sv - by) / (ay - by);
            if (xi < x_min)
                x_min = xi;
            if (xi > x_max)
                x_max = xi;
        }

        if (x_max >= x_min)
        {
            vgfx_line(
                canvas->gfx_win, (int32_t)floor(x_min), scan_y, (int32_t)ceil(x_max), scan_y, col);
        }
    }
}

void rt_canvas_round_box(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color)
{
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Clamp radius to half of smallest dimension
    int64_t max_radius = rtg_min64(w, h) / 2;
    if (radius > max_radius)
        radius = max_radius;
    if (radius < 0)
        radius = 0;

    if (radius == 0)
    {
        // Just draw a regular filled rectangle
        vgfx_fill_rect(canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, col);
        return;
    }

    // Draw the center rectangle
    vgfx_fill_rect(canvas->gfx_win,
                   (int32_t)(x + radius),
                   (int32_t)y,
                   (int32_t)(w - 2 * radius),
                   (int32_t)h,
                   col);

    // Draw left and right rectangles
    vgfx_fill_rect(canvas->gfx_win,
                   (int32_t)x,
                   (int32_t)(y + radius),
                   (int32_t)radius,
                   (int32_t)(h - 2 * radius),
                   col);
    vgfx_fill_rect(canvas->gfx_win,
                   (int32_t)(x + w - radius),
                   (int32_t)(y + radius),
                   (int32_t)radius,
                   (int32_t)(h - 2 * radius),
                   col);

    // Draw four corner filled circles
    vgfx_fill_circle(
        canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + radius), (int32_t)radius, col);
    vgfx_fill_circle(canvas->gfx_win,
                     (int32_t)(x + w - radius - 1),
                     (int32_t)(y + radius),
                     (int32_t)radius,
                     col);
    vgfx_fill_circle(canvas->gfx_win,
                     (int32_t)(x + radius),
                     (int32_t)(y + h - radius - 1),
                     (int32_t)radius,
                     col);
    vgfx_fill_circle(canvas->gfx_win,
                     (int32_t)(x + w - radius - 1),
                     (int32_t)(y + h - radius - 1),
                     (int32_t)radius,
                     col);
}

void rt_canvas_round_frame(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color)
{
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Clamp radius
    int64_t max_radius = rtg_min64(w, h) / 2;
    if (radius > max_radius)
        radius = max_radius;
    if (radius < 0)
        radius = 0;

    if (radius == 0)
    {
        // Draw regular rectangle outline
        vgfx_line(canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)(x + w - 1), (int32_t)y, col);
        vgfx_line(canvas->gfx_win,
                  (int32_t)x,
                  (int32_t)(y + h - 1),
                  (int32_t)(x + w - 1),
                  (int32_t)(y + h - 1),
                  col);
        vgfx_line(canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)x, (int32_t)(y + h - 1), col);
        vgfx_line(canvas->gfx_win,
                  (int32_t)(x + w - 1),
                  (int32_t)y,
                  (int32_t)(x + w - 1),
                  (int32_t)(y + h - 1),
                  col);
        return;
    }

    // Draw horizontal lines (top and bottom, excluding corners)
    vgfx_line(canvas->gfx_win,
              (int32_t)(x + radius),
              (int32_t)y,
              (int32_t)(x + w - radius - 1),
              (int32_t)y,
              col);
    vgfx_line(canvas->gfx_win,
              (int32_t)(x + radius),
              (int32_t)(y + h - 1),
              (int32_t)(x + w - radius - 1),
              (int32_t)(y + h - 1),
              col);

    // Draw vertical lines (left and right, excluding corners)
    vgfx_line(canvas->gfx_win,
              (int32_t)x,
              (int32_t)(y + radius),
              (int32_t)x,
              (int32_t)(y + h - radius - 1),
              col);
    vgfx_line(canvas->gfx_win,
              (int32_t)(x + w - 1),
              (int32_t)(y + radius),
              (int32_t)(x + w - 1),
              (int32_t)(y + h - radius - 1),
              col);

    // Draw corner arcs using circle algorithm
    vgfx_circle(
        canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + radius), (int32_t)radius, col);
    vgfx_circle(canvas->gfx_win,
                (int32_t)(x + w - radius - 1),
                (int32_t)(y + radius),
                (int32_t)radius,
                col);
    vgfx_circle(canvas->gfx_win,
                (int32_t)(x + radius),
                (int32_t)(y + h - radius - 1),
                (int32_t)radius,
                col);
    vgfx_circle(canvas->gfx_win,
                (int32_t)(x + w - radius - 1),
                (int32_t)(y + h - radius - 1),
                (int32_t)radius,
                col);
}

void rt_canvas_flood_fill(void *canvas_ptr, int64_t start_x, int64_t start_y, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    // Scale start coordinates to physical pixels
    float cs = vgfx_window_get_scale(canvas->gfx_win);
    if (cs > 1.0f)
    {
        start_x = (int64_t)(start_x * cs);
        start_y = (int64_t)(start_y * cs);
    }

    // Bounds check (physical)
    if (start_x < 0 || start_x >= fb.width || start_y < 0 || start_y >= fb.height)
        return;

    // Get the target color (color to replace)
    uint8_t *start_pixel = &fb.pixels[start_y * fb.stride + start_x * 4];
    uint32_t target_r = start_pixel[0];
    uint32_t target_g = start_pixel[1];
    uint32_t target_b = start_pixel[2];

    // Get fill color components
    uint8_t fill_r = (color >> 16) & 0xFF;
    uint8_t fill_g = (color >> 8) & 0xFF;
    uint8_t fill_b = color & 0xFF;

    // Don't fill if target color is the same as fill color
    if (target_r == fill_r && target_g == fill_g && target_b == fill_b)
        return;

    /* O-03: Use a dynamically-growing stack starting at 4096 entries
     * instead of pre-allocating the worst-case (width * height) upfront.
     * This avoids O(r^2) allocations for small fill regions. */
    int64_t stack_cap = 4096;
    int64_t *stack_x = (int64_t *)malloc((size_t)stack_cap * sizeof(int64_t));
    int64_t *stack_y = (int64_t *)malloc((size_t)stack_cap * sizeof(int64_t));
    if (!stack_x || !stack_y)
    {
        free(stack_x);
        free(stack_y);
        return;
    }

    int64_t stack_top = 0;
    stack_x[stack_top] = start_x;
    stack_y[stack_top] = start_y;
    stack_top++;

    while (stack_top > 0)
    {
        stack_top--;
        int64_t x = stack_x[stack_top];
        int64_t y = stack_y[stack_top];

        // Skip if out of bounds
        if (x < 0 || x >= fb.width || y < 0 || y >= fb.height)
            continue;

        uint8_t *pixel = &fb.pixels[y * fb.stride + x * 4];

        // Skip if not target color
        if (pixel[0] != target_r || pixel[1] != target_g || pixel[2] != target_b)
            continue;

        // Fill this pixel
        pixel[0] = fill_r;
        pixel[1] = fill_g;
        pixel[2] = fill_b;
        pixel[3] = 255;

        // Grow stack if needed before pushing 4 neighbors
        if (stack_top + 4 > stack_cap)
        {
            int64_t new_cap = stack_cap * 2;
            int64_t max_cap = fb.width * fb.height + 4;
            if (new_cap > max_cap)
                new_cap = max_cap;
            int64_t *nx = (int64_t *)realloc(stack_x, (size_t)new_cap * sizeof(int64_t));
            int64_t *ny = (int64_t *)realloc(stack_y, (size_t)new_cap * sizeof(int64_t));
            if (!nx || !ny)
            {
                free(nx ? nx : stack_x);
                free(ny ? ny : stack_y);
                return;
            }
            stack_x = nx;
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

void rt_canvas_triangle(void *canvas_ptr,
                        int64_t x1,
                        int64_t y1,
                        int64_t x2,
                        int64_t y2,
                        int64_t x3,
                        int64_t y3,
                        int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Sort vertices by y-coordinate (y1 <= y2 <= y3)
    if (y1 > y2)
    {
        int64_t tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    if (y2 > y3)
    {
        int64_t tmp = x2;
        x2 = x3;
        x3 = tmp;
        tmp = y2;
        y2 = y3;
        y3 = tmp;
    }
    if (y1 > y2)
    {
        int64_t tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    // Handle degenerate cases
    if (y1 == y3)
    {
        // Horizontal line
        int64_t min_x = rtg_min64(rtg_min64(x1, x2), x3);
        int64_t max_x = rtg_max64(rtg_max64(x1, x2), x3);
        vgfx_line(canvas->gfx_win, (int32_t)min_x, (int32_t)y1, (int32_t)max_x, (int32_t)y1, col);
        return;
    }

    // Fill triangle using scanline algorithm
    for (int64_t y = y1; y <= y3; y++)
    {
        int64_t xa, xb;

        if (y < y2)
        {
            // Upper part of triangle
            if (y2 != y1)
                xa = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
            else
                xa = x1;
            xa = x1 + (x2 - x1) * (y - y1) / rtg_max64(y2 - y1, 1);
        }
        else
        {
            // Lower part of triangle
            if (y3 != y2)
                xa = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
            else
                xa = x2;
        }

        // Long edge from y1 to y3
        if (y3 != y1)
            xb = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
        else
            xb = x1;

        if (xa > xb)
        {
            int64_t tmp = xa;
            xa = xb;
            xb = tmp;
        }

        vgfx_line(canvas->gfx_win, (int32_t)xa, (int32_t)y, (int32_t)xb, (int32_t)y, col);
    }
}

void rt_canvas_triangle_frame(void *canvas_ptr,
                              int64_t x1,
                              int64_t y1,
                              int64_t x2,
                              int64_t y2,
                              int64_t x3,
                              int64_t y3,
                              int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Draw three lines
    vgfx_line(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)x2, (int32_t)y2, col);
    vgfx_line(canvas->gfx_win, (int32_t)x2, (int32_t)y2, (int32_t)x3, (int32_t)y3, col);
    vgfx_line(canvas->gfx_win, (int32_t)x3, (int32_t)y3, (int32_t)x1, (int32_t)y1, col);
}

void rt_canvas_ellipse(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
{
    if (!canvas_ptr || rx <= 0 || ry <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // If rx == ry, it's a circle
    if (rx == ry)
    {
        vgfx_fill_circle(canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)rx, col);
        return;
    }

    // Bresenham's ellipse algorithm (filled)
    int64_t rx2 = rx * rx;
    int64_t ry2 = ry * ry;
    int64_t two_rx2 = 2 * rx2;
    int64_t two_ry2 = 2 * ry2;

    int64_t x = 0;
    int64_t y = ry;
    int64_t px = 0;
    int64_t py = two_rx2 * y;

    // Region 1
    int64_t p = ry2 - (rx2 * ry) + (rx2 / 4);
    while (px < py)
    {
        // Draw horizontal scanlines
        vgfx_line(canvas->gfx_win,
                  (int32_t)(cx - x),
                  (int32_t)(cy + y),
                  (int32_t)(cx + x),
                  (int32_t)(cy + y),
                  col);
        vgfx_line(canvas->gfx_win,
                  (int32_t)(cx - x),
                  (int32_t)(cy - y),
                  (int32_t)(cx + x),
                  (int32_t)(cy - y),
                  col);

        x++;
        px += two_ry2;
        if (p < 0)
            p += ry2 + px;
        else
        {
            y--;
            py -= two_rx2;
            p += ry2 + px - py;
        }
    }

    // Region 2
    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2 + ry2 / 4;
    while (y >= 0)
    {
        vgfx_line(canvas->gfx_win,
                  (int32_t)(cx - x),
                  (int32_t)(cy + y),
                  (int32_t)(cx + x),
                  (int32_t)(cy + y),
                  col);
        vgfx_line(canvas->gfx_win,
                  (int32_t)(cx - x),
                  (int32_t)(cy - y),
                  (int32_t)(cx + x),
                  (int32_t)(cy - y),
                  col);

        y--;
        py -= two_rx2;
        if (p > 0)
            p += rx2 - py;
        else
        {
            x++;
            px += two_ry2;
            p += rx2 - py + px;
        }
    }
}

void rt_canvas_ellipse_frame(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
{
    if (!canvas_ptr || rx <= 0 || ry <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // If rx == ry, it's a circle
    if (rx == ry)
    {
        vgfx_circle(canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)rx, col);
        return;
    }

    // Bresenham's ellipse algorithm (outline)
    int64_t rx2 = rx * rx;
    int64_t ry2 = ry * ry;
    int64_t two_rx2 = 2 * rx2;
    int64_t two_ry2 = 2 * ry2;

    int64_t x = 0;
    int64_t y = ry;
    int64_t px = 0;
    int64_t py = two_rx2 * y;

    // Plot initial points
    vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy + y), col);
    vgfx_pset(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy + y), col);
    vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy - y), col);
    vgfx_pset(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy - y), col);

    // Region 1
    int64_t p = ry2 - (rx2 * ry) + (rx2 / 4);
    while (px < py)
    {
        x++;
        px += two_ry2;
        if (p < 0)
            p += ry2 + px;
        else
        {
            y--;
            py -= two_rx2;
            p += ry2 + px - py;
        }

        vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy + y), col);
        vgfx_pset(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy + y), col);
        vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy - y), col);
        vgfx_pset(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy - y), col);
    }

    // Region 2
    p = ry2 * (x * x + x) + rx2 * (y - 1) * (y - 1) - rx2 * ry2 + ry2 / 4;
    while (y >= 0)
    {
        y--;
        py -= two_rx2;
        if (p > 0)
            p += rx2 - py;
        else
        {
            x++;
            px += two_ry2;
            p += rx2 - py + px;
        }

        vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy + y), col);
        vgfx_pset(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy + y), col);
        vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy - y), col);
        vgfx_pset(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy - y), col);
    }
}

//=============================================================================
// Advanced Curves & Shapes
//=============================================================================

void rt_canvas_arc(void *canvas_ptr,
                   int64_t cx,
                   int64_t cy,
                   int64_t radius,
                   int64_t start_angle,
                   int64_t end_angle,
                   int64_t color)
{
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Normalize angles
    while (start_angle < 0)
        start_angle += 360;
    while (end_angle < 0)
        end_angle += 360;
    start_angle = start_angle % 360;
    end_angle = end_angle % 360;

    if (end_angle <= start_angle)
        end_angle += 360;

    // Draw filled arc using scanline approach
    for (int64_t y = -radius; y <= radius; y++)
    {
        for (int64_t x = -radius; x <= radius; x++)
        {
            if (x * x + y * y <= radius * radius)
            {
                // Calculate angle of this point (in degrees, 0 = right)
                int64_t angle;
                if (x == 0 && y == 0)
                {
                    vgfx_pset(canvas->gfx_win, (int32_t)(cx), (int32_t)(cy), col);
                    continue;
                }

                // atan2 approximation
                if (x >= 0 && y >= 0)
                    angle = (y * 90) / (rtg_abs64(x) + rtg_abs64(y));
                else if (x < 0 && y >= 0)
                    angle = 90 + (rtg_abs64(x) * 90) / (rtg_abs64(x) + rtg_abs64(y));
                else if (x < 0 && y < 0)
                    angle = 180 + (rtg_abs64(y) * 90) / (rtg_abs64(x) + rtg_abs64(y));
                else
                    angle = 270 + (rtg_abs64(x) * 90) / (rtg_abs64(x) + rtg_abs64(y));

                // Check if angle is within arc range
                int64_t check_angle = angle;
                if (check_angle < start_angle)
                    check_angle += 360;

                if (check_angle >= start_angle && check_angle <= end_angle)
                {
                    vgfx_pset(canvas->gfx_win, (int32_t)(cx + x), (int32_t)(cy - y), col);
                }
            }
        }
    }
}

void rt_canvas_arc_frame(void *canvas_ptr,
                         int64_t cx,
                         int64_t cy,
                         int64_t radius,
                         int64_t start_angle,
                         int64_t end_angle,
                         int64_t color)
{
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Normalize angles
    while (start_angle < 0)
        start_angle += 360;
    while (end_angle < 0)
        end_angle += 360;
    start_angle = start_angle % 360;
    end_angle = end_angle % 360;

    if (end_angle <= start_angle)
        end_angle += 360;

    // Draw arc outline by stepping through angles
    int64_t steps = (end_angle - start_angle) * radius / 30;
    if (steps < 10)
        steps = 10;
    if (steps > 360)
        steps = 360;

    for (int64_t i = 0; i <= steps; i++)
    {
        int64_t angle = start_angle + (end_angle - start_angle) * i / steps;
        int64_t px = cx + (radius * rtg_cos_deg_fp(angle)) / 1024;
        int64_t py = cy - (radius * rtg_sin_deg_fp(angle)) / 1024;
        vgfx_pset(canvas->gfx_win, (int32_t)px, (int32_t)py, col);
    }
}

void rt_canvas_bezier(void *canvas_ptr,
                      int64_t x1,
                      int64_t y1,
                      int64_t cx,
                      int64_t cy,
                      int64_t x2,
                      int64_t y2,
                      int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;

    // Quadratic Bezier: B(t) = (1-t)^2*P1 + 2(1-t)t*C + t^2*P2
    // Use fixed-point arithmetic (t * 1024)
    int64_t steps = 50;
    int64_t px = x1, py = y1;

    for (int64_t i = 1; i <= steps; i++)
    {
        int64_t t = (i * 1024) / steps;
        int64_t t2 = (t * t) / 1024;
        int64_t mt = 1024 - t;
        int64_t mt2 = (mt * mt) / 1024;
        int64_t tmt2 = (2 * t * mt) / 1024;

        int64_t nx = (mt2 * x1 + tmt2 * cx + t2 * x2) / 1024;
        int64_t ny = (mt2 * y1 + tmt2 * cy + t2 * y2) / 1024;

        vgfx_line(canvas->gfx_win, (int32_t)px, (int32_t)py, (int32_t)nx, (int32_t)ny, col);
        px = nx;
        py = ny;
    }
}

void rt_canvas_polyline(void *canvas_ptr, void *points_ptr, int64_t count, int64_t color)
{
    if (!canvas_ptr || !points_ptr || count < 2)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;
    int64_t *points = (int64_t *)points_ptr;

    for (int64_t i = 0; i < count - 1; i++)
    {
        int64_t x1 = points[i * 2];
        int64_t y1 = points[i * 2 + 1];
        int64_t x2 = points[(i + 1) * 2];
        int64_t y2 = points[(i + 1) * 2 + 1];
        vgfx_line(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)x2, (int32_t)y2, col);
    }
}

void rt_canvas_polygon(void *canvas_ptr, void *points_ptr, int64_t count, int64_t color)
{
    if (!canvas_ptr || !points_ptr || count < 3)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;
    int64_t *points = (int64_t *)points_ptr;

    // Find bounding box
    int64_t min_y = points[1], max_y = points[1];
    for (int64_t i = 1; i < count; i++)
    {
        int64_t y = points[i * 2 + 1];
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
    }

    // Scanline fill algorithm
    for (int64_t y = min_y; y <= max_y; y++)
    {
        // Find all edge intersections with this scanline
        int64_t intersections[64];
        int64_t num_intersections = 0;

        for (int64_t i = 0; i < count && num_intersections < 62; i++)
        {
            int64_t j = (i + 1) % count;
            int64_t y1 = points[i * 2 + 1];
            int64_t y2 = points[j * 2 + 1];

            if ((y1 <= y && y2 > y) || (y2 <= y && y1 > y))
            {
                int64_t x1 = points[i * 2];
                int64_t x2 = points[j * 2];
                int64_t x = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
                intersections[num_intersections++] = x;
            }
        }

        // Sort intersections
        for (int64_t i = 0; i < num_intersections - 1; i++)
        {
            for (int64_t j = i + 1; j < num_intersections; j++)
            {
                if (intersections[j] < intersections[i])
                {
                    int64_t tmp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = tmp;
                }
            }
        }

        // Fill between pairs of intersections
        for (int64_t i = 0; i + 1 < num_intersections; i += 2)
        {
            vgfx_line(canvas->gfx_win,
                      (int32_t)intersections[i],
                      (int32_t)y,
                      (int32_t)intersections[i + 1],
                      (int32_t)y,
                      col);
        }
    }
}

void rt_canvas_polygon_frame(void *canvas_ptr, void *points_ptr, int64_t count, int64_t color)
{
    if (!canvas_ptr || !points_ptr || count < 3)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_color_t col = (vgfx_color_t)color;
    int64_t *points = (int64_t *)points_ptr;

    // Draw lines connecting all vertices, including back to start
    for (int64_t i = 0; i < count; i++)
    {
        int64_t j = (i + 1) % count;
        int64_t x1 = points[i * 2];
        int64_t y1 = points[i * 2 + 1];
        int64_t x2 = points[j * 2];
        int64_t y2 = points[j * 2 + 1];
        vgfx_line(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)x2, (int32_t)y2, col);
    }
}

//=============================================================================
// Gradients
//=============================================================================

void rt_canvas_gradient_h(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2)
{
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    // Precompute gradient colours for each column, then blit each row of height h
    // with a single memcpy-equivalent pass — avoids w*vgfx_line() call overhead.
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
    {
        // Fallback: per-column vgfx_line for mock/headless contexts.
        // vgfx_line auto-scales via coord_scale, so pass logical coords.
        for (int64_t col = 0; col < w; col++)
        {
            int64_t color = rt_color_lerp(c1, c2, col * 100 / (w - 1 > 0 ? w - 1 : 1));
            vgfx_line(canvas->gfx_win,
                      (int32_t)(x + col),
                      (int32_t)y,
                      (int32_t)(x + col),
                      (int32_t)(y + h - 1),
                      (vgfx_color_t)color);
        }
        return;
    }

    // Scale logical coordinates to physical framebuffer space.
    float cs = vgfx_window_get_scale(canvas->gfx_win);
    int32_t isf = (cs > 1.0f) ? (int32_t)cs : 1;
    int64_t px = x * isf, py = y * isf;
    int64_t pw = w * isf, ph = h * isf;

    // Build a single gradient row (clamped to physical framebuffer bounds).
    int64_t x0 = px < 0 ? 0 : px;
    int64_t x1_clip = px + pw > fb.width ? fb.width : px + pw;
    int64_t y0 = py < 0 ? 0 : py;
    int64_t y1_clip = py + ph > fb.height ? fb.height : py + ph;

    if (x1_clip <= x0 || y1_clip <= y0)
        return;

    int64_t draw_w = x1_clip - x0;
    uint8_t *row_buf = (uint8_t *)malloc((size_t)(draw_w * 4));
    if (!row_buf)
        return;

    // Gradient interpolation uses logical width so colour distribution is unchanged.
    int64_t w_minus1 = w > 1 ? w - 1 : 1;
    for (int64_t i = 0; i < draw_w; i++)
    {
        // Map physical column back to logical for gradient interpolation.
        int64_t col = ((x0 - px) + i) / isf;
        int64_t color = rt_color_lerp(c1, c2, col * 100 / w_minus1);
        row_buf[i * 4 + 0] = (uint8_t)((color >> 16) & 0xFF); // R
        row_buf[i * 4 + 1] = (uint8_t)((color >> 8) & 0xFF);  // G
        row_buf[i * 4 + 2] = (uint8_t)(color & 0xFF);         // B
        row_buf[i * 4 + 3] = 0xFF;                            // A
    }

    for (int64_t row = y0; row < y1_clip; row++)
        memcpy(&fb.pixels[(size_t)(row * fb.stride + x0 * 4)], row_buf, (size_t)(draw_w * 4));

    free(row_buf);
}

void rt_canvas_gradient_v(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2)
{
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    // Write each row directly into the framebuffer — one colour per row, no per-row
    // vgfx_line() overhead.
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
    {
        // Fallback: vgfx_line auto-scales via coord_scale, so pass logical coords.
        for (int64_t row = 0; row < h; row++)
        {
            int64_t color = rt_color_lerp(c1, c2, row * 100 / (h - 1 > 0 ? h - 1 : 1));
            vgfx_line(canvas->gfx_win,
                      (int32_t)x,
                      (int32_t)(y + row),
                      (int32_t)(x + w - 1),
                      (int32_t)(y + row),
                      (vgfx_color_t)color);
        }
        return;
    }

    // Scale logical coordinates to physical framebuffer space.
    float cs = vgfx_window_get_scale(canvas->gfx_win);
    int32_t isf = (cs > 1.0f) ? (int32_t)cs : 1;
    int64_t px = x * isf, py = y * isf;
    int64_t pw = w * isf, ph = h * isf;

    int64_t x0 = px < 0 ? 0 : px;
    int64_t x1_clip = px + pw > fb.width ? fb.width : px + pw;
    int64_t y0 = py < 0 ? 0 : py;
    int64_t y1_clip = py + ph > fb.height ? fb.height : py + ph;

    if (x1_clip <= x0 || y1_clip <= y0)
        return;

    int64_t draw_w = x1_clip - x0;
    int64_t h_minus1 = h > 1 ? h - 1 : 1;

    for (int64_t row = y0; row < y1_clip; row++)
    {
        // Map physical row back to logical for gradient interpolation.
        int64_t r_idx = (row - py) / isf;
        int64_t color = rt_color_lerp(c1, c2, r_idx * 100 / h_minus1);
        uint8_t cr = (uint8_t)((color >> 16) & 0xFF);
        uint8_t cg = (uint8_t)((color >> 8) & 0xFF);
        uint8_t cb = (uint8_t)(color & 0xFF);
        uint8_t *dst = &fb.pixels[(size_t)(row * fb.stride + x0 * 4)];
        for (int64_t i = 0; i < draw_w; i++)
        {
            dst[i * 4 + 0] = cr;
            dst[i * 4 + 1] = cg;
            dst[i * 4 + 2] = cb;
            dst[i * 4 + 3] = 0xFF;
        }
    }
}

//=============================================================================
// Extended Color Functions
//=============================================================================

int64_t rt_color_from_hsl(int64_t h, int64_t s, int64_t l)
{
    // Clamp inputs
    h = ((h % 360) + 360) % 360;
    if (s < 0)
        s = 0;
    if (s > 100)
        s = 100;
    if (l < 0)
        l = 0;
    if (l > 100)
        l = 100;

    int64_t r, g, b;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);

    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_get_h(int64_t color)
{
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return h;
}

int64_t rt_color_get_s(int64_t color)
{
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return s;
}

int64_t rt_color_get_l(int64_t color)
{
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    return l;
}

int64_t rt_color_lerp(int64_t c1, int64_t c2, int64_t t)
{
    if (t < 0)
        t = 0;
    if (t > 100)
        t = 100;

    int64_t r1 = (c1 >> 16) & 0xFF;
    int64_t g1 = (c1 >> 8) & 0xFF;
    int64_t b1 = c1 & 0xFF;

    int64_t r2 = (c2 >> 16) & 0xFF;
    int64_t g2 = (c2 >> 8) & 0xFF;
    int64_t b2 = c2 & 0xFF;

    int64_t r = r1 + (r2 - r1) * t / 100;
    int64_t g = g1 + (g2 - g1) * t / 100;
    int64_t b = b1 + (b2 - b1) * t / 100;

    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_get_r(int64_t color)
{
    return (color >> 16) & 0xFF;
}

int64_t rt_color_get_g(int64_t color)
{
    return (color >> 8) & 0xFF;
}

int64_t rt_color_get_b(int64_t color)
{
    return color & 0xFF;
}

int64_t rt_color_get_a(int64_t color)
{
    return (color >> 24) & 0xFF;
}

int64_t rt_color_brighten(int64_t color, int64_t amount)
{
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;

    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;

    // Increase each channel toward 255
    r = r + (255 - r) * amount / 100;
    g = g + (255 - g) * amount / 100;
    b = b + (255 - b) * amount / 100;

    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_darken(int64_t color, int64_t amount)
{
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;

    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;

    // Decrease each channel toward 0
    r = r - r * amount / 100;
    g = g - g * amount / 100;
    b = b - b * amount / 100;

    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_from_hex(rt_string hex)
{
    const char *s = rt_string_cstr(hex);
    if (!s)
        return 0;
    if (*s == '#')
        s++;
    size_t len = strlen(s);
    unsigned long val = strtoul(s, NULL, 16);
    if (len == 6)
        return (int64_t)val; // 0xRRGGBB
    if (len == 8)
    {
        // Input is RRGGBBAA, store as AARRGGBB
        int64_t r = (val >> 24) & 0xFF;
        int64_t g = (val >> 16) & 0xFF;
        int64_t b = (val >> 8) & 0xFF;
        int64_t a = val & 0xFF;
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
    if (len == 3)
    {
        // Shorthand: RGB -> RRGGBB
        int64_t r = (val >> 8) & 0xF;
        int64_t g = (val >> 4) & 0xF;
        int64_t b = val & 0xF;
        return ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
    }
    return (int64_t)val;
}

rt_string rt_color_to_hex(int64_t color)
{
    char buf[10];
    int64_t a = (color >> 24) & 0xFF;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int len;
    if (a != 0 && a != 255)
        len = snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", (int)r, (int)g, (int)b, (int)a);
    else
        len = snprintf(buf, sizeof(buf), "#%02X%02X%02X", (int)r, (int)g, (int)b);
    return rt_string_from_bytes(buf, (size_t)len);
}

int64_t rt_color_saturate(int64_t color, int64_t amount)
{
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    s = s + amount;
    if (s > 100)
        s = 100;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_desaturate(int64_t color, int64_t amount)
{
    if (amount < 0)
        amount = 0;
    if (amount > 100)
        amount = 100;
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    s = s - amount;
    if (s < 0)
        s = 0;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_complement(int64_t color)
{
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    int64_t h, s, l;
    rtg_rgb_to_hsl(r, g, b, &h, &s, &l);
    h = (h + 180) % 360;
    rtg_hsl_to_rgb(h, s, l, &r, &g, &b);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

int64_t rt_color_grayscale(int64_t color)
{
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8) & 0xFF;
    int64_t b = color & 0xFF;
    // Luminance formula: 0.299R + 0.587G + 0.114B
    int64_t gray = (r * 299 + g * 587 + b * 114) / 1000;
    return ((gray & 0xFF) << 16) | ((gray & 0xFF) << 8) | (gray & 0xFF);
}

int64_t rt_color_invert(int64_t color)
{
    int64_t r = 255 - ((color >> 16) & 0xFF);
    int64_t g = 255 - ((color >> 8) & 0xFF);
    int64_t b = 255 - (color & 0xFF);
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

#endif /* VIPER_ENABLE_GRAPHICS */
