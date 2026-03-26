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
#include "rt_decal3d.h"
#include "rt_fbx_loader.h"
#include "rt_graphics.h"
#include "rt_instbatch3d.h"
#include "rt_morphtarget3d.h"
#include "rt_navmesh3d.h"
#include "rt_particles3d.h"
#include "rt_path3d.h"
#include "rt_physics3d.h"
#include "rt_postfx3d.h"
#include "rt_raycast3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include "rt_sprite3d.h"
#include "rt_string.h"
#include "rt_terrain3d.h"
#include "rt_texatlas3d.h"
#include "rt_transform3d.h"
#include "rt_water3d.h"

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

/// @brief Destroy and free destroy resources.
/// @param canvas
void rt_canvas_destroy(void *canvas)
{
    (void)canvas;
}

/// @brief Perform width operation.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_width(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Perform height operation.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_height(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Perform should close operation.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_should_close(void *canvas)
{
    (void)canvas;
    return 1;
}

/// @brief Perform flip operation.
/// @param canvas
void rt_canvas_flip(void *canvas)
{
    (void)canvas;
}

/// @brief Clear all clear.
/// @param canvas
/// @param color
void rt_canvas_clear(void *canvas, int64_t color)
{
    (void)canvas;
    (void)color;
}

/// @brief Perform line operation.
/// @param canvas
/// @param x1
/// @param y1
/// @param x2
/// @param y2
/// @param color
void rt_canvas_line(void *canvas, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color)
{
    (void)canvas;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
}

/// @brief Perform box operation.
/// @param canvas
/// @param x
/// @param y
/// @param w
/// @param h
/// @param color
void rt_canvas_box(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

/// @brief Perform frame operation.
/// @param canvas
/// @param x
/// @param y
/// @param w
/// @param h
/// @param color
void rt_canvas_frame(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

/// @brief Perform disc operation.
/// @param canvas
/// @param cx
/// @param cy
/// @param radius
/// @param color
void rt_canvas_disc(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
}

/// @brief Perform ring operation.
/// @param canvas
/// @param cx
/// @param cy
/// @param radius
/// @param color
void rt_canvas_ring(void *canvas, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    (void)canvas;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
}

/// @brief Perform plot operation.
/// @param canvas
/// @param x
/// @param y
/// @param color
void rt_canvas_plot(void *canvas, int64_t x, int64_t y, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)color;
}

/// @brief Perform poll operation.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_poll(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Perform key held operation.
/// @param canvas
/// @param key
/// @return Result value.
int64_t rt_canvas_key_held(void *canvas, int64_t key)
{
    (void)canvas;
    (void)key;
    return 0;
}

/// @brief Perform text operation.
/// @param canvas
/// @param x
/// @param y
/// @param text
/// @param color
void rt_canvas_text(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}

/// @brief Perform text bg operation.
/// @param canvas
/// @param x
/// @param y
/// @param text
/// @param fg
/// @param bg
void rt_canvas_text_bg(void *canvas, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)fg;
    (void)bg;
}

/// @brief Perform text width operation.
/// @param text
/// @return Result value.
int64_t rt_canvas_text_width(rt_string text)
{
    (void)text;
    return 0;
}

/// @brief Perform text height operation.
/// @return Result value.
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

/// @brief Perform text scaled width operation.
/// @param text
/// @param scale
/// @return Result value.
int64_t rt_canvas_text_scaled_width(rt_string text, int64_t scale)
{
    (void)text;
    (void)scale;
    return 0;
}

/// @brief Perform text centered operation.
/// @param canvas
/// @param y
/// @param text
/// @param color
void rt_canvas_text_centered(void *canvas, int64_t y, rt_string text, int64_t color)
{
    (void)canvas;
    (void)y;
    (void)text;
    (void)color;
}

/// @brief Perform text right operation.
/// @param canvas
/// @param margin
/// @param y
/// @param text
/// @param color
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

/// @brief Perform blit operation.
/// @param canvas
/// @param x
/// @param y
/// @param pixels
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

/// @brief Perform blit alpha operation.
/// @param canvas
/// @param x
/// @param y
/// @param pixels
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

/// @brief Perform flood fill operation.
/// @param canvas
/// @param x
/// @param y
/// @param color
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

/// @brief Perform ellipse operation.
/// @param canvas
/// @param cx
/// @param cy
/// @param rx
/// @param ry
/// @param color
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

/// @brief Perform polyline operation.
/// @param canvas
/// @param points
/// @param count
/// @param color
void rt_canvas_polyline(void *canvas, void *points, int64_t count, int64_t color)
{
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
}

/// @brief Perform polygon operation.
/// @param canvas
/// @param points
/// @param count
/// @param color
void rt_canvas_polygon(void *canvas, void *points, int64_t count, int64_t color)
{
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
}

/// @brief Perform polygon frame operation.
/// @param canvas
/// @param points
/// @param count
/// @param color
void rt_canvas_polygon_frame(void *canvas, void *points, int64_t count, int64_t color)
{
    (void)canvas;
    (void)points;
    (void)count;
    (void)color;
}

/// @brief Get the pixel value.
/// @param canvas
/// @param x
/// @param y
/// @return Result value.
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

/// @brief Save bmp.
/// @param canvas
/// @param path
/// @return Result value.
int64_t rt_canvas_save_bmp(void *canvas, rt_string path)
{
    (void)canvas;
    (void)path;
    return 0;
}

/// @brief Save png.
/// @param canvas
/// @param path
/// @return Result value.
int64_t rt_canvas_save_png(void *canvas, rt_string path)
{
    (void)canvas;
    (void)path;
    return 0;
}

// Color constants — packed 0x00RRGGBB
/// @brief Perform red operation.
/// @return Result value.
int64_t rt_color_red(void)
{
    return 0xFF0000;
}

/// @brief Perform green operation.
/// @return Result value.
int64_t rt_color_green(void)
{
    return 0x00FF00;
}

/// @brief Perform blue operation.
/// @return Result value.
int64_t rt_color_blue(void)
{
    return 0x0000FF;
}

/// @brief Perform white operation.
/// @return Result value.
int64_t rt_color_white(void)
{
    return 0xFFFFFF;
}

/// @brief Perform black operation.
/// @return Result value.
int64_t rt_color_black(void)
{
    return 0x000000;
}

/// @brief Perform yellow operation.
/// @return Result value.
int64_t rt_color_yellow(void)
{
    return 0xFFFF00;
}

/// @brief Perform cyan operation.
/// @return Result value.
int64_t rt_color_cyan(void)
{
    return 0x00FFFF;
}

/// @brief Perform magenta operation.
/// @return Result value.
int64_t rt_color_magenta(void)
{
    return 0xFF00FF;
}

/// @brief Perform gray operation.
/// @return Result value.
int64_t rt_color_gray(void)
{
    return 0x808080;
}

/// @brief Perform orange operation.
/// @return Result value.
int64_t rt_color_orange(void)
{
    return 0xFFA500;
}

/// @brief Perform rgb operation.
/// @param r
/// @param g
/// @param b
/// @return Result value.
int64_t rt_color_rgb(int64_t r, int64_t g, int64_t b)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)(((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8);
}

/// @brief Perform rgba operation.
/// @param r
/// @param g
/// @param b
/// @param a
/// @return Result value.
int64_t rt_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    uint8_t a8 = (a < 0) ? 0 : (a > 255) ? 255 : (uint8_t)a;
    return (int64_t)(((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) |
                     (uint32_t)b8);
}

/// @brief Perform from hsl operation.
/// @param h
/// @param s
/// @param l
/// @return Result value.
int64_t rt_color_from_hsl(int64_t h, int64_t s, int64_t l)
{
    (void)h;
    (void)s;
    (void)l;
    return 0;
}

/// @brief Get the h value.
/// @param color
/// @return Result value.
int64_t rt_color_get_h(int64_t color)
{
    (void)color;
    return 0;
}

/// @brief Get the s value.
/// @param color
/// @return Result value.
int64_t rt_color_get_s(int64_t color)
{
    (void)color;
    return 0;
}

/// @brief Get the l value.
/// @param color
/// @return Result value.
int64_t rt_color_get_l(int64_t color)
{
    (void)color;
    return 0;
}

/// @brief Perform lerp operation.
/// @param c1
/// @param c2
/// @param t
/// @return Result value.
int64_t rt_color_lerp(int64_t c1, int64_t c2, int64_t t)
{
    (void)c1;
    (void)c2;
    (void)t;
    return 0;
}

/// @brief Get the r value.
/// @param color
/// @return Result value.
int64_t rt_color_get_r(int64_t color)
{
    return (color >> 16) & 0xFF;
}

/// @brief Get the g value.
/// @param color
/// @return Result value.
int64_t rt_color_get_g(int64_t color)
{
    return (color >> 8) & 0xFF;
}

/// @brief Get the b value.
/// @param color
/// @return Result value.
int64_t rt_color_get_b(int64_t color)
{
    return color & 0xFF;
}

/// @brief Get the a value.
/// @param color
/// @return Result value.
int64_t rt_color_get_a(int64_t color)
{
    return (color >> 24) & 0xFF;
}

/// @brief Perform brighten operation.
/// @param color
/// @param amount
/// @return Result value.
int64_t rt_color_brighten(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

/// @brief Perform darken operation.
/// @param color
/// @param amount
/// @return Result value.
int64_t rt_color_darken(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

/// @brief Perform from hex operation.
/// @param hex
/// @return Result value.
int64_t rt_color_from_hex(rt_string hex)
{
    (void)hex;
    return 0;
}

/// @brief Perform to hex operation.
/// @param color
/// @return Result value.
rt_string rt_color_to_hex(int64_t color)
{
    (void)color;
    return rt_string_from_bytes("#000000", 7);
}

/// @brief Perform saturate operation.
/// @param color
/// @param amount
/// @return Result value.
int64_t rt_color_saturate(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

/// @brief Perform desaturate operation.
/// @param color
/// @param amount
/// @return Result value.
int64_t rt_color_desaturate(int64_t color, int64_t amount)
{
    (void)color;
    (void)amount;
    return 0;
}

/// @brief Perform complement operation.
/// @param color
/// @return Result value.
int64_t rt_color_complement(int64_t color)
{
    (void)color;
    return 0;
}

/// @brief Perform grayscale operation.
/// @param color
/// @return Result value.
int64_t rt_color_grayscale(int64_t color)
{
    (void)color;
    return 0;
}

/// @brief Perform invert operation.
/// @param color
/// @return Result value.
int64_t rt_color_invert(int64_t color)
{
    (void)color;
    return 0;
}

/// @brief Set the clip rect value.
/// @param canvas
/// @param x
/// @param y
/// @param w
/// @param h
void rt_canvas_set_clip_rect(void *canvas, int64_t x, int64_t y, int64_t w, int64_t h)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

/// @brief Clear all clip rect.
/// @param canvas
void rt_canvas_clear_clip_rect(void *canvas)
{
    (void)canvas;
}

/// @brief Set the title value.
/// @param canvas
/// @param title
void rt_canvas_set_title(void *canvas, rt_string title)
{
    (void)canvas;
    (void)title;
}

/// @brief Get the title value.
/// @param canvas
/// @return Result value.
rt_string rt_canvas_get_title(void *canvas)
{
    (void)canvas;
    return rt_string_from_bytes("", 0);
}

/// @brief Perform resize operation.
/// @param canvas
/// @param width
/// @param height
void rt_canvas_resize(void *canvas, int64_t width, int64_t height)
{
    (void)canvas;
    (void)width;
    (void)height;
}

/// @brief Perform close operation.
/// @param canvas
void rt_canvas_close(void *canvas)
{
    (void)canvas;
}

void *rt_canvas_screenshot(void *canvas)
{
    (void)canvas;
    return NULL;
}

/// @brief Perform fullscreen operation.
/// @param canvas
void rt_canvas_fullscreen(void *canvas)
{
    (void)canvas;
}

/// @brief Perform windowed operation.
/// @param canvas
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

/// @brief Get the scale value.
/// @param canvas
/// @return Result value.
double rt_canvas_get_scale(void *canvas)
{
    (void)canvas;
    return 1.0;
}

/// @brief Get the position value.
/// @param canvas
/// @param x
/// @param y
void rt_canvas_get_position(void *canvas, int64_t *x, int64_t *y)
{
    (void)canvas;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
}

/// @brief Set the position value.
/// @param canvas
/// @param x
/// @param y
void rt_canvas_set_position(void *canvas, int64_t x, int64_t y)
{
    (void)canvas;
    (void)x;
    (void)y;
}

/// @brief Get the fps value.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_get_fps(void *canvas)
{
    (void)canvas;
    return -1;
}

/// @brief Set the fps value.
/// @param canvas
/// @param fps
void rt_canvas_set_fps(void *canvas, int64_t fps)
{
    (void)canvas;
    (void)fps;
}

/// @brief Get the delta time value.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_get_delta_time(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Set the dt max value.
/// @param canvas
/// @param max_ms
void rt_canvas_set_dt_max(void *canvas, int64_t max_ms)
{
    (void)canvas;
    (void)max_ms;
}

/// @brief Begin frame.
/// @param canvas
/// @return Result value.
int64_t rt_canvas_begin_frame(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Check if maximized.
/// @param canvas
/// @return Result value.
int8_t rt_canvas_is_maximized(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Perform maximize operation.
/// @param canvas
void rt_canvas_maximize(void *canvas)
{
    (void)canvas;
}

/// @brief Check if minimized.
/// @param canvas
/// @return Result value.
int8_t rt_canvas_is_minimized(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Perform minimize operation.
/// @param canvas
void rt_canvas_minimize(void *canvas)
{
    (void)canvas;
}

/// @brief Perform restore operation.
/// @param canvas
void rt_canvas_restore(void *canvas)
{
    (void)canvas;
}

/// @brief Check if focused.
/// @param canvas
/// @return Result value.
int8_t rt_canvas_is_focused(void *canvas)
{
    (void)canvas;
    return 0;
}

/// @brief Perform focus operation.
/// @param canvas
void rt_canvas_focus(void *canvas)
{
    (void)canvas;
}

/// @brief Perform prevent close operation.
/// @param canvas
/// @param prevent
void rt_canvas_prevent_close(void *canvas, int64_t prevent)
{
    (void)canvas;
    (void)prevent;
}

/// @brief Get the monitor size value.
/// @param canvas
/// @param w
/// @param h
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

/// @brief Perform canvas3d set skybox operation.
/// @param c
/// @param cm
void rt_canvas3d_set_skybox(void *c, void *cm)
{
    (void)c;
    (void)cm;
}

/// @brief Perform canvas3d clear skybox operation.
/// @param c
void rt_canvas3d_clear_skybox(void *c)
{
    (void)c;
}

/// @brief Perform material3d set env map operation.
/// @param o
/// @param cm
void rt_material3d_set_env_map(void *o, void *cm)
{
    (void)o;
    (void)cm;
}

/// @brief Perform material3d set reflectivity operation.
/// @param o
/// @param r
void rt_material3d_set_reflectivity(void *o, double r)
{
    (void)o;
    (void)r;
}

/// @brief Perform material3d get reflectivity operation.
/// @param o
/// @return Result value.
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

/// @brief Perform rendertarget3d get width operation.
/// @param o
/// @return Result value.
int64_t rt_rendertarget3d_get_width(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform rendertarget3d get height operation.
/// @param o
/// @return Result value.
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

/// @brief Perform canvas3d set render target operation.
/// @param c
/// @param t
void rt_canvas3d_set_render_target(void *c, void *t)
{
    (void)c;
    (void)t;
}

/// @brief Perform canvas3d reset render target operation.
/// @param c
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

/// @brief Perform canvas3d clear operation.
/// @param o
/// @param r
/// @param g
/// @param b
void rt_canvas3d_clear(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Perform canvas3d begin operation.
/// @param o
/// @param c
void rt_canvas3d_begin(void *o, void *c)
{
    (void)o;
    (void)c;
}

/// @brief Perform canvas3d draw mesh operation.
/// @param o
/// @param m
/// @param t
/// @param mt
void rt_canvas3d_draw_mesh(void *o, void *m, void *t, void *mt)
{
    (void)o;
    (void)m;
    (void)t;
    (void)mt;
}

/// @brief Perform canvas3d end operation.
/// @param o
void rt_canvas3d_end(void *o)
{
    (void)o;
}

/// @brief Perform canvas3d flip operation.
/// @param o
void rt_canvas3d_flip(void *o)
{
    (void)o;
}

/// @brief Perform canvas3d poll operation.
/// @param o
/// @return Result value.
int64_t rt_canvas3d_poll(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d should close operation.
/// @param o
/// @return Result value.
int8_t rt_canvas3d_should_close(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d set wireframe operation.
/// @param o
/// @param e
void rt_canvas3d_set_wireframe(void *o, int8_t e)
{
    (void)o;
    (void)e;
}

/// @brief Perform canvas3d set backface cull operation.
/// @param o
/// @param e
void rt_canvas3d_set_backface_cull(void *o, int8_t e)
{
    (void)o;
    (void)e;
}

/// @brief Perform canvas3d get width operation.
/// @param o
/// @return Result value.
int64_t rt_canvas3d_get_width(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d get height operation.
/// @param o
/// @return Result value.
int64_t rt_canvas3d_get_height(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d get fps operation.
/// @param o
/// @return Result value.
int64_t rt_canvas3d_get_fps(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d get delta time operation.
/// @param o
/// @return Result value.
int64_t rt_canvas3d_get_delta_time(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d set dt max operation.
/// @param o
/// @param m
void rt_canvas3d_set_dt_max(void *o, int64_t m)
{
    (void)o;
    (void)m;
}

/// @brief Perform canvas3d set light operation.
/// @param o
/// @param i
/// @param l
void rt_canvas3d_set_light(void *o, int64_t i, void *l)
{
    (void)o;
    (void)i;
    (void)l;
}

/// @brief Perform canvas3d set ambient operation.
/// @param o
/// @param r
/// @param g
/// @param b
void rt_canvas3d_set_ambient(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Perform canvas3d draw line3d operation.
/// @param o
/// @param f
/// @param t
/// @param c
void rt_canvas3d_draw_line3d(void *o, void *f, void *t, int64_t c)
{
    (void)o;
    (void)f;
    (void)t;
    (void)c;
}

/// @brief Perform canvas3d draw point3d operation.
/// @param o
/// @param p
/// @param c
/// @param s
void rt_canvas3d_draw_point3d(void *o, void *p, int64_t c, int64_t s)
{
    (void)o;
    (void)p;
    (void)c;
    (void)s;
}

/// @brief Perform canvas3d get backend operation.
/// @param o
/// @return Result value.
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

/// @brief Perform mesh3d get vertex count operation.
/// @param o
/// @return Result value.
int64_t rt_mesh3d_get_vertex_count(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform mesh3d get triangle count operation.
/// @param o
/// @return Result value.
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

/// @brief Perform mesh3d add triangle operation.
/// @param o
/// @param v0
/// @param v1
/// @param v2
void rt_mesh3d_add_triangle(void *o, int64_t v0, int64_t v1, int64_t v2)
{
    (void)o;
    (void)v0;
    (void)v1;
    (void)v2;
}

/// @brief Perform mesh3d recalc normals operation.
/// @param o
void rt_mesh3d_recalc_normals(void *o)
{
    (void)o;
}

void *rt_mesh3d_clone(void *o)
{
    (void)o;
    return NULL;
}

/// @brief Perform mesh3d transform operation.
/// @param o
/// @param m
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

/// @brief Perform camera3d look at operation.
/// @param o
/// @param e
/// @param t
/// @param u
void rt_camera3d_look_at(void *o, void *e, void *t, void *u)
{
    (void)o;
    (void)e;
    (void)t;
    (void)u;
}

/// @brief Perform camera3d orbit operation.
/// @param o
/// @param t
/// @param d
/// @param y
/// @param p
void rt_camera3d_orbit(void *o, void *t, double d, double y, double p)
{
    (void)o;
    (void)t;
    (void)d;
    (void)y;
    (void)p;
}

/// @brief Perform camera3d get fov operation.
/// @param o
/// @return Result value.
double rt_camera3d_get_fov(void *o)
{
    (void)o;
    return 0.0;
}

/// @brief Perform camera3d set fov operation.
/// @param o
/// @param f
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

/// @brief Perform camera3d set position operation.
/// @param o
/// @param p
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

/// @brief Perform material3d set color operation.
/// @param o
/// @param r
/// @param g
/// @param b
void rt_material3d_set_color(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Perform material3d set texture operation.
/// @param o
/// @param p
void rt_material3d_set_texture(void *o, void *p)
{
    (void)o;
    (void)p;
}

/// @brief Perform material3d set shininess operation.
/// @param o
/// @param s
void rt_material3d_set_shininess(void *o, double s)
{
    (void)o;
    (void)s;
}

/// @brief Perform material3d set alpha operation.
/// @param o
/// @param a
void rt_material3d_set_alpha(void *o, double a)
{
    (void)o;
    (void)a;
}

/// @brief Perform material3d get alpha operation.
/// @param o
/// @return Result value.
double rt_material3d_get_alpha(void *o)
{
    (void)o;
    return 1.0;
}

/// @brief Perform material3d set unlit operation.
/// @param o
/// @param u
void rt_material3d_set_unlit(void *o, int8_t u)
{
    (void)o;
    (void)u;
}

/// @brief Perform material3d set normal map operation.
/// @param o
/// @param p
void rt_material3d_set_normal_map(void *o, void *p)
{
    (void)o;
    (void)p;
}

/// @brief Perform material3d set specular map operation.
/// @param o
/// @param p
void rt_material3d_set_specular_map(void *o, void *p)
{
    (void)o;
    (void)p;
}

/// @brief Perform material3d set emissive map operation.
/// @param o
/// @param p
void rt_material3d_set_emissive_map(void *o, void *p)
{
    (void)o;
    (void)p;
}

/// @brief Perform material3d set emissive color operation.
/// @param o
/// @param r
/// @param g
/// @param b
void rt_material3d_set_emissive_color(void *o, double r, double g, double b)
{
    (void)o;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Perform mesh3d calc tangents operation.
/// @param o
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

/// @brief Perform light3d set intensity operation.
/// @param o
/// @param i
void rt_light3d_set_intensity(void *o, double i)
{
    (void)o;
    (void)i;
}

/// @brief Perform light3d set color operation.
/// @param o
/// @param r
/// @param g
/// @param b
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

/// @brief Perform scene3d add operation.
/// @param s
/// @param n
void rt_scene3d_add(void *s, void *n)
{
    (void)s;
    (void)n;
}

/// @brief Perform scene3d remove operation.
/// @param s
/// @param n
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

/// @brief Perform scene3d draw operation.
/// @param s
/// @param c
/// @param cam
void rt_scene3d_draw(void *s, void *c, void *cam)
{
    (void)s;
    (void)c;
    (void)cam;
}

/// @brief Perform scene3d clear operation.
/// @param s
void rt_scene3d_clear(void *s)
{
    (void)s;
}

/// @brief Perform scene3d get node count operation.
/// @param s
/// @return Result value.
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

/// @brief Perform node3d set position operation.
/// @param n
/// @param x
/// @param y
/// @param z
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

/// @brief Perform node3d set rotation operation.
/// @param n
/// @param q
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

/// @brief Perform node3d set scale operation.
/// @param n
/// @param x
/// @param y
/// @param z
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

/// @brief Perform node3d add child operation.
/// @param n
/// @param c
void rt_scene_node3d_add_child(void *n, void *c)
{
    (void)n;
    (void)c;
}

/// @brief Perform node3d remove child operation.
/// @param n
/// @param c
void rt_scene_node3d_remove_child(void *n, void *c)
{
    (void)n;
    (void)c;
}

/// @brief Perform node3d child count operation.
/// @param n
/// @return Result value.
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

/// @brief Perform node3d set mesh operation.
/// @param n
/// @param m
void rt_scene_node3d_set_mesh(void *n, void *m)
{
    (void)n;
    (void)m;
}

/// @brief Perform node3d set material operation.
/// @param n
/// @param m
void rt_scene_node3d_set_material(void *n, void *m)
{
    (void)n;
    (void)m;
}

/// @brief Perform node3d set visible operation.
/// @param n
/// @param v
void rt_scene_node3d_set_visible(void *n, int8_t v)
{
    (void)n;
    (void)v;
}

/// @brief Perform node3d get visible operation.
/// @param n
/// @return Result value.
int8_t rt_scene_node3d_get_visible(void *n)
{
    (void)n;
    return 0;
}

/// @brief Perform node3d set name operation.
/// @param n
/// @param s
void rt_scene_node3d_set_name(void *n, rt_string s)
{
    (void)n;
    (void)s;
}

/// @brief Perform node3d get name operation.
/// @param n
/// @return Result value.
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

/// @brief Perform scene3d get culled count operation.
/// @param s
/// @return Result value.
int64_t rt_scene3d_get_culled_count(void *s)
{
    (void)s;
    return 0;
}

/* LOD stubs */
/// @brief Perform node3d add lod operation.
/// @param n
/// @param d
/// @param m
void rt_scene_node3d_add_lod(void *n, double d, void *m)
{
    (void)n;
    (void)d;
    (void)m;
}

/// @brief Perform node3d clear lod operation.
/// @param n
void rt_scene_node3d_clear_lod(void *n)
{
    (void)n;
}

/* Skeleton3D / Animation3D / AnimPlayer3D stubs */
void *rt_skeleton3d_new(void)
{
    rt_trap("Skeleton3D.New: graphics support not compiled in");
    return NULL;
}

/// @brief Perform skeleton3d add bone operation.
/// @param s
/// @param n
/// @param p
/// @param m
/// @return Result value.
int64_t rt_skeleton3d_add_bone(void *s, rt_string n, int64_t p, void *m)
{
    (void)s;
    (void)n;
    (void)p;
    (void)m;
    return -1;
}

/// @brief Perform skeleton3d compute inverse bind operation.
/// @param s
void rt_skeleton3d_compute_inverse_bind(void *s)
{
    (void)s;
}

/// @brief Perform skeleton3d get bone count operation.
/// @param s
/// @return Result value.
int64_t rt_skeleton3d_get_bone_count(void *s)
{
    (void)s;
    return 0;
}

/// @brief Perform skeleton3d find bone operation.
/// @param s
/// @param n
/// @return Result value.
int64_t rt_skeleton3d_find_bone(void *s, rt_string n)
{
    (void)s;
    (void)n;
    return -1;
}

/// @brief Perform skeleton3d get bone name operation.
/// @param s
/// @param i
/// @return Result value.
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

/// @brief Perform animation3d add keyframe operation.
/// @param a
/// @param b
/// @param t
/// @param p
/// @param r
/// @param s
void rt_animation3d_add_keyframe(void *a, int64_t b, double t, void *p, void *r, void *s)
{
    (void)a;
    (void)b;
    (void)t;
    (void)p;
    (void)r;
    (void)s;
}

/// @brief Perform animation3d set looping operation.
/// @param a
/// @param l
void rt_animation3d_set_looping(void *a, int8_t l)
{
    (void)a;
    (void)l;
}

/// @brief Perform animation3d get looping operation.
/// @param a
/// @return Result value.
int8_t rt_animation3d_get_looping(void *a)
{
    (void)a;
    return 0;
}

/// @brief Perform animation3d get duration operation.
/// @param a
/// @return Result value.
double rt_animation3d_get_duration(void *a)
{
    (void)a;
    return 0.0;
}

/// @brief Perform animation3d get name operation.
/// @param a
/// @return Result value.
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

/// @brief Perform anim player3d play operation.
/// @param p
/// @param a
void rt_anim_player3d_play(void *p, void *a)
{
    (void)p;
    (void)a;
}

/// @brief Perform anim player3d crossfade operation.
/// @param p
/// @param a
/// @param d
void rt_anim_player3d_crossfade(void *p, void *a, double d)
{
    (void)p;
    (void)a;
    (void)d;
}

/// @brief Perform anim player3d stop operation.
/// @param p
void rt_anim_player3d_stop(void *p)
{
    (void)p;
}

/// @brief Perform anim player3d update operation.
/// @param p
/// @param d
void rt_anim_player3d_update(void *p, double d)
{
    (void)p;
    (void)d;
}

/// @brief Perform anim player3d set speed operation.
/// @param p
/// @param s
void rt_anim_player3d_set_speed(void *p, double s)
{
    (void)p;
    (void)s;
}

/// @brief Perform anim player3d get speed operation.
/// @param p
/// @return Result value.
double rt_anim_player3d_get_speed(void *p)
{
    (void)p;
    return 1.0;
}

/// @brief Perform anim player3d is playing operation.
/// @param p
/// @return Result value.
int8_t rt_anim_player3d_is_playing(void *p)
{
    (void)p;
    return 0;
}

/// @brief Perform anim player3d get time operation.
/// @param p
/// @return Result value.
double rt_anim_player3d_get_time(void *p)
{
    (void)p;
    return 0.0;
}

/// @brief Perform anim player3d set time operation.
/// @param p
/// @param t
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

/// @brief Perform mesh3d set skeleton operation.
/// @param m
/// @param s
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

/// @brief Perform canvas3d draw mesh skinned operation.
/// @param c
/// @param m
/// @param t
/// @param mat
/// @param p
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

/// @brief Perform fbx mesh count operation.
/// @param f
/// @return Result value.
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

/// @brief Perform fbx animation count operation.
/// @param f
/// @return Result value.
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

/// @brief Perform fbx get animation name operation.
/// @param f
/// @param i
/// @return Result value.
rt_string rt_fbx_get_animation_name(void *f, int64_t i)
{
    (void)f;
    (void)i;
    return NULL;
}

/// @brief Perform fbx material count operation.
/// @param f
/// @return Result value.
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

/// @brief Perform morphtarget3d add shape operation.
/// @param m
/// @param n
/// @return Result value.
int64_t rt_morphtarget3d_add_shape(void *m, rt_string n)
{
    (void)m;
    (void)n;
    return -1;
}

/// @brief Perform morphtarget3d set delta operation.
/// @param m
/// @param s
/// @param v
/// @param dx
/// @param dy
/// @param dz
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

/// @brief Perform morphtarget3d set weight operation.
/// @param m
/// @param s
/// @param w
void rt_morphtarget3d_set_weight(void *m, int64_t s, double w)
{
    (void)m;
    (void)s;
    (void)w;
}

/// @brief Perform morphtarget3d get weight operation.
/// @param m
/// @param s
/// @return Result value.
double rt_morphtarget3d_get_weight(void *m, int64_t s)
{
    (void)m;
    (void)s;
    return 0.0;
}

/// @brief Perform morphtarget3d set weight by name operation.
/// @param m
/// @param n
/// @param w
void rt_morphtarget3d_set_weight_by_name(void *m, rt_string n, double w)
{
    (void)m;
    (void)n;
    (void)w;
}

/// @brief Perform morphtarget3d get shape count operation.
/// @param m
/// @return Result value.
int64_t rt_morphtarget3d_get_shape_count(void *m)
{
    (void)m;
    return 0;
}

/// @brief Perform mesh3d set morph targets operation.
/// @param m
/// @param mt
void rt_mesh3d_set_morph_targets(void *m, void *mt)
{
    (void)m;
    (void)mt;
}

/// @brief Perform canvas3d draw mesh morphed operation.
/// @param c
/// @param m
/// @param t
/// @param mat
/// @param mt
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

/// @brief Perform particles3d set position operation.
/// @param o
/// @param x
/// @param y
/// @param z
void rt_particles3d_set_position(void *o, double x, double y, double z)
{
    (void)o;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Perform particles3d set direction operation.
/// @param o
/// @param dx
/// @param dy
/// @param dz
/// @param s
void rt_particles3d_set_direction(void *o, double dx, double dy, double dz, double s)
{
    (void)o;
    (void)dx;
    (void)dy;
    (void)dz;
    (void)s;
}

/// @brief Perform particles3d set speed operation.
/// @param o
/// @param mn
/// @param mx
void rt_particles3d_set_speed(void *o, double mn, double mx)
{
    (void)o;
    (void)mn;
    (void)mx;
}

/// @brief Perform particles3d set lifetime operation.
/// @param o
/// @param mn
/// @param mx
void rt_particles3d_set_lifetime(void *o, double mn, double mx)
{
    (void)o;
    (void)mn;
    (void)mx;
}

/// @brief Perform particles3d set size operation.
/// @param o
/// @param s
/// @param e
void rt_particles3d_set_size(void *o, double s, double e)
{
    (void)o;
    (void)s;
    (void)e;
}

/// @brief Perform particles3d set gravity operation.
/// @param o
/// @param gx
/// @param gy
/// @param gz
void rt_particles3d_set_gravity(void *o, double gx, double gy, double gz)
{
    (void)o;
    (void)gx;
    (void)gy;
    (void)gz;
}

/// @brief Perform particles3d set color operation.
/// @param o
/// @param sc
/// @param ec
void rt_particles3d_set_color(void *o, int64_t sc, int64_t ec)
{
    (void)o;
    (void)sc;
    (void)ec;
}

/// @brief Perform particles3d set alpha operation.
/// @param o
/// @param sa
/// @param ea
void rt_particles3d_set_alpha(void *o, double sa, double ea)
{
    (void)o;
    (void)sa;
    (void)ea;
}

/// @brief Perform particles3d set rate operation.
/// @param o
/// @param r
void rt_particles3d_set_rate(void *o, double r)
{
    (void)o;
    (void)r;
}

/// @brief Perform particles3d set additive operation.
/// @param o
/// @param a
void rt_particles3d_set_additive(void *o, int8_t a)
{
    (void)o;
    (void)a;
}

/// @brief Perform particles3d set texture operation.
/// @param o
/// @param t
void rt_particles3d_set_texture(void *o, void *t)
{
    (void)o;
    (void)t;
}

/// @brief Perform particles3d set emitter shape operation.
/// @param o
/// @param s
void rt_particles3d_set_emitter_shape(void *o, int64_t s)
{
    (void)o;
    (void)s;
}

/// @brief Perform particles3d set emitter size operation.
/// @param o
/// @param sx
/// @param sy
/// @param sz
void rt_particles3d_set_emitter_size(void *o, double sx, double sy, double sz)
{
    (void)o;
    (void)sx;
    (void)sy;
    (void)sz;
}

/// @brief Perform particles3d start operation.
/// @param o
void rt_particles3d_start(void *o)
{
    (void)o;
}

/// @brief Perform particles3d stop operation.
/// @param o
void rt_particles3d_stop(void *o)
{
    (void)o;
}

/// @brief Perform particles3d burst operation.
/// @param o
/// @param n
void rt_particles3d_burst(void *o, int64_t n)
{
    (void)o;
    (void)n;
}

/// @brief Perform particles3d clear operation.
/// @param o
void rt_particles3d_clear(void *o)
{
    (void)o;
}

/// @brief Perform particles3d update operation.
/// @param o
/// @param dt
void rt_particles3d_update(void *o, double dt)
{
    (void)o;
    (void)dt;
}

/// @brief Perform particles3d draw operation.
/// @param o
/// @param c
/// @param cam
void rt_particles3d_draw(void *o, void *c, void *cam)
{
    (void)o;
    (void)c;
    (void)cam;
}

/// @brief Perform particles3d get count operation.
/// @param o
/// @return Result value.
int64_t rt_particles3d_get_count(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform particles3d get emitting operation.
/// @param o
/// @return Result value.
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

/// @brief Perform postfx3d add bloom operation.
/// @param o
/// @param t
/// @param i
/// @param b
void rt_postfx3d_add_bloom(void *o, double t, double i, int64_t b)
{
    (void)o;
    (void)t;
    (void)i;
    (void)b;
}

/// @brief Perform postfx3d add tonemap operation.
/// @param o
/// @param m
/// @param e
void rt_postfx3d_add_tonemap(void *o, int64_t m, double e)
{
    (void)o;
    (void)m;
    (void)e;
}

/// @brief Perform postfx3d add fxaa operation.
/// @param o
void rt_postfx3d_add_fxaa(void *o)
{
    (void)o;
}

/// @brief Perform postfx3d add color grade operation.
/// @param o
/// @param b
/// @param c
/// @param s
void rt_postfx3d_add_color_grade(void *o, double b, double c, double s)
{
    (void)o;
    (void)b;
    (void)c;
    (void)s;
}

/// @brief Perform postfx3d add vignette operation.
/// @param o
/// @param r
/// @param s
void rt_postfx3d_add_vignette(void *o, double r, double s)
{
    (void)o;
    (void)r;
    (void)s;
}

/// @brief Perform postfx3d set enabled operation.
/// @param o
/// @param e
void rt_postfx3d_set_enabled(void *o, int8_t e)
{
    (void)o;
    (void)e;
}

/// @brief Perform postfx3d get enabled operation.
/// @param o
/// @return Result value.
int8_t rt_postfx3d_get_enabled(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform postfx3d clear operation.
/// @param o
void rt_postfx3d_clear(void *o)
{
    (void)o;
}

/// @brief Perform postfx3d get effect count operation.
/// @param o
/// @return Result value.
int64_t rt_postfx3d_get_effect_count(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform canvas3d set post fx operation.
/// @param c
/// @param fx
void rt_canvas3d_set_post_fx(void *c, void *fx)
{
    (void)c;
    (void)fx;
}

/// @brief Perform postfx3d apply to canvas operation.
/// @param c
void rt_postfx3d_apply_to_canvas(void *c)
{
    (void)c;
}

/* Ray3D / AABB3D / RayHit3D stubs */
/// @brief Perform ray3d intersect triangle operation.
/// @param o
/// @param d
/// @param v0
/// @param v1
/// @param v2
/// @return Result value.
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

/// @brief Perform ray3d intersect aabb operation.
/// @param o
/// @param d
/// @param mn
/// @param mx
/// @return Result value.
double rt_ray3d_intersect_aabb(void *o, void *d, void *mn, void *mx)
{
    (void)o;
    (void)d;
    (void)mn;
    (void)mx;
    return -1.0;
}

/// @brief Perform ray3d intersect sphere operation.
/// @param o
/// @param d
/// @param c
/// @param r
/// @return Result value.
double rt_ray3d_intersect_sphere(void *o, void *d, void *c, double r)
{
    (void)o;
    (void)d;
    (void)c;
    (void)r;
    return -1.0;
}

/// @brief Perform aabb3d overlaps operation.
/// @param a0
/// @param a1
/// @param b0
/// @param b1
/// @return Result value.
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

/// @brief Perform ray3d hit distance operation.
/// @param h
/// @return Result value.
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

/// @brief Perform ray3d hit triangle operation.
/// @param h
/// @return Result value.
int64_t rt_ray3d_hit_triangle(void *h)
{
    (void)h;
    return -1;
}

/// @brief Perform sphere3d overlaps operation.
/// @param a
/// @param ra
/// @param b
/// @param rb
/// @return Result value.
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

/// @brief Perform aabb3d sphere overlaps operation.
/// @param mn
/// @param mx
/// @param c
/// @param r
/// @return Result value.
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

/// @brief Perform capsule3d sphere overlaps operation.
/// @param a
/// @param b
/// @param cr
/// @param c
/// @param sr
/// @return Result value.
int8_t rt_capsule3d_sphere_overlaps(void *a, void *b, double cr, void *c, double sr)
{
    (void)a;
    (void)b;
    (void)cr;
    (void)c;
    (void)sr;
    return 0;
}

/// @brief Perform capsule3d aabb overlaps operation.
/// @param a
/// @param b
/// @param r
/// @param mn
/// @param mx
/// @return Result value.
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
/// @brief Perform camera3d fps init operation.
/// @param c
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

/// @brief Perform camera3d get yaw operation.
/// @param c
/// @return Result value.
double rt_camera3d_get_yaw(void *c)
{
    (void)c;
    return 0.0;
}

/// @brief Perform camera3d get pitch operation.
/// @param c
/// @return Result value.
double rt_camera3d_get_pitch(void *c)
{
    (void)c;
    return 0.0;
}

/// @brief Perform camera3d set yaw operation.
/// @param c
/// @param v
void rt_camera3d_set_yaw(void *c, double v)
{
    (void)c;
    (void)v;
}

/// @brief Perform camera3d set pitch operation.
/// @param c
/// @param v
void rt_camera3d_set_pitch(void *c, double v)
{
    (void)c;
    (void)v;
}

/* HUD overlay stubs */
/// @brief Perform canvas3d draw rect2d operation.
/// @param c
/// @param x
/// @param y
/// @param w
/// @param h
/// @param cl
void rt_canvas3d_draw_rect2d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl)
{
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
}

/// @brief Perform canvas3d draw crosshair operation.
/// @param c
/// @param cl
/// @param sz
void rt_canvas3d_draw_crosshair(void *c, int64_t cl, int64_t sz)
{
    (void)c;
    (void)cl;
    (void)sz;
}

/// @brief Perform canvas3d draw text2d operation.
/// @param c
/// @param x
/// @param y
/// @param t
/// @param cl
void rt_canvas3d_draw_text2d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl)
{
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
}

/* Debug gizmo stubs */
/// @brief Perform canvas3d draw aabb wire operation.
/// @param c
/// @param mn
/// @param mx
/// @param cl
void rt_canvas3d_draw_aabb_wire(void *c, void *mn, void *mx, int64_t cl)
{
    (void)c;
    (void)mn;
    (void)mx;
    (void)cl;
}

/// @brief Perform canvas3d draw sphere wire operation.
/// @param c
/// @param ctr
/// @param r
/// @param cl
void rt_canvas3d_draw_sphere_wire(void *c, void *ctr, double r, int64_t cl)
{
    (void)c;
    (void)ctr;
    (void)r;
    (void)cl;
}

/// @brief Perform canvas3d draw debug ray operation.
/// @param c
/// @param o
/// @param d
/// @param l
/// @param cl
void rt_canvas3d_draw_debug_ray(void *c, void *o, void *d, double l, int64_t cl)
{
    (void)c;
    (void)o;
    (void)d;
    (void)l;
    (void)cl;
}

/// @brief Perform canvas3d draw axis operation.
/// @param c
/// @param o
/// @param s
void rt_canvas3d_draw_axis(void *c, void *o, double s)
{
    (void)c;
    (void)o;
    (void)s;
}

/* Fog stubs */
/// @brief Perform canvas3d set fog operation.
/// @param c
/// @param n
/// @param f
/// @param r
/// @param g
/// @param b
void rt_canvas3d_set_fog(void *c, double n, double f, double r, double g, double b)
{
    (void)c;
    (void)n;
    (void)f;
    (void)r;
    (void)g;
    (void)b;
}

/// @brief Perform canvas3d clear fog operation.
/// @param c
void rt_canvas3d_clear_fog(void *c)
{
    (void)c;
}

/* Shadow stubs */
/// @brief Perform canvas3d enable shadows operation.
/// @param c
/// @param r
void rt_canvas3d_enable_shadows(void *c, int64_t r)
{
    (void)c;
    (void)r;
}

/// @brief Perform canvas3d disable shadows operation.
/// @param c
void rt_canvas3d_disable_shadows(void *c)
{
    (void)c;
}

/// @brief Perform canvas3d set shadow bias operation.
/// @param c
/// @param b
void rt_canvas3d_set_shadow_bias(void *c, double b)
{
    (void)c;
    (void)b;
}

/* Audio3D stubs */
/// @brief Perform audio3d set listener operation.
/// @param p
/// @param f
void rt_audio3d_set_listener(void *p, void *f)
{
    (void)p;
    (void)f;
}

/// @brief Perform audio3d play at operation.
/// @param s
/// @param p
/// @param d
/// @param v
/// @return Result value.
int64_t rt_audio3d_play_at(void *s, void *p, double d, int64_t v)
{
    (void)s;
    (void)p;
    (void)d;
    (void)v;
    return 0;
}

/// @brief Perform audio3d update voice operation.
/// @param v
/// @param p
/// @param md
void rt_audio3d_update_voice(int64_t v, void *p, double md)
{
    (void)v;
    (void)p;
    (void)md;
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

/// @brief Perform world3d step operation.
/// @param w
/// @param dt
void rt_world3d_step(void *w, double dt)
{
    (void)w;
    (void)dt;
}

/// @brief Perform world3d add operation.
/// @param w
/// @param b
void rt_world3d_add(void *w, void *b)
{
    (void)w;
    (void)b;
}

/// @brief Perform world3d remove operation.
/// @param w
/// @param b
void rt_world3d_remove(void *w, void *b)
{
    (void)w;
    (void)b;
}

/// @brief Perform world3d body count operation.
/// @param w
/// @return Result value.
int64_t rt_world3d_body_count(void *w)
{
    (void)w;
    return 0;
}

/// @brief Perform world3d set gravity operation.
/// @param w
/// @param gx
/// @param gy
/// @param gz
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

/// @brief Perform body3d set position operation.
/// @param o
/// @param x
/// @param y
/// @param z
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

/// @brief Perform body3d set velocity operation.
/// @param o
/// @param vx
/// @param vy
/// @param vz
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

/// @brief Perform body3d apply force operation.
/// @param o
/// @param fx
/// @param fy
/// @param fz
void rt_body3d_apply_force(void *o, double fx, double fy, double fz)
{
    (void)o;
    (void)fx;
    (void)fy;
    (void)fz;
}

/// @brief Perform body3d apply impulse operation.
/// @param o
/// @param ix
/// @param iy
/// @param iz
void rt_body3d_apply_impulse(void *o, double ix, double iy, double iz)
{
    (void)o;
    (void)ix;
    (void)iy;
    (void)iz;
}

/// @brief Perform body3d set restitution operation.
/// @param o
/// @param r
void rt_body3d_set_restitution(void *o, double r)
{
    (void)o;
    (void)r;
}

/// @brief Perform body3d get restitution operation.
/// @param o
/// @return Result value.
double rt_body3d_get_restitution(void *o)
{
    (void)o;
    return 0.0;
}

/// @brief Perform body3d set friction operation.
/// @param o
/// @param f
void rt_body3d_set_friction(void *o, double f)
{
    (void)o;
    (void)f;
}

/// @brief Perform body3d get friction operation.
/// @param o
/// @return Result value.
double rt_body3d_get_friction(void *o)
{
    (void)o;
    return 0.0;
}

/// @brief Perform body3d set collision layer operation.
/// @param o
/// @param l
void rt_body3d_set_collision_layer(void *o, int64_t l)
{
    (void)o;
    (void)l;
}

/// @brief Perform body3d get collision layer operation.
/// @param o
/// @return Result value.
int64_t rt_body3d_get_collision_layer(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform body3d set collision mask operation.
/// @param o
/// @param m
void rt_body3d_set_collision_mask(void *o, int64_t m)
{
    (void)o;
    (void)m;
}

/// @brief Perform body3d get collision mask operation.
/// @param o
/// @return Result value.
int64_t rt_body3d_get_collision_mask(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform body3d set static operation.
/// @param o
/// @param s
void rt_body3d_set_static(void *o, int8_t s)
{
    (void)o;
    (void)s;
}

/// @brief Perform body3d is static operation.
/// @param o
/// @return Result value.
int8_t rt_body3d_is_static(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform body3d set trigger operation.
/// @param o
/// @param t
void rt_body3d_set_trigger(void *o, int8_t t)
{
    (void)o;
    (void)t;
}

/// @brief Perform body3d is trigger operation.
/// @param o
/// @return Result value.
int8_t rt_body3d_is_trigger(void *o)
{
    (void)o;
    return 0;
}

/// @brief Perform body3d is grounded operation.
/// @param o
/// @return Result value.
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

/// @brief Perform body3d get mass operation.
/// @param o
/// @return Result value.
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

/// @brief Perform character3d move operation.
/// @param c
/// @param v
/// @param dt
void rt_character3d_move(void *c, void *v, double dt)
{
    (void)c;
    (void)v;
    (void)dt;
}

/// @brief Perform character3d set step height operation.
/// @param c
/// @param h
void rt_character3d_set_step_height(void *c, double h)
{
    (void)c;
    (void)h;
}

/// @brief Perform character3d get step height operation.
/// @param c
/// @return Result value.
double rt_character3d_get_step_height(void *c)
{
    (void)c;
    return 0.3;
}

/// @brief Perform character3d set slope limit operation.
/// @param c
/// @param d
void rt_character3d_set_slope_limit(void *c, double d)
{
    (void)c;
    (void)d;
}

/// @brief Perform character3d is grounded operation.
/// @param c
/// @return Result value.
int8_t rt_character3d_is_grounded(void *c)
{
    (void)c;
    return 0;
}

/// @brief Perform character3d just landed operation.
/// @param c
/// @return Result value.
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

/// @brief Perform character3d set position operation.
/// @param c
/// @param x
/// @param y
/// @param z
void rt_character3d_set_position(void *c, double x, double y, double z)
{
    (void)c;
    (void)x;
    (void)y;
    (void)z;
}

/* Trigger3D stubs */
void *rt_trigger3d_new(double x0, double y0, double z0, double x1, double y1, double z1)
{
    (void)x0;
    (void)y0;
    (void)z0;
    (void)x1;
    (void)y1;
    (void)z1;
    return NULL;
}

/// @brief Perform trigger3d contains operation.
/// @param t
/// @param p
/// @return Result value.
int8_t rt_trigger3d_contains(void *t, void *p)
{
    (void)t;
    (void)p;
    return 0;
}

/// @brief Perform trigger3d update operation.
/// @param t
/// @param w
void rt_trigger3d_update(void *t, void *w)
{
    (void)t;
    (void)w;
}

/// @brief Perform trigger3d get enter count operation.
/// @param t
/// @return Result value.
int64_t rt_trigger3d_get_enter_count(void *t)
{
    (void)t;
    return 0;
}

/// @brief Perform trigger3d get exit count operation.
/// @param t
/// @return Result value.
int64_t rt_trigger3d_get_exit_count(void *t)
{
    (void)t;
    return 0;
}

/// @brief Perform trigger3d set bounds operation.
/// @param t
/// @param x0
/// @param y0
/// @param z0
/// @param x1
/// @param y1
/// @param z1
void rt_trigger3d_set_bounds(
    void *t, double x0, double y0, double z0, double x1, double y1, double z1)
{
    (void)t;
    (void)x0;
    (void)y0;
    (void)z0;
    (void)x1;
    (void)y1;
    (void)z1;
}

/* Camera shake/follow stubs */
/// @brief Perform camera3d shake operation.
/// @param c
/// @param i
/// @param d
/// @param dc
void rt_camera3d_shake(void *c, double i, double d, double dc)
{
    (void)c;
    (void)i;
    (void)d;
    (void)dc;
}

/// @brief Perform camera3d smooth follow operation.
/// @param c
/// @param t
/// @param d
/// @param h
/// @param s
/// @param dt
void rt_camera3d_smooth_follow(void *c, void *t, double d, double h, double s, double dt)
{
    (void)c;
    (void)t;
    (void)d;
    (void)h;
    (void)s;
    (void)dt;
}

/// @brief Perform camera3d smooth look at operation.
/// @param c
/// @param t
/// @param s
/// @param dt
void rt_camera3d_smooth_look_at(void *c, void *t, double s, double dt)
{
    (void)c;
    (void)t;
    (void)s;
    (void)dt;
}

/* Transform3D stubs */
void *rt_transform3d_new(void)
{
    return NULL;
}

/// @brief Perform transform3d set position operation.
/// @param x
/// @param a
/// @param b
/// @param c
void rt_transform3d_set_position(void *x, double a, double b, double c)
{
    (void)x;
    (void)a;
    (void)b;
    (void)c;
}

void *rt_transform3d_get_position(void *x)
{
    (void)x;
    return NULL;
}

/// @brief Perform transform3d set rotation operation.
/// @param x
/// @param q
void rt_transform3d_set_rotation(void *x, void *q)
{
    (void)x;
    (void)q;
}

void *rt_transform3d_get_rotation(void *x)
{
    (void)x;
    return NULL;
}

/// @brief Perform transform3d set euler operation.
/// @param x
/// @param p
/// @param y
/// @param r
void rt_transform3d_set_euler(void *x, double p, double y, double r)
{
    (void)x;
    (void)p;
    (void)y;
    (void)r;
}

/// @brief Perform transform3d set scale operation.
/// @param x
/// @param a
/// @param b
/// @param c
void rt_transform3d_set_scale(void *x, double a, double b, double c)
{
    (void)x;
    (void)a;
    (void)b;
    (void)c;
}

void *rt_transform3d_get_scale(void *x)
{
    (void)x;
    return NULL;
}

void *rt_transform3d_get_matrix(void *x)
{
    (void)x;
    return NULL;
}

/// @brief Perform transform3d translate operation.
/// @param x
/// @param d
void rt_transform3d_translate(void *x, void *d)
{
    (void)x;
    (void)d;
}

/// @brief Perform transform3d rotate operation.
/// @param x
/// @param a
/// @param ang
void rt_transform3d_rotate(void *x, void *a, double ang)
{
    (void)x;
    (void)a;
    (void)ang;
}

/// @brief Perform transform3d look at operation.
/// @param x
/// @param t
/// @param u
void rt_transform3d_look_at(void *x, void *t, void *u)
{
    (void)x;
    (void)t;
    (void)u;
}

/* Path3D stubs */
void *rt_path3d_new(void)
{
    return NULL;
}

/// @brief Perform path3d add point operation.
/// @param p
/// @param v
void rt_path3d_add_point(void *p, void *v)
{
    (void)p;
    (void)v;
}

void *rt_path3d_get_position_at(void *p, double t)
{
    (void)p;
    (void)t;
    return NULL;
}

void *rt_path3d_get_direction_at(void *p, double t)
{
    (void)p;
    (void)t;
    return NULL;
}

/// @brief Perform path3d get length operation.
/// @param p
/// @return Result value.
double rt_path3d_get_length(void *p)
{
    (void)p;
    return 0.0;
}

/// @brief Perform path3d get point count operation.
/// @param p
/// @return Result value.
int64_t rt_path3d_get_point_count(void *p)
{
    (void)p;
    return 0;
}

/// @brief Perform path3d set looping operation.
/// @param p
/// @param l
void rt_path3d_set_looping(void *p, int8_t l)
{
    (void)p;
    (void)l;
}

/// @brief Perform path3d clear operation.
/// @param p
void rt_path3d_clear(void *p)
{
    (void)p;
}

/* InstanceBatch3D stubs */
void *rt_instbatch3d_new(void *m, void *mt)
{
    (void)m;
    (void)mt;
    return NULL;
}

/// @brief Perform instbatch3d add operation.
/// @param b
/// @param t
void rt_instbatch3d_add(void *b, void *t)
{
    (void)b;
    (void)t;
}

/// @brief Perform instbatch3d remove operation.
/// @param b
/// @param i
void rt_instbatch3d_remove(void *b, int64_t i)
{
    (void)b;
    (void)i;
}

/// @brief Perform instbatch3d set operation.
/// @param b
/// @param i
/// @param t
void rt_instbatch3d_set(void *b, int64_t i, void *t)
{
    (void)b;
    (void)i;
    (void)t;
}

/// @brief Perform instbatch3d clear operation.
/// @param b
void rt_instbatch3d_clear(void *b)
{
    (void)b;
}

/// @brief Perform instbatch3d count operation.
/// @param b
/// @return Result value.
int64_t rt_instbatch3d_count(void *b)
{
    (void)b;
    return 0;
}

/// @brief Perform canvas3d draw instanced operation.
/// @param c
/// @param b
void rt_canvas3d_draw_instanced(void *c, void *b)
{
    (void)c;
    (void)b;
}

/* Terrain3D stubs */
void *rt_terrain3d_new(int64_t w, int64_t d)
{
    (void)w;
    (void)d;
    return NULL;
}

/// @brief Perform terrain3d set heightmap operation.
/// @param t
/// @param p
void rt_terrain3d_set_heightmap(void *t, void *p)
{
    (void)t;
    (void)p;
}

/// @brief Perform terrain3d set material operation.
/// @param t
/// @param m
void rt_terrain3d_set_material(void *t, void *m)
{
    (void)t;
    (void)m;
}

/// @brief Perform terrain3d set scale operation.
/// @param t
/// @param sx
/// @param sy
/// @param sz
void rt_terrain3d_set_scale(void *t, double sx, double sy, double sz)
{
    (void)t;
    (void)sx;
    (void)sy;
    (void)sz;
}

/// @brief Perform terrain3d get height at operation.
/// @param t
/// @param x
/// @param z
/// @return Result value.
double rt_terrain3d_get_height_at(void *t, double x, double z)
{
    (void)t;
    (void)x;
    (void)z;
    return 0.0;
}

void *rt_terrain3d_get_normal_at(void *t, double x, double z)
{
    (void)t;
    (void)x;
    (void)z;
    return NULL;
}

/// @brief Perform canvas3d draw terrain operation.
/// @param c
/// @param t
void rt_canvas3d_draw_terrain(void *c, void *t)
{
    (void)c;
    (void)t;
}

/* NavMesh3D stubs */
void *rt_navmesh3d_build(void *m, double r, double h)
{
    (void)m;
    (void)r;
    (void)h;
    return NULL;
}

void *rt_navmesh3d_find_path(void *n, void *f, void *t)
{
    (void)n;
    (void)f;
    (void)t;
    return NULL;
}

void *rt_navmesh3d_sample_position(void *n, void *p)
{
    (void)n;
    (void)p;
    return NULL;
}

/// @brief Perform navmesh3d is walkable operation.
/// @param n
/// @param p
/// @return Result value.
int8_t rt_navmesh3d_is_walkable(void *n, void *p)
{
    (void)n;
    (void)p;
    return 0;
}

/// @brief Perform navmesh3d get triangle count operation.
/// @param n
/// @return Result value.
int64_t rt_navmesh3d_get_triangle_count(void *n)
{
    (void)n;
    return 0;
}

/// @brief Perform navmesh3d set max slope operation.
/// @param n
/// @param d
void rt_navmesh3d_set_max_slope(void *n, double d)
{
    (void)n;
    (void)d;
}

/// @brief Perform navmesh3d debug draw operation.
/// @param n
/// @param c
void rt_navmesh3d_debug_draw(void *n, void *c)
{
    (void)n;
    (void)c;
}

/* AnimBlend3D stubs */
void *rt_anim_blend3d_new(void *s)
{
    (void)s;
    return NULL;
}

/// @brief Perform anim blend3d add state operation.
/// @param b
/// @param n
/// @param a
/// @return Result value.
int64_t rt_anim_blend3d_add_state(void *b, rt_string n, void *a)
{
    (void)b;
    (void)n;
    (void)a;
    return -1;
}

/// @brief Perform anim blend3d set weight operation.
/// @param b
/// @param s
/// @param w
void rt_anim_blend3d_set_weight(void *b, int64_t s, double w)
{
    (void)b;
    (void)s;
    (void)w;
}

/// @brief Perform anim blend3d set weight by name operation.
/// @param b
/// @param n
/// @param w
void rt_anim_blend3d_set_weight_by_name(void *b, rt_string n, double w)
{
    (void)b;
    (void)n;
    (void)w;
}

/// @brief Perform anim blend3d get weight operation.
/// @param b
/// @param s
/// @return Result value.
double rt_anim_blend3d_get_weight(void *b, int64_t s)
{
    (void)b;
    (void)s;
    return 0.0;
}

/// @brief Perform anim blend3d set speed operation.
/// @param b
/// @param s
/// @param sp
void rt_anim_blend3d_set_speed(void *b, int64_t s, double sp)
{
    (void)b;
    (void)s;
    (void)sp;
}

/// @brief Perform anim blend3d update operation.
/// @param b
/// @param dt
void rt_anim_blend3d_update(void *b, double dt)
{
    (void)b;
    (void)dt;
}

/// @brief Perform anim blend3d state count operation.
/// @param b
/// @return Result value.
int64_t rt_anim_blend3d_state_count(void *b)
{
    (void)b;
    return 0;
}

/// @brief Perform canvas3d draw mesh blended operation.
/// @param c
/// @param m
/// @param t
/// @param mt
/// @param bl
void rt_canvas3d_draw_mesh_blended(void *c, void *m, void *t, void *mt, void *bl)
{
    (void)c;
    (void)m;
    (void)t;
    (void)mt;
    (void)bl;
}

/* Decal3D stubs */
void *rt_decal3d_new(void *p, void *n, double s, void *t)
{
    (void)p;
    (void)n;
    (void)s;
    (void)t;
    return NULL;
}

/// @brief Perform decal3d set lifetime operation.
/// @param d
/// @param s
void rt_decal3d_set_lifetime(void *d, double s)
{
    (void)d;
    (void)s;
}

/// @brief Perform decal3d update operation.
/// @param d
/// @param dt
void rt_decal3d_update(void *d, double dt)
{
    (void)d;
    (void)dt;
}

/// @brief Perform decal3d is expired operation.
/// @param d
/// @return Result value.
int8_t rt_decal3d_is_expired(void *d)
{
    (void)d;
    return 1;
}

/// @brief Perform canvas3d draw decal operation.
/// @param c
/// @param d
void rt_canvas3d_draw_decal(void *c, void *d)
{
    (void)c;
    (void)d;
}

/* Sprite3D stubs */
void *rt_sprite3d_new(void *t)
{
    (void)t;
    return NULL;
}

/// @brief Perform sprite3d set position operation.
/// @param s
/// @param x
/// @param y
/// @param z
void rt_sprite3d_set_position(void *s, double x, double y, double z)
{
    (void)s;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Perform sprite3d set scale operation.
/// @param s
/// @param w
/// @param h
void rt_sprite3d_set_scale(void *s, double w, double h)
{
    (void)s;
    (void)w;
    (void)h;
}

/// @brief Perform sprite3d set anchor operation.
/// @param s
/// @param ax
/// @param ay
void rt_sprite3d_set_anchor(void *s, double ax, double ay)
{
    (void)s;
    (void)ax;
    (void)ay;
}

/// @brief Perform sprite3d set frame operation.
/// @param s
/// @param fx
/// @param fy
/// @param fw
/// @param fh
void rt_sprite3d_set_frame(void *s, int64_t fx, int64_t fy, int64_t fw, int64_t fh)
{
    (void)s;
    (void)fx;
    (void)fy;
    (void)fw;
    (void)fh;
}

/// @brief Perform canvas3d draw sprite3d operation.
/// @param c
/// @param s
/// @param cam
void rt_canvas3d_draw_sprite3d(void *c, void *s, void *cam)
{
    (void)c;
    (void)s;
    (void)cam;
}

/* Water3D stubs */
void *rt_water3d_new(double w, double d)
{
    (void)w;
    (void)d;
    return NULL;
}

/// @brief Perform water3d set height operation.
/// @param w
/// @param y
void rt_water3d_set_height(void *w, double y)
{
    (void)w;
    (void)y;
}

/// @brief Perform water3d set wave params operation.
/// @param w
/// @param s
/// @param a
/// @param f
void rt_water3d_set_wave_params(void *w, double s, double a, double f)
{
    (void)w;
    (void)s;
    (void)a;
    (void)f;
}

/// @brief Perform water3d set color operation.
/// @param w
/// @param r
/// @param g
/// @param b
/// @param a
void rt_water3d_set_color(void *w, double r, double g, double b, double a)
{
    (void)w;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
}

/// @brief Perform water3d update operation.
/// @param w
/// @param dt
void rt_water3d_update(void *w, double dt)
{
    (void)w;
    (void)dt;
}

/// @brief Perform canvas3d draw water operation.
/// @param c
/// @param w
/// @param cam
void rt_canvas3d_draw_water(void *c, void *w, void *cam)
{
    (void)c;
    (void)w;
    (void)cam;
}

/* PostFX F5-F7 stubs */
/// @brief Perform postfx3d add ssao operation.
/// @param p
/// @param r
/// @param i
/// @param s
void rt_postfx3d_add_ssao(void *p, double r, double i, int64_t s)
{
    (void)p;
    (void)r;
    (void)i;
    (void)s;
}

/// @brief Perform postfx3d add dof operation.
/// @param p
/// @param f
/// @param a
/// @param m
void rt_postfx3d_add_dof(void *p, double f, double a, double m)
{
    (void)p;
    (void)f;
    (void)a;
    (void)m;
}

/// @brief Perform postfx3d add motion blur operation.
/// @param p
/// @param i
/// @param s
void rt_postfx3d_add_motion_blur(void *p, double i, int64_t s)
{
    (void)p;
    (void)i;
    (void)s;
}

/* Occlusion culling stub (F3) */
/// @brief Perform canvas3d set occlusion culling operation.
/// @param c
/// @param e
void rt_canvas3d_set_occlusion_culling(void *c, int8_t e)
{
    (void)c;
    (void)e;
}

/// @brief Perform canvas3d begin 2d operation.
/// @param c
void rt_canvas3d_begin_2d(void *c)
{
    (void)c;
}

/// @brief Perform canvas3d draw rect 3d operation.
/// @param c
/// @param x
/// @param y
/// @param w
/// @param h
/// @param cl
void rt_canvas3d_draw_rect_3d(void *c, int64_t x, int64_t y, int64_t w, int64_t h, int64_t cl)
{
    (void)c;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cl;
}

/// @brief Perform canvas3d draw text 3d operation.
/// @param c
/// @param x
/// @param y
/// @param t
/// @param cl
void rt_canvas3d_draw_text_3d(void *c, int64_t x, int64_t y, rt_string t, int64_t cl)
{
    (void)c;
    (void)x;
    (void)y;
    (void)t;
    (void)cl;
}

/* TextureAtlas3D stubs (F4) */
void *rt_texatlas3d_new(int64_t w, int64_t h)
{
    (void)w;
    (void)h;
    return NULL;
}

/// @brief Perform texatlas3d add operation.
/// @param a
/// @param p
/// @return Result value.
int64_t rt_texatlas3d_add(void *a, void *p)
{
    (void)a;
    (void)p;
    return -1;
}

void *rt_texatlas3d_get_texture(void *a)
{
    (void)a;
    return NULL;
}

/// @brief Perform texatlas3d get uv rect operation.
/// @param a
/// @param id
/// @param u0
/// @param v0
/// @param u1
/// @param v1
void rt_texatlas3d_get_uv_rect(void *a, int64_t id, double *u0, double *v0, double *u1, double *v1)
{
    (void)a;
    (void)id;
    if (u0)
        *u0 = 0;
    if (v0)
        *v0 = 0;
    if (u1)
        *u1 = 1;
    if (v1)
        *v1 = 1;
}
