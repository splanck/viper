//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_graphics.c
// Purpose: Runtime bridge functions for ViperGFX graphics library.
// Key invariants: All functions check for NULL canvas handles.
// Ownership/Lifetime: Canvases are allocated on creation and freed on destroy.
// Links: src/lib/graphics/include/vgfx.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdlib.h>

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx.h"

/// @brief Internal canvas wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vgfx window handle.
typedef struct
{
    void *vptr;              ///< VTable pointer (reserved for future use)
    vgfx_window_t gfx_win;   ///< ViperGFX window handle
    int64_t should_close;    ///< Close request flag
    vgfx_event_t last_event; ///< Last polled event for retrieval
} rt_canvas;

void *rt_canvas_new(rt_string title, int64_t width, int64_t height)
{
    rt_canvas *canvas = malloc(sizeof(rt_canvas));
    if (!canvas)
        return NULL;

    canvas->vptr = NULL;
    canvas->should_close = 0;
    canvas->last_event.type = VGFX_EVENT_NONE;

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    if (title)
        params.title = rt_string_cstr(title);

    canvas->gfx_win = vgfx_create_window(&params);
    if (!canvas->gfx_win)
    {
        free(canvas);
        return NULL;
    }

    return canvas;
}

void rt_canvas_destroy(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_destroy_window(canvas->gfx_win);

    free(canvas);
}

int64_t rt_canvas_width(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    int32_t width = 0;
    vgfx_get_size(canvas->gfx_win, &width, NULL);
    return (int64_t)width;
}

int64_t rt_canvas_height(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    int32_t height = 0;
    vgfx_get_size(canvas->gfx_win, NULL, &height);
    return (int64_t)height;
}

int64_t rt_canvas_should_close(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 1;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    return canvas->should_close;
}

void rt_canvas_flip(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_update(canvas->gfx_win);
}

void rt_canvas_clear(void *canvas_ptr, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_cls(canvas->gfx_win, (vgfx_color_t)color);
}

void rt_canvas_line(void *canvas_ptr, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_line(canvas->gfx_win,
                  (int32_t)x1,
                  (int32_t)y1,
                  (int32_t)x2,
                  (int32_t)y2,
                  (vgfx_color_t)color);
}

void rt_canvas_box(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_fill_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
}

void rt_canvas_frame(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
}

void rt_canvas_disc(void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_fill_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
}

void rt_canvas_ring(void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
}

void rt_canvas_plot(void *canvas_ptr, int64_t x, int64_t y, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_pset(canvas->gfx_win, (int32_t)x, (int32_t)y, (vgfx_color_t)color);
}

int64_t rt_canvas_poll(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    if (vgfx_poll_event(canvas->gfx_win, &canvas->last_event))
    {
        if (canvas->last_event.type == VGFX_EVENT_CLOSE)
            canvas->should_close = 1;

        return (int64_t)canvas->last_event.type;
    }

    return 0;
}

int64_t rt_canvas_key_held(void *canvas_ptr, int64_t key)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    return (int64_t)vgfx_key_down(canvas->gfx_win, (vgfx_key_t)key);
}

int64_t rt_color_rgb(int64_t r, int64_t g, int64_t b)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)vgfx_rgb(r8, g8, b8);
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

#else /* !VIPER_ENABLE_GRAPHICS */

/* Stub implementations when graphics library is not available */

void *rt_canvas_new(rt_string title, int64_t width, int64_t height)
{
    (void)title;
    (void)width;
    (void)height;
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

#endif /* VIPER_ENABLE_GRAPHICS */
