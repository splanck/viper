//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_graphics.c
// Purpose: Runtime bridge functions for ViperGFX graphics library.
// Key invariants: All functions check for NULL window handles.
// Ownership/Lifetime: Windows are allocated on creation and freed on destroy.
// Links: src/lib/graphics/include/vgfx.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdlib.h>

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx.h"

/// @brief Internal window wrapper structure.
/// @details Contains the vptr (for future OOP support) and the vgfx window handle.
typedef struct
{
    void *vptr;              ///< VTable pointer (reserved for future use)
    vgfx_window_t gfx_win;   ///< ViperGFX window handle
    int64_t should_close;    ///< Close request flag
    vgfx_event_t last_event; ///< Last polled event for retrieval
} rt_gfx_window;

void *rt_gfx_window_new(int64_t width, int64_t height, rt_string title)
{
    rt_gfx_window *win = malloc(sizeof(rt_gfx_window));
    if (!win)
        return NULL;

    win->vptr = NULL;
    win->should_close = 0;
    win->last_event.type = VGFX_EVENT_NONE;

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    if (title)
        params.title = rt_string_cstr(title);

    win->gfx_win = vgfx_create_window(&params);
    if (!win->gfx_win)
    {
        free(win);
        return NULL;
    }

    return win;
}

void rt_gfx_window_destroy(void *window)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_destroy_window(win->gfx_win);

    free(win);
}

void rt_gfx_window_update(void *window)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_update(win->gfx_win);
}

void rt_gfx_window_clear(void *window, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_cls(win->gfx_win, (vgfx_color_t)color);
}

void rt_gfx_draw_line(void *window, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_line(win->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)x2, (int32_t)y2,
                  (vgfx_color_t)color);
}

void rt_gfx_draw_rect(void *window, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_fill_rect(win->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h,
                       (vgfx_color_t)color);
}

void rt_gfx_draw_rect_outline(
    void *window, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_rect(win->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h,
                  (vgfx_color_t)color);
}

void rt_gfx_draw_circle(void *window, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_fill_circle(win->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius,
                         (vgfx_color_t)color);
}

void rt_gfx_draw_circle_outline(void *window, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_circle(win->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
}

void rt_gfx_set_pixel(void *window, int64_t x, int64_t y, int64_t color)
{
    if (!window)
        return;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (win->gfx_win)
        vgfx_pset(win->gfx_win, (int32_t)x, (int32_t)y, (vgfx_color_t)color);
}

int64_t rt_gfx_window_width(void *window)
{
    if (!window)
        return 0;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (!win->gfx_win)
        return 0;

    int32_t width = 0;
    vgfx_get_size(win->gfx_win, &width, NULL);
    return (int64_t)width;
}

int64_t rt_gfx_window_height(void *window)
{
    if (!window)
        return 0;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (!win->gfx_win)
        return 0;

    int32_t height = 0;
    vgfx_get_size(win->gfx_win, NULL, &height);
    return (int64_t)height;
}

int64_t rt_gfx_window_should_close(void *window)
{
    if (!window)
        return 1;

    rt_gfx_window *win = (rt_gfx_window *)window;
    return win->should_close;
}

int64_t rt_gfx_poll_event(void *window)
{
    if (!window)
        return 0;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (!win->gfx_win)
        return 0;

    if (vgfx_poll_event(win->gfx_win, &win->last_event))
    {
        if (win->last_event.type == VGFX_EVENT_CLOSE)
            win->should_close = 1;

        return (int64_t)win->last_event.type;
    }

    return 0;
}

int64_t rt_gfx_key_down(void *window, int64_t key)
{
    if (!window)
        return 0;

    rt_gfx_window *win = (rt_gfx_window *)window;
    if (!win->gfx_win)
        return 0;

    return (int64_t)vgfx_key_down(win->gfx_win, (vgfx_key_t)key);
}

int64_t rt_gfx_rgb(int64_t r, int64_t g, int64_t b)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)vgfx_rgb(r8, g8, b8);
}

#else /* !VIPER_ENABLE_GRAPHICS */

/* Stub implementations when graphics library is not available */

void *rt_gfx_window_new(int64_t width, int64_t height, rt_string title)
{
    (void)width;
    (void)height;
    (void)title;
    return NULL;
}

void rt_gfx_window_destroy(void *window)
{
    (void)window;
}

void rt_gfx_window_update(void *window)
{
    (void)window;
}

void rt_gfx_window_clear(void *window, int64_t color)
{
    (void)window;
    (void)color;
}

void rt_gfx_draw_line(void *window, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color)
{
    (void)window;
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
}

void rt_gfx_draw_rect(void *window, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    (void)window;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void rt_gfx_draw_rect_outline(
    void *window, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color)
{
    (void)window;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

void rt_gfx_draw_circle(void *window, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    (void)window;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
}

void rt_gfx_draw_circle_outline(void *window, int64_t cx, int64_t cy, int64_t radius, int64_t color)
{
    (void)window;
    (void)cx;
    (void)cy;
    (void)radius;
    (void)color;
}

void rt_gfx_set_pixel(void *window, int64_t x, int64_t y, int64_t color)
{
    (void)window;
    (void)x;
    (void)y;
    (void)color;
}

int64_t rt_gfx_window_width(void *window)
{
    (void)window;
    return 0;
}

int64_t rt_gfx_window_height(void *window)
{
    (void)window;
    return 0;
}

int64_t rt_gfx_window_should_close(void *window)
{
    (void)window;
    return 1;
}

int64_t rt_gfx_poll_event(void *window)
{
    (void)window;
    return 0;
}

int64_t rt_gfx_key_down(void *window, int64_t key)
{
    (void)window;
    (void)key;
    return 0;
}

int64_t rt_gfx_rgb(int64_t r, int64_t g, int64_t b)
{
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)(((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8);
}

#endif /* VIPER_ENABLE_GRAPHICS */
