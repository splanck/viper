//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_graphics_stubs.c
// Purpose: Stub implementations for all rt_canvas_* and rt_color_* functions
//   when VIPER_ENABLE_GRAPHICS is not defined. Allows non-graphics builds to
//   link cleanly without pulling in ViperGFX or platform windowing code.
//
// Key invariants:
//   - This file is compiled only when VIPER_ENABLE_GRAPHICS is NOT defined.
//   - rt_canvas_new traps immediately; all other canvas stubs are silent no-ops
//     returning zero/NULL as appropriate.
//   - rt_color_rgb/rgba/get_r/g/b/a provide correct bit manipulation so color
//     logic works even without a canvas.
//
// Ownership/Lifetime:
//   - No resources are allocated; all functions are stateless stubs.
//
// Links: src/runtime/graphics/rt_graphics.h (public API),
//        src/runtime/graphics/rt_graphics.c (real implementations)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_string.h"

#include <stdint.h>

extern void rt_trap(const char *msg);

void *rt_canvas_new(rt_string title, int64_t width, int64_t height)
{
    (void)title;
    (void)width;
    (void)height;
    rt_trap("Canvas.New: graphics support not compiled in");
    return NULL;
}

void rt_canvas_destroy(void *canvas)
{
    (void)canvas;
}

int64_t rt_canvas_width(void *canvas)
{
    (void)canvas;
    return 0;
}

int64_t rt_canvas_height(void *canvas)
{
    (void)canvas;
    return 0;
}

int64_t rt_canvas_should_close(void *canvas)
{
    (void)canvas;
    return 1;
}

void rt_canvas_flip(void *canvas)
{
    (void)canvas;
}

void rt_canvas_clear(void *canvas, int64_t color)
{
    (void)canvas;
    (void)color;
}

void rt_canvas_line(void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color)
{
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
}

void rt_canvas_box(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void rt_canvas_frame(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void rt_canvas_disc(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
}

void rt_canvas_ring(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
}

void rt_canvas_plot(void *canvas, int64_t x, int64_t y, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)color;
}

int64_t rt_canvas_poll(void *canvas)
{
    (void)canvas;
    return 0;
}

int64_t rt_canvas_key_held(void *canvas, int64_t key)
{
    (void)canvas;
    (void)key;
    return 0;
}

void rt_canvas_text(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}

void rt_canvas_text_bg(void *canvas, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
}

int64_t rt_canvas_text_width(rt_string text)
{
    (void)text;
    return 0;
}

int64_t rt_canvas_text_height(void)
{
    return 8;
}

void rt_canvas_text_scaled(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)scale;
    (void)color;
}

void rt_canvas_text_scaled_bg(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t fg, int64_t bg)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)scale;
    (void)fg;
    (void)bg;
}

int64_t rt_canvas_text_scaled_width(rt_string text, int64_t scale)
{
    (void)text;
    (void)scale;
    return 0;
}

void rt_canvas_text_centered(void *canvas, int64_t y, rt_string text, int64_t color)
{
    (void)canvas;
    (void)y;
    (void)text;
    (void)color;
}

void rt_canvas_text_right(void *canvas, int64_t margin, int64_t y, rt_string text, int64_t color)
{
    (void)canvas;
    (void)margin;
    (void)y;
    (void)text;
    (void)color;
}

void rt_canvas_text_centered_scaled(
    void *canvas, int64_t y, rt_string text, int64_t scale, int64_t color)
{
    (void)canvas;
    (void)y;
    (void)text;
    (void)scale;
    (void)color;
}

void rt_canvas_box_alpha(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t alpha)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    (void)alpha;
}

void rt_canvas_disc_alpha(
    void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color, int64_t alpha)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
    (void)alpha;
}

void rt_canvas_blit(void *canvas, int64_t x, int64_t y, void *pixels)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)pixels;
}

void rt_canvas_blit_region(void *canvas,
                           int64_t dx,
                           int64_t dy,
                           void *pixels,
                           int64_t sx,
                           int64_t sy,
                           int64_t w,
                           int64_t h)
{
    (void)canvas;
    (void)dx;
    (void)dy;
    (void)pixels;
    (void)sx;
    (void)sy;
    (void)w;
    (void)h;
}

void rt_canvas_blit_alpha(void *canvas, int64_t x, int64_t y, void *pixels)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)pixels;
}

void rt_canvas_thick_line(
    void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t thickness, int64_t color)
{
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)thickness;
    (void)color;
}

void rt_canvas_round_box(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)radius;
    (void)color;
}

void rt_canvas_round_frame(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t radius, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)radius;
    (void)color;
}

void rt_canvas_flood_fill(void *canvas, int64_t x, int64_t y, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)color;
}

void rt_canvas_triangle(void *canvas,
                        int64_t x1,
                        int64_t y1,
                        int64_t x2,
                        int64_t y2,
                        int64_t x3,
                        int64_t y3,
                        int64_t color)
{
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)x3;
    (void)y3;
    (void)color;
}

void rt_canvas_triangle_frame(void *canvas,
                              int64_t x1,
                              int64_t y1,
                              int64_t x2,
                              int64_t y2,
                              int64_t x3,
                              int64_t y3,
                              int64_t color)
{
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)x3;
    (void)y3;
    (void)color;
}

void rt_canvas_ellipse(void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)rx;
    (void)ry;
    (void)color;
}

void rt_canvas_ellipse_frame(
    void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)rx;
    (void)ry;
    (void)color;
}

void rt_canvas_arc(void *canvas,
                   int64_t cx,
                   int64_t cy,
                   int64_t radius,
                   int64_t start_angle,
                   int64_t end_angle,
                   int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)start_angle;
    (void)end_angle;
    (void)color;
}

void rt_canvas_arc_frame(void *canvas,
                         int64_t cx,
                         int64_t cy,
                         int64_t radius,
                         int64_t start_angle,
                         int64_t end_angle,
                         int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)start_angle;
    (void)end_angle;
    (void)color;
}

void rt_canvas_bezier(void *canvas,
                      int64_t x1,
                      int64_t y1,
                      int64_t cx,
                      int64_t cy,
                      int64_t x2,
                      int64_t y2,
                      int64_t color)
{
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)cx;
    (void)cy;
    (void)x2;
    (void)y2;
    (void)color;
}

void rt_canvas_polyline(void *canvas, void *points, int64_t count, int64_t color)
{
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
}

void rt_canvas_polygon(void *canvas, void *points, int64_t count, int64_t color)
{
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
}

void rt_canvas_polygon_frame(void *canvas, void *points, int64_t count, int64_t color)
{
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
}

int64_t rt_canvas_get_pixel(void *canvas, int64_t x, int64_t y)
{
    (void)canvas;
    (void)x;
    (void)y;
    return 0;
}

void *rt_canvas_copy_rect(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return NULL;
}

int64_t rt_canvas_save_bmp(void *canvas, rt_string path)
{
    (void)canvas;
    (void)path;
    return 0;
}

int64_t rt_canvas_save_png(void *canvas, rt_string path)
{
    (void)canvas;
    (void)path;
    return 0;
}

int64_t rt_color_rgb(int64_t r, int64_t g, int64_t b)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)(((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8);
}

int64_t rt_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    uint8_t a8 = (a < 0) ? 0 : (a > 255) ? 255 : (uint8_t)a;
    return (int64_t)(((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) |
                     (uint32_t)b8);
}

int64_t rt_color_from_hsl(int64_t h, int64_t s, int64_t l)
{
    (void)h;
    (void)s;
    (void)l;
    return 0;
}

int64_t rt_color_get_h(int64_t color)
{
    (void)color;
    return 0;
}

int64_t rt_color_get_s(int64_t color)
{
    (void)color;
    return 0;
}

int64_t rt_color_get_l(int64_t color)
{
    (void)color;
    return 0;
}

int64_t rt_color_lerp(int64_t c1, int64_t c2, int64_t t)
{
    (void)c1;
    (void)c2;
    (void)t;
    return 0;
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
    (void)color;
    (void)amount;
    return 0;
}

int64_t rt_color_darken(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

int64_t rt_color_from_hex(rt_string hex)
{
    (void)hex;
    return 0;
}

rt_string rt_color_to_hex(int64_t color)
{
    (void)color;
    return rt_string_from_bytes("#000000", 7);
}

int64_t rt_color_saturate(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

int64_t rt_color_desaturate(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

int64_t rt_color_complement(int64_t color)
{
    (void)color;
    return 0;
}

int64_t rt_color_grayscale(int64_t color)
{
    (void)color;
    return 0;
}

int64_t rt_color_invert(int64_t color)
{
    (void)color;
    return 0;
}

void rt_canvas_set_clip_rect(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void rt_canvas_clear_clip_rect(void *canvas)
{
    (void)canvas;
}

void rt_canvas_set_title(void *canvas, rt_string title)
{
    (void)canvas;
    (void)title;
}

rt_string rt_canvas_get_title(void *canvas)
{
    (void)canvas;
    return rt_string_from_bytes("", 0);
}

void rt_canvas_resize(void *canvas, int64_t width, int64_t height)
{
    (void)canvas;
    (void)width;
    (void)height;
}

void rt_canvas_close(void *canvas)
{
    (void)canvas;
}

void *rt_canvas_screenshot(void *canvas)
{
    (void)canvas;
    return NULL;
}

void rt_canvas_fullscreen(void *canvas)
{
    (void)canvas;
}

void rt_canvas_windowed(void *canvas)
{
    (void)canvas;
}

void rt_canvas_gradient_h(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c1;
    (void)c2;
}

void rt_canvas_gradient_v(
    void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t c1, int64_t c2)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c1;
    (void)c2;
}

double rt_canvas_get_scale(void *canvas)
{
    (void)canvas;
    return 1.0;
}

void rt_canvas_get_position(void *canvas, int64_t *x, int64_t *y)
{
    (void)canvas;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
}

void rt_canvas_set_position(void *canvas, int64_t x, int64_t y)
{
    (void)canvas;
    (void)x;
    (void)y;
}

int64_t rt_canvas_get_fps(void *canvas)
{
    (void)canvas;
    return -1;
}

void rt_canvas_set_fps(void *canvas, int64_t fps)
{
    (void)canvas;
    (void)fps;
}

int64_t rt_canvas_get_delta_time(void *canvas)
{
    (void)canvas;
    return 0;
}

void rt_canvas_set_dt_max(void *canvas, int64_t max_ms)
{
    (void)canvas;
    (void)max_ms;
}

int64_t rt_canvas_begin_frame(void *canvas)
{
    (void)canvas;
    return 0;
}

int8_t rt_canvas_is_maximized(void *canvas)
{
    (void)canvas;
    return 0;
}

void rt_canvas_maximize(void *canvas)
{
    (void)canvas;
}

int8_t rt_canvas_is_minimized(void *canvas)
{
    (void)canvas;
    return 0;
}

void rt_canvas_minimize(void *canvas)
{
    (void)canvas;
}

void rt_canvas_restore(void *canvas)
{
    (void)canvas;
}

int8_t rt_canvas_is_focused(void *canvas)
{
    (void)canvas;
    return 0;
}

void rt_canvas_focus(void *canvas)
{
    (void)canvas;
}

void rt_canvas_prevent_close(void *canvas, int64_t prevent)
{
    (void)canvas;
    (void)prevent;
}

void rt_canvas_get_monitor_size(void *canvas, int64_t *w, int64_t *h)
{
    (void)canvas;
    if (w)
        *w = 0;
    if (h)
        *h = 0;
}
