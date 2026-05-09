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

/// @brief Check whether `VIPER_TRACE_CANVAS_BOX` is set, with one-shot caching.
/// @details The env-var lookup happens once on first call and is cached
///          in a static so the trace check on every `Canvas.Box()` is
///          a single int compare. Used by debug builds to log the
///          first 32 box draws — useful when reproducing layout bugs.
static int rt_trace_canvas_box_enabled(void) {
    static int cached = -1;
    if (cached == -1)
        cached = getenv("VIPER_TRACE_CANVAS_BOX") ? 1 : 0;
    return cached;
}

/// @brief Decode the next UTF-8 codepoint from `str` starting at `*index`.
/// @details Implements the standard 1/2/3/4-byte UTF-8 walk:
///          - `0xxxxxxx`            → ASCII (1 byte).
///          - `110xxxxx 10xxxxxx`   → U+0080..U+07FF (2 bytes).
///          - `1110xxxx 10xxxxxx ×2` → U+0800..U+FFFF (3 bytes).
///          - `11110xxx 10xxxxxx ×3` → U+10000..U+10FFFF (4 bytes).
///          For each multi-byte form, validates the continuation bytes
///          (`10xxxxxx`) and rejects:
///          - Overlong encodings (e.g. 2-byte sequence < U+0080).
///          - Surrogate halves (U+D800..U+DFFF) — never legal in UTF-8.
///          - Out-of-range scalars (> U+10FFFF).
///          Substitutes `?` (U+003F) for any malformed sequence so the
///          renderer never blows up on bad input. Advances `*index`
///          by the consumed byte count and returns 0 at EOF.
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

/// @brief Measure rendered text width by counting UTF-8 codepoints (not bytes).
/// @details The 8×8 bitmap font is monospace, so width is simply
///          `codepoints * 8 * scale`. Counting *codepoints* (not raw
///          bytes) ensures non-ASCII glyphs and ASCII glyphs both
///          contribute exactly one cell — `"héllo"` measures the same
///          as `"hello"` regardless of UTF-8 byte length.
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
    return rtg_mul_sat64(rtg_mul_sat64(count, 8), scale);
}

/// @brief Round a long double to the nearest int64, saturating at INT64_MIN/MAX instead of overflowing.
static int64_t rt_canvas_round_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)(value >= 0.0L ? floorl(value + 0.5L) : ceill(value - 0.5L));
}

/// @brief Floor a long double to int64, saturating at INT64_MIN/MAX instead of overflowing.
static int64_t rt_canvas_floor_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)floorl(value);
}

/// @brief Ceil a long double to int64, saturating at INT64_MIN/MAX instead of overflowing.
static int64_t rt_canvas_ceil_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)ceill(value);
}

/// @brief Liang-Barsky parametric line-clip test against one half-plane.
/// @details Tightens the parametric range [u1, u2] for one of the four clip
///          edges and returns 0 when the segment is fully rejected. Called four
///          times by rt_canvas_clip_line_to_logical (left, right, top, bottom).
/// @param p   Half-plane direction: negative for "entering" edges (left/top),
///            positive for "exiting" edges (right/bottom). Zero means the line
///            is parallel to this edge — kept iff @p q is non-negative.
/// @param q   Distance from the segment start to the half-plane.
/// @param u1  In/out: lower parametric bound, advanced when entering.
/// @param u2  In/out: upper parametric bound, retreated when exiting.
/// @return 1 if the segment may still be visible after this edge, 0 if rejected.
static int8_t rt_canvas_clip_line_test(long double p,
                                       long double q,
                                       long double *u1,
                                       long double *u2) {
    if (p == 0.0L)
        return q >= 0.0L;
    long double r = q / p;
    if (p < 0.0L) {
        if (r > *u2)
            return 0;
        if (r > *u1)
            *u1 = r;
    } else {
        if (r < *u1)
            return 0;
        if (r < *u2)
            *u2 = r;
    }
    return 1;
}

/// @brief Clip a line segment to the canvas's logical clip rect (Liang-Barsky in long-double).
/// @details Applies the four-edge clip via rt_canvas_clip_line_test and rewrites
///          the endpoints to the visible portion. Uses long double for the
///          parametric arithmetic so int64 endpoints far apart don't lose
///          precision during the (q / p) division. Final endpoints are rounded
///          back to int64 with saturation, then verified to fit in int32 (the
///          ViperGFX backend takes int32 coordinates).
/// @param canvas Canvas whose clip rect provides the bounds.
/// @param x1,y1  In/out: line start. Replaced with the clipped start on success.
/// @param x2,y2  In/out: line end. Replaced with the clipped end on success.
/// @return 1 if any portion of the line is visible (endpoints updated), 0 if
///         fully outside the clip rect or if endpoints don't fit int32.
static int8_t rt_canvas_clip_line_to_logical(
    rt_canvas *canvas, int64_t *x1, int64_t *y1, int64_t *x2, int64_t *y2) {
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return 0;

    long double lx1 = (long double)*x1;
    long double ly1 = (long double)*y1;
    long double dx = (long double)*x2 - lx1;
    long double dy = (long double)*y2 - ly1;
    long double min_x = (long double)clip_x;
    long double min_y = (long double)clip_y;
    long double max_x = (long double)(rtg_add_sat64(clip_x, clip_w) - 1);
    long double max_y = (long double)(rtg_add_sat64(clip_y, clip_h) - 1);
    long double u1 = 0.0L;
    long double u2 = 1.0L;

    if (!rt_canvas_clip_line_test(-dx, lx1 - min_x, &u1, &u2) ||
        !rt_canvas_clip_line_test(dx, max_x - lx1, &u1, &u2) ||
        !rt_canvas_clip_line_test(-dy, ly1 - min_y, &u1, &u2) ||
        !rt_canvas_clip_line_test(dy, max_y - ly1, &u1, &u2))
        return 0;

    *x1 = rt_canvas_round_ld_to_i64_sat(lx1 + u1 * dx);
    *y1 = rt_canvas_round_ld_to_i64_sat(ly1 + u1 * dy);
    *x2 = rt_canvas_round_ld_to_i64_sat(lx1 + u2 * dx);
    *y2 = rt_canvas_round_ld_to_i64_sat(ly1 + u2 * dy);
    return rtg_i64_fits_i32(*x1) && rtg_i64_fits_i32(*y1) && rtg_i64_fits_i32(*x2) &&
           rtg_i64_fits_i32(*y2);
}

/// @brief Compute the last inclusive pixel of a rect span: start + max(length-1, 0), saturating.
static int64_t rt_canvas_rect_last(int64_t start, int64_t length) {
    if (length <= 1)
        return start;
    return rtg_add_sat64(start, length - 1);
}

/// @brief Draw a line between two points on the canvas.
void rt_canvas_line(
    void *canvas_ptr, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (canvas && canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        if (!rt_canvas_clip_line_to_logical(canvas, &x1, &y1, &x2, &y2))
            return;
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

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (canvas && canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        if (!rt_canvas_clip_intersect_logical(canvas, &x, &y, &w, &h))
            return;
        vgfx_fill_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
    }
}

/// @brief Draw an unfilled rectangle (outline) on the canvas.
void rt_canvas_frame(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (canvas && canvas->gfx_win) {
        if (w <= 0 || h <= 0)
            return;
        int64_t x1 = rt_canvas_rect_last(x, w);
        int64_t y1 = rt_canvas_rect_last(y, h);
        rt_canvas_line(canvas_ptr, x, y, x1, y, color);
        rt_canvas_line(canvas_ptr, x, y1, x1, y1, color);
        rt_canvas_line(canvas_ptr, x, y, x, y1, color);
        rt_canvas_line(canvas_ptr, x1, y, x1, y1, color);
    }
}

/// @brief Draw a filled circle on the canvas.
void rt_canvas_disc(void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (canvas && canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        if (radius < 0 || !rtg_i64_fits_i32(cx) || !rtg_i64_fits_i32(cy) ||
            !rtg_i64_fits_i32(radius))
            return;
        vgfx_fill_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
    }
}

/// @brief Draw an unfilled circle (outline) on the canvas.
void rt_canvas_ring(void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (canvas && canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        if (radius < 0 || !rtg_i64_fits_i32(cx) || !rtg_i64_fits_i32(cy) ||
            !rtg_i64_fits_i32(radius))
            return;
        vgfx_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
    }
}

/// @brief Draw a single pixel at the given coordinates.
void rt_canvas_plot(void *canvas_ptr, int64_t x, int64_t y, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (canvas && canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        int64_t w = 1;
        int64_t h = 1;
        if (!rt_canvas_clip_intersect_logical(canvas, &x, &y, &w, &h))
            return;
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
    int64_t packed =
        (int64_t)(((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8);
    return packed | RT_COLOR_EXPLICIT_ALPHA_FLAG;
}

/// @brief Test whether (x, y) is inside the clip rect [clip_x, clip_x+clip_w) × [clip_y, clip_y+clip_h)
///        using saturating int64 math, with a bonus int32-fits check for the eventual vgfx call.
/// @details Used by the per-pixel plot/fill helpers below. Saturating addition
///          ensures clip_x + clip_w doesn't wrap on extreme inputs.
/// @return 1 if the point is inside the clip rect AND fits in int32, 0 otherwise.
static int8_t rt_canvas_point_in_clip_i64(int64_t x,
                                          int64_t y,
                                          int64_t clip_x,
                                          int64_t clip_y,
                                          int64_t clip_w,
                                          int64_t clip_h) {
    if (clip_w <= 0 || clip_h <= 0)
        return 0;
    if (x < clip_x || y < clip_y)
        return 0;
    if (x >= rtg_add_sat64(clip_x, clip_w) || y >= rtg_add_sat64(clip_y, clip_h))
        return 0;
    return rtg_i64_fits_i32(x) && rtg_i64_fits_i32(y);
}

/// @brief Plot one pixel at (x, y) iff it falls inside the clip rect.
/// @details Used by line/disc/ring/text rasterizers that step pixel-by-pixel
///          and want clip-correct behavior without paying for a separate
///          ViperGFX clip-set per pixel. NULL canvas / NULL gfx_win are no-ops.
static void rt_canvas_pset_clipped(rt_canvas *canvas,
                                   int64_t x,
                                   int64_t y,
                                   vgfx_color_t color,
                                   int64_t clip_x,
                                   int64_t clip_y,
                                   int64_t clip_w,
                                   int64_t clip_h) {
    if (!canvas || !canvas->gfx_win ||
        !rt_canvas_point_in_clip_i64(x, y, clip_x, clip_y, clip_w, clip_h))
        return;
    vgfx_pset(canvas->gfx_win, (int32_t)x, (int32_t)y, color);
}

/// @brief Fill the intersection of the (x, y, w, h) rect with the clip rect.
/// @details Computes [x0, x1) × [y0, y1) as the intersection in saturating
///          int64 math, then verifies each side fits in int32 before issuing
///          the vgfx_fill_rect call. Empty intersections (x1 <= x0) are no-ops.
///          Used by box/frame primitives that may straddle the clip boundary.
static void rt_canvas_fill_rect_clipped(rt_canvas *canvas,
                                        int64_t x,
                                        int64_t y,
                                        int64_t w,
                                        int64_t h,
                                        vgfx_color_t color,
                                        int64_t clip_x,
                                        int64_t clip_y,
                                        int64_t clip_w,
                                        int64_t clip_h) {
    if (!canvas || !canvas->gfx_win || w <= 0 || h <= 0 || clip_w <= 0 || clip_h <= 0)
        return;

    int64_t x0 = rtg_max64(x, clip_x);
    int64_t y0 = rtg_max64(y, clip_y);
    int64_t x1 = rtg_min64(rtg_add_sat64(x, w), rtg_add_sat64(clip_x, clip_w));
    int64_t y1 = rtg_min64(rtg_add_sat64(y, h), rtg_add_sat64(clip_y, clip_h));
    if (x1 <= x0 || y1 <= y0)
        return;
    if (!rtg_i64_fits_i32(x0) || !rtg_i64_fits_i32(y0) || !rtg_i64_fits_i32(x1 - x0) ||
        !rtg_i64_fits_i32(y1 - y0))
        return;
    vgfx_fill_rect(canvas->gfx_win, (int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0),
                   (int32_t)(y1 - y0), color);
}

//=============================================================================
// Text Rendering
//=============================================================================

/// @brief Draw text at the given position on the canvas.
void rt_canvas_text(void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t color) {
    if (!canvas_ptr || !text)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t col = (vgfx_color_t)color;
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
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
                    rt_canvas_pset_clipped(canvas,
                                           rtg_add_sat64(cx, col_idx),
                                           rtg_add_sat64(y, row),
                                           col,
                                           clip_x,
                                           clip_y,
                                           clip_w,
                                           clip_h);
                }
            }
        }
        cx = rtg_add_sat64(cx, 8);
    }
}

/// @brief Draw text at (x, y) with foreground @p fg and explicit @p bg fill behind each glyph.
/// Useful for status bars and code editors where the background must be opaque.
void rt_canvas_text_bg(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t fg, int64_t bg) {
    if (!canvas_ptr || !text)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t fg_col = (vgfx_color_t)fg;
    vgfx_color_t bg_col = (vgfx_color_t)bg;
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
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
                rt_canvas_pset_clipped(canvas,
                                       rtg_add_sat64(cx, col_idx),
                                       rtg_add_sat64(y, row),
                                       (bits & (0x80 >> col_idx)) ? fg_col : bg_col,
                                       clip_x,
                                       clip_y,
                                       clip_w,
                                       clip_h);
            }
        }
        cx = rtg_add_sat64(cx, 8);
    }
}

/// @brief Return the rendered width of `text` in pixels at 1× scale.
/// @details Counts UTF-8 codepoints (so multibyte glyphs each take one
///          cell of the monospace 8×8 font) and multiplies by 8.
int64_t rt_canvas_text_width(rt_string text) {
    return rt_canvas_text_codepoint_width(text, 1);
}

/// @brief Return the height of one line of text in pixels — always 8 for the built-in font.
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

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t col = (vgfx_color_t)color;
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
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
                    rt_canvas_fill_rect_clipped(
                        canvas,
                        rtg_add_sat64(cx, rtg_mul_sat64(col_idx, scale)),
                        rtg_add_sat64(y, rtg_mul_sat64(row, scale)),
                        scale,
                        scale,
                        col,
                        clip_x,
                        clip_y,
                        clip_w,
                        clip_h);
                }
            }
        }
        cx = rtg_add_sat64(cx, rtg_mul_sat64(8, scale));
    }
}

/// @brief Like `_text_scaled` but fills the @p bg color behind each glyph (full per-pixel cell).
void rt_canvas_text_scaled_bg(
    void *canvas_ptr, int64_t x, int64_t y, rt_string text, int64_t scale, int64_t fg, int64_t bg) {
    if (!canvas_ptr || !text || scale < 1)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    const char *str = rt_string_cstr(text);
    if (!str)
        return;

    int64_t cx = x;
    vgfx_color_t fg_col = (vgfx_color_t)fg;
    vgfx_color_t bg_col = (vgfx_color_t)bg;
    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return;
    size_t byte_len = (size_t)rt_str_len(text);
    size_t index = 0;
    int codepoint = 0;

    while (rt_canvas_next_codepoint(str, byte_len, &index, &codepoint)) {
        int glyph_cp = (codepoint >= 32 && codepoint <= 126) ? codepoint : '?';
        const uint8_t *glyph = rt_font_get_glyph(glyph_cp);

        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col_idx = 0; col_idx < 8; col_idx++) {
                rt_canvas_fill_rect_clipped(
                    canvas,
                    rtg_add_sat64(cx, rtg_mul_sat64(col_idx, scale)),
                    rtg_add_sat64(y, rtg_mul_sat64(row, scale)),
                    scale,
                    scale,
                    (bits & (0x80 >> col_idx)) ? fg_col : bg_col,
                    clip_x,
                    clip_y,
                    clip_w,
                    clip_h);
            }
        }
        cx = rtg_add_sat64(cx, rtg_mul_sat64(8, scale));
    }
}

/// @brief Return the rendered width of `text` in pixels when drawn at the given integer scale.
int64_t rt_canvas_text_scaled_width(rt_string text, int64_t scale) {
    return rt_canvas_text_codepoint_width(text, scale);
}

//=============================================================================
// Centered / Right-Aligned Text Helpers
//=============================================================================

/// @brief Draw `text` horizontally centered in the canvas at row `y`.
/// @details Pre-measures via `text_width`, then offsets x so the
///          rendered glyphs sit symmetrically about the canvas
///          centerline.
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

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    if (alpha <= 0)
        return;
    if (!rt_canvas_clip_intersect_logical(canvas, &x, &y, &w, &h))
        return;
    if (alpha >= 255) {
        vgfx_fill_rect(
            canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, (vgfx_color_t)color);
        return;
    }

    uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | ((uint32_t)color & 0x00FFFFFF);

    int64_t y_end = y + h;
    int64_t x_end = x + w;
    for (int64_t py = y; py < y_end; py++) {
        for (int64_t px = x; px < x_end; px++) {
            vgfx_pset_alpha(canvas->gfx_win, (int32_t)px, (int32_t)py, argb);
        }
    }
}

/// @brief Fill a disc with @p color blended at @p alpha [0..255] over the existing pixels.
void rt_canvas_disc_alpha(
    void *canvas_ptr, int64_t cx, int64_t cy, int64_t radius, int64_t color, int64_t alpha) {
    if (!canvas_ptr || radius <= 0)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_resync_window_state(canvas);

    if (alpha <= 0)
        return;
    if (alpha >= 255 && rtg_i64_fits_i32(cx) && rtg_i64_fits_i32(cy) && rtg_i64_fits_i32(radius)) {
        vgfx_fill_circle(
            canvas->gfx_win, (int32_t)cx, (int32_t)cy, (int32_t)radius, (vgfx_color_t)color);
        return;
    }

    uint32_t argb = ((uint32_t)(alpha & 0xFF) << 24) | ((uint32_t)color & 0x00FFFFFF);

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
    for (int64_t py = y0; py <= y1; py++) {
        long double dy = (long double)py - (long double)cy;
        long double rem = r2 - dy * dy;
        if (rem < 0.0L)
            continue;
        long double dx = sqrtl(rem);
        int64_t x0 = rt_canvas_floor_ld_to_i64_sat((long double)cx - dx);
        int64_t x1 = rt_canvas_ceil_ld_to_i64_sat((long double)cx + dx);
        int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w) - 1;
        if (x0 < clip_x)
            x0 = clip_x;
        if (x1 > clip_x1)
            x1 = clip_x1;
        for (int64_t px = x0; px <= x1; px++) {
            if (alpha >= 255)
                vgfx_pset(canvas->gfx_win, (int32_t)px, (int32_t)py, (vgfx_color_t)color);
            else
                vgfx_pset_alpha(canvas->gfx_win, (int32_t)px, (int32_t)py, argb);
        }
    }
}

//=============================================================================
// Pixel Blitting
//=============================================================================

/// @brief Clip a blit region to both source pixels bounds and destination canvas + clip rect.
/// @details In-place adjusts `(dx, dy, sx, sy, w, h)` so the resulting
///          rectangle is fully contained in:
///          1. The source `pixels` (skip the leading area where `sx`
///             or `sy` is negative; trim the trailing area that
///             extends past the source extent).
///          2. The canvas's logical clip rectangle (skip leading area
///             that lands left/above the clip; trim trailing area
///             that extends right/below).
///          Returns 0 if the resulting region is empty (caller should
///          skip the blit). Centralized here so all four blit variants
///          (opaque, region, alpha, alpha-region) share identical
///          clipping behavior — keeps clip semantics consistent and
///          off-by-one bugs to one place.
static int8_t rt_canvas_prepare_blit_region(rt_canvas *canvas,
                                            rt_pixels_impl *pixels,
                                            int64_t *dx,
                                            int64_t *dy,
                                            int64_t *sx,
                                            int64_t *sy,
                                            int64_t *w,
                                            int64_t *h) {
    if (!canvas || !canvas->gfx_win || !pixels || !pixels->data || !dx || !dy || !sx || !sy || !w ||
        !h)
        return 0;

    if (*w <= 0 || *h <= 0)
        return 0;

    int64_t dummy_limit = INT64_MAX;
    if (!rtg_clip_copy_axis(dummy_limit, pixels->width, dx, sx, w) ||
        !rtg_clip_copy_axis(dummy_limit, pixels->height, dy, sy, h))
        return 0;

    int64_t clip_x = 0;
    int64_t clip_y = 0;
    int64_t clip_w = 0;
    int64_t clip_h = 0;
    if (!rt_canvas_get_logical_clip_bounds(canvas, &clip_x, &clip_y, &clip_w, &clip_h))
        return 0;

    if (*dx < clip_x) {
        int64_t skip = *dx == INT64_MIN ? *w : clip_x - *dx;
        if (skip >= *w)
            return 0;
        *sx += skip;
        *w -= skip;
        *dx = clip_x;
    }
    if (*dy < clip_y) {
        int64_t skip = *dy == INT64_MIN ? *h : clip_y - *dy;
        if (skip >= *h)
            return 0;
        *sy += skip;
        *h -= skip;
        *dy = clip_y;
    }

    int64_t clip_x1 = rtg_add_sat64(clip_x, clip_w);
    int64_t clip_y1 = rtg_add_sat64(clip_y, clip_h);
    if (*dx >= clip_x1 || *dy >= clip_y1)
        return 0;
    if (*w > clip_x1 - *dx)
        *w = clip_x1 - *dx;
    if (*h > clip_y1 - *dy)
        *h = clip_y1 - *dy;

    return *w > 0 && *h > 0;
}

/// @brief Copy a rectangular region from one surface to another.
void rt_canvas_blit(void *canvas_ptr, int64_t x, int64_t y, void *pixels_ptr) {
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    rt_pixels_impl *pixels = rt_pixels_checked_impl(pixels_ptr, "Canvas.Blit: invalid pixels");
    if (!canvas || !canvas->gfx_win || !pixels->data)
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

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    rt_pixels_impl *pixels = rt_pixels_checked_impl(pixels_ptr, "Canvas.BlitRegion: invalid pixels");
    if (!canvas || !canvas->gfx_win || !pixels->data)
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

/// @brief Blit a Pixels source onto the canvas with per-pixel alpha blending.
/// @details Like `rt_canvas_blit` but each source pixel composites onto
///          the destination using straight alpha:
///          - α = 0   → skip (preserves dest exactly).
///          - α = 255 → overwrite (fast path, no division).
///          - else    → straight-alpha source-over, preserving destination alpha.
///          Honors the canvas's HiDPI scale by expanding each logical
///          source pixel into the corresponding physical pixel block
///          (so a 1× sprite stays sharp on a 2× display).
void rt_canvas_blit_alpha(void *canvas_ptr, int64_t x, int64_t y, void *pixels_ptr) {
    if (!canvas_ptr || !pixels_ptr)
        return;

    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    rt_pixels_impl *pixels = rt_pixels_checked_impl(pixels_ptr, "Canvas.BlitAlpha: invalid pixels");
    if (!canvas || !canvas->gfx_win || !pixels->data)
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
                        uint32_t inv_alpha = 255u - sa;
                        uint32_t da = dst[3];
                        uint32_t out_a = sa + (da * inv_alpha + 127u) / 255u;
                        if (out_a == 0) {
                            dst[0] = 0;
                            dst[1] = 0;
                            dst[2] = 0;
                            dst[3] = 0;
                        } else {
                            uint32_t r_pm = sr * sa + (dst[0] * da * inv_alpha + 127u) / 255u;
                            uint32_t g_pm = sg * sa + (dst[1] * da * inv_alpha + 127u) / 255u;
                            uint32_t b_pm = sb * sa + (dst[2] * da * inv_alpha + 127u) / 255u;
                            uint32_t r = (r_pm + out_a / 2u) / out_a;
                            uint32_t g = (g_pm + out_a / 2u) / out_a;
                            uint32_t b = (b_pm + out_a / 2u) / out_a;
                            dst[0] = (uint8_t)(r > 255u ? 255u : r);
                            dst[1] = (uint8_t)(g > 255u ? 255u : g);
                            dst[2] = (uint8_t)(b > 255u ? 255u : b);
                            dst[3] = (uint8_t)out_a;
                        }
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

/// @brief Read a single pixel's color from the canvas at the given logical coordinates.
/// @details Returns the packed 0xRRGGBB color at (x, y), or 0 if the
///          canvas isn't ready or (x, y) is out of bounds. Useful for
///          procedural painting tools and color picking. Goes through
///          `vgfx_point` rather than direct framebuffer access so the
///          read honors any pending coordinate transforms.
int64_t rt_canvas_get_pixel(void *canvas_ptr, int64_t x, int64_t y) {
    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas)
        return 0;
    if (!canvas->gfx_win)
        return 0;

    rt_canvas_resync_window_state(canvas);
    if (!rtg_i64_fits_i32(x) || !rtg_i64_fits_i32(y))
        return 0;

    vgfx_color_t color;
    if (vgfx_point(canvas->gfx_win, (int32_t)x, (int32_t)y, &color) != 0) {
        return (int64_t)color;
    }
    return 0;
}

/// @brief Copy a rectangular region of the canvas into a freshly allocated Pixels object.
/// @details Goes through the live framebuffer rather than per-pixel
///          `vgfx_point` calls — for an `N×M` rect that's `O(N*M)`
///          instead of `O(N*M)` clipped queries, easily 10× faster
///          on large screenshots. HiDPI: each logical pixel samples
///          its scaled top-left physical pixel, so the returned
///          Pixels stays at logical resolution (matches what the
///          user drew, not what the GPU rendered). Out-of-range
///          source pixels are recorded as 0 so the resulting buffer
///          is dense and easy to feed back into `Canvas.Blit`.
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

    rt_pixels_impl *pix = rt_pixels_checked_impl_or_null(pixels);
    if (!pix || !pix->data)
        return pixels;

    for (int64_t py = 0; py < h; py++) {
        int64_t src_y = rtg_scale_up_i64(rtg_add_sat64(y, py), scale);
        if (src_y < 0 || src_y >= fb.height)
            continue;

        uint8_t *src_row = &fb.pixels[(size_t)src_y * (size_t)fb.stride];
        uint32_t *dst_row = &pix->data[(size_t)(py * w)];

        for (int64_t px = 0; px < w; px++) {
            int64_t src_x = rtg_scale_up_i64(rtg_add_sat64(x, px), scale);
            if (src_x < 0 || src_x >= fb.width) {
                dst_row[px] = 0;
                continue;
            }
            uint8_t r = src_row[(size_t)src_x * 4u + 0u];
            uint8_t g = src_row[(size_t)src_x * 4u + 1u];
            uint8_t b = src_row[(size_t)src_x * 4u + 2u];
            uint8_t a = src_row[(size_t)src_x * 4u + 3u];
            dst_row[px] =
                ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
        }
    }

    return pixels;
}

/// @brief Save the canvas contents to a 24-bit BMP file at `path`.
/// @details Implementation steps:
///          1. Snapshot the live canvas via `rt_canvas_copy_rect`
///             into a temporary Pixels object.
///          2. Delegate to `rt_pixels_save_bmp` for the actual file
///             write (handles BMP header, row alignment, alpha→24-bit
///             demotion).
///          3. Release the temporary Pixels after the write completes.
/// @return 1 on success, 0 on any failure (no canvas, write error, ...).
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

    int64_t result = rt_pixels_save_bmp(pixels, path);
    if (rt_obj_release_check0(pixels))
        rt_obj_free(pixels);

    return result;
}

/// @brief Save the canvas contents to a PNG file at `path`.
/// @details Same flow as `_save_bmp` but routes to `rt_pixels_save_png`
///          which produces a deflate-compressed RGBA PNG (preserving
///          per-pixel alpha unlike the BMP path). Smaller files than
///          BMP for typical UI screenshots; slower to write because
///          of the compression pass.
/// @return 1 on success, 0 on any failure.
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

    int64_t result = rt_pixels_save_png(pixels, path);
    if (rt_obj_release_check0(pixels))
        rt_obj_free(pixels);
    return result;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
