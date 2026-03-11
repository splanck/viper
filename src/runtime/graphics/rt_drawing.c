//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_drawing.c
// Purpose: Basic drawing primitives for the Canvas runtime. Includes line, box,
//   frame, disc, ring, plot, text rendering (normal and scaled), alpha-blended
//   shapes, pixel blitting (opaque and alpha), get_pixel, copy_rect, and
//   save_bmp/save_png.
//
// Key invariants:
//   - All functions guard against NULL canvas_ptr and NULL gfx_win.
//   - Colors use 0x00RRGGBB format (alpha ignored by most primitives).
//   - Coordinate origin is top-left; x increases right, y increases down.
//
// Ownership/Lifetime:
//   - No ownership changes; operates on existing canvas handles.
//
// Links: rt_graphics_internal.h, rt_graphics.h (public API),
//        rt_font.h (glyph data), rt_pixels.h (Pixels buffer)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_internal.h"

#ifdef VIPER_ENABLE_GRAPHICS

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
                    vgfx_pset(canvas->gfx_win, (int32_t)(cx + col_idx), (int32_t)(y + row), col);
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
    return rt_str_len(text) * 8;
}

int64_t rt_canvas_text_height(void)
{
    return 8;
}

//=============================================================================
// Scaled Text Rendering
//=============================================================================

void rt_canvas_text_scaled(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color)
{
    if (!canvas_ptr || !text || scale < 1)
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

        for (int row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++)
            {
                if (bits & (0x80 >> col_idx))
                {
                    vgfx_fill_rect(canvas->gfx_win,
                                   (int32_t)(cx + col_idx * scale),
                                   (int32_t)(y + row * scale),
                                   (int32_t)scale,
                                   (int32_t)scale,
                                   col);
                }
            }
        }
        cx += 8 * scale;
    }
}

void rt_canvas_text_scaled_bg(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t fg, int64_t bg)
{
    if (!canvas_ptr || !text || scale < 1)
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

        for (int row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++)
            {
                vgfx_fill_rect(canvas->gfx_win,
                               (int32_t)(cx + col_idx * scale),
                               (int32_t)(y + row * scale),
                               (int32_t)scale,
                               (int32_t)scale,
                               (bits & (0x80 >> col_idx)) ? fg_col : bg_col);
            }
        }
        cx += 8 * scale;
    }
}

int64_t rt_canvas_text_scaled_width(rt_string text, int64_t scale)
{
    if (!text || scale < 1)
        return 0;
    return rt_str_len(text) * 8 * scale;
}

//=============================================================================
// Centered / Right-Aligned Text Helpers
//=============================================================================

void rt_canvas_text_centered(void *canvas_ptr, int64_t y, rt_string text, int64_t color)
{
    if (!canvas_ptr || !text)
        return;
    int64_t w = rt_canvas_width(canvas_ptr);
    int64_t tw = rt_str_len(text) * 8;
    int64_t x = (w - tw) / 2;
    rt_canvas_text(canvas_ptr, x, y, text, color);
}

void rt_canvas_text_right(
    void *canvas_ptr, int64_t margin, int64_t y, rt_string text, int64_t color)
{
    if (!canvas_ptr || !text)
        return;
    int64_t w = rt_canvas_width(canvas_ptr);
    int64_t tw = rt_str_len(text) * 8;
    int64_t x = w - tw - margin;
    rt_canvas_text(canvas_ptr, x, y, text, color);
}

void rt_canvas_text_centered_scaled(
    void *canvas_ptr, int64_t y, rt_string text, int64_t color, int64_t scale)
{
    if (!canvas_ptr || !text || scale < 1)
        return;
    int64_t w = rt_canvas_width(canvas_ptr);
    int64_t tw = rt_str_len(text) * 8 * scale;
    int64_t x = (w - tw) / 2;
    rt_canvas_text_scaled(canvas_ptr, x, y, text, scale, color);
}

//=============================================================================
// Alpha-Blended Shapes
//=============================================================================

void rt_canvas_box_alpha(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t alpha)
{
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    if (alpha <= 0)
        return;
    if (alpha >= 255)
    {
        vgfx_fill_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
        return;
    }

    uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | ((uint32_t)color & 0x00FFFFFF);

    for (int64_t py = y; py < y + h; py++)
    {
        for (int64_t px = x; px < x + w; px++)
        {
            vgfx_pset_alpha(canvas->gfx_win, (int32_t)px, (int32_t)py, argb);
        }
    }
}

void rt_canvas_disc_alpha(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color, int64_t alpha)
{
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    if (alpha <= 0)
        return;
    if (alpha >= 255)
    {
        vgfx_fill_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
        return;
    }

    uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | ((uint32_t)color & 0x00FFFFFF);
    int64_t r2 = radius * radius;

    for (int64_t dy = -radius; dy <= radius; dy++)
    {
        for (int64_t dx = -radius; dx <= radius; dx++)
        {
            if (dx * dx + dy * dy <= r2)
            {
                vgfx_pset_alpha(canvas->gfx_win, (int32_t)(cx + dx), (int32_t)(cy + dy), argb);
            }
        }
    }
}

//=============================================================================
// Pixel Blitting
//=============================================================================

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

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    float cs = vgfx_window_get_scale(canvas->gfx_win);
    int32_t isf = (cs > 1.0f) ? (int32_t)cs : 1;

    // Scale destination to physical pixels
    int64_t dst_x = x * isf;
    int64_t dst_y = y * isf;
    int64_t src_w = pixels->width;
    int64_t src_h = pixels->height;
    int64_t src_x = 0;
    int64_t src_y = 0;

    // Clip source against scaled destination bounds
    if (dst_x < 0)
    {
        int64_t skip = (-dst_x + isf - 1) / isf; // logical pixels to skip
        src_x += skip;
        src_w -= skip;
        dst_x += skip * isf;
    }
    if (dst_y < 0)
    {
        int64_t skip = (-dst_y + isf - 1) / isf;
        src_y += skip;
        src_h -= skip;
        dst_y += skip * isf;
    }
    if (dst_x + src_w * isf > fb.width)
        src_w = (fb.width - dst_x) / isf;
    if (dst_y + src_h * isf > fb.height)
        src_h = (fb.height - dst_y) / isf;

    if (src_w <= 0 || src_h <= 0)
        return;

    // Copy pixels with nearest-neighbor upscale (each src pixel -> isf x isf block)
    for (int64_t row = 0; row < src_h; row++)
    {
        uint32_t *src_row_data = &pixels->data[(src_y + row) * pixels->width + src_x];
        for (int64_t col = 0; col < src_w; col++)
        {
            uint32_t rgba = src_row_data[col];
            uint8_t r = (rgba >> 24) & 0xFF;
            uint8_t g = (rgba >> 16) & 0xFF;
            uint8_t b = (rgba >> 8) & 0xFF;
            uint8_t a = rgba & 0xFF;

            for (int32_t sy = 0; sy < isf; sy++)
            {
                int64_t py = dst_y + row * isf + sy;
                if (py >= fb.height)
                    break;
                uint8_t *dst = &fb.pixels[py * fb.stride + (dst_x + col * isf) * 4];
                for (int32_t sx = 0; sx < isf; sx++)
                {
                    dst[sx * 4 + 0] = r;
                    dst[sx * 4 + 1] = g;
                    dst[sx * 4 + 2] = b;
                    dst[sx * 4 + 3] = a;
                }
            }
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

    float cs = vgfx_window_get_scale(canvas->gfx_win);
    int32_t isf = (cs > 1.0f) ? (int32_t)cs : 1;

    // Clip source to pixels bounds (source coords are not scaled)
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

    // Scale destination to physical
    int64_t pdx = dx * isf;
    int64_t pdy = dy * isf;

    // Clip destination to framebuffer bounds (in logical, then convert)
    if (pdx < 0)
    {
        int64_t skip = (-pdx + isf - 1) / isf;
        w -= skip;
        sx += skip;
        pdx += skip * isf;
    }
    if (pdy < 0)
    {
        int64_t skip = (-pdy + isf - 1) / isf;
        h -= skip;
        sy += skip;
        pdy += skip * isf;
    }
    if (pdx + w * isf > fb.width)
        w = (fb.width - pdx) / isf;
    if (pdy + h * isf > fb.height)
        h = (fb.height - pdy) / isf;

    if (w <= 0 || h <= 0)
        return;

    // Copy pixels with nearest-neighbor upscale
    for (int64_t row = 0; row < h; row++)
    {
        uint32_t *src_row = &pixels->data[(sy + row) * pixels->width + sx];
        for (int64_t col = 0; col < w; col++)
        {
            uint32_t rgba = src_row[col];
            uint8_t r = (rgba >> 24) & 0xFF;
            uint8_t g = (rgba >> 16) & 0xFF;
            uint8_t b = (rgba >> 8) & 0xFF;
            uint8_t a = rgba & 0xFF;

            for (int32_t ys = 0; ys < isf; ys++)
            {
                int64_t py = pdy + row * isf + ys;
                if (py >= fb.height)
                    break;
                uint8_t *dst = &fb.pixels[py * fb.stride + (pdx + col * isf) * 4];
                for (int32_t xs = 0; xs < isf; xs++)
                {
                    dst[xs * 4 + 0] = r;
                    dst[xs * 4 + 1] = g;
                    dst[xs * 4 + 2] = b;
                    dst[xs * 4 + 3] = a;
                }
            }
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

    float cs = vgfx_window_get_scale(canvas->gfx_win);
    int32_t isf = (cs > 1.0f) ? (int32_t)cs : 1;

    int64_t src_w = pixels->width;
    int64_t src_h = pixels->height;
    int64_t dst_x = x * isf;
    int64_t dst_y = y * isf;
    int64_t src_x = 0;
    int64_t src_y = 0;

    // Clip to destination bounds
    if (dst_x < 0)
    {
        int64_t skip = (-dst_x + isf - 1) / isf;
        src_x += skip;
        src_w -= skip;
        dst_x += skip * isf;
    }
    if (dst_y < 0)
    {
        int64_t skip = (-dst_y + isf - 1) / isf;
        src_y += skip;
        src_h -= skip;
        dst_y += skip * isf;
    }
    if (dst_x + src_w * isf > fb.width)
        src_w = (fb.width - dst_x) / isf;
    if (dst_y + src_h * isf > fb.height)
        src_h = (fb.height - dst_y) / isf;

    if (src_w <= 0 || src_h <= 0)
        return;

    // Alpha blend pixels with nearest-neighbor upscale
    for (int64_t row = 0; row < src_h; row++)
    {
        uint32_t *src_row = &pixels->data[(src_y + row) * pixels->width + src_x];

        for (int64_t col = 0; col < src_w; col++)
        {
            uint32_t rgba = src_row[col];
            uint8_t sr = (rgba >> 24) & 0xFF;
            uint8_t sg = (rgba >> 16) & 0xFF;
            uint8_t sb = (rgba >> 8) & 0xFF;
            uint8_t sa = rgba & 0xFF;

            if (sa == 0)
                continue;

            for (int32_t ys = 0; ys < isf; ys++)
            {
                int64_t py = dst_y + row * isf + ys;
                if (py >= fb.height)
                    break;
                uint8_t *dst = &fb.pixels[py * fb.stride + (dst_x + col * isf) * 4];

                for (int32_t xs = 0; xs < isf; xs++)
                {
                    if (sa == 255)
                    {
                        dst[xs * 4 + 0] = sr;
                        dst[xs * 4 + 1] = sg;
                        dst[xs * 4 + 2] = sb;
                        dst[xs * 4 + 3] = 255;
                    }
                    else
                    {
                        uint16_t inv_alpha = 255 - sa;
                        dst[xs * 4 + 0] = (uint8_t)((sr * sa + dst[xs * 4 + 0] * inv_alpha) / 255);
                        dst[xs * 4 + 1] = (uint8_t)((sg * sa + dst[xs * 4 + 1] * inv_alpha) / 255);
                        dst[xs * 4 + 2] = (uint8_t)((sb * sa + dst[xs * 4 + 2] * inv_alpha) / 255);
                        dst[xs * 4 + 3] = 255;
                    }
                }
            }
        }
    }
}

//=============================================================================
// Canvas Utilities (get_pixel, copy_rect, save_bmp, save_png)
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

    // Copy pixels from canvas to buffer via direct framebuffer access — avoids
    // O(w*h) vgfx_point() calls (each involves clipping + bounds checking).
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
    {
        // Framebuffer unavailable (mock/headless); fall back to empty buffer.
        return pixels;
    }

    // Scale logical coordinates to physical framebuffer space.
    // Each logical pixel samples the top-left corner of its sf x sf physical block.
    float cs = vgfx_window_get_scale(canvas->gfx_win);
    int32_t isf = (cs > 1.0f) ? (int32_t)cs : 1;

    rt_pixels_impl *pix = (rt_pixels_impl *)pixels;

    for (int64_t py = 0; py < h; py++)
    {
        int64_t src_y = (y + py) * isf;
        if (src_y < 0 || src_y >= fb.height)
            continue;

        uint8_t *src_row = &fb.pixels[(size_t)(src_y * fb.stride)];
        uint32_t *dst_row = &pix->data[(size_t)(py * w)];

        for (int64_t px = 0; px < w; px++)
        {
            int64_t src_x = (x + px) * isf;
            if (src_x < 0 || src_x >= fb.width)
            {
                dst_row[px] = 0;
                continue;
            }
            uint8_t r = src_row[(size_t)(src_x * 4 + 0)];
            uint8_t g = src_row[(size_t)(src_x * 4 + 1)];
            uint8_t b = src_row[(size_t)(src_x * 4 + 2)];
            // Force full alpha, matching original vgfx_point-based behaviour.
            dst_row[px] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | 0xFF;
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

int64_t rt_canvas_save_png(void *canvas_ptr, rt_string path)
{
    if (!canvas_ptr || !path)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    int32_t w, h;
    if (vgfx_get_size(canvas->gfx_win, &w, &h) != 0)
        return 0;

    void *pixels = rt_canvas_copy_rect(canvas_ptr, 0, 0, w, h);
    if (!pixels)
        return 0;

    return rt_pixels_save_png(pixels, path);
}

#endif /* VIPER_ENABLE_GRAPHICS */
