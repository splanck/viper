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

#include "rt_audio3d.h"
#include "rt_canvas3d.h"
#include "rt_fbx_loader.h"
#include "rt_graphics.h"
#include "rt_morphtarget3d.h"
#include "rt_particles3d.h"
#include "rt_physics3d.h"
#include "rt_transform3d.h"
#include "rt_path3d.h"
#include "rt_postfx3d.h"
#include "rt_raycast3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
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
    void *canvas, int64_t y, rt_string text, int64_t color, int64_t scale)
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

// Color constants — packed 0x00RRGGBB
int64_t rt_color_red(void)
{
    return 0xFF0000;
}

int64_t rt_color_green(void)
{
    return 0x00FF00;
}

int64_t rt_color_blue(void)
{
    return 0x0000FF;
}

int64_t rt_color_white(void)
{
    return 0xFFFFFF;
}

int64_t rt_color_black(void)
{
    return 0x000000;
}

int64_t rt_color_yellow(void)
{
    return 0xFFFF00;
}

int64_t rt_color_cyan(void)
{
    return 0x00FFFF;
}

int64_t rt_color_magenta(void)
{
    return 0xFF00FF;
}

int64_t rt_color_gray(void)
{
    return 0x808080;
}

int64_t rt_color_orange(void)
{
    return 0xFFA500;
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

//=============================================================================
// Graphics 3D stubs — Canvas3D, Mesh3D, Camera3D, Material3D, Light3D
//=============================================================================

void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz)
{
    (void)px;
    (void)nx;
    (void)py;
    (void)ny;
    (void)pz;
    (void)nz;
    rt_trap("CubeMap3D.New: graphics support not compiled in");
    return NULL;
}

void rt_canvas3d_set_skybox(void *c, void *cm)
{
    (void)c;
    (void)cm;
}

void rt_canvas3d_clear_skybox(void *c)
{
    (void)c;
}

void rt_material3d_set_env_map(void *o, void *cm)
{
    (void)o;
    (void)cm;
}

void rt_material3d_set_reflectivity(void *o, double r)
{
    (void)o;
    (void)r;
}

double rt_material3d_get_reflectivity(void *o)
{
    (void)o;
    return 0.0;
}

void *rt_rendertarget3d_new(int64_t w, int64_t h)
{
    (void)w;
    (void)h;
    rt_trap("RenderTarget3D.New: graphics support not compiled in");
    return NULL;
}

int64_t rt_rendertarget3d_get_width(void *o)
{
    (void)o;
    return 0;
}

int64_t rt_rendertarget3d_get_height(void *o)
{
    (void)o;
    return 0;
}

void *rt_rendertarget3d_as_pixels(void *o)
{
    (void)o;
    return NULL;
}

void rt_canvas3d_set_render_target(void *c, void *t)
{
    (void)c;
    (void)t;
}

void rt_canvas3d_reset_render_target(void *c)
{
    (void)c;
}

void *rt_canvas3d_new(rt_string title, int64_t w, int64_t h)
{
    (void)title;
    (void)w;
    (void)h;
    rt_trap("Canvas3D.New: graphics support not compiled in");
    return NULL;
}

void rt_canvas3d_clear(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

void rt_canvas3d_begin(void *o, void *c)
{
    (void)o;
    (void)c;
}

void rt_canvas3d_draw_mesh(void *o, void *m, void *t, void *mt)
{
    (void)o;
    (void)m;
    (void)t;
    (void)mt;
}

void rt_canvas3d_end(void *o)
{
    (void)o;
}

void rt_canvas3d_flip(void *o)
{
    (void)o;
}

int64_t rt_canvas3d_poll(void *o)
{
    (void)o;
    return 0;
}

int8_t rt_canvas3d_should_close(void *o)
{
    (void)o;
    return 0;
}

void rt_canvas3d_set_wireframe(void *o, int8_t e)
{
    (void)o;
    (void)e;
}

void rt_canvas3d_set_backface_cull(void *o, int8_t e)
{
    (void)o;
    (void)e;
}

int64_t rt_canvas3d_get_width(void *o)
{
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_height(void *o)
{
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_fps(void *o)
{
    (void)o;
    return 0;
}

int64_t rt_canvas3d_get_delta_time(void *o)
{
    (void)o;
    return 0;
}

void rt_canvas3d_set_dt_max(void *o, int64_t m)
{
    (void)o;
    (void)m;
}

void rt_canvas3d_set_light(void *o, int64_t i, void *l)
{
    (void)o;
    (void)i;
    (void)l;
}

void rt_canvas3d_set_ambient(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

void rt_canvas3d_draw_line3d(void *o, void *f, void *t, int64_t c)
{
    (void)o;
    (void)f;
    (void)t;
    (void)c;
}

void rt_canvas3d_draw_point3d(void *o, void *p, int64_t c, int64_t s)
{
    (void)o;
    (void)p;
    (void)c;
    (void)s;
}

rt_string rt_canvas3d_get_backend(void *o)
{
    (void)o;
    return NULL;
}

void *rt_canvas3d_screenshot(void *o)
{
    (void)o;
    return NULL;
}

void *rt_mesh3d_new(void)
{
    rt_trap("Mesh3D.New: graphics support not compiled in");
    return NULL;
}

void *rt_mesh3d_new_box(double sx, double sy, double sz)
{
    (void)sx;
    (void)sy;
    (void)sz;
    rt_trap("Mesh3D.NewBox: graphics support not compiled in");
    return NULL;
}

void *rt_mesh3d_new_sphere(double r, int64_t s)
{
    (void)r;
    (void)s;
    rt_trap("Mesh3D.NewSphere: graphics support not compiled in");
    return NULL;
}

void *rt_mesh3d_new_plane(double sx, double sz)
{
    (void)sx;
    (void)sz;
    rt_trap("Mesh3D.NewPlane: graphics support not compiled in");
    return NULL;
}

void *rt_mesh3d_new_cylinder(double r, double h, int64_t s)
{
    (void)r;
    (void)h;
    (void)s;
    rt_trap("Mesh3D.NewCylinder: graphics support not compiled in");
    return NULL;
}

void *rt_mesh3d_from_obj(rt_string p)
{
    (void)p;
    rt_trap("Mesh3D.FromOBJ: graphics support not compiled in");
    return NULL;
}

int64_t rt_mesh3d_get_vertex_count(void *o)
{
    (void)o;
    return 0;
}

int64_t rt_mesh3d_get_triangle_count(void *o)
{
    (void)o;
    return 0;
}

void rt_mesh3d_add_vertex(
    void *o, double x, double y, double z, double nx, double ny, double nz, double u, double v)
{
    (void)o;
    (void)x;
    (void)y;
    (void)z;
    (void)nx;
    (void)ny;
    (void)nz;
    (void)u;
    (void)v;
}

void rt_mesh3d_add_triangle(void *o, int64_t v0, int64_t v1, int64_t v2)
{
    (void)o;
    (void)v0;
    (void)v1;
    (void)v2;
}

void rt_mesh3d_recalc_normals(void *o)
{
    (void)o;
}

void *rt_mesh3d_clone(void *o)
{
    (void)o;
    return NULL;
}

void rt_mesh3d_transform(void *o, void *m)
{
    (void)o;
    (void)m;
}

void *rt_camera3d_new(double f, double a, double n, double fa)
{
    (void)f;
    (void)a;
    (void)n;
    (void)fa;
    rt_trap("Camera3D.New: graphics support not compiled in");
    return NULL;
}

void rt_camera3d_look_at(void *o, void *e, void *t, void *u)
{
    (void)o;
    (void)e;
    (void)t;
    (void)u;
}

void rt_camera3d_orbit(void *o, void *t, double d, double y, double p)
{
    (void)o;
    (void)t;
    (void)d;
    (void)y;
    (void)p;
}

double rt_camera3d_get_fov(void *o)
{
    (void)o;
    return 0.0;
}

void rt_camera3d_set_fov(void *o, double f)
{
    (void)o;
    (void)f;
}

void *rt_camera3d_get_position(void *o)
{
    (void)o;
    return NULL;
}

void rt_camera3d_set_position(void *o, void *p)
{
    (void)o;
    (void)p;
}

void *rt_camera3d_get_forward(void *o)
{
    (void)o;
    return NULL;
}

void *rt_camera3d_get_right(void *o)
{
    (void)o;
    return NULL;
}

void *rt_camera3d_screen_to_ray(void *o, int64_t sx, int64_t sy, int64_t sw, int64_t sh)
{
    (void)o;
    (void)sx;
    (void)sy;
    (void)sw;
    (void)sh;
    return NULL;
}

void *rt_material3d_new(void)
{
    rt_trap("Material3D.New: graphics support not compiled in");
    return NULL;
}

void *rt_material3d_new_color(double r, double g, double b)
{
    (void)r;
    (void)g;
    (void)b;
    rt_trap("Material3D.NewColor: graphics support not compiled in");
    return NULL;
}

void *rt_material3d_new_textured(void *p)
{
    (void)p;
    rt_trap("Material3D.NewTextured: graphics support not compiled in");
    return NULL;
}

void rt_material3d_set_color(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

void rt_material3d_set_texture(void *o, void *p)
{
    (void)o;
    (void)p;
}

void rt_material3d_set_shininess(void *o, double s)
{
    (void)o;
    (void)s;
}

void rt_material3d_set_alpha(void *o, double a)
{
    (void)o;
    (void)a;
}

double rt_material3d_get_alpha(void *o)
{
    (void)o;
    return 1.0;
}

void rt_material3d_set_unlit(void *o, int8_t u)
{
    (void)o;
    (void)u;
}

void rt_material3d_set_normal_map(void *o, void *p)
{
    (void)o;
    (void)p;
}

void rt_material3d_set_specular_map(void *o, void *p)
{
    (void)o;
    (void)p;
}

void rt_material3d_set_emissive_map(void *o, void *p)
{
    (void)o;
    (void)p;
}

void rt_material3d_set_emissive_color(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

void rt_mesh3d_calc_tangents(void *o)
{
    (void)o;
}

void *rt_light3d_new_directional(void *d, double r, double g, double b)
{
    (void)d;
    (void)r;
    (void)g;
    (void)b;
    rt_trap("Light3D.NewDirectional: graphics support not compiled in");
    return NULL;
}

void *rt_light3d_new_point(void *p, double r, double g, double b, double a)
{
    (void)p;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    rt_trap("Light3D.NewPoint: graphics support not compiled in");
    return NULL;
}

void *rt_light3d_new_ambient(double r, double g, double b)
{
    (void)r;
    (void)g;
    (void)b;
    rt_trap("Light3D.NewAmbient: graphics support not compiled in");
    return NULL;
}

void rt_light3d_set_intensity(void *o, double i)
{
    (void)o;
    (void)i;
}

void rt_light3d_set_color(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/* Scene3D / SceneNode3D stubs */
void *rt_scene3d_new(void)
{
    rt_trap("Scene3D.New: graphics support not compiled in");
    return NULL;
}

void *rt_scene3d_get_root(void *s)
{
    (void)s;
    return NULL;
}

void rt_scene3d_add(void *s, void *n)
{
    (void)s;
    (void)n;
}

void rt_scene3d_remove(void *s, void *n)
{
    (void)s;
    (void)n;
}

void *rt_scene3d_find(void *s, rt_string n)
{
    (void)s;
    (void)n;
    return NULL;
}

void rt_scene3d_draw(void *s, void *c, void *cam)
{
    (void)s;
    (void)c;
    (void)cam;
}

void rt_scene3d_clear(void *s)
{
    (void)s;
}

int64_t rt_scene3d_get_node_count(void *s)
{
    (void)s;
    return 0;
}

void *rt_scene_node3d_new(void)
{
    rt_trap("SceneNode3D.New: graphics support not compiled in");
    return NULL;
}

void rt_scene_node3d_set_position(void *n, double x, double y, double z)
{
    (void)n;
    (void)x;
    (void)y;
    (void)z;
}

void *rt_scene_node3d_get_position(void *n)
{
    (void)n;
    return NULL;
}

void rt_scene_node3d_set_rotation(void *n, void *q)
{
    (void)n;
    (void)q;
}

void *rt_scene_node3d_get_rotation(void *n)
{
    (void)n;
    return NULL;
}

void rt_scene_node3d_set_scale(void *n, double x, double y, double z)
{
    (void)n;
    (void)x;
    (void)y;
    (void)z;
}

void *rt_scene_node3d_get_scale(void *n)
{
    (void)n;
    return NULL;
}

void *rt_scene_node3d_get_world_matrix(void *n)
{
    (void)n;
    return NULL;
}

void rt_scene_node3d_add_child(void *n, void *c)
{
    (void)n;
    (void)c;
}

void rt_scene_node3d_remove_child(void *n, void *c)
{
    (void)n;
    (void)c;
}

int64_t rt_scene_node3d_child_count(void *n)
{
    (void)n;
    return 0;
}

void *rt_scene_node3d_get_child(void *n, int64_t i)
{
    (void)n;
    (void)i;
    return NULL;
}

void *rt_scene_node3d_get_parent(void *n)
{
    (void)n;
    return NULL;
}

void *rt_scene_node3d_find(void *n, rt_string name)
{
    (void)n;
    (void)name;
    return NULL;
}

void rt_scene_node3d_set_mesh(void *n, void *m)
{
    (void)n;
    (void)m;
}

void rt_scene_node3d_set_material(void *n, void *m)
{
    (void)n;
    (void)m;
}

void rt_scene_node3d_set_visible(void *n, int8_t v)
{
    (void)n;
    (void)v;
}

int8_t rt_scene_node3d_get_visible(void *n)
{
    (void)n;
    return 0;
}

void rt_scene_node3d_set_name(void *n, rt_string s)
{
    (void)n;
    (void)s;
}

rt_string rt_scene_node3d_get_name(void *n)
{
    (void)n;
    return NULL;
}

void *rt_scene_node3d_get_aabb_min(void *n)
{
    (void)n;
    return NULL;
}

void *rt_scene_node3d_get_aabb_max(void *n)
{
    (void)n;
    return NULL;
}

int64_t rt_scene3d_get_culled_count(void *s)
{
    (void)s;
    return 0;
}

/* Skeleton3D / Animation3D / AnimPlayer3D stubs */
void *rt_skeleton3d_new(void)
{
    rt_trap("Skeleton3D.New: graphics support not compiled in");
    return NULL;
}

int64_t rt_skeleton3d_add_bone(void *s, rt_string n, int64_t p, void *m)
{
    (void)s;
    (void)n;
    (void)p;
    (void)m;
    return -1;
}

void rt_skeleton3d_compute_inverse_bind(void *s)
{
    (void)s;
}

int64_t rt_skeleton3d_get_bone_count(void *s)
{
    (void)s;
    return 0;
}

int64_t rt_skeleton3d_find_bone(void *s, rt_string n)
{
    (void)s;
    (void)n;
    return -1;
}

rt_string rt_skeleton3d_get_bone_name(void *s, int64_t i)
{
    (void)s;
    (void)i;
    return NULL;
}

void *rt_skeleton3d_get_bone_bind_pose(void *s, int64_t i)
{
    (void)s;
    (void)i;
    return NULL;
}

void *rt_animation3d_new(rt_string n, double d)
{
    (void)n;
    (void)d;
    rt_trap("Animation3D.New: graphics support not compiled in");
    return NULL;
}

void rt_animation3d_add_keyframe(void *a, int64_t b, double t, void *p, void *r, void *s)
{
    (void)a;
    (void)b;
    (void)t;
    (void)p;
    (void)r;
    (void)s;
}

void rt_animation3d_set_looping(void *a, int8_t l)
{
    (void)a;
    (void)l;
}

int8_t rt_animation3d_get_looping(void *a)
{
    (void)a;
    return 0;
}

double rt_animation3d_get_duration(void *a)
{
    (void)a;
    return 0.0;
}

rt_string rt_animation3d_get_name(void *a)
{
    (void)a;
    return NULL;
}

void *rt_anim_player3d_new(void *s)
{
    (void)s;
    rt_trap("AnimPlayer3D.New: graphics support not compiled in");
    return NULL;
}

void rt_anim_player3d_play(void *p, void *a)
{
    (void)p;
    (void)a;
}

void rt_anim_player3d_crossfade(void *p, void *a, double d)
{
    (void)p;
    (void)a;
    (void)d;
}

void rt_anim_player3d_stop(void *p)
{
    (void)p;
}

void rt_anim_player3d_update(void *p, double d)
{
    (void)p;
    (void)d;
}

void rt_anim_player3d_set_speed(void *p, double s)
{
    (void)p;
    (void)s;
}

double rt_anim_player3d_get_speed(void *p)
{
    (void)p;
    return 1.0;
}

int8_t rt_anim_player3d_is_playing(void *p)
{
    (void)p;
    return 0;
}

double rt_anim_player3d_get_time(void *p)
{
    (void)p;
    return 0.0;
}

void rt_anim_player3d_set_time(void *p, double t)
{
    (void)p;
    (void)t;
}

void *rt_anim_player3d_get_bone_matrix(void *p, int64_t i)
{
    (void)p;
    (void)i;
    return NULL;
}

void rt_mesh3d_set_skeleton(void *m, void *s)
{
    (void)m;
    (void)s;
}

void rt_mesh3d_set_bone_weights(void *m,
                                int64_t v,
                                int64_t b0,
                                double w0,
                                int64_t b1,
                                double w1,
                                int64_t b2,
                                double w2,
                                int64_t b3,
                                double w3)
{
    (void)m;
    (void)v;
    (void)b0;
    (void)w0;
    (void)b1;
    (void)w1;
    (void)b2;
    (void)w2;
    (void)b3;
    (void)w3;
}

void rt_canvas3d_draw_mesh_skinned(void *c, void *m, void *t, void *mat, void *p)
{
    (void)c;
    (void)m;
    (void)t;
    (void)mat;
    (void)p;
}

/* FBX Loader stubs */
void *rt_fbx_load(rt_string p)
{
    (void)p;
    rt_trap("FBX.Load: graphics support not compiled in");
    return NULL;
}

int64_t rt_fbx_mesh_count(void *f)
{
    (void)f;
    return 0;
}

void *rt_fbx_get_mesh(void *f, int64_t i)
{
    (void)f;
    (void)i;
    return NULL;
}

void *rt_fbx_get_skeleton(void *f)
{
    (void)f;
    return NULL;
}

int64_t rt_fbx_animation_count(void *f)
{
    (void)f;
    return 0;
}

void *rt_fbx_get_animation(void *f, int64_t i)
{
    (void)f;
    (void)i;
    return NULL;
}

rt_string rt_fbx_get_animation_name(void *f, int64_t i)
{
    (void)f;
    (void)i;
    return NULL;
}

int64_t rt_fbx_material_count(void *f)
{
    (void)f;
    return 0;
}

void *rt_fbx_get_material(void *f, int64_t i)
{
    (void)f;
    (void)i;
    return NULL;
}

/* MorphTarget3D stubs */
void *rt_morphtarget3d_new(int64_t vc)
{
    (void)vc;
    rt_trap("MorphTarget3D.New: graphics support not compiled in");
    return NULL;
}

int64_t rt_morphtarget3d_add_shape(void *m, rt_string n)
{
    (void)m;
    (void)n;
    return -1;
}

void rt_morphtarget3d_set_delta(void *m, int64_t s, int64_t v, double dx, double dy, double dz)
{
    (void)m;
    (void)s;
    (void)v;
    (void)dx;
    (void)dy;
    (void)dz;
}

void rt_morphtarget3d_set_normal_delta(
    void *m, int64_t s, int64_t v, double dx, double dy, double dz)
{
    (void)m;
    (void)s;
    (void)v;
    (void)dx;
    (void)dy;
    (void)dz;
}

void rt_morphtarget3d_set_weight(void *m, int64_t s, double w)
{
    (void)m;
    (void)s;
    (void)w;
}

double rt_morphtarget3d_get_weight(void *m, int64_t s)
{
    (void)m;
    (void)s;
    return 0.0;
}

void rt_morphtarget3d_set_weight_by_name(void *m, rt_string n, double w)
{
    (void)m;
    (void)n;
    (void)w;
}

int64_t rt_morphtarget3d_get_shape_count(void *m)
{
    (void)m;
    return 0;
}

void rt_mesh3d_set_morph_targets(void *m, void *mt)
{
    (void)m;
    (void)mt;
}

void rt_canvas3d_draw_mesh_morphed(void *c, void *m, void *t, void *mat, void *mt)
{
    (void)c;
    (void)m;
    (void)t;
    (void)mat;
    (void)mt;
}

/* Particles3D stubs */
void *rt_particles3d_new(int64_t n)
{
    (void)n;
    rt_trap("Particles3D.New: graphics support not compiled in");
    return NULL;
}

void rt_particles3d_set_position(void *o, double x, double y, double z)
{
    (void)o;
    (void)x;
    (void)y;
    (void)z;
}

void rt_particles3d_set_direction(void *o, double dx, double dy, double dz, double s)
{
    (void)o;
    (void)dx;
    (void)dy;
    (void)dz;
    (void)s;
}

void rt_particles3d_set_speed(void *o, double mn, double mx)
{
    (void)o;
    (void)mn;
    (void)mx;
}

void rt_particles3d_set_lifetime(void *o, double mn, double mx)
{
    (void)o;
    (void)mn;
    (void)mx;
}

void rt_particles3d_set_size(void *o, double s, double e)
{
    (void)o;
    (void)s;
    (void)e;
}

void rt_particles3d_set_gravity(void *o, double gx, double gy, double gz)
{
    (void)o;
    (void)gx;
    (void)gy;
    (void)gz;
}

void rt_particles3d_set_color(void *o, int64_t sc, int64_t ec)
{
    (void)o;
    (void)sc;
    (void)ec;
}

void rt_particles3d_set_alpha(void *o, double sa, double ea)
{
    (void)o;
    (void)sa;
    (void)ea;
}

void rt_particles3d_set_rate(void *o, double r)
{
    (void)o;
    (void)r;
}

void rt_particles3d_set_additive(void *o, int8_t a)
{
    (void)o;
    (void)a;
}

void rt_particles3d_set_texture(void *o, void *t)
{
    (void)o;
    (void)t;
}

void rt_particles3d_set_emitter_shape(void *o, int64_t s)
{
    (void)o;
    (void)s;
}

void rt_particles3d_set_emitter_size(void *o, double sx, double sy, double sz)
{
    (void)o;
    (void)sx;
    (void)sy;
    (void)sz;
}

void rt_particles3d_start(void *o)
{
    (void)o;
}

void rt_particles3d_stop(void *o)
{
    (void)o;
}

void rt_particles3d_burst(void *o, int64_t n)
{
    (void)o;
    (void)n;
}

void rt_particles3d_clear(void *o)
{
    (void)o;
}

void rt_particles3d_update(void *o, double dt)
{
    (void)o;
    (void)dt;
}

void rt_particles3d_draw(void *o, void *c, void *cam)
{
    (void)o;
    (void)c;
    (void)cam;
}

int64_t rt_particles3d_get_count(void *o)
{
    (void)o;
    return 0;
}

int8_t rt_particles3d_get_emitting(void *o)
{
    (void)o;
    return 0;
}

/* PostFX3D stubs */
void *rt_postfx3d_new(void)
{
    rt_trap("PostFX3D.New: graphics support not compiled in");
    return NULL;
}

void rt_postfx3d_add_bloom(void *o, double t, double i, int64_t b)
{
    (void)o;
    (void)t;
    (void)i;
    (void)b;
}

void rt_postfx3d_add_tonemap(void *o, int64_t m, double e)
{
    (void)o;
    (void)m;
    (void)e;
}

void rt_postfx3d_add_fxaa(void *o)
{
    (void)o;
}

void rt_postfx3d_add_color_grade(void *o, double b, double c, double s)
{
    (void)o;
    (void)b;
    (void)c;
    (void)s;
}

void rt_postfx3d_add_vignette(void *o, double r, double s)
{
    (void)o;
    (void)r;
    (void)s;
}

void rt_postfx3d_set_enabled(void *o, int8_t e)
{
    (void)o;
    (void)e;
}

int8_t rt_postfx3d_get_enabled(void *o)
{
    (void)o;
    return 0;
}

void rt_postfx3d_clear(void *o)
{
    (void)o;
}

int64_t rt_postfx3d_get_effect_count(void *o)
{
    (void)o;
    return 0;
}

void rt_canvas3d_set_post_fx(void *c, void *fx)
{
    (void)c;
    (void)fx;
}

void rt_postfx3d_apply_to_canvas(void *c)
{
    (void)c;
}

/* Ray3D / AABB3D / RayHit3D stubs */
double rt_ray3d_intersect_triangle(void *o, void *d, void *v0, void *v1, void *v2)
{
    (void)o;
    (void)d;
    (void)v0;
    (void)v1;
    (void)v2;
    return -1.0;
}

void *rt_ray3d_intersect_mesh(void *o, void *d, void *m, void *t)
{
    (void)o;
    (void)d;
    (void)m;
    (void)t;
    return NULL;
}

double rt_ray3d_intersect_aabb(void *o, void *d, void *mn, void *mx)
{
    (void)o;
    (void)d;
    (void)mn;
    (void)mx;
    return -1.0;
}

double rt_ray3d_intersect_sphere(void *o, void *d, void *c, double r)
{
    (void)o;
    (void)d;
    (void)c;
    (void)r;
    return -1.0;
}

int8_t rt_aabb3d_overlaps(void *a0, void *a1, void *b0, void *b1)
{
    (void)a0;
    (void)a1;
    (void)b0;
    (void)b1;
    return 0;
}

void *rt_aabb3d_penetration(void *a0, void *a1, void *b0, void *b1)
{
    (void)a0;
    (void)a1;
    (void)b0;
    (void)b1;
    return NULL;
}

double rt_ray3d_hit_distance(void *h)
{
    (void)h;
    return -1.0;
}

void *rt_ray3d_hit_point(void *h)
{
    (void)h;
    return NULL;
}

void *rt_ray3d_hit_normal(void *h)
{
    (void)h;
    return NULL;
}

int64_t rt_ray3d_hit_triangle(void *h)
{
    (void)h;
    return -1;
}

int8_t rt_sphere3d_overlaps(void *a, double ra, void *b, double rb)
{
    (void)a;
    (void)ra;
    (void)b;
    (void)rb;
    return 0;
}

void *rt_sphere3d_penetration(void *a, double ra, void *b, double rb)
{
    (void)a;
    (void)ra;
    (void)b;
    (void)rb;
    return NULL;
}

void *rt_aabb3d_closest_point(void *mn, void *mx, void *p)
{
    (void)mn;
    (void)mx;
    (void)p;
    return NULL;
}

int8_t rt_aabb3d_sphere_overlaps(void *mn, void *mx, void *c, double r)
{
    (void)mn;
    (void)mx;
    (void)c;
    (void)r;
    return 0;
}

void *rt_segment3d_closest_point(void *a, void *b, void *p)
{
    (void)a;
    (void)b;
    (void)p;
    return NULL;
}

int8_t rt_capsule3d_sphere_overlaps(void *a, void *b, double cr, void *c, double sr)
{
    (void)a;
    (void)b;
    (void)cr;
    (void)c;
    (void)sr;
    return 0;
}

int8_t rt_capsule3d_aabb_overlaps(void *a, void *b, double r, void *mn, void *mx)
{
    (void)a;
    (void)b;
    (void)r;
    (void)mn;
    (void)mx;
    return 0;
}

/* FPS Camera stubs */
void rt_camera3d_fps_init(void *c)
{
    (void)c;
}

void rt_camera3d_fps_update(
    void *c, double a, double b, double d, double e, double f, double g, double h)
{
    (void)c;
    (void)a;
    (void)b;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
}

double rt_camera3d_get_yaw(void *c)
{
    (void)c;
    return 0.0;
}

double rt_camera3d_get_pitch(void *c)
{
    (void)c;
    return 0.0;
}

void rt_camera3d_set_yaw(void *c, double v)
{
    (void)c;
    (void)v;
}

void rt_camera3d_set_pitch(void *c, double v)
{
    (void)c;
    (void)v;
}

/* HUD overlay stubs */
void rt_canvas3d_draw_rect2d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl)
{
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
}

void rt_canvas3d_draw_crosshair(void *c, int64_t cl, int64_t sz)
{
    (void)c;
    (void)cl;
    (void)sz;
}

void rt_canvas3d_draw_text2d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl)
{
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
}

/* Debug gizmo stubs */
void rt_canvas3d_draw_aabb_wire(void *c, void *mn, void *mx, int64_t cl) { (void)c; (void)mn; (void)mx; (void)cl; }
void rt_canvas3d_draw_sphere_wire(void *c, void *ctr, double r, int64_t cl) { (void)c; (void)ctr; (void)r; (void)cl; }
void rt_canvas3d_draw_debug_ray(void *c, void *o, void *d, double l, int64_t cl) { (void)c; (void)o; (void)d; (void)l; (void)cl; }
void rt_canvas3d_draw_axis(void *c, void *o, double s) { (void)c; (void)o; (void)s; }

/* Fog stubs */
void rt_canvas3d_set_fog(void *c, double n, double f, double r, double g, double b) { (void)c; (void)n; (void)f; (void)r; (void)g; (void)b; }
void rt_canvas3d_clear_fog(void *c) { (void)c; }

/* Shadow stubs */
void rt_canvas3d_enable_shadows(void *c, int64_t r) { (void)c; (void)r; }
void rt_canvas3d_disable_shadows(void *c) { (void)c; }
void rt_canvas3d_set_shadow_bias(void *c, double b) { (void)c; (void)b; }

/* Audio3D stubs */
void rt_audio3d_set_listener(void *p, void *f)
{
    (void)p;
    (void)f;
}

int64_t rt_audio3d_play_at(void *s, void *p, double d, int64_t v)
{
    (void)s;
    (void)p;
    (void)d;
    (void)v;
    return 0;
}

void rt_audio3d_update_voice(int64_t v, void *p)
{
    (void)v;
    (void)p;
}

/* Physics3D World stubs */
void *rt_world3d_new(double gx, double gy, double gz)
{
    (void)gx;
    (void)gy;
    (void)gz;
    rt_trap("Physics3DWorld.New: graphics support not compiled in");
    return NULL;
}

void rt_world3d_step(void *w, double dt)
{
    (void)w;
    (void)dt;
}

void rt_world3d_add(void *w, void *b)
{
    (void)w;
    (void)b;
}

void rt_world3d_remove(void *w, void *b)
{
    (void)w;
    (void)b;
}

int64_t rt_world3d_body_count(void *w)
{
    (void)w;
    return 0;
}

void rt_world3d_set_gravity(void *w, double gx, double gy, double gz)
{
    (void)w;
    (void)gx;
    (void)gy;
    (void)gz;
}

/* Physics3D Body stubs */
void *rt_body3d_new_aabb(double hx, double hy, double hz, double mass)
{
    (void)hx;
    (void)hy;
    (void)hz;
    (void)mass;
    return NULL;
}

void *rt_body3d_new_sphere(double radius, double mass)
{
    (void)radius;
    (void)mass;
    return NULL;
}

void *rt_body3d_new_capsule(double radius, double height, double mass)
{
    (void)radius;
    (void)height;
    (void)mass;
    return NULL;
}

void rt_body3d_set_position(void *o, double x, double y, double z)
{
    (void)o;
    (void)x;
    (void)y;
    (void)z;
}

void *rt_body3d_get_position(void *o)
{
    (void)o;
    return NULL;
}

void rt_body3d_set_velocity(void *o, double vx, double vy, double vz)
{
    (void)o;
    (void)vx;
    (void)vy;
    (void)vz;
}

void *rt_body3d_get_velocity(void *o)
{
    (void)o;
    return NULL;
}

void rt_body3d_apply_force(void *o, double fx, double fy, double fz)
{
    (void)o;
    (void)fx;
    (void)fy;
    (void)fz;
}

void rt_body3d_apply_impulse(void *o, double ix, double iy, double iz)
{
    (void)o;
    (void)ix;
    (void)iy;
    (void)iz;
}

void rt_body3d_set_restitution(void *o, double r)
{
    (void)o;
    (void)r;
}

double rt_body3d_get_restitution(void *o)
{
    (void)o;
    return 0.0;
}

void rt_body3d_set_friction(void *o, double f)
{
    (void)o;
    (void)f;
}

double rt_body3d_get_friction(void *o)
{
    (void)o;
    return 0.0;
}

void rt_body3d_set_collision_layer(void *o, int64_t l)
{
    (void)o;
    (void)l;
}

int64_t rt_body3d_get_collision_layer(void *o)
{
    (void)o;
    return 0;
}

void rt_body3d_set_collision_mask(void *o, int64_t m)
{
    (void)o;
    (void)m;
}

int64_t rt_body3d_get_collision_mask(void *o)
{
    (void)o;
    return 0;
}

void rt_body3d_set_static(void *o, int8_t s)
{
    (void)o;
    (void)s;
}

int8_t rt_body3d_is_static(void *o)
{
    (void)o;
    return 0;
}

void rt_body3d_set_trigger(void *o, int8_t t)
{
    (void)o;
    (void)t;
}

int8_t rt_body3d_is_trigger(void *o)
{
    (void)o;
    return 0;
}

int8_t rt_body3d_is_grounded(void *o)
{
    (void)o;
    return 0;
}

void *rt_body3d_get_ground_normal(void *o)
{
    (void)o;
    return NULL;
}

double rt_body3d_get_mass(void *o)
{
    (void)o;
    return 0.0;
}

/* Character3D stubs */
void *rt_character3d_new(double radius, double height, double mass)
{
    (void)radius;
    (void)height;
    (void)mass;
    return NULL;
}

void rt_character3d_move(void *c, void *v, double dt)
{
    (void)c;
    (void)v;
    (void)dt;
}

void rt_character3d_set_step_height(void *c, double h)
{
    (void)c;
    (void)h;
}

double rt_character3d_get_step_height(void *c)
{
    (void)c;
    return 0.3;
}

void rt_character3d_set_slope_limit(void *c, double d)
{
    (void)c;
    (void)d;
}

int8_t rt_character3d_is_grounded(void *c)
{
    (void)c;
    return 0;
}

int8_t rt_character3d_just_landed(void *c)
{
    (void)c;
    return 0;
}

void *rt_character3d_get_position(void *c)
{
    (void)c;
    return NULL;
}

void rt_character3d_set_position(void *c, double x, double y, double z)
{
    (void)c;
    (void)x;
    (void)y;
    (void)z;
}

/* Trigger3D stubs */
void   *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1) { (void)x0; (void)y0; (void)z0; (void)x1; (void)y1; (void)z1; return NULL; }
int8_t  rt_trigger3d_contains(void *t, void *p) { (void)t; (void)p; return 0; }
void    rt_trigger3d_update(void *t, void *w) { (void)t; (void)w; }
int64_t rt_trigger3d_get_enter_count(void *t) { (void)t; return 0; }
int64_t rt_trigger3d_get_exit_count(void *t) { (void)t; return 0; }
void    rt_trigger3d_set_bounds(void *t, double x0, double y0, double z0, double x1, double y1, double z1) { (void)t; (void)x0; (void)y0; (void)z0; (void)x1; (void)y1; (void)z1; }

/* Camera shake/follow stubs */
void rt_camera3d_shake(void *c, double i, double d, double dc) { (void)c; (void)i; (void)d; (void)dc; }
void rt_camera3d_smooth_follow(void *c, void *t, double d, double h, double s, double dt) { (void)c; (void)t; (void)d; (void)h; (void)s; (void)dt; }
void rt_camera3d_smooth_look_at(void *c, void *t, double s, double dt) { (void)c; (void)t; (void)s; (void)dt; }

/* Transform3D stubs */
void   *rt_transform3d_new(void) { return NULL; }
void    rt_transform3d_set_position(void *x, double a, double b, double c) { (void)x; (void)a; (void)b; (void)c; }
void   *rt_transform3d_get_position(void *x) { (void)x; return NULL; }
void    rt_transform3d_set_rotation(void *x, void *q) { (void)x; (void)q; }
void   *rt_transform3d_get_rotation(void *x) { (void)x; return NULL; }
void    rt_transform3d_set_euler(void *x, double p, double y, double r) { (void)x; (void)p; (void)y; (void)r; }
void    rt_transform3d_set_scale(void *x, double a, double b, double c) { (void)x; (void)a; (void)b; (void)c; }
void   *rt_transform3d_get_scale(void *x) { (void)x; return NULL; }
void   *rt_transform3d_get_matrix(void *x) { (void)x; return NULL; }
void    rt_transform3d_translate(void *x, void *d) { (void)x; (void)d; }
void    rt_transform3d_rotate(void *x, void *a, double ang) { (void)x; (void)a; (void)ang; }
void    rt_transform3d_look_at(void *x, void *t, void *u) { (void)x; (void)t; (void)u; }

/* Path3D stubs */
void   *rt_path3d_new(void) { return NULL; }
void    rt_path3d_add_point(void *p, void *v) { (void)p; (void)v; }
void   *rt_path3d_get_position_at(void *p, double t) { (void)p; (void)t; return NULL; }
void   *rt_path3d_get_direction_at(void *p, double t) { (void)p; (void)t; return NULL; }
double  rt_path3d_get_length(void *p) { (void)p; return 0.0; }
int64_t rt_path3d_get_point_count(void *p) { (void)p; return 0; }
void    rt_path3d_set_looping(void *p, int8_t l) { (void)p; (void)l; }
void    rt_path3d_clear(void *p) { (void)p; }
