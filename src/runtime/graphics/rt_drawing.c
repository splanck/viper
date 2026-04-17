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
#include <stdio.h>
#include <stdlib.h>

#ifdef VIPER_ENABLE_GRAPHICS

static int rt_trace_canvas_box_enabled(void) {
    static int cached = -1;
    if (cached == -1)
        cached = getenv("VIPER_TRACE_CANVAS_BOX") ? 1 : 0;
    return cached;
}

static int rt_canvas_next_codepoint(const char *str,
                                    size_t byte_len,
                                    size_t *index,
                                    int *codepoint_out) {
    if (!str || !index || !codepoint_out || *index >= byte_len)
        return 0;

    size_t i = *index;
    unsigned char c0 = (unsigned char)str[i];
    uint32_t cp = '?';
    size_t advance = 1;

    if (c0 < 0x80) {
        cp = c0;
    } else if ((c0 & 0xE0u) == 0xC0u && i + 1 < byte_len) {
        unsigned char c1 = (unsigned char)str[i + 1];
        if ((c1 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
            advance = 2;
            if (cp < 0x80u)
                cp = '?';
        }
    } else if ((c0 & 0xF0u) == 0xE0u && i + 2 < byte_len) {
        unsigned char c1 = (unsigned char)str[i + 1];
        unsigned char c2 = (unsigned char)str[i + 2];
        if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x0Fu) << 12) | ((uint32_t)(c1 & 0x3Fu) << 6) |
                 (uint32_t)(c2 & 0x3Fu);
            advance = 3;
            if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu))
                cp = '?';
        }
    } else if ((c0 & 0xF8u) == 0xF0u && i + 3 < byte_len) {
        unsigned char c1 = (unsigned char)str[i + 1];
        unsigned char c2 = (unsigned char)str[i + 2];
        unsigned char c3 = (unsigned char)str[i + 3];
        if ((c1 & 0xC0u) == 0x80u && (c2 & 0xC0u) == 0x80u && (c3 & 0xC0u) == 0x80u) {
            cp = ((uint32_t)(c0 & 0x07u) << 18) | ((uint32_t)(c1 & 0x3Fu) << 12) |
                 ((uint32_t)(c2 & 0x3Fu) << 6) | (uint32_t)(c3 & 0x3Fu);
            advance = 4;
            if (cp < 0x10000u || cp > 0x10FFFFu)
                cp = '?';
        }
    }

    *index = i + advance;
    *codepoint_out = (int)cp;
    return 1;
}

static int64_t rt_canvas_text_codepoint_width(rt_string text, int64_t scale) {
    if (!text || scale < 1)
        return 0;

    const char *str = rt_string_cstr(text);
    if (!str)
        return 0;

    size_t byte_len = (size_t)rt_str_len(text);
    size_t index = 0;
    int64_t count = 0;
    int codepoint = 0;
    while (rt_canvas_next_codepoint(str, byte_len, &index, &codepoint))
        count++;
    return count * 8 * scale;
}

/// @brief Draw a line between two points on the canvas.
void rt_canvas_line(
    void *canvas_ptr, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_line(canvas->gfx_win,
                  (int32_t)x1,
                  (int32_t)y1,
                  (int32_t)x2,
                  (int32_t)y2,
                  (vgfx_color_t)color);
    }
}

/// @brief Draw a filled rectangle on the canvas.
void rt_canvas_box(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    static int trace_count = 0;
    if (!canvas_ptr)
        return;

    if (rt_trace_canvas_box_enabled() && trace_count < 32) {
        fprintf(stderr,
                "[rt_canvas_box] #%d x=%lld y=%lld w=%lld h=%lld color=%#llx\n",
                trace_count,
                (long long)x,
                (long long)y,
                (long long)w,
                (long long)h,
                (unsigned long long)color);
        ++trace_count;
    }

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_fill_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
    }
}

/// @brief Draw an unfilled rectangle (outline) on the canvas.
void rt_canvas_frame(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
    }
}

/// @brief Draw a filled circle on the canvas.
void rt_canvas_disc(void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_fill_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
    }
}

/// @brief Draw an unfilled circle (outline) on the canvas.
void rt_canvas_ring(void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
    }
}

/// @brief Draw a single pixel at the given coordinates.
void rt_canvas_plot(void *canvas_ptr, int64_t x, int64_t y, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_pset(canvas->gfx_win, (int32_t)x, (int32_t)y, (vgfx_color_t)color);
    }
}

// Color constants — packed 0x00RRGGBB
/// @brief Return the predefined red color constant.
int64_t rt_color_red(void) {
    return 0xFF0000;
}

/// @brief Return the predefined green color constant.
int64_t rt_color_green(void) {
    return 0x00FF00;
}

/// @brief Return the predefined blue color constant.
int64_t rt_color_blue(void) {
    return 0x0000FF;
}

/// @brief Return the predefined white color constant.
int64_t rt_color_white(void) {
    return 0xFFFFFF;
}

/// @brief Return the predefined black color constant.
int64_t rt_color_black(void) {
    return 0x000000;
}

/// @brief Return the predefined yellow color constant.
int64_t rt_color_yellow(void) {
    return 0xFFFF00;
}

/// @brief Return the predefined cyan color constant.
int64_t rt_color_cyan(void) {
    return 0x00FFFF;
}

/// @brief Return the predefined magenta color constant.
int64_t rt_color_magenta(void) {
    return 0xFF00FF;
}

/// @brief Return the predefined gray color constant.
int64_t rt_color_gray(void) {
    return 0x808080;
}

/// @brief Return the predefined orange color constant.
int64_t rt_color_orange(void) {
    return 0xFFA500;
}

/// @brief Construct a color from red, green, blue components (0-255).
int64_t rt_color_rgb(int64_t r, int64_t g, int64_t b) {
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    return (int64_t)vgfx_rgb(r8, g8, b8);
}

/// @brief Construct a color from red, green, blue, alpha components (0-255).
int64_t rt_color_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
    uint8_t r8 = (r < 0) ? 0 : (r > 255) ? 255 : (uint8_t)r;
    uint8_t g8 = (g < 0) ? 0 : (g > 255) ? 255 : (uint8_t)g;
    uint8_t b8 = (b < 0) ? 0 : (b > 255) ? 255 : (uint8_t)b;
    uint8_t a8 = (a < 0) ? 0 : (a > 255) ? 255 : (uint8_t)a;
    int64_t packed = (int64_t)(((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) |
                               (uint32_t)b8);
    if (a8 == 0)
        packed |= RT_COLOR_EXPLICIT_ALPHA_FLAG;
    return packed;
}

//=============================================================================
// Text Rendering
//=============================================================================

/// @brief Draw text at the given position on the canvas.
void rt_canvas_text(void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t color) {
    if (!canvas_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t col = (vgfx_color_t)color;
    size_t byte_len = (size_t)rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (rt_canvas_next_codepoint(str, byte_len, &index, &codepoint)) {
        int glyph_cp = (codepoint >= 32 && codepoint <= 126) ? codepoint : '?';
        const uint8_t *glyph = rt_font_get_glyph(glyph_cp);

        // Draw 8x8 glyph
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++) {
                if (bits & (0x80 >> col_idx)) {
                    vgfx_pset(canvas->gfx_win, (int32_t)(cx + col_idx), (int32_t)(y + row), col);
                }
            }
        }
        cx += 8;
    }
}

/// @brief Draw text at (x, y) with foreground @p fg and explicit @p bg fill behind each glyph.
/// Useful for status bars and code editors where the background must be opaque.
void rt_canvas_text_bg(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg) {
    if (!canvas_ptr || !text)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t fg_col = (vgfx_color_t)fg;
    vgfx_color_t bg_col = (vgfx_color_t)bg;
    size_t byte_len = (size_t)rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (rt_canvas_next_codepoint(str, byte_len, &index, &codepoint)) {
        int glyph_cp = (codepoint >= 32 && codepoint <= 126) ? codepoint : '?';
        const uint8_t *glyph = rt_font_get_glyph(glyph_cp);

        // Draw 8x8 glyph with background
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++) {
                vgfx_pset(canvas->gfx_win,
                          (int32_t)(cx + col_idx),
                          (int32_t)(y + row),
                          (bits & (0x80 >> col_idx)) ? fg_col : bg_col);
            }
        }
        cx += 8;
    }
}

/// @brief Width the text.
int64_t rt_canvas_text_width(rt_string text) {
    return rt_canvas_text_codepoint_width(text, 1);
}

/// @brief Height the text.
int64_t rt_canvas_text_height(void) {
    return 8;
}

//=============================================================================
// Scaled Text Rendering
//=============================================================================

/// @brief Draw text with each pixel of the 8×8 built-in font expanded into a `scale × scale` rect.
/// Useful for HiDPI/big-pixel UIs without loading a separate larger font.
void rt_canvas_text_scaled(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t color) {
    if (!canvas_ptr || !text || scale < 1)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t col = (vgfx_color_t)color;
    size_t byte_len = (size_t)rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (rt_canvas_next_codepoint(str, byte_len, &index, &codepoint)) {
        int glyph_cp = (codepoint >= 32 && codepoint <= 126) ? codepoint : '?';
        const uint8_t *glyph = rt_font_get_glyph(glyph_cp);

        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++) {
                if (bits & (0x80 >> col_idx)) {
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

/// @brief Like `_text_scaled` but fills the @p bg color behind each glyph (full per-pixel cell).
void rt_canvas_text_scaled_bg(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t fg, int64_t bg) {
    if (!canvas_ptr || !text || scale < 1)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t fg_col = (vgfx_color_t)fg;
    vgfx_color_t bg_col = (vgfx_color_t)bg;
    size_t byte_len = (size_t)rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (rt_canvas_next_codepoint(str, byte_len, &index, &codepoint)) {
        int glyph_cp = (codepoint >= 32 && codepoint <= 126) ? codepoint : '?';
        const uint8_t *glyph = rt_font_get_glyph(glyph_cp);

        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++) {
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

/// @brief Scaled the width of the text.
int64_t rt_canvas_text_scaled_width(rt_string text, int64_t scale) {
    return rt_canvas_text_codepoint_width(text, scale);
}

//=============================================================================
// Centered / Right-Aligned Text Helpers
//=============================================================================

/// @brief Centered the text.
void rt_canvas_text_centered(void *canvas_ptr, int64_t y, rt_string text, int64_t color) {
    if (!canvas_ptr || !text)
        return;
    int64_t w = rt_canvas_width(canvas_ptr);
    int64_t tw = rt_canvas_text_width(text);
    int64_t x = (w - tw) / 2;
    rt_canvas_text(canvas_ptr, x, y, text, color);
}

/// @brief Draw text right-aligned to the canvas with @p margin pixels of padding.
void rt_canvas_text_right(
    void *canvas_ptr, int64_t margin, int64_t y, rt_string text, int64_t color) {
    if (!canvas_ptr || !text)
        return;
    int64_t w = rt_canvas_width(canvas_ptr);
    int64_t tw = rt_canvas_text_width(text);
    int64_t x = w - tw - margin;
    rt_canvas_text(canvas_ptr, x, y, text, color);
}

/// @brief Draw scaled text horizontally centered in the canvas at row @p y.
void rt_canvas_text_centered_scaled(
    void *canvas_ptr, int64_t y, rt_string text, int64_t color, int64_t scale) {
    if (!canvas_ptr || !text || scale < 1)
        return;
    int64_t w = rt_canvas_width(canvas_ptr);
    int64_t tw = rt_canvas_text_scaled_width(text, scale);
    int64_t x = (w - tw) / 2;
    rt_canvas_text_scaled(canvas_ptr, x, y, text, scale, color);
}

//=============================================================================
// Alpha-Blended Shapes
//=============================================================================

/// @brief Fill a rectangle with @p color blended at @p alpha [0..255] over the existing pixels.
void rt_canvas_box_alpha(
    void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color, int64_t alpha) {
    if (!canvas_ptr || w <= 0 || h <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    if (alpha <= 0)
        return;
    if (alpha >= 255) {
        vgfx_fill_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
        return;
    }

    uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | ((uint32_t)color & 0x00FFFFFF);

    for (int64_t py = y; py < y + h; py++) {
        for (int64_t px = x; px < x + w; px++) {
            vgfx_pset_alpha(canvas->gfx_win, (int32_t)px, (int32_t)py, argb);
        }
    }
}

/// @brief Fill a disc with @p color blended at @p alpha [0..255] over the existing pixels.
void rt_canvas_disc_alpha(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color, int64_t alpha) {
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    if (alpha <= 0)
        return;
    if (alpha >= 255) {
        vgfx_fill_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
        return;
    }

    uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | ((uint32_t)color & 0x00FFFFFF);
    int64_t r2 = radius * radius;

    for (int64_t dy = -radius; dy <= radius; dy++) {
        for (int64_t dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= r2) {
                vgfx_pset_alpha(canvas->gfx_win, (int32_t)(cx + dx), (int32_t)(cy + dy), argb);
            }
        }
    }
}

//=============================================================================
// Pixel Blitting
//=============================================================================

static int8_t rt_canvas_prepare_blit_region(rt_canvas *canvas,
                                            rt_pixels_impl *pixels,
                                            int64_t *dx,
                                            int64_t *dy,
                                            int64_t *sx,
                                            int64_t *sy,
                                            int64_t *w,
                                            int64_t *h) {
    if (!canvas || !canvas->gfx_win || !pixels || !pixels->data || !dx || !dy || !sx || !sy ||
        !w || !h)
        return 0;

    if (*w <= 0 || *h <= 0)
        return 0;

    if (*sx < 0) {
        int64_t skip = -*sx;
        *dx += skip;
        *w -= skip;
        *sx = 0;
    }
    if (*sy < 0) {
        int64_t skip = -*sy;
        *dy += skip;
        *h -= skip;
        *sy = 0;
    }
    if (*sx + *w > pixels->width)
        *w = pixels->width - *sx;
    if (*sy + *h > pixels->height)
        *h = pixels->height - *sy;
    if (*w <= 0 || *h <= 0)
        return 0;

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return 0;

    if (*dx < clip_x) {
        int64_t skip = clip_x - *dx;
        *sx += skip;
        *w -= skip;
        *dx = clip_x;
    }
    if (*dy < clip_y) {
        int64_t skip = clip_y - *dy;
        *sy += skip;
        *h -= skip;
        *dy = clip_y;
    }

    int64_t clip_x1 = clip_x + clip_w;
    int64_t clip_y1 = clip_y + clip_h;
    if (*dx + *w > clip_x1)
        *w = clip_x1 - *dx;
    if (*dy + *h > clip_y1)
        *h = clip_y1 - *dy;

    return *w > 0 && *h > 0;
}

/// @brief Copy a rectangular region from one surface to another.
void rt_canvas_blit(void *canvas_ptr, int64_t x, int64_t y, void *pixels_ptr) {
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    rt_pixels_impl *pixels = (rt_pixels_impl *)pixels_ptr;
    if (!canvas->gfx_win || !pixels->data)
        return;

    rt_canvas_resync_window_state(canvas);

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    int64_t dx = x;
    int64_t dy = y;
    int64_t sx = 0;
    int64_t sy = 0;
    int64_t w = pixels->width;
    int64_t h = pixels->height;
    if (!rt_canvas_prepare_blit_region(canvas, pixels, &dx, &dy, &sx, &sy, &w, &h))
        return;

    float scale = vgfx_window_get_scale(canvas->gfx_win);

    for (int64_t row = 0; row < h; row++) {
        int64_t py0 = rtg_scale_up_i64(dy + row, scale);
        int64_t py1 = rtg_scale_up_i64(dy + row + 1, scale);
        if (py1 <= py0)
            py1 = py0 + 1;
        if (py0 < 0)
            py0 = 0;
        if (py1 > fb.height)
            py1 = fb.height;
        if (py1 <= py0)
            continue;

        uint32_t *src_row_data = &pixels->data[(sy + row) * pixels->width + sx];
        for (int64_t col = 0; col < w; col++) {
            int64_t px0 = rtg_scale_up_i64(dx + col, scale);
            int64_t px1 = rtg_scale_up_i64(dx + col + 1, scale);
            if (px1 <= px0)
                px1 = px0 + 1;
            if (px0 < 0)
                px0 = 0;
            if (px1 > fb.width)
                px1 = fb.width;
            if (px1 <= px0)
                continue;

            uint32_t rgba = src_row_data[col];
            uint8_t r = (rgba >> 24) & 0xFF;
            uint8_t g = (rgba >> 16) & 0xFF;
            uint8_t b = (rgba >> 8) & 0xFF;
            uint8_t a = rgba & 0xFF;

            for (int64_t py = py0; py < py1; py++) {
                uint8_t *dst = &fb.pixels[(size_t)py * (size_t)fb.stride + (size_t)px0 * 4u];
                for (int64_t px = px0; px < px1; px++) {
                    dst[0] = r;
                    dst[1] = g;
                    dst[2] = b;
                    dst[3] = a;
                    dst += 4;
                }
            }
        }
    }
}

/// @brief Blit a sub-rectangle of @p pixels_ptr onto the canvas at (x, y).
/// Auto-clipped to source and destination bounds; out-of-range source rects are no-ops.
void rt_canvas_blit_region(void *canvas_ptr,
                           int64_t dx,
                           int64_t dy,
                           void *pixels_ptr,
                           int64_t sx,
                           int64_t sy,
                           int64_t w,
                           int64_t h) {
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    rt_pixels_impl *pixels = (rt_pixels_impl *)pixels_ptr;
    if (!canvas->gfx_win || !pixels->data)
        return;

    rt_canvas_resync_window_state(canvas);

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    if (!rt_canvas_prepare_blit_region(canvas, pixels, &dx, &dy, &sx, &sy, &w, &h))
        return;

    float scale = vgfx_window_get_scale(canvas->gfx_win);

    for (int64_t row = 0; row < h; row++) {
        int64_t py0 = rtg_scale_up_i64(dy + row, scale);
        int64_t py1 = rtg_scale_up_i64(dy + row + 1, scale);
        if (py1 <= py0)
            py1 = py0 + 1;
        if (py0 < 0)
            py0 = 0;
        if (py1 > fb.height)
            py1 = fb.height;
        if (py1 <= py0)
            continue;

        uint32_t *src_row = &pixels->data[(sy + row) * pixels->width + sx];
        for (int64_t col = 0; col < w; col++) {
            int64_t px0 = rtg_scale_up_i64(dx + col, scale);
            int64_t px1 = rtg_scale_up_i64(dx + col + 1, scale);
            if (px1 <= px0)
                px1 = px0 + 1;
            if (px0 < 0)
                px0 = 0;
            if (px1 > fb.width)
                px1 = fb.width;
            if (px1 <= px0)
                continue;

            uint32_t rgba = src_row[col];
            uint8_t r = (rgba >> 24) & 0xFF;
            uint8_t g = (rgba >> 16) & 0xFF;
            uint8_t b = (rgba >> 8) & 0xFF;
            uint8_t a = rgba & 0xFF;

            for (int64_t py = py0; py < py1; py++) {
                uint8_t *dst = &fb.pixels[(size_t)py * (size_t)fb.stride + (size_t)px0 * 4u];
                for (int64_t px = px0; px < px1; px++) {
                    dst[0] = r;
                    dst[1] = g;
                    dst[2] = b;
                    dst[3] = a;
                    dst += 4;
                }
            }
        }
    }
}

/// @brief Alpha the blit.
void rt_canvas_blit_alpha(void *canvas_ptr, int64_t x, int64_t y, void *pixels_ptr) {
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    rt_pixels_impl *pixels = (rt_pixels_impl *)pixels_ptr;
    if (!canvas->gfx_win || !pixels->data)
        return;

    rt_canvas_resync_window_state(canvas);

    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb))
        return;

    int64_t dx = x;
    int64_t dy = y;
    int64_t sx = 0;
    int64_t sy = 0;
    int64_t w = pixels->width;
    int64_t h = pixels->height;
    if (!rt_canvas_prepare_blit_region(canvas, pixels, &dx, &dy, &sx, &sy, &w, &h))
        return;

    float scale = vgfx_window_get_scale(canvas->gfx_win);

    for (int64_t row = 0; row < h; row++) {
        int64_t py0 = rtg_scale_up_i64(dy + row, scale);
        int64_t py1 = rtg_scale_up_i64(dy + row + 1, scale);
        if (py1 <= py0)
            py1 = py0 + 1;
        if (py0 < 0)
            py0 = 0;
        if (py1 > fb.height)
            py1 = fb.height;
        if (py1 <= py0)
            continue;

        uint32_t *src_row = &pixels->data[(sy + row) * pixels->width + sx];
        for (int64_t col = 0; col < w; col++) {
            int64_t px0 = rtg_scale_up_i64(dx + col, scale);
            int64_t px1 = rtg_scale_up_i64(dx + col + 1, scale);
            if (px1 <= px0)
                px1 = px0 + 1;
            if (px0 < 0)
                px0 = 0;
            if (px1 > fb.width)
                px1 = fb.width;
            if (px1 <= px0)
                continue;

            uint32_t rgba = src_row[col];
            uint8_t sr = (rgba >> 24) & 0xFF;
            uint8_t sg = (rgba >> 16) & 0xFF;
            uint8_t sb = (rgba >> 8) & 0xFF;
            uint8_t sa = rgba & 0xFF;

            if (sa == 0)
                continue;

            for (int64_t py = py0; py < py1; py++) {
                uint8_t *dst = &fb.pixels[(size_t)py * (size_t)fb.stride + (size_t)px0 * 4u];
                for (int64_t px = px0; px < px1; px++) {
                    if (sa == 255) {
                        dst[0] = sr;
                        dst[1] = sg;
                        dst[2] = sb;
                        dst[3] = 255;
                    } else {
                        uint16_t inv_alpha = 255 - sa;
                        dst[0] = (uint8_t)((sr * sa + dst[0] * inv_alpha) / 255);
                        dst[1] = (uint8_t)((sg * sa + dst[1] * inv_alpha) / 255);
                        dst[2] = (uint8_t)((sb * sa + dst[2] * inv_alpha) / 255);
                        dst[3] = 255;
                    }
                    dst += 4;
                }
            }
        }
    }
}

//=============================================================================
// Canvas Utilities (get_pixel, copy_rect, save_bmp, save_png)
//=============================================================================

/// @brief Get the pixel value.
/// @param canvas_ptr
/// @param x
/// @param y
/// @return Result value.
int64_t rt_canvas_get_pixel(void *canvas_ptr, int64_t x, int64_t y) {
    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas)
        return 0;
    if (!canvas->gfx_win)
        return 0;

    rt_canvas_resync_window_state(canvas);

    vgfx_color_t color;
    if (vgfx_point(canvas->gfx_win, (int32_t)x, (int32_t)y, &color) != 0) {
        return (int64_t)color;
    }
    return 0;
}

void *rt_canvas_copy_rect(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (w <= 0 || h <= 0)
        return NULL;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas)
        return NULL;
    if (!canvas->gfx_win)
        return NULL;

    rt_canvas_resync_window_state(canvas);

    // Create a new Pixels buffer
    void *pixels = rt_pixels_new(w, h);
    if (!pixels)
        return NULL;

    // Copy pixels from canvas to buffer via direct framebuffer access — avoids
    // O(w*h) vgfx_point() calls (each involves clipping + bounds checking).
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(canvas->gfx_win, &fb)) {
        // Framebuffer unavailable (mock/headless); fall back to empty buffer.
        return pixels;
    }

    // Sample the physical pixel at the logical pixel's scaled top-left corner.
    float scale = vgfx_window_get_scale(canvas->gfx_win);

    rt_pixels_impl *pix = (rt_pixels_impl *)pixels;

    for (int64_t py = 0; py < h; py++) {
        int64_t src_y = rtg_scale_up_i64(y + py, scale);
        if (src_y < 0 || src_y >= fb.height)
            continue;

        uint8_t *src_row = &fb.pixels[(size_t)src_y * (size_t)fb.stride];
        uint32_t *dst_row = &pix->data[(size_t)(py * w)];

        for (int64_t px = 0; px < w; px++) {
            int64_t src_x = rtg_scale_up_i64(x + px, scale);
            if (src_x < 0 || src_x >= fb.width) {
                dst_row[px] = 0;
                continue;
            }
            uint8_t r = src_row[(size_t)src_x * 4u + 0u];
            uint8_t g = src_row[(size_t)src_x * 4u + 1u];
            uint8_t b = src_row[(size_t)src_x * 4u + 2u];
            uint8_t a = src_row[(size_t)src_x * 4u + 3u];
            dst_row[px] = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) |
                          (uint32_t)a;
        }
    }

    return pixels;
}

/// @brief Save bmp.
/// @param canvas_ptr
/// @param path
/// @return Result value.
/// @brief Save the canvas contents to a 24-bit BMP file. Returns 1 on success, 0 on failure.
int64_t rt_canvas_save_bmp(void *canvas_ptr, rt_string path) {
    if (!path)
        return 0;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas)
        return 0;
    if (!canvas->gfx_win)
        return 0;

    int32_t w, h;
    if (vgfx_get_size(canvas->gfx_win, &w, &h) == 0)
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

/// @brief Save png.
/// @param canvas_ptr
/// @param path
/// @return Result value.
/// @brief Save the canvas contents to a PNG file (zlib-compressed). Returns 1 on success.
int64_t rt_canvas_save_png(void *canvas_ptr, rt_string path) {
    if (!path)
        return 0;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas)
        return 0;
    if (!canvas->gfx_win)
        return 0;

    int32_t w, h;
    if (vgfx_get_size(canvas->gfx_win, &w, &h) == 0)
        return 0;

    void *pixels = rt_canvas_copy_rect(canvas_ptr, 0, 0, w, h);
    if (!pixels)
        return 0;

    return rt_pixels_save_png(pixels, path);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
