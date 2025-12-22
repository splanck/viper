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
#include "rt_font.h"
#include "rt_input.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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

static void rt_canvas_finalize(void *obj)
{
    if (!obj)
        return;

    rt_canvas *canvas = (rt_canvas *)obj;
    if (canvas->gfx_win)
    {
        vgfx_destroy_window(canvas->gfx_win);
        canvas->gfx_win = NULL;
    }
}

void *rt_canvas_new(rt_string title, int64_t width, int64_t height)
{
    rt_canvas *canvas = (rt_canvas *)rt_obj_new_i64(0, (int64_t)sizeof(rt_canvas));
    if (!canvas)
        return NULL;

    canvas->vptr = NULL;
    canvas->gfx_win = NULL;
    canvas->should_close = 0;
    canvas->last_event.type = VGFX_EVENT_NONE;
    rt_obj_set_finalizer(canvas, rt_canvas_finalize);

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    if (title)
        params.title = rt_string_cstr(title);

    canvas->gfx_win = vgfx_create_window(&params);
    if (!canvas->gfx_win)
    {
        if (rt_obj_release_check0(canvas))
            rt_obj_free(canvas);
        return NULL;
    }

    // Initialize keyboard input for this canvas
    rt_keyboard_set_canvas(canvas->gfx_win);

    // Initialize mouse input for this canvas
    rt_mouse_set_canvas(canvas->gfx_win);

    // Initialize gamepad input (no canvas reference needed)
    rt_pad_init();

    return canvas;
}

void rt_canvas_destroy(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;

    if (rt_obj_release_check0(canvas_ptr))
        rt_obj_free(canvas_ptr);
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

    // Reset keyboard, mouse, and gamepad per-frame state at the start of polling
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();

    // Poll gamepads for state updates
    rt_pad_poll();

    // Update mouse position from current cursor location
    int32_t mx = 0, my = 0;
    vgfx_mouse_pos(canvas->gfx_win, &mx, &my);
    rt_mouse_update_pos((int64_t)mx, (int64_t)my);

    if (vgfx_poll_event(canvas->gfx_win, &canvas->last_event))
    {
        if (canvas->last_event.type == VGFX_EVENT_CLOSE)
            canvas->should_close = 1;

        // Forward keyboard events to keyboard module
        if (canvas->last_event.type == VGFX_EVENT_KEY_DOWN)
            rt_keyboard_on_key_down((int64_t)canvas->last_event.data.key.key);
        else if (canvas->last_event.type == VGFX_EVENT_KEY_UP)
            rt_keyboard_on_key_up((int64_t)canvas->last_event.data.key.key);

        // Forward mouse events to mouse module
        if (canvas->last_event.type == VGFX_EVENT_MOUSE_MOVE)
        {
            rt_mouse_update_pos((int64_t)canvas->last_event.data.mouse_move.x,
                                (int64_t)canvas->last_event.data.mouse_move.y);
        }
        else if (canvas->last_event.type == VGFX_EVENT_MOUSE_DOWN)
        {
            rt_mouse_button_down((int64_t)canvas->last_event.data.mouse_button.button);
        }
        else if (canvas->last_event.type == VGFX_EVENT_MOUSE_UP)
        {
            rt_mouse_button_up((int64_t)canvas->last_event.data.mouse_button.button);
        }

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

//=============================================================================
// Text Rendering
//=============================================================================

void rt_canvas_text(void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t color)
{
    if (!canvas_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t col = (vgfx_color_t)color;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        int c = (unsigned char)str[i];
        const uint8_t *glyph = rt_font_get_glyph(c);

        // Draw 8x8 glyph
        for (int row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++)
            {
                if (bits & (0x80 >> col_idx))
                {
                    vgfx_pset(canvas->gfx_win,
                              (int32_t)(cx + col_idx),
                              (int32_t)(y + row),
                              col);
                }
            }
        }
        cx += 8;
    }
}

void rt_canvas_text_bg(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg)
{
    if (!canvas_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t fg_col = (vgfx_color_t)fg;
    vgfx_color_t bg_col = (vgfx_color_t)bg;

    for (size_t i = 0; str[i] != '\0'; i++)
    {
        int c = (unsigned char)str[i];
        const uint8_t *glyph = rt_font_get_glyph(c);

        // Draw 8x8 glyph with background
        for (int row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++)
            {
                vgfx_pset(canvas->gfx_win,
                          (int32_t)(cx + col_idx),
                          (int32_t)(y + row),
                          (bits & (0x80 >> col_idx)) ? fg_col : bg_col);
            }
        }
        cx += 8;
    }
}

int64_t rt_canvas_text_width(rt_string text)
{
    if (!text)
        return 0;
    return rt_len(text) * 8;
}

int64_t rt_canvas_text_height(void)
{
    return 8;
}

//=============================================================================
// Pixel Blitting
//=============================================================================

// Forward declaration for pixels internal access
typedef struct rt_pixels_impl
{
    int64_t width;
    int64_t height;
    uint32_t *data;
} rt_pixels_impl;

void rt_canvas_blit(void *canvas_ptr, int64_t x, int64_t y, void *pixels_ptr)
{
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    rt_pixels_impl *pixels = (rt_pixels_impl *)pixels_ptr;
    if (!pixels->data)
        return;

    // Get framebuffer for direct access
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    // Blit with clipping
    int64_t src_w = pixels->width;
    int64_t src_h = pixels->height;
    int64_t dst_x = x;
    int64_t dst_y = y;
    int64_t src_x = 0;
    int64_t src_y = 0;

    // Clip to destination bounds
    if (dst_x < 0)
    {
        src_x -= dst_x;
        src_w += dst_x;
        dst_x = 0;
    }
    if (dst_y < 0)
    {
        src_y -= dst_y;
        src_h += dst_y;
        dst_y = 0;
    }
    if (dst_x + src_w > fb.width)
        src_w = fb.width - dst_x;
    if (dst_y + src_h > fb.height)
        src_h = fb.height - dst_y;

    if (src_w <= 0 || src_h <= 0)
        return;

    // Copy pixels (convert from RGBA to framebuffer format)
    for (int64_t row = 0; row < src_h; row++)
    {
        uint32_t *src_row = &pixels->data[(src_y + row) * pixels->width + src_x];
        uint8_t *dst_row = &fb.pixels[((dst_y + row) * fb.stride) + (dst_x * 4)];

        for (int64_t col = 0; col < src_w; col++)
        {
            uint32_t rgba = src_row[col];
            // Pixels format: 0xRRGGBBAA, framebuffer: RGBA bytes
            dst_row[col * 4 + 0] = (rgba >> 24) & 0xFF; // R
            dst_row[col * 4 + 1] = (rgba >> 16) & 0xFF; // G
            dst_row[col * 4 + 2] = (rgba >> 8) & 0xFF;  // B
            dst_row[col * 4 + 3] = rgba & 0xFF;         // A
        }
    }
}

void rt_canvas_blit_region(void *canvas_ptr,
                           int64_t dx,
                           int64_t dy,
                           void *pixels_ptr,
                           int64_t sx,
                           int64_t sy,
                           int64_t w,
                           int64_t h)
{
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    rt_pixels_impl *pixels = (rt_pixels_impl *)pixels_ptr;
    if (!pixels->data)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    // Clip source to pixels bounds
    if (sx < 0)
    {
        w += sx;
        dx -= sx;
        sx = 0;
    }
    if (sy < 0)
    {
        h += sy;
        dy -= sy;
        sy = 0;
    }
    if (sx + w > pixels->width)
        w = pixels->width - sx;
    if (sy + h > pixels->height)
        h = pixels->height - sy;

    // Clip destination to framebuffer bounds
    if (dx < 0)
    {
        w += dx;
        sx -= dx;
        dx = 0;
    }
    if (dy < 0)
    {
        h += dy;
        sy -= dy;
        dy = 0;
    }
    if (dx + w > fb.width)
        w = fb.width - dx;
    if (dy + h > fb.height)
        h = fb.height - dy;

    if (w <= 0 || h <= 0)
        return;

    // Copy pixels
    for (int64_t row = 0; row < h; row++)
    {
        uint32_t *src_row = &pixels->data[(sy + row) * pixels->width + sx];
        uint8_t *dst_row = &fb.pixels[((dy + row) * fb.stride) + (dx * 4)];

        for (int64_t col = 0; col < w; col++)
        {
            uint32_t rgba = src_row[col];
            dst_row[col * 4 + 0] = (rgba >> 24) & 0xFF;
            dst_row[col * 4 + 1] = (rgba >> 16) & 0xFF;
            dst_row[col * 4 + 2] = (rgba >> 8) & 0xFF;
            dst_row[col * 4 + 3] = rgba & 0xFF;
        }
    }
}

void rt_canvas_blit_alpha(void *canvas_ptr, int64_t x, int64_t y, void *pixels_ptr)
{
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    rt_pixels_impl *pixels = (rt_pixels_impl *)pixels_ptr;
    if (!pixels->data)
        return;

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    int64_t src_w = pixels->width;
    int64_t src_h = pixels->height;
    int64_t dst_x = x;
    int64_t dst_y = y;
    int64_t src_x = 0;
    int64_t src_y = 0;

    // Clip to destination bounds
    if (dst_x < 0)
    {
        src_x -= dst_x;
        src_w += dst_x;
        dst_x = 0;
    }
    if (dst_y < 0)
    {
        src_y -= dst_y;
        src_h += dst_y;
        dst_y = 0;
    }
    if (dst_x + src_w > fb.width)
        src_w = fb.width - dst_x;
    if (dst_y + src_h > fb.height)
        src_h = fb.height - dst_y;

    if (src_w <= 0 || src_h <= 0)
        return;

    // Alpha blend pixels
    for (int64_t row = 0; row < src_h; row++)
    {
        uint32_t *src_row = &pixels->data[(src_y + row) * pixels->width + src_x];
        uint8_t *dst_row = &fb.pixels[((dst_y + row) * fb.stride) + (dst_x * 4)];

        for (int64_t col = 0; col < src_w; col++)
        {
            uint32_t rgba = src_row[col];
            uint8_t sr = (rgba >> 24) & 0xFF;
            uint8_t sg = (rgba >> 16) & 0xFF;
            uint8_t sb = (rgba >> 8) & 0xFF;
            uint8_t sa = rgba & 0xFF;

            if (sa == 255)
            {
                // Fully opaque - direct copy
                dst_row[col * 4 + 0] = sr;
                dst_row[col * 4 + 1] = sg;
                dst_row[col * 4 + 2] = sb;
                dst_row[col * 4 + 3] = 255;
            }
            else if (sa > 0)
            {
                // Alpha blend: out = src * alpha + dst * (1 - alpha)
                uint8_t dr = dst_row[col * 4 + 0];
                uint8_t dg = dst_row[col * 4 + 1];
                uint8_t db = dst_row[col * 4 + 2];
                uint16_t inv_alpha = 255 - sa;

                dst_row[col * 4 + 0] = (uint8_t)((sr * sa + dr * inv_alpha) / 255);
                dst_row[col * 4 + 1] = (uint8_t)((sg * sa + dg * inv_alpha) / 255);
                dst_row[col * 4 + 2] = (uint8_t)((sb * sa + db * inv_alpha) / 255);
                dst_row[col * 4 + 3] = 255;
            }
            // sa == 0: fully transparent, skip
        }
    }
}

//=============================================================================
// Extended Drawing Primitives
//=============================================================================

// Helper: absolute value for int64_t
static int64_t abs64(int64_t x)
{
    return x < 0 ? -x : x;
}

// Helper: min/max for int64_t
static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}
static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

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

    // Draw thick line using filled circles along the line (round caps)
    int64_t dx = abs64(x2 - x1);
    int64_t dy = abs64(y2 - y1);
    int64_t steps = dx > dy ? dx : dy;

    if (steps == 0)
    {
        // Just draw a filled circle at the point
        vgfx_fill_circle(canvas->gfx_win, (int32_t)x1, (int32_t)y1, (int32_t)(thickness / 2), col);
        return;
    }

    int64_t half = thickness / 2;
    for (int64_t i = 0; i <= steps; i++)
    {
        int64_t px = x1 + (x2 - x1) * i / steps;
        int64_t py = y1 + (y2 - y1) * i / steps;
        vgfx_fill_circle(canvas->gfx_win, (int32_t)px, (int32_t)py, (int32_t)half, col);
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
    int64_t max_radius = min64(w, h) / 2;
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
    vgfx_fill_rect(canvas->gfx_win, (int32_t)(x + radius), (int32_t)y, (int32_t)(w - 2 * radius),
                   (int32_t)h, col);

    // Draw left and right rectangles
    vgfx_fill_rect(canvas->gfx_win, (int32_t)x, (int32_t)(y + radius), (int32_t)radius,
                   (int32_t)(h - 2 * radius), col);
    vgfx_fill_rect(canvas->gfx_win, (int32_t)(x + w - radius), (int32_t)(y + radius), (int32_t)radius,
                   (int32_t)(h - 2 * radius), col);

    // Draw four corner filled circles
    vgfx_fill_circle(canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + radius), (int32_t)radius,
                     col);
    vgfx_fill_circle(canvas->gfx_win, (int32_t)(x + w - radius - 1), (int32_t)(y + radius),
                     (int32_t)radius, col);
    vgfx_fill_circle(canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + h - radius - 1),
                     (int32_t)radius, col);
    vgfx_fill_circle(canvas->gfx_win, (int32_t)(x + w - radius - 1), (int32_t)(y + h - radius - 1),
                     (int32_t)radius, col);
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
    int64_t max_radius = min64(w, h) / 2;
    if (radius > max_radius)
        radius = max_radius;
    if (radius < 0)
        radius = 0;

    if (radius == 0)
    {
        // Draw regular rectangle outline
        vgfx_line(canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)(x + w - 1), (int32_t)y, col);
        vgfx_line(canvas->gfx_win, (int32_t)x, (int32_t)(y + h - 1), (int32_t)(x + w - 1),
                  (int32_t)(y + h - 1), col);
        vgfx_line(canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)x, (int32_t)(y + h - 1), col);
        vgfx_line(canvas->gfx_win, (int32_t)(x + w - 1), (int32_t)y, (int32_t)(x + w - 1),
                  (int32_t)(y + h - 1), col);
        return;
    }

    // Draw horizontal lines (top and bottom, excluding corners)
    vgfx_line(canvas->gfx_win, (int32_t)(x + radius), (int32_t)y, (int32_t)(x + w - radius - 1),
              (int32_t)y, col);
    vgfx_line(canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + h - 1),
              (int32_t)(x + w - radius - 1), (int32_t)(y + h - 1), col);

    // Draw vertical lines (left and right, excluding corners)
    vgfx_line(canvas->gfx_win, (int32_t)x, (int32_t)(y + radius), (int32_t)x,
              (int32_t)(y + h - radius - 1), col);
    vgfx_line(canvas->gfx_win, (int32_t)(x + w - 1), (int32_t)(y + radius), (int32_t)(x + w - 1),
              (int32_t)(y + h - radius - 1), col);

    // Draw corner arcs using circle algorithm
    vgfx_circle(canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + radius), (int32_t)radius, col);
    vgfx_circle(canvas->gfx_win, (int32_t)(x + w - radius - 1), (int32_t)(y + radius), (int32_t)radius,
                col);
    vgfx_circle(canvas->gfx_win, (int32_t)(x + radius), (int32_t)(y + h - radius - 1), (int32_t)radius,
                col);
    vgfx_circle(canvas->gfx_win, (int32_t)(x + w - radius - 1), (int32_t)(y + h - radius - 1),
                (int32_t)radius, col);
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

    // Bounds check
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

    // Simple scanline flood fill using a stack
    // Allocate stack on heap (worst case: every pixel)
    int64_t max_stack = fb.width * fb.height;
    int64_t *stack_x = (int64_t *)malloc((size_t)max_stack * sizeof(int64_t));
    int64_t *stack_y = (int64_t *)malloc((size_t)max_stack * sizeof(int64_t));
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

        // Push neighbors (4-connected)
        if (stack_top + 4 <= max_stack)
        {
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
        int64_t min_x = min64(min64(x1, x2), x3);
        int64_t max_x = max64(max64(x1, x2), x3);
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
            xa = x1 + (x2 - x1) * (y - y1) / max64(y2 - y1, 1);
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
        vgfx_line(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy + y), (int32_t)(cx + x),
                  (int32_t)(cy + y), col);
        vgfx_line(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy - y), (int32_t)(cx + x),
                  (int32_t)(cy - y), col);

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
        vgfx_line(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy + y), (int32_t)(cx + x),
                  (int32_t)(cy + y), col);
        vgfx_line(canvas->gfx_win, (int32_t)(cx - x), (int32_t)(cy - y), (int32_t)(cx + x),
                  (int32_t)(cy - y), col);

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
// Phase 4: Advanced Curves & Shapes
//=============================================================================

// Helper: Simple sine/cosine approximation using integer math (degrees)
// Returns value * 1024 for fixed-point precision
static int64_t sin_deg_fp(int64_t deg)
{
    // Normalize to 0-359
    deg = deg % 360;
    if (deg < 0)
        deg += 360;

    // Simple lookup approximation with linear interpolation
    // sin values * 1024 for 0, 30, 45, 60, 90 degrees
    static const int64_t sin_table[13] = {0,   176,  342,  500,  643,  766,  866,
                                          940, 985,  1004, 992,  951,  883};
    static const int64_t angles[13] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};

    int64_t sign = 1;
    if (deg >= 180)
    {
        deg -= 180;
        sign = -1;
    }
    if (deg > 90)
    {
        deg = 180 - deg;
    }

    // Linear interpolation in table
    int64_t idx = deg / 10;
    if (idx >= 12)
        idx = 11;
    int64_t frac = deg % 10;
    int64_t v0 = (idx < 9) ? sin_table[idx] : sin_table[9 - (idx - 9)];
    int64_t v1 = (idx + 1 < 9) ? sin_table[idx + 1] : sin_table[9 - (idx + 1 - 9)];
    if (idx >= 9)
    {
        v0 = sin_table[9 - (idx - 9)];
        v1 = (idx + 1 <= 9) ? sin_table[idx + 1] : sin_table[9 - (idx + 1 - 9)];
    }

    // Simpler approach: use lookup directly
    int64_t val;
    if (deg <= 90)
    {
        int64_t i = deg / 10;
        if (i > 9)
            i = 9;
        val = sin_table[i];
    }
    else
    {
        int64_t i = (180 - deg) / 10;
        if (i > 9)
            i = 9;
        val = sin_table[i];
    }

    return sign * val;
}

static int64_t cos_deg_fp(int64_t deg)
{
    return sin_deg_fp(deg + 90);
}

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
                    angle = (y * 90) / (abs64(x) + abs64(y));
                else if (x < 0 && y >= 0)
                    angle = 90 + (abs64(x) * 90) / (abs64(x) + abs64(y));
                else if (x < 0 && y < 0)
                    angle = 180 + (abs64(y) * 90) / (abs64(x) + abs64(y));
                else
                    angle = 270 + (abs64(x) * 90) / (abs64(x) + abs64(y));

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
        int64_t px = cx + (radius * cos_deg_fp(angle)) / 1024;
        int64_t py = cy - (radius * sin_deg_fp(angle)) / 1024;
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
            vgfx_line(canvas->gfx_win, (int32_t)intersections[i], (int32_t)y,
                      (int32_t)intersections[i + 1], (int32_t)y, col);
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
// Phase 5: Canvas Utilities
//=============================================================================

int64_t rt_canvas_get_pixel(void *canvas_ptr, int64_t x, int64_t y)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    vgfx_color_t color;
    if (vgfx_point(canvas->gfx_win, (int32_t)x, (int32_t)y, &color) == 0)
    {
        return (int64_t)color;
    }
    return 0;
}

void *rt_canvas_copy_rect(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (!canvas_ptr || w <= 0 || h <= 0)
        return NULL;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return NULL;

    // Create a new Pixels buffer
    void *pixels = rt_pixels_new(w, h);
    if (!pixels)
        return NULL;

    // Copy pixels from canvas to buffer
    for (int64_t py = 0; py < h; py++)
    {
        for (int64_t px = 0; px < w; px++)
        {
            vgfx_color_t color;
            if (vgfx_point(canvas->gfx_win, (int32_t)(x + px), (int32_t)(y + py), &color) == 0)
            {
                // Convert from 0x00RRGGBB to 0xRRGGBBAA (full alpha)
                int64_t rgba = ((int64_t)color << 8) | 0xFF;
                rt_pixels_set(pixels, px, py, rgba);
            }
        }
    }

    return pixels;
}

int64_t rt_canvas_save_bmp(void *canvas_ptr, rt_string path)
{
    if (!canvas_ptr || !path)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    int32_t w, h;
    if (vgfx_get_size(canvas->gfx_win, &w, &h) != 0)
        return 0;

    // Create a temporary Pixels buffer with canvas contents
    void *pixels = rt_canvas_copy_rect(canvas_ptr, 0, 0, w, h);
    if (!pixels)
        return 0;

    // Save to BMP
    int64_t result = rt_pixels_save_bmp(pixels, path);

    // Note: pixels is managed by runtime, will be GC'd

    return result;
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

void rt_canvas_text(void *canvas, int64_t x, int64_t y, rt_string text, int64_t color)
{
    (void)canvas;
    (void)x;
    (void)y;
    (void)text;
    (void)color;
}

void rt_canvas_text_bg(
    void *canvas, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg)
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

void rt_canvas_thick_line(void *canvas,
                          int64_t x1,
                          int64_t y1,
                          int64_t x2,
                          int64_t y2,
                          int64_t thickness,
                          int64_t color)
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

void rt_canvas_ellipse(
    void *canvas, int64_t cx, int64_t cy, int64_t rx, int64_t ry, int64_t color)
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
