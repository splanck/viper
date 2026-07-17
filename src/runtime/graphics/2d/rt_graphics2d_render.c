//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_graphics2d_render.c
// Purpose: Higher-level 2D renderer classes — ShapeRenderer2D, TextRenderer2D,
//          SdfFont, NineSlice2D, and DebugDraw2D. Split out of rt_graphics2d.c;
//          shares the 2D foundation helpers (blit/draw/size math) via
//          rt_graphics2d_internal.h.
//
// Key invariants:
//   - Class handles are validated via the *_checked casts before use.
//   - All blitting routes through the shared rt2d_blit_pixels so tint/alpha/blend
//     behavior stays consistent with the rest of the 2D layer.
//
// Ownership/Lifetime:
//   - Class impls are GC objects; retained fonts / command buffers are released
//     on finalize. Helpers borrow caller handles.
//
// Links: src/runtime/graphics/2d/rt_graphics2d.c (other 2D classes + helpers),
//        src/runtime/graphics/2d/rt_graphics2d_internal.h (shared helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics2d.h"
#include "rt_graphics2d_internal.h"

#include "rt_bitmapfont.h"
#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Clamp a value to [min, max] (file-local copy; the name is shared by
///        several runtime modules as a static helper).
static int64_t clamp_i64(int64_t value, int64_t min, int64_t max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// Renderer class IDs
#define RT2D_SHAPERENDERER_CLASS_ID INT64_C(-0x62010B)
#define RT2D_TEXTRENDERER_CLASS_ID INT64_C(-0x62010C)
#define RT2D_SDFFONT_CLASS_ID INT64_C(-0x62010D)
#define RT2D_NINESLICE_CLASS_ID INT64_C(-0x62010E)
#define RT2D_DEBUGDRAW_CLASS_ID INT64_C(-0x62010F)

// Renderer impl structs
typedef struct {
    int64_t stroke;
    int64_t fill;
} rt_shaperenderer2d_impl;

typedef struct {
    void *font;
    int64_t scale;
    int64_t color;
} rt_textrenderer2d_impl;

typedef struct {
    void *bitmap_font;
    int64_t spread;
} rt_sdffont_impl;

typedef struct {
    void *pixels;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
} rt_nineslice2d_impl;

typedef struct {
    int32_t type;
    int64_t x0;
    int64_t y0;
    int64_t x1;
    int64_t y1;
    int64_t value;
    int64_t color;
} rt_debugdraw2d_cmd;

typedef struct {
    rt_debugdraw2d_cmd *cmds;
    int64_t count;
    int64_t capacity;
} rt_debugdraw2d_impl;

//=============================================================================
// ShapeRenderer2D
//=============================================================================
// Stateful line / rect / circle / path drawer. Holds current stroke + fill
// colors; drawing ops write directly into a Pixels buffer via the shared
// `rt_pixels_draw_*` primitives. A negative color (the default for `fill`)
// skips that half of the draw — e.g. rect with `fill = -1` is stroke-only.

/// @brief Allocate a ShapeRenderer2D with default colors (white stroke, no fill).
void *rt_shaperenderer2d_new(void) {
    rt_shaperenderer2d_impl *renderer = (rt_shaperenderer2d_impl *)rt_obj_new_i64(
        RT2D_SHAPERENDERER_CLASS_ID, (int64_t)sizeof(rt_shaperenderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->stroke = 0x00FFFFFF;
    renderer->fill = -1;
    return renderer;
}

/// @brief Set the stroke (outline) color; pass any negative value to disable stroke.
void rt_shaperenderer2d_set_stroke(void *renderer, int64_t rgba) {
    if (rt2d_has_class(renderer, RT2D_SHAPERENDERER_CLASS_ID))
        ((rt_shaperenderer2d_impl *)renderer)->stroke = rgba;
}

/// @brief Set the fill color; pass any negative value to disable fill (stroke-only mode).
void rt_shaperenderer2d_set_fill(void *renderer, int64_t rgba) {
    if (rt2d_has_class(renderer, RT2D_SHAPERENDERER_CLASS_ID))
        ((rt_shaperenderer2d_impl *)renderer)->fill = rgba;
}

/// @brief Draw a line from `(x0, y0)` to `(x1, y1)` using the current stroke color.
/// @details Uses `rt_pixels_draw_line`; no-op if stroke is negative or any pointer is NULL.
void rt_shaperenderer2d_line(
    void *renderer, void *pixels, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    if (!rt2d_has_class(renderer, RT2D_SHAPERENDERER_CLASS_ID) || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->stroke < 0)
        return;
    rt_pixels_draw_line(pixels, x0, y0, x1, y1, rt2d_draw_rgb(impl->stroke));
}

/// @brief Draw a rectangle at `(x, y)` with `width × height`.
/// @details Draws a filled solid rect first (if fill >= 0), then a 1-px
///          outline frame on top (if stroke >= 0), so the stroke is always
///          visible over the fill color.
void rt_shaperenderer2d_rect(
    void *renderer, void *pixels, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!rt2d_has_class(renderer, RT2D_SHAPERENDERER_CLASS_ID) || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->fill >= 0)
        rt_pixels_draw_box(pixels, x, y, width, height, rt2d_draw_rgb(impl->fill));
    if (impl->stroke >= 0)
        rt_pixels_draw_frame(pixels, x, y, width, height, rt2d_draw_rgb(impl->stroke));
}

/// @brief Draw a circle at `(x, y)` with the given `radius`.
/// @details Same fill-then-stroke ordering as `rt_shaperenderer2d_rect`.
///          Fill uses `rt_pixels_draw_disc`; stroke uses `rt_pixels_draw_ring`.
void rt_shaperenderer2d_circle(void *renderer, void *pixels, int64_t x, int64_t y, int64_t radius) {
    if (!rt2d_has_class(renderer, RT2D_SHAPERENDERER_CLASS_ID) || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->fill >= 0)
        rt_pixels_draw_disc(pixels, x, y, radius, rt2d_draw_rgb(impl->fill));
    if (impl->stroke >= 0)
        rt_pixels_draw_ring(pixels, x, y, radius, rt2d_draw_rgb(impl->stroke));
}

/// @brief Render a Path2D into `pixels` using the current stroke color.
/// @details Delegates entirely to `rt_path2d_draw_to_pixels`; fill is ignored
///          for paths (paths have no closed-region fill support).
void rt_shaperenderer2d_path(void *renderer, void *pixels, void *path) {
    if (!rt2d_has_class(renderer, RT2D_SHAPERENDERER_CLASS_ID) || !pixels || !path)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->stroke < 0)
        return;
    rt_path2d_draw_to_pixels(path, pixels, impl->stroke);
}

//=============================================================================
// TextRenderer2D
//=============================================================================
// Wraps a BitmapFont (optional) plus a scale + color, and exposes measure /
// draw entries that go through the existing `rt_canvas_text_*` primitives.
// When no font is bound, measurement and drawing fall back to the Canvas
// built-in font so a TextRenderer2D is always usable from construction.

/// @brief GC finalizer — releases the retained BitmapFont reference.
static void textrenderer2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_TEXTRENDERER_CLASS_ID))
        return;
    rt_textrenderer2d_impl *renderer = (rt_textrenderer2d_impl *)obj;
    rt2d_release_ref_slot(&renderer->font);
}

/// @brief Allocate a TextRenderer2D with default state (no font, 1x scale, white).
void *rt_textrenderer2d_new(void) {
    rt_textrenderer2d_impl *renderer = (rt_textrenderer2d_impl *)rt_obj_new_i64(
        RT2D_TEXTRENDERER_CLASS_ID, (int64_t)sizeof(rt_textrenderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->scale = 1;
    renderer->color = 0x00FFFFFF;
    rt_obj_set_finalizer(renderer, textrenderer2d_finalize);
    return renderer;
}

/// @brief Bind a BitmapFont to this renderer, retaining a reference.
/// @details Releases any previously held font before storing the new one,
///          following the standard retain-before-release slot discipline.
///          Pass NULL to revert to the built-in Canvas font.
void rt_textrenderer2d_set_font(void *renderer, void *font) {
    if (!rt2d_has_class(renderer, RT2D_TEXTRENDERER_CLASS_ID) ||
        (font && !rt2d_is_bitmap_font_handle(font)))
        return;
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    rt2d_retain_ref(font);
    rt2d_release_ref_slot(&impl->font);
    impl->font = font;
}

/// @brief Set the integer pixel scale factor; clamped to [1, 64].
void rt_textrenderer2d_set_scale(void *renderer, int64_t scale) {
    if (rt2d_has_class(renderer, RT2D_TEXTRENDERER_CLASS_ID))
        ((rt_textrenderer2d_impl *)renderer)->scale = clamp_i64(scale, 1, 64);
}

/// @brief Set the text color as a packed 0x00RRGGBB value (alpha bits are masked off).
void rt_textrenderer2d_set_color(void *renderer, int64_t rgb) {
    if (rt2d_has_class(renderer, RT2D_TEXTRENDERER_CLASS_ID))
        ((rt_textrenderer2d_impl *)renderer)->color = rgb & 0x00FFFFFF;
}

/// @brief Measure the pixel width of `text` using the bound font and scale.
/// @details Falls back to `rt_canvas_text_width` when no BitmapFont is bound.
///          Width is multiplied by scale using saturating arithmetic to avoid overflow.
int64_t rt_textrenderer2d_measure_width(void *renderer, rt_string text) {
    if (!rt2d_has_class(renderer, RT2D_TEXTRENDERER_CLASS_ID))
        return rt_canvas_text_width(text);
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    int64_t width =
        impl->font ? rt_bitmapfont_text_width(impl->font, text) : rt_canvas_text_width(text);
    return rt2d_saturating_mul_i64(width, impl->scale);
}

/// @brief Measure the pixel height of one line of text with the bound font and scale.
/// @details The `text` argument is ignored — line height is font-uniform, not
///          string-dependent. Falls back to `rt_canvas_text_height` when no font is bound.
int64_t rt_textrenderer2d_measure_height(void *renderer, rt_string text) {
    (void)text;
    if (!rt2d_has_class(renderer, RT2D_TEXTRENDERER_CLASS_ID))
        return rt_canvas_text_height();
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    int64_t height = impl->font ? rt_bitmapfont_text_height(impl->font) : rt_canvas_text_height();
    return rt2d_saturating_mul_i64(height, impl->scale);
}

/// @brief Draw `text` at `(x, y)` into `canvas` using the bound font, scale, and color.
/// @details If a BitmapFont is bound, uses `rt_canvas_text_font_scaled`; otherwise
///          falls back to `rt_canvas_text_scaled` with the built-in Canvas font.
void rt_textrenderer2d_draw(void *renderer, void *canvas, int64_t x, int64_t y, rt_string text) {
    if (!rt2d_has_class(renderer, RT2D_TEXTRENDERER_CLASS_ID) || !canvas)
        return;
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    if (impl->font)
        rt_canvas_text_font_scaled(canvas, x, y, text, impl->font, impl->scale, impl->color);
    else
        rt_canvas_text_scaled(canvas, x, y, text, impl->scale, impl->color);
}

//=============================================================================
// SdfFont
//=============================================================================
// Forward-compatible name for a signed-distance-field font. The current
// backend wraps a BitmapFont and stores a `spread` parameter; real SDF
// raster drawing is a future addition. Callers should code against the
// SdfFont surface today and gain crisper scaling when the backend upgrades.

/// @brief GC finalizer — releases the retained BitmapFont reference.
static void sdffont_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_SDFFONT_CLASS_ID))
        return;
    rt_sdffont_impl *font = (rt_sdffont_impl *)obj;
    rt2d_release_ref_slot(&font->bitmap_font);
}

/// @brief Wrap a BitmapFont as an SdfFont with the given SDF spread parameter.
/// @details `spread` is clamped to `[1, 64]`. Consumers that support real SDF
///          rendering will use `spread` directly; the current bitmap-backed
///          implementation records it but ignores it at draw time.
void *rt_sdffont_new(void *bitmap_font, int64_t spread) {
    if (bitmap_font && !rt2d_is_bitmap_font_handle(bitmap_font))
        return NULL;
    rt2d_retain_ref(bitmap_font);
    rt_sdffont_impl *font =
        (rt_sdffont_impl *)rt_obj_new_i64(RT2D_SDFFONT_CLASS_ID, (int64_t)sizeof(rt_sdffont_impl));
    if (!font) {
        void *owned_font = bitmap_font;
        rt2d_release_ref_slot(&owned_font);
        return NULL;
    }
    font->bitmap_font = bitmap_font;
    font->spread = clamp_i64(spread, 1, 64);
    rt_obj_set_finalizer(font, sdffont_finalize);
    return font;
}

/// @brief Return the underlying BitmapFont pointer (not retained — caller must not release it).
void *rt_sdffont_get_bitmap_font(void *font) {
    return rt2d_has_class(font, RT2D_SDFFONT_CLASS_ID) ? ((rt_sdffont_impl *)font)->bitmap_font
                                                       : NULL;
}

/// @brief Return the SDF spread value stored at construction time (range [1, 64]).
int64_t rt_sdffont_get_spread(void *font) {
    return rt2d_has_class(font, RT2D_SDFFONT_CLASS_ID) ? ((rt_sdffont_impl *)font)->spread : 0;
}

//=============================================================================
// NineSlice2D
//=============================================================================
// Stretchable UI image — four corner tiles stay fixed-size, four edge tiles
// stretch along one axis, the center tile stretches both axes. Used for
// resizable panels, buttons, and window frames where the border decoration
// shouldn't smear under scale. The `left / top / right / bottom` parameters
// are source-image border widths, measured inward from each edge.

/// @brief GC finalizer — releases the retained source Pixels.
static void nineslice2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_NINESLICE_CLASS_ID))
        return;
    rt_nineslice2d_impl *slice = (rt_nineslice2d_impl *)obj;
    rt2d_release_ref_slot(&slice->pixels);
}

/// @brief Wrap a source Pixels image as a nine-slice with the given border widths.
/// @details Border widths are clamped to `[0, image_dim]` so passing e.g. a
///          border larger than the image falls back to the whole image edge.
///          The caller retains ownership of their `pixels` reference; this
///          constructor takes its own.
void *rt_nineslice2d_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom) {
    if (!pixels)
        return NULL;
    if (!rt_obj_is_instance(pixels, RT_PIXELS_CLASS_ID, sizeof(rt_pixels_impl)))
        return NULL;
    int64_t pixels_width = rt_pixels_width(pixels);
    int64_t pixels_height = rt_pixels_height(pixels);
    rt2d_retain_ref(pixels);
    rt_nineslice2d_impl *slice = (rt_nineslice2d_impl *)rt_obj_new_i64(
        RT2D_NINESLICE_CLASS_ID, (int64_t)sizeof(rt_nineslice2d_impl));
    if (!slice) {
        void *owned_pixels = pixels;
        rt2d_release_ref_slot(&owned_pixels);
        return NULL;
    }
    slice->pixels = pixels;
    slice->left = clamp_i64(left, 0, pixels_width);
    slice->top = clamp_i64(top, 0, pixels_height);
    slice->right = clamp_i64(right, 0, pixels_width);
    slice->bottom = clamp_i64(bottom, 0, pixels_height);
    rt_obj_set_finalizer(slice, nineslice2d_finalize);
    return slice;
}

/// @brief Copy one rectangular region from `source` into `target`, scaling as
///        needed. Used by the nine-slice draw to place each of the 9 sub-rects.
/// @details Fast path when source and destination sizes match — a direct
///          `rt2d_blit_pixels` with no intermediate allocation. When they don't,
///          allocates a temporary region copy (via `rt2d_copy_region_pixels`),
///          scales it to the destination dimensions (`rt_pixels_scale`), and
///          blits the scaled result. Both temporaries are released before
///          returning. No-op if any dimension is non-positive.
static void nineslice_copy_scaled(void *target,
                                  int64_t dx,
                                  int64_t dy,
                                  int64_t dw,
                                  int64_t dh,
                                  void *source,
                                  int64_t sx,
                                  int64_t sy,
                                  int64_t sw,
                                  int64_t sh) {
    if (!target || !source || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;
    if (dw == sw && dh == sh) {
        rt2d_blit_pixels(target, dx, dy, source, sx, sy, sw, sh, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
        return;
    }
    void *region = rt2d_copy_region_pixels(source, sx, sy, sw, sh);
    if (!region)
        return;
    void *scaled = rt_pixels_scale(region, dw, dh);
    if (scaled)
        rt2d_blit_pixels(target, dx, dy, scaled, 0, 0, dw, dh, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
    rt2d_release_ref_slot(&scaled);
    rt2d_release_ref_slot(&region);
}

/// @brief Render the nine-slice into `target` at position `(x, y)`, stretched to
///        `width × height`.
/// @details The core layout computes two rectangle sets:
///          - **Source:** `sl`, `sr`, `st`, `sb` are the border widths, clamped
///            so they can never overlap (right is clamped to `width - left`).
///            `scw` / `sch` are the remaining center dimensions.
///          - **Destination:** `dl`, `dr`, `dt`, `db` use the same border
///            widths but clamped against the destination dimensions, so a
///            nine-slice drawn smaller than its source still produces
///            sensible (shrunken) borders. `dcw` / `dch` are the stretched
///            center dimensions.
///
///          Then nine `nineslice_copy_scaled` calls place the nine sub-rects
///          in row-major order: top-left / top-center / top-right,
///          middle-left / middle-center / middle-right, bottom-left /
///          bottom-center / bottom-right. The four corners always copy at
///          native size; the four edges each stretch along one axis; the
///          center stretches on both.
void rt_nineslice2d_draw_to_pixels(
    void *slice, void *target, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!rt2d_has_class(slice, RT2D_NINESLICE_CLASS_ID) || !target || width <= 0 || height <= 0)
        return;
    rt_nineslice2d_impl *impl = (rt_nineslice2d_impl *)slice;
    int64_t source_width = rt_pixels_width(impl->pixels);
    int64_t source_height = rt_pixels_height(impl->pixels);
    int64_t sl = clamp_i64(impl->left, 0, source_width);
    int64_t sr = clamp_i64(impl->right, 0, source_width - sl);
    int64_t st = clamp_i64(impl->top, 0, source_height);
    int64_t sb = clamp_i64(impl->bottom, 0, source_height - st);
    int64_t dl = clamp_i64(sl, 0, width);
    int64_t dr = clamp_i64(sr, 0, width - dl);
    int64_t dt = clamp_i64(st, 0, height);
    int64_t db = clamp_i64(sb, 0, height - dt);
    int64_t scw = source_width - sl - sr;
    int64_t sch = source_height - st - sb;
    int64_t dcw = width - dl - dr;
    int64_t dch = height - dt - db;

    int64_t x_dl = rt2d_saturating_add_i64(x, dl);
    int64_t x_dl_dcw = rt2d_saturating_add_i64(x_dl, dcw);
    int64_t y_dt = rt2d_saturating_add_i64(y, dt);
    int64_t y_dt_dch = rt2d_saturating_add_i64(y_dt, dch);

    nineslice_copy_scaled(target, x, y, dl, dt, impl->pixels, 0, 0, sl, st);
    nineslice_copy_scaled(target, x_dl, y, dcw, dt, impl->pixels, sl, 0, scw, st);
    nineslice_copy_scaled(target, x_dl_dcw, y, dr, dt, impl->pixels, sl + scw, 0, sr, st);

    nineslice_copy_scaled(target, x, y_dt, dl, dch, impl->pixels, 0, st, sl, sch);
    nineslice_copy_scaled(target, x_dl, y_dt, dcw, dch, impl->pixels, sl, st, scw, sch);
    nineslice_copy_scaled(target, x_dl_dcw, y_dt, dr, dch, impl->pixels, sl + scw, st, sr, sch);

    nineslice_copy_scaled(target, x, y_dt_dch, dl, db, impl->pixels, 0, st + sch, sl, sb);
    nineslice_copy_scaled(target, x_dl, y_dt_dch, dcw, db, impl->pixels, sl, st + sch, scw, sb);
    nineslice_copy_scaled(
        target, x_dl_dcw, y_dt_dch, dr, db, impl->pixels, sl + scw, st + sch, sr, sb);
}

//=============================================================================
// DebugDraw2D
//=============================================================================
// Retained queue of debug line / rect / circle primitives. Typical usage:
// gameplay code accumulates shapes during the logic update, the renderer
// flushes them all at the end of the frame. `Clear` resets the queue;
// queueing after clear starts fresh without any retained allocations.

/// @brief GC finalizer — frees the command buffer.
static void debugdraw2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_DEBUGDRAW_CLASS_ID))
        return;
    rt_debugdraw2d_impl *debug_draw = (rt_debugdraw2d_impl *)obj;
    free(debug_draw->cmds);
}

/// @brief Allocate a DebugDraw2D with the given initial command-buffer capacity.
/// @details Capacity is clamped by `rt2d_initial_capacity` (floor 16, ceiling 1Mi).
///          Returns NULL on allocation failure.
void *rt_debugdraw2d_new(int64_t capacity) {
    rt_debugdraw2d_impl *debug_draw = (rt_debugdraw2d_impl *)rt_obj_new_i64(
        RT2D_DEBUGDRAW_CLASS_ID, (int64_t)sizeof(rt_debugdraw2d_impl));
    if (!debug_draw)
        return NULL;
    debug_draw->capacity = rt2d_initial_capacity(capacity);
    debug_draw->cmds =
        (rt_debugdraw2d_cmd *)calloc((size_t)debug_draw->capacity, sizeof(*debug_draw->cmds));
    if (!debug_draw->cmds) {
        if (rt_obj_release_check0(debug_draw))
            rt_obj_free(debug_draw);
        return NULL;
    }
    rt_obj_set_finalizer(debug_draw, debugdraw2d_finalize);
    return debug_draw;
}

/// @brief Discard all queued commands without freeing the backing buffer.
void rt_debugdraw2d_clear(void *debug_draw) {
    if (rt2d_has_class(debug_draw, RT2D_DEBUGDRAW_CLASS_ID))
        ((rt_debugdraw2d_impl *)debug_draw)->count = 0;
}

/// @brief Return the number of commands currently queued.
int64_t rt_debugdraw2d_count(void *debug_draw) {
    return rt2d_has_class(debug_draw, RT2D_DEBUGDRAW_CLASS_ID)
               ? ((rt_debugdraw2d_impl *)debug_draw)->count
               : 0;
}

/// @brief Ensure the debug-draw command buffer has capacity for at least @p needed entries.
/// @details Grows geometrically from RT2D_INITIAL_CAP, doubling until capacity ≥ needed.
///          Guards against integer overflow when computing the byte size for realloc.
/// @return 1 on success; 0 on OOM or overflow.
static int32_t debugdraw2d_reserve(rt_debugdraw2d_impl *debug_draw, int64_t needed) {
    if (!debug_draw || needed <= debug_draw->capacity)
        return 1;
    int64_t cap = debug_draw->capacity > 0 ? debug_draw->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_debugdraw2d_cmd))
        return 0;
    rt_debugdraw2d_cmd *cmds =
        (rt_debugdraw2d_cmd *)realloc(debug_draw->cmds, (size_t)cap * sizeof(*cmds));
    if (!cmds)
        return 0;
    memset(cmds + debug_draw->capacity, 0, (size_t)(cap - debug_draw->capacity) * sizeof(*cmds));
    debug_draw->cmds = cmds;
    debug_draw->capacity = cap;
    return 1;
}

/// @brief Append a single typed debug-draw command (line, circle, rect, text, etc.) to the buffer.
/// @details Calls debugdraw2d_reserve to grow if needed; traps on overflow. The @p type field
///          selects the shape; @p value and @p rgba carry shape-specific extra data (e.g.,
///          radius or packed color).
static void debugdraw2d_add(rt_debugdraw2d_impl *debug_draw,
                            int32_t type,
                            int64_t x0,
                            int64_t y0,
                            int64_t x1,
                            int64_t y1,
                            int64_t value,
                            int64_t rgba) {
    if (!debug_draw)
        return;
    if (!debugdraw2d_reserve(debug_draw, debug_draw->count + 1)) {
        rt_trap("DebugDraw2D: capacity overflow");
        return;
    }
    rt_debugdraw2d_cmd *cmd = &debug_draw->cmds[debug_draw->count++];
    cmd->type = type;
    cmd->x0 = x0;
    cmd->y0 = y0;
    cmd->x1 = x1;
    cmd->y1 = y1;
    cmd->value = value;
    cmd->color = rgba;
}

/// @brief Queue a line from `(x0, y0)` to `(x1, y1)` with `rgba` color (type=1).
void rt_debugdraw2d_line(
    void *debug_draw, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t rgba) {
    rt_debugdraw2d_impl *impl = rt2d_has_class(debug_draw, RT2D_DEBUGDRAW_CLASS_ID)
                                    ? (rt_debugdraw2d_impl *)debug_draw
                                    : NULL;
    debugdraw2d_add(impl, 1, x0, y0, x1, y1, 0, rgba);
}

/// @brief Queue a rectangle outline at `(x, y)` with `width × height` and `rgba` color (type=2).
void rt_debugdraw2d_rect(
    void *debug_draw, int64_t x, int64_t y, int64_t width, int64_t height, int64_t rgba) {
    rt_debugdraw2d_impl *impl = rt2d_has_class(debug_draw, RT2D_DEBUGDRAW_CLASS_ID)
                                    ? (rt_debugdraw2d_impl *)debug_draw
                                    : NULL;
    debugdraw2d_add(impl, 2, x, y, width, height, 0, rgba);
}

/// @brief Queue a circle outline at `(x, y)` with the given `radius` and `rgba` color (type=3).
void rt_debugdraw2d_circle(void *debug_draw, int64_t x, int64_t y, int64_t radius, int64_t rgba) {
    rt_debugdraw2d_impl *impl = rt2d_has_class(debug_draw, RT2D_DEBUGDRAW_CLASS_ID)
                                    ? (rt_debugdraw2d_impl *)debug_draw
                                    : NULL;
    debugdraw2d_add(impl, 3, x, y, 0, 0, radius, rgba);
}

/// @brief Flush all queued commands into `pixels`.
/// @details Iterates the command list and dispatches to the appropriate
///          `rt_pixels_draw_*` primitive by command type:
///          - type 1 → `rt_pixels_draw_line`
///          - type 2 → `rt_pixels_draw_frame` (outline rect)
///          - type 3 → `rt_pixels_draw_ring` (outline circle)
///          Unknown types are silently skipped.
void rt_debugdraw2d_draw_to_pixels(void *debug_draw, void *pixels) {
    rt_debugdraw2d_impl *impl = rt2d_has_class(debug_draw, RT2D_DEBUGDRAW_CLASS_ID)
                                    ? (rt_debugdraw2d_impl *)debug_draw
                                    : NULL;
    if (!impl || !pixels)
        return;
    for (int64_t i = 0; i < impl->count; i++) {
        rt_debugdraw2d_cmd *cmd = &impl->cmds[i];
        int64_t color = rt2d_draw_rgb(cmd->color);
        if (cmd->type == 1)
            rt_pixels_draw_line(pixels, cmd->x0, cmd->y0, cmd->x1, cmd->y1, color);
        else if (cmd->type == 2)
            rt_pixels_draw_frame(pixels, cmd->x0, cmd->y0, cmd->x1, cmd->y1, color);
        else if (cmd->type == 3)
            rt_pixels_draw_ring(pixels, cmd->x0, cmd->y0, cmd->value, color);
    }
}

//=============================================================================
