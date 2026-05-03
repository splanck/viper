//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_graphics2d.c
// Purpose: Viper.Graphics 2D support runtime — CPU-backed implementations of
//   RenderTarget2D / Surface2D, Texture2D / GpuTexture2D, Renderer2D,
//   Material2D, Shader2D / PostProcess2D, Viewport2D / ScreenScaler,
//   TileSet2D / TileLayer2D / ObjectLayer2D / AutoTile2D, Path2D /
//   ShapeRenderer2D, TextRenderer2D / SdfFont, NineSlice2D, and DebugDraw2D.
// Key invariants:
//   - All drawing ultimately lowers to `rt_pixels_*` primitives; this file
//     owns no GPU resources. The surface is shaped so a GPU backend can be
//     swapped in later without changing callers.
//   - Pixels storage is `0xRRGGBBAA`; shape and debug-draw colors accept
//     Canvas-style `0x00RRGGBB` and Color.RGBA-style `0xAARRGGBB`.
//   - Every object holds retained references via `retain_ref` /
//     `release_ref_slot` — replacing a slot always retains the new value
//     before releasing the old so self-assignment can't drop refcount to 0.
//   - Viewport2D.Scale is fixed-point with `1000 == 1.0x`.
// Ownership/Lifetime:
//   - Runtime-allocated impl structs are freed by GC finalizers registered
//     at construction; every `_new` variant pairs with a `_finalize` that
//     releases owned pixels / font / cmds allocations.
//   - Renderer2D / DebugDraw2D command buffers own their `cmds` arrays but
//     the source/target pixels they reference are retained externally.
// Links: rt_graphics2d.h, rt_pixels.c, rt_canvas.c.
//
//===----------------------------------------------------------------------===//

#include "rt_graphics2d.h"

#include "rt_bitmapfont.h"
#include "rt_camera.h"
#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_sprite.h"
#include "rt_texatlas.h"
#include "rt_tilemap.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT2D_MAX_DIM 1000000LL
#define RT2D_MAX_ELEMENTS 268435456LL
#define RT2D_INITIAL_CAP 16
#define RT2D_PI 3.14159265358979323846
#define RT2D_RENDERTARGET_MAGIC 0x525432445247544cLL /* "RT2DRGTL" */
#define RT2D_TEXTURE_MAGIC 0x525432445445584cLL      /* "RT2DTEXL" */
#define RT2D_RENDERER_MAGIC 0x52543244524e444cLL     /* "RT2DRNDL" */

typedef struct {
    int64_t magic;
    void *pixels;
} rt_rendertarget2d_impl;

typedef struct {
    int64_t magic;
    void *pixels;
    int64_t filter;
    int64_t wrap;
} rt_texture2d_impl;

typedef struct {
    void *source;
    int32_t source_kind;
    int64_t x;
    int64_t y;
    int64_t sx;
    int64_t sy;
    int64_t width;
    int64_t height;
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
    int64_t sampler_filter;
    int64_t sampler_wrap;
} rt_renderer2d_cmd;

typedef struct {
    int64_t magic;
    rt_renderer2d_cmd *cmds;
    int64_t count;
    int64_t capacity;
    int64_t active;
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
} rt_renderer2d_impl;

typedef struct {
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
} rt_material2d_impl;

typedef struct {
    int64_t effect;
    int64_t amount;
    int64_t color;
} rt_shader2d_impl;

typedef rt_shader2d_impl rt_postprocess2d_impl;

typedef struct {
    int64_t virtual_width;
    int64_t virtual_height;
    int64_t screen_width;
    int64_t screen_height;
    int64_t integer_scaling;
    int64_t scale;
    int64_t offset_x;
    int64_t offset_y;
} rt_viewport2d_impl;

typedef struct {
    void *pixels;
    int64_t tile_width;
    int64_t tile_height;
} rt_tileset2d_impl;

typedef struct {
    int64_t width;
    int64_t height;
    int64_t visible;
    int64_t opacity;
    int64_t *tiles;
} rt_tilelayer2d_impl;

typedef struct {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    int64_t type;
} rt_objectlayer2d_entry;

typedef struct {
    rt_objectlayer2d_entry *items;
    int64_t count;
    int64_t capacity;
} rt_objectlayer2d_impl;

typedef struct {
    int64_t variants[16];
} rt_autotile2d_impl;

typedef struct {
    int64_t x;
    int64_t y;
    int32_t move;
} rt_path2d_point;

typedef struct {
    rt_path2d_point *points;
    int64_t count;
    int64_t capacity;
} rt_path2d_impl;

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
// Internal helpers — shared by every class in this file
//=============================================================================

/// @brief Clamp an int64 to `[min, max]`.
static int64_t clamp_i64(int64_t value, int64_t min, int64_t max) {
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

/// @brief Clamp to the unsigned-byte range `[0, 255]`. Used for RGBA channel math
///        that passes through int64 intermediates to avoid overflow on multiplies.
static int64_t clamp_u8_i64(int64_t value) {
    return clamp_i64(value, 0, 255);
}

/// @brief Saturating integer addition: returns INT64_MAX / INT64_MIN instead of wrapping.
static int64_t saturating_add_i64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Saturating integer multiplication using `long double` to detect overflow before truncation.
static int64_t saturating_mul_i64(int64_t a, int64_t b) {
    long double value = (long double)a * (long double)b;
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)value;
}

/// @brief Validate and cast a `void *` to `rt_rendertarget2d_impl *` by checking the magic cookie.
/// @details Returns NULL for NULL input or handles whose magic doesn't match, preventing
///   type-confused pointer dereferences from badly-typed Zia/BASIC caller code.
static rt_rendertarget2d_impl *rendertarget2d_checked(void *target) {
    if (!target)
        return NULL;
    rt_rendertarget2d_impl *impl = (rt_rendertarget2d_impl *)target;
    return impl->magic == RT2D_RENDERTARGET_MAGIC ? impl : NULL;
}

/// @brief Validate and cast a `void *` to `rt_texture2d_impl *` by checking the magic cookie.
static rt_texture2d_impl *texture2d_checked(void *texture) {
    if (!texture)
        return NULL;
    rt_texture2d_impl *impl = (rt_texture2d_impl *)texture;
    return impl->magic == RT2D_TEXTURE_MAGIC ? impl : NULL;
}

/// @brief Validate and cast a `void *` to `rt_renderer2d_impl *` by checking the magic cookie.
static rt_renderer2d_impl *renderer2d_checked(void *renderer) {
    if (!renderer)
        return NULL;
    rt_renderer2d_impl *impl = (rt_renderer2d_impl *)renderer;
    return impl->magic == RT2D_RENDERER_MAGIC ? impl : NULL;
}

/// @brief Test whether @p value falls in the half-open interval `[start, start+length)`.
/// @details Uses an unsigned subtraction trick to collapse the "negative start" and
///   "value < start" cases into a single bounds check without signed-overflow UB.
static int32_t point_in_interval_i64(int64_t start, int64_t length, int64_t value) {
    if (length <= 0 || value < start)
        return 0;
    return ((uint64_t)value - (uint64_t)start) < (uint64_t)length;
}

/// @brief Test whether two intervals `[a_start, a_start+a_length)` and `[b_start, b_start+b_length)` overlap.
/// @details Implemented as "does either interval contain the other's start?" — one containment implies
///   overlap in both directions. Zero-length intervals never overlap.
static int32_t intervals_overlap_i64(int64_t a_start,
                                     int64_t a_length,
                                     int64_t b_start,
                                     int64_t b_length) {
    if (a_length <= 0 || b_length <= 0)
        return 0;
    return point_in_interval_i64(a_start, a_length, b_start) ||
           point_in_interval_i64(b_start, b_length, a_start);
}

/// @brief Return the unsigned distance from @p value to zero (i.e. `abs(value)` as uint64).
/// @details The expression `(uint64_t)(-(value+1))+1u` correctly handles INT64_MIN without
///   signed-overflow UB: for INT64_MIN, `-(value+1) = INT64_MAX` (all-ones), so the uint64
///   result is `INT64_MAX + 1 = 2^63`, matching the true absolute value.
static uint64_t distance_to_zero_i64(int64_t value) {
    return (uint64_t)(-(value + 1)) + 1u;
}

/// @brief Clip a blit operation along one axis, adjusting destination, source, and length in place.
/// @details Handles the four cases where the destination or source position is negative or runs past
///   the limit: computes the largest offset `skip` that makes both positions non-negative, advances
///   both positions by `skip`, reduces `length` accordingly, then clips `length` to the remaining
///   space in both buffers. Sets `*length = 0` and returns 0 on any out-of-bounds condition. Called
///   twice per blit (once for X, once for Y) so the blit loop never has to range-check coordinates.
static int32_t blit_clip_axis(
    int64_t *dst_pos, int64_t *src_pos, int64_t *length, int64_t dst_limit, int64_t src_limit) {
    if (!dst_pos || !src_pos || !length || *length <= 0 || dst_limit <= 0 || src_limit <= 0) {
        if (length)
            *length = 0;
        return 0;
    }
    if ((*dst_pos >= 0 && *dst_pos >= dst_limit) || (*src_pos >= 0 && *src_pos >= src_limit)) {
        *length = 0;
        return 0;
    }

    uint64_t skip = 0;
    if (*dst_pos < 0)
        skip = distance_to_zero_i64(*dst_pos);
    if (*src_pos < 0) {
        uint64_t src_skip = distance_to_zero_i64(*src_pos);
        if (src_skip > skip)
            skip = src_skip;
    }
    if (skip > 0) {
        if (skip >= (uint64_t)*length) {
            *length = 0;
            return 0;
        }
        if (*dst_pos >= 0 && skip >= (uint64_t)(dst_limit - *dst_pos)) {
            *length = 0;
            return 0;
        }
        if (*src_pos >= 0 && skip >= (uint64_t)(src_limit - *src_pos)) {
            *length = 0;
            return 0;
        }
        int64_t offset = (int64_t)skip;
        *dst_pos += offset;
        *src_pos += offset;
        *length -= offset;
    }
    if (*dst_pos < 0 || *src_pos < 0 || *dst_pos >= dst_limit || *src_pos >= src_limit) {
        *length = 0;
        return 0;
    }

    int64_t dst_remaining = dst_limit - *dst_pos;
    int64_t src_remaining = src_limit - *src_pos;
    if (*length > dst_remaining)
        *length = dst_remaining;
    if (*length > src_remaining)
        *length = src_remaining;
    return *length > 0;
}

/// @brief Round a `long double` to the nearest `int64_t`, saturating at INT64_MIN/INT64_MAX.
/// @details Used for viewport coordinate transforms where the intermediate result must
///   be in `long double` to avoid losing precision, but must ultimately become an integer
///   pixel coordinate. Non-finite values are clamped to the appropriate extreme.
static int64_t round_long_double_to_i64(long double value) {
    if (!isfinite((double)value))
        return value < 0.0L ? INT64_MIN : INT64_MAX;
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)(value >= 0.0L ? value + 0.5L : value - 0.5L);
}

/// @brief Clamp a dimension to `[1, RT2D_MAX_DIM]`. Zero / negative requests become
///        1 so nothing downstream has to special-case an empty row or column.
static int64_t normalized_dim(int64_t value) {
    return clamp_i64(value, 1, RT2D_MAX_DIM);
}

/// @brief Validate and compute `width * height` for buffer allocation, guarding
///        against integer overflow and hard dimension limits.
/// @details Performs three separate overflow checks in sequence:
///            1. `width > RT2D_MAX_DIM || height > RT2D_MAX_DIM` — reject pathological
///               dimensions before even multiplying.
///            2. `width > INT64_MAX / height` — catch the `width * height` overflow.
///            3. `count > INT64_MAX / elem_size` — catch the byte-count overflow.
///          The result is also bounded by `RT2D_MAX_ELEMENTS` (currently 256Mi) so
///          the allocator never sees a request larger than the design ceiling even
///          when the math doesn't overflow. Returns 0 on any failure so the caller
///          fails the allocation cleanly rather than truncating silently.
/// @param width Requested width in pixels / cells.
/// @param height Requested height in pixels / cells.
/// @param elem_size Bytes per element (e.g. `sizeof(uint32_t)` for RGBA32).
/// @param out_count Optional output — receives `width * height` on success.
/// @return 1 on success, 0 on invalid dimensions or overflow.
static int32_t checked_count(int64_t width, int64_t height, int64_t elem_size, int64_t *out_count) {
    if (width <= 0 || height <= 0 || elem_size <= 0)
        return 0;
    if (width > RT2D_MAX_DIM || height > RT2D_MAX_DIM)
        return 0;
    if (width > INT64_MAX / height)
        return 0;
    int64_t count = width * height;
    if (count > RT2D_MAX_ELEMENTS)
        return 0;
    if (count > INT64_MAX / elem_size)
        return 0;
    if (out_count)
        *out_count = count;
    return 1;
}

/// @brief Pick an initial backing-array capacity for a dynamic list.
/// @details Floor 16 (`RT2D_INITIAL_CAP`) so a single-element list doesn't
///          thrash through tiny reallocs; ceiling 1Mi entries to cap a pathological
///          "reserve everything up front" caller. Negative / zero requests default
///          to the floor.
static int64_t initial_capacity(int64_t requested) {
    if (requested <= 0)
        return RT2D_INITIAL_CAP;
    if (requested > 1048576)
        return 1048576;
    return requested;
}

/// @brief Retain a heap-payload object slot, no-op for non-payload handles.
/// @details Object handles in this runtime can be either real GC-managed payloads
///          or opaque handles (constants, interned values) that don't need refcount
///          maintenance. `rt_heap_is_payload` distinguishes the two; this wrapper
///          means callers don't have to probe the heap bit themselves.
static void retain_ref(void *obj) {
    if (obj && rt_heap_is_payload(obj))
        rt_obj_retain_maybe(obj);
}

/// @brief Release one reference through a slot and zero the slot atomically.
/// @details Used by every `_finalize` and every "replace slot" setter in this file.
///          The slot is cleared *before* the potential free so observers can't see
///          a dangling pointer mid-release. Non-payload handles are skipped just
///          like `retain_ref`.
static void release_ref_slot(void **slot) {
    if (!slot || !*slot)
        return;
    void *obj = *slot;
    *slot = NULL;
    if (rt_heap_is_payload(obj) && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Normalize a user-supplied color to `0x00RRGGBB` form.
/// @details Shape/debug APIs take Canvas-style `0x00RRGGBB` or Color.RGBA-style
///          `0xAARRGGBB`. Alpha is ignored by these RGB-only drawing primitives.
static int64_t draw_rgb(int64_t color) {
    uint64_t c = (uint64_t)color & 0xFFFFFFFFu;
    if (c <= 0x00FFFFFFu)
        return (int64_t)c;
    return (int64_t)(c & 0x00FFFFFFu);
}

/// @brief Apply a tint multiplier and alpha scale to a single `0xRRGGBBAA` texel.
/// @details Tint is a `0x00RRGGBB` multiplicative color (each channel ∈ [0,1]
///          scaled to [0,255]); alpha is a scalar in [0,255]. A negative tint
///          skips the multiply — used by callers that want pure alpha scaling
///          without touching color. Integer-math rounding uses the standard
///          `(a * b + 127) / 255` bias so channel products match the "divide by
///          255" behaviour you'd expect from floating-point.
static uint32_t apply_tint_alpha(uint32_t rgba, int64_t tint, int64_t alpha) {
    int64_t r = (rgba >> 24) & 255;
    int64_t g = (rgba >> 16) & 255;
    int64_t b = (rgba >> 8) & 255;
    int64_t a = rgba & 255;
    if (tint >= 0) {
        int64_t tr = (tint >> 16) & 255;
        int64_t tg = (tint >> 8) & 255;
        int64_t tb = tint & 255;
        r = (r * tr + 127) / 255;
        g = (g * tg + 127) / 255;
        b = (b * tb + 127) / 255;
    }
    a = (a * clamp_u8_i64(alpha) + 127) / 255;
    return (uint32_t)((r << 24) | (g << 16) | (b << 8) | a);
}

/// @brief Composite a source texel over a destination texel using the given blend
///        mode, returning the final `0xRRGGBBAA` result.
/// @details Three modes:
///          - **OPAQUE** (or `src.a == 255`): source replaces destination wholesale,
///            so the fully-opaque fast path skips the per-channel math entirely.
///          - **ADD** (additive): source is scaled by its own alpha, then added to
///            destination with per-channel clamp at 255. Destination alpha rises
///            using the standard over-style `a + sa - a*sa/255` formula so adding
///            onto an existing alpha surface doesn't reset it.
///          - **ALPHA** (default): straight-alpha Porter-Duff source-over.
///          All multiplies use the `(a * b + 127) / 255` rounding bias so results
///          match float-reference output within 1 ULP.
static uint32_t blend_pixel(uint32_t dst, uint32_t src, int64_t blend_mode) {
    int64_t sr = (src >> 24) & 255;
    int64_t sg = (src >> 16) & 255;
    int64_t sb = (src >> 8) & 255;
    int64_t sa = src & 255;
    int64_t dr = (dst >> 24) & 255;
    int64_t dg = (dst >> 16) & 255;
    int64_t db = (dst >> 8) & 255;
    int64_t da = dst & 255;

    if (blend_mode == RT_GRAPHICS2D_BLEND_OPAQUE)
        return src;

    if (blend_mode == RT_GRAPHICS2D_BLEND_ADD) {
        int64_t r = clamp_u8_i64(dr + (sr * sa + 127) / 255);
        int64_t g = clamp_u8_i64(dg + (sg * sa + 127) / 255);
        int64_t b = clamp_u8_i64(db + (sb * sa + 127) / 255);
        int64_t a = clamp_u8_i64(da + sa - (da * sa + 127) / 255);
        return (uint32_t)((r << 24) | (g << 16) | (b << 8) | a);
    }

    if (sa >= 255)
        return src;

    return rt_pixels_alpha_over_rgba(dst, src);
}

/// @brief Copy a region from `src` to `dst` with tint + alpha + blend mode, clipping
///        both source and destination to their respective bounds.
/// @details The workhorse behind every texture / sprite / nine-slice draw in this
///          file. Clipping is done up front in four passes (negative source x/y,
///          negative dest x/y, over-right source, over-right dest) — each pass
///          trims the copy rectangle AND compensates the opposite rectangle so the
///          remaining area stays aligned. Fully-transparent source pixels
///          (`alpha == 0` after tint+alpha application) are skipped entirely to
///          avoid a wasted `rt_pixels_set` round-trip.
///
///          Does NOT validate that tint / alpha / blend_mode are in their defined
///          enum ranges — clamping happens upstream in the public API setters, not
///          here. Keeps the inner loop branch-light.
/// @param dst Destination Pixels (written in place).
/// @param dx,dy Destination top-left in pixels.
/// @param src Source Pixels (read-only).
/// @param sx,sy Source top-left in pixels (may be negative; will be clipped).
/// @param width,height Copy size in pixels (may overflow bounds; will be clipped).
/// @param tint 0x00RRGGBB multiplicative tint, or negative to skip tinting.
/// @param alpha Alpha scale in [0, 255].
/// @param blend_mode One of `RT_GRAPHICS2D_BLEND_*`.
static void blit_pixels(void *dst,
                        int64_t dx,
                        int64_t dy,
                        void *src,
                        int64_t sx,
                        int64_t sy,
                        int64_t width,
                        int64_t height,
                        int64_t tint,
                        int64_t alpha,
                        int64_t blend_mode) {
    if (!dst || !src || width <= 0 || height <= 0)
        return;

    int64_t dst_width = rt_pixels_width(dst);
    int64_t dst_height = rt_pixels_height(dst);
    int64_t src_width = rt_pixels_width(src);
    int64_t src_height = rt_pixels_height(src);

    if (!blit_clip_axis(&dx, &sx, &width, dst_width, src_width) ||
        !blit_clip_axis(&dy, &sy, &height, dst_height, src_height))
        return;

    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t source = (uint32_t)rt_pixels_get(src, sx + x, sy + y);
            source = apply_tint_alpha(source, tint, alpha);
            if ((source & 255u) == 0)
                continue;
            uint32_t dest = (uint32_t)rt_pixels_get(dst, dx + x, dy + y);
            rt_pixels_set(dst, dx + x, dy + y, (int64_t)blend_pixel(dest, source, blend_mode));
        }
    }
}

/// @brief Tint + alpha-scale every pixel in a buffer in place (no blend, no clipping).
/// @details Used by `Material2D.Apply` and similar "pre-process this Pixels buffer"
///          entry points where the caller wants the tinted copy as a standalone
///          `Pixels` rather than as the source for a blit. Walks the full width×height
///          grid applying `apply_tint_alpha` to each texel; the source alpha is
///          respected so fully-transparent texels stay transparent.
static void apply_tint_alpha_in_place(void *pixels, int64_t tint, int64_t alpha) {
    if (!pixels)
        return;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t color = (uint32_t)rt_pixels_get(pixels, x, y);
            rt_pixels_set(pixels, x, y, (int64_t)apply_tint_alpha(color, tint, alpha));
        }
    }
}

/// @brief Extract a rectangular sub-region of @p pixels into a new Pixels buffer.
/// @details Allocates a `width × height` Pixels and copies from `(sx, sy)` in the source.
///   The source coordinates are NOT clipped — callers that may have out-of-bounds regions
///   should call `clip_region_to_source` first. Returns NULL if allocation fails or inputs
///   are invalid.
static void *copy_region_pixels(
    void *pixels, int64_t sx, int64_t sy, int64_t width, int64_t height) {
    if (!pixels || width <= 0 || height <= 0)
        return NULL;
    void *out = rt_pixels_new(width, height);
    if (!out)
        return NULL;
    rt_pixels_copy(out, 0, 0, pixels, sx, sy, width, height);
    return out;
}

/// @brief Clip a source sub-rectangle to the actual Pixels bounds, adjusting destination offset.
/// @details When the source rectangle extends past the left or top edge (negative sx/sy), the
///   overlap is clipped by advancing the destination offset and shrinking the rectangle.
///   When the rectangle runs past the right or bottom edge of the source, width/height are
///   trimmed. Updates all six in-out parameters and returns 1 if a non-empty intersection
///   remains, 0 if the rectangle is fully out of bounds.
static int8_t clip_region_to_source(
    void *pixels, int64_t *dx, int64_t *dy, int64_t *sx, int64_t *sy, int64_t *w, int64_t *h) {
    if (!pixels || !dx || !dy || !sx || !sy || !w || !h || *w <= 0 || *h <= 0)
        return 0;

    int64_t src_w = rt_pixels_width(pixels);
    int64_t src_h = rt_pixels_height(pixels);
    if (src_w <= 0 || src_h <= 0)
        return 0;

    if (*sx < 0) {
        int64_t skip = *sx == INT64_MIN ? *w : -*sx;
        if (skip >= *w)
            return 0;
        if (*dx > INT64_MAX - skip)
            return 0;
        *dx += skip;
        *sx = 0;
        *w -= skip;
    }
    if (*sy < 0) {
        int64_t skip = *sy == INT64_MIN ? *h : -*sy;
        if (skip >= *h)
            return 0;
        if (*dy > INT64_MAX - skip)
            return 0;
        *dy += skip;
        *sy = 0;
        *h -= skip;
    }

    if (*sx >= src_w || *sy >= src_h)
        return 0;
    int64_t src_remaining_w = src_w - *sx;
    int64_t src_remaining_h = src_h - *sy;
    if (*w > src_remaining_w)
        *w = src_remaining_w;
    if (*h > src_remaining_h)
        *h = src_remaining_h;
    return *w > 0 && *h > 0;
}

/// @brief Clone @p pixels and apply tint+alpha in place; returns NULL if no processing is needed.
/// @details The "no-op" condition (tint < 0 and alpha == 255) is checked first to avoid
///   an unnecessary clone when neither tint nor alpha modification is active. Callers use the
///   NULL return to avoid the clone overhead in the common "unmodified" case — if non-NULL is
///   returned the caller owns the clone and must release it.
static void *processed_pixels_or_null(void *pixels, int64_t tint, int64_t alpha) {
    if (!pixels || (tint < 0 && clamp_u8_i64(alpha) == 255))
        return NULL;
    void *out = rt_pixels_clone(pixels);
    if (out)
        apply_tint_alpha_in_place(out, tint, alpha);
    return out;
}

//=============================================================================
// RenderTarget2D (aliased as Surface2D)
//=============================================================================
// Offscreen RGBA surface. Wraps a `Pixels` buffer and exposes alpha-aware
// DrawPixels / DrawRegion that route through the shared `blit_pixels` blitter.

/// @brief GC finalizer — releases the owned Pixels buffer.
static void rendertarget2d_finalize(void *obj) {
    rt_rendertarget2d_impl *target = (rt_rendertarget2d_impl *)obj;
    target->magic = 0;
    release_ref_slot(&target->pixels);
}

/// @brief Allocate a new offscreen render target sized `width × height` (RGBA).
/// @details Validates the dimensions through `checked_count` (caps at RT2D_MAX_DIM
///          and rejects overflowing byte counts) and traps on invalid inputs — the
///          caller gets a clear error instead of a silent NULL return for a bad
///          size. The owned `Pixels` buffer is allocated separately; if that
///          allocation fails, the partially-constructed impl is released cleanly.
/// @return RenderTarget2D handle, or NULL on allocation failure (trap already
///         fired for invalid dimensions).
void *rt_rendertarget2d_new(int64_t width, int64_t height) {
    if (!checked_count(width, height, 4, NULL)) {
        rt_trap("RenderTarget2D.New: invalid dimensions");
        return NULL;
    }
    rt_rendertarget2d_impl *target =
        (rt_rendertarget2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_rendertarget2d_impl));
    if (!target)
        return NULL;
    target->magic = RT2D_RENDERTARGET_MAGIC;
    target->pixels = rt_pixels_new(width, height);
    if (!target->pixels) {
        if (rt_obj_release_check0(target))
            rt_obj_free(target);
        return NULL;
    }
    rt_obj_set_finalizer(target, rendertarget2d_finalize);
    return target;
}

/// @brief Return the width of the render target's backing Pixels buffer in pixels, or 0 if invalid.
int64_t rt_rendertarget2d_width(void *target) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    return impl ? rt_pixels_width(impl->pixels) : 0;
}

/// @brief Return the height of the render target's backing Pixels buffer in pixels, or 0 if invalid.
int64_t rt_rendertarget2d_height(void *target) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    return impl ? rt_pixels_height(impl->pixels) : 0;
}

/// @brief Return the backing Pixels buffer (borrowed reference — do not release).
void *rt_rendertarget2d_get_pixels(void *target) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    return impl ? impl->pixels : NULL;
}

/// @brief Fill the entire render target with @p rgba (0xRRGGBBAA); no-op for invalid handles.
void rt_rendertarget2d_clear(void *target, int64_t rgba) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    if (!impl)
        return;
    rt_pixels_fill(impl->pixels, rgba);
}

/// @brief Replace the owned Pixels buffer with a fresh `width × height` allocation.
/// @details Traps on invalid dimensions, no-ops on allocation failure (leaves the
///          existing buffer intact). Uses retain-then-release slot ordering so a
///          pathological "resize to my current dimensions" can't briefly drop the
///          refcount to zero.
void rt_rendertarget2d_resize(void *target, int64_t width, int64_t height) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    if (!impl)
        return;
    if (!checked_count(width, height, 4, NULL)) {
        rt_trap("RenderTarget2D.Resize: invalid dimensions");
        return;
    }
    void *pixels = rt_pixels_new(width, height);
    if (!pixels)
        return;
    release_ref_slot(&impl->pixels);
    impl->pixels = pixels;
}

/// @brief Blit @p pixels onto the render target at `(x, y)` using straight-alpha compositing.
/// @details Covers the full source Pixels extent (no region selection). Clips to the target
///   bounds automatically. Uses ALPHA blend mode with no tint and full opacity.
void rt_rendertarget2d_draw_pixels(void *target, int64_t x, int64_t y, void *pixels) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    if (!impl || !pixels)
        return;
    blit_pixels(impl->pixels,
                x,
                y,
                pixels,
                0,
                0,
                rt_pixels_width(pixels),
                rt_pixels_height(pixels),
                -1,
                255,
                RT_GRAPHICS2D_BLEND_ALPHA);
}

/// @brief Blit a sub-rectangle `(sx, sy, width, height)` of @p pixels onto the target at `(x, y)`.
/// @details Selects a region of the source Pixels and blits it with straight-alpha compositing,
///   no tint, and full opacity. Both source and destination coordinates are clipped to their
///   respective buffer bounds by `blit_pixels`.
void rt_rendertarget2d_draw_region(void *target,
                                   int64_t x,
                                   int64_t y,
                                   void *pixels,
                                   int64_t sx,
                                   int64_t sy,
                                   int64_t width,
                                   int64_t height) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    if (!impl || !pixels)
        return;
    blit_pixels(impl->pixels,
                x,
                y,
                pixels,
                sx,
                sy,
                width,
                height,
                -1,
                255,
                RT_GRAPHICS2D_BLEND_ALPHA);
}

//=============================================================================
// Texture2D (aliased as GpuTexture2D)
//=============================================================================
// Retained `Pixels` handle plus sampling state — filter mode (NEAREST / LINEAR)
// and wrap mode (CLAMP / REPEAT). CPU-backed in this implementation; the name
// is forward-compatible with a future GPU-texture handle.

/// @brief GC finalizer — releases the retained Pixels reference.
static void texture2d_finalize(void *obj) {
    rt_texture2d_impl *texture = (rt_texture2d_impl *)obj;
    texture->magic = 0;
    release_ref_slot(&texture->pixels);
}

/// @brief Wrap an existing `Pixels` buffer as a Texture2D with default sampling
///        state (NEAREST filter, CLAMP wrap).
/// @details Takes a reference on `pixels`; the caller can drop theirs immediately.
///          Returns NULL on allocation failure; fires no trap so a caller probing
///          "was this pixels buffer loadable" can check for NULL without having
///          to catch a trap.
void *rt_texture2d_new(void *pixels) {
    if (!pixels)
        return NULL;
    rt_texture2d_impl *texture =
        (rt_texture2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_texture2d_impl));
    if (!texture)
        return NULL;
    texture->magic = RT2D_TEXTURE_MAGIC;
    texture->pixels = pixels;
    texture->filter = RT_GRAPHICS2D_FILTER_NEAREST;
    texture->wrap = RT_GRAPHICS2D_WRAP_CLAMP;
    retain_ref(pixels);
    rt_obj_set_finalizer(texture, texture2d_finalize);
    return texture;
}

/// @brief Load a Pixels buffer from disk and wrap it as a Texture2D.
/// @details The transient Pixels reference is released after the Texture2D takes its own
///   retain, so the caller gets a Texture2D with a net-zero effect on the Pixels refcount.
///   Returns NULL if the file cannot be loaded or the Texture2D allocation fails.
void *rt_texture2d_from_file(rt_string path) {
    void *pixels = rt_pixels_load(path);
    if (!pixels)
        return NULL;
    void *texture = rt_texture2d_new(pixels);
    release_ref_slot(&pixels);
    return texture;
}

/// @brief Return the pixel width of the texture, or 0 for an invalid handle.
int64_t rt_texture2d_width(void *texture) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    return impl ? rt_pixels_width(impl->pixels) : 0;
}

/// @brief Return the pixel height of the texture, or 0 for an invalid handle.
int64_t rt_texture2d_height(void *texture) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    return impl ? rt_pixels_height(impl->pixels) : 0;
}

/// @brief Return the underlying Pixels buffer (borrowed reference — caller must not release).
void *rt_texture2d_get_pixels(void *texture) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    return impl ? impl->pixels : NULL;
}

/// @brief Clone the underlying Pixels buffer (caller owns the returned copy).
void *rt_texture2d_clone_pixels(void *texture) {
    void *pixels = rt_texture2d_get_pixels(texture);
    return pixels ? rt_pixels_clone(pixels) : NULL;
}

/// @brief Set the sampling filter mode (`RT_GRAPHICS2D_FILTER_NEAREST` or `LINEAR`).
void rt_texture2d_set_filter(void *texture, int64_t filter) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    if (!impl)
        return;
    impl->filter = (filter == RT_GRAPHICS2D_FILTER_LINEAR) ? RT_GRAPHICS2D_FILTER_LINEAR
                                                           : RT_GRAPHICS2D_FILTER_NEAREST;
}

/// @brief Get the current sampling filter mode, defaulting to NEAREST for invalid handles.
int64_t rt_texture2d_get_filter(void *texture) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    return impl ? impl->filter : RT_GRAPHICS2D_FILTER_NEAREST;
}

/// @brief Set the texture wrap mode (`RT_GRAPHICS2D_WRAP_CLAMP` or `REPEAT`).
void rt_texture2d_set_wrap(void *texture, int64_t wrap) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    if (!impl)
        return;
    impl->wrap =
        (wrap == RT_GRAPHICS2D_WRAP_REPEAT) ? RT_GRAPHICS2D_WRAP_REPEAT : RT_GRAPHICS2D_WRAP_CLAMP;
}

/// @brief Get the current wrap mode, defaulting to CLAMP for invalid handles.
int64_t rt_texture2d_get_wrap(void *texture) {
    rt_texture2d_impl *impl = texture2d_checked(texture);
    return impl ? impl->wrap : RT_GRAPHICS2D_WRAP_CLAMP;
}

/// @brief Release the source reference in a draw command and zero-wipe the slot.
static void renderer2d_release_cmd(rt_renderer2d_cmd *cmd) {
    if (!cmd)
        return;
    release_ref_slot(&cmd->source);
    memset(cmd, 0, sizeof(*cmd));
}

/// @brief GC finalizer — releases all retained source references and frees the command buffer.
static void renderer2d_finalize(void *obj) {
    rt_renderer2d_impl *renderer = (rt_renderer2d_impl *)obj;
    renderer->magic = 0;
    if (renderer->cmds) {
        for (int64_t i = 0; i < renderer->count; i++)
            renderer2d_release_cmd(&renderer->cmds[i]);
        free(renderer->cmds);
    }
}

//=============================================================================
// Renderer2D
//=============================================================================
// Retained command stream. `Begin` clears + arms the queue; `Draw*` calls append
// commands with the current tint / alpha / blend mode snapshot; `End` (via the
// `Flush` / `Present` API elsewhere in the file) walks the commands and blits
// each one through `blit_pixels`. Works on any Pixels-compatible target.

/// @brief Grow the command buffer to fit at least `needed` entries.
/// @details Doubling-growth with overflow guards at both the element-count level
///          and the byte-count level (so a malicious caller can't wrap the
///          multiplication by `sizeof(rt_renderer2d_cmd)` past `INT64_MAX`).
///          Fresh slots are zero-initialized so the `_release_cmd` loop in
///          `_clear` can safely touch every slot up to `capacity`.
/// @return 1 on success (including no-op), 0 on overflow or realloc failure.
static int32_t renderer2d_reserve(rt_renderer2d_impl *renderer, int64_t needed) {
    if (!renderer || needed <= renderer->capacity)
        return 1;
    int64_t cap = renderer->capacity > 0 ? renderer->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_renderer2d_cmd))
        return 0;
    rt_renderer2d_cmd *cmds =
        (rt_renderer2d_cmd *)realloc(renderer->cmds, (size_t)cap * sizeof(rt_renderer2d_cmd));
    if (!cmds)
        return 0;
    memset(cmds + renderer->capacity, 0, (size_t)(cap - renderer->capacity) * sizeof(*cmds));
    renderer->cmds = cmds;
    renderer->capacity = cap;
    return 1;
}

/// @brief Allocate a new Renderer2D with the given initial command-buffer capacity.
/// @details Default render state: no tint (`tint = -1`), full alpha (`alpha = 255`),
///          ALPHA blend mode. The renderer starts inactive; call `Begin` before
///          the first `Draw*`. Initial capacity is clamped to a sensible range by
///          `initial_capacity` (floor 16, ceiling 1Mi).
void *rt_renderer2d_new(int64_t capacity) {
    rt_renderer2d_impl *renderer =
        (rt_renderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_renderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->magic = RT2D_RENDERER_MAGIC;
    renderer->capacity = initial_capacity(capacity);
    renderer->cmds =
        (rt_renderer2d_cmd *)calloc((size_t)renderer->capacity, sizeof(rt_renderer2d_cmd));
    if (!renderer->cmds) {
        if (rt_obj_release_check0(renderer))
            rt_obj_free(renderer);
        return NULL;
    }
    renderer->tint = -1;
    renderer->alpha = 255;
    renderer->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    rt_obj_set_finalizer(renderer, renderer2d_finalize);
    return renderer;
}

/// @brief Release all queued draw commands and reset the command count to zero.
void rt_renderer2d_clear(void *renderer) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        renderer2d_release_cmd(&impl->cmds[i]);
    impl->count = 0;
}

/// @brief Clear the command queue and mark the renderer active for new draw commands.
void rt_renderer2d_begin(void *renderer) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (!impl)
        return;
    rt_renderer2d_clear(renderer);
    impl->active = 1;
}

/// @brief Return the number of draw commands currently queued.
int64_t rt_renderer2d_count(void *renderer) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    return impl ? impl->count : 0;
}

/// @brief Set the multiplicative tint color (`0x00RRGGBB`); negative disables tinting.
void rt_renderer2d_set_tint(void *renderer, int64_t rgb) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (impl)
        impl->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

/// @brief Set the alpha scale clamped to `[0, 255]`; 255 = fully opaque.
void rt_renderer2d_set_alpha(void *renderer, int64_t alpha) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (impl)
        impl->alpha = clamp_u8_i64(alpha);
}

/// @brief Set the blend mode (`RT_GRAPHICS2D_BLEND_*`) clamped to the valid range `[0, 2]`.
void rt_renderer2d_set_blend_mode(void *renderer, int64_t blend_mode) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (impl)
        impl->blend_mode = clamp_i64(blend_mode, 0, 2);
}

/// @brief Append one draw command to the active queue, snapshotting current render
///        state (tint / alpha / blend mode) into the command so later state changes
///        don't retroactively affect already-queued draws.
/// @details Refuses to queue when the renderer isn't active (caller forgot to call
///          `Begin`) or the source is NULL. Capacity-overflow on the command buffer
///          traps rather than silently dropping draws — a silently-dropped draw in
///          a retained command stream is almost always a bug the caller would want
///          to see.
/// @param source_kind Tag identifying what `source` points to (Pixels, Texture2D,
///                    etc.) so the flush loop knows how to extract draw pixels.
/// @param x,y Destination position in the target's coordinate space.
/// @param sx,sy,width,height Source sub-rectangle in the source pixels.
static void renderer2d_queue(rt_renderer2d_impl *renderer,
                             int32_t source_kind,
                             void *source,
                             int64_t x,
                             int64_t y,
                             int64_t sx,
                             int64_t sy,
                             int64_t width,
                             int64_t height,
                             int64_t sampler_filter,
                             int64_t sampler_wrap) {
    if (!renderer || renderer->magic != RT2D_RENDERER_MAGIC || !renderer->active || !source)
        return;
    if (!renderer2d_reserve(renderer, renderer->count + 1)) {
        rt_trap("Renderer2D: command capacity overflow");
        return;
    }
    rt_renderer2d_cmd *cmd = &renderer->cmds[renderer->count++];
    cmd->source = source;
    cmd->source_kind = source_kind;
    cmd->x = x;
    cmd->y = y;
    cmd->sx = sx;
    cmd->sy = sy;
    cmd->width = width;
    cmd->height = height;
    cmd->tint = renderer->tint;
    cmd->alpha = renderer->alpha;
    cmd->blend_mode = renderer->blend_mode;
    cmd->sampler_filter = sampler_filter;
    cmd->sampler_wrap = sampler_wrap;
    retain_ref(source);
}

/// @brief Queue a draw command to blit the full @p pixels buffer at `(x, y)` with current render state.
void rt_renderer2d_draw_pixels(void *renderer, void *pixels, int64_t x, int64_t y) {
    rt_pixels_impl *p = rt_pixels_checked_impl(pixels, "Renderer2D.DrawPixels: null pixels");
    if (!p)
        return;
    renderer2d_queue(renderer2d_checked(renderer),
                     0,
                     pixels,
                     x,
                     y,
                     0,
                     0,
                     p->width,
                     p->height,
                     RT_GRAPHICS2D_FILTER_NEAREST,
                     RT_GRAPHICS2D_WRAP_CLAMP);
}

/// @brief Queue a draw command to blit the full Texture2D at `(x, y)`, propagating its sampler state.
void rt_renderer2d_draw_texture(void *renderer, void *texture, int64_t x, int64_t y) {
    if (!texture || !texture2d_checked(texture))
        return;
    renderer2d_queue(renderer2d_checked(renderer),
                     1,
                     texture,
                     x,
                     y,
                     0,
                     0,
                     rt_texture2d_width(texture),
                     rt_texture2d_height(texture),
                     rt_texture2d_get_filter(texture),
                     rt_texture2d_get_wrap(texture));
}

/// @brief Queue a draw command to blit the sub-region `(sx, sy, width, height)` of @p pixels at `(x, y)`.
void rt_renderer2d_draw_region(void *renderer,
                               void *pixels,
                               int64_t x,
                               int64_t y,
                               int64_t sx,
                               int64_t sy,
                               int64_t width,
                               int64_t height) {
    if (!rt_pixels_checked_impl(pixels, "Renderer2D.DrawRegion: null pixels"))
        return;
    renderer2d_queue(renderer2d_checked(renderer),
                     0,
                     pixels,
                     x,
                     y,
                     sx,
                     sy,
                     width,
                     height,
                     RT_GRAPHICS2D_FILTER_NEAREST,
                     RT_GRAPHICS2D_WRAP_CLAMP);
}

/// @brief Execute one queued draw command against a Pixels render target.
/// @details Resolves the source (Texture2D → its Pixels, or raw Pixels directly) then calls
///   `blit_pixels` with the command's snapshot of tint, alpha, blend mode, and region.
static void renderer2d_flush_cmd_to_target(rt_renderer2d_cmd *cmd, void *target_pixels) {
    void *pixels = cmd->source_kind == 1 ? rt_texture2d_get_pixels(cmd->source) : cmd->source;
    if (!pixels)
        return;
    blit_pixels(target_pixels,
                cmd->x,
                cmd->y,
                pixels,
                cmd->sx,
                cmd->sy,
                cmd->width,
                cmd->height,
                cmd->tint,
                cmd->alpha,
                cmd->blend_mode);
}

/// @brief Blit a Pixels buffer to a Canvas surface using the specified blend mode.
/// @details Routes through the appropriate Canvas API function based on blend mode:
///   OPAQUE → `rt_canvas_blit`, ALPHA → `rt_canvas_blit_alpha`, ADD → read-modify-write
///   via `rt_canvas_copy_rect` + `blit_pixels(ADD)` + `rt_canvas_blit`. The additive
///   path is the only one that needs a round-trip through a temporary Pixels buffer.
static void renderer2d_blit_pixels_to_canvas(
    void *canvas, int64_t x, int64_t y, void *pixels, int64_t blend_mode) {
    if (!canvas || !pixels)
        return;
    if (blend_mode == RT_GRAPHICS2D_BLEND_OPAQUE) {
        rt_canvas_blit(canvas, x, y, pixels);
        return;
    }
    if (blend_mode != RT_GRAPHICS2D_BLEND_ADD) {
        rt_canvas_blit_alpha(canvas, x, y, pixels);
        return;
    }

    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    if (width <= 0 || height <= 0)
        return;
    void *dest = rt_canvas_copy_rect(canvas, x, y, width, height);
    if (!dest)
        return;
    blit_pixels(dest, 0, 0, pixels, 0, 0, width, height, -1, 255, RT_GRAPHICS2D_BLEND_ADD);
    rt_canvas_blit(canvas, x, y, dest);
    release_ref_slot(&dest);
}

/// @brief Execute all queued draw commands against a RenderTarget2D.
/// @details Does NOT deactivate the renderer — use `rt_renderer2d_end` for the
///   Canvas path or `rt_renderer2d_clear` to reset without flushing.
void rt_renderer2d_flush_to_target(void *renderer, void *target) {
    if (!renderer || !target)
        return;
    void *target_pixels = rt_rendertarget2d_get_pixels(target);
    if (!target_pixels)
        return;
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        renderer2d_flush_cmd_to_target(&impl->cmds[i], target_pixels);
}

/// @brief Flush the queued command stream to a Canvas, then deactivate the queue.
/// @details For every command:
///          1. Resolve the source Pixels (Texture2D → `.pixels`, Pixels → self).
///          2. If the command specifies a sub-region, copy just that region out
///             so the canvas blit doesn't touch surrounding texels.
///          3. If the command carries tint or alpha, produce a tint+alpha-applied
///             copy via `processed_pixels_or_null`.
///          4. Blit the resulting pixels to the canvas at `(x, y)` using the
///             canvas's standard alpha compositing.
///          5. Release the intermediate `region` / `processed` buffers.
///          After flush, `active = 0` so a subsequent `Begin` is required to
///          queue more draws.
void rt_renderer2d_end(void *renderer, void *canvas) {
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (!impl)
        return;
    if (!impl->active)
        return;
    if (canvas) {
        for (int64_t i = 0; i < impl->count; i++) {
            rt_renderer2d_cmd *cmd = &impl->cmds[i];
            void *pixels =
                cmd->source_kind == 1 ? rt_texture2d_get_pixels(cmd->source) : cmd->source;
            if (!pixels)
                continue;
            void *region = NULL;
            void *draw_pixels = pixels;
            int64_t blit_x = cmd->x;
            int64_t blit_y = cmd->y;
            if (cmd->sx != 0 || cmd->sy != 0 || cmd->width != rt_pixels_width(pixels) ||
                cmd->height != rt_pixels_height(pixels)) {
                int64_t sx = cmd->sx;
                int64_t sy = cmd->sy;
                int64_t width = cmd->width;
                int64_t height = cmd->height;
                if (!clip_region_to_source(pixels, &blit_x, &blit_y, &sx, &sy, &width, &height))
                    continue;
                region = copy_region_pixels(pixels, sx, sy, width, height);
                draw_pixels = region;
            }
            void *processed = processed_pixels_or_null(draw_pixels, cmd->tint, cmd->alpha);
            if (processed)
                draw_pixels = processed;
            if (draw_pixels)
                renderer2d_blit_pixels_to_canvas(canvas, blit_x, blit_y, draw_pixels, cmd->blend_mode);
            release_ref_slot(&processed);
            release_ref_slot(&region);
        }
    }
    impl->active = 0;
    rt_renderer2d_clear(renderer);
}

//=============================================================================
// Material2D
//=============================================================================
// Stateless tint / alpha / blend-mode container. Unlike Renderer2D it doesn't
// queue draws — `Apply(pixels)` produces a tint+alpha-processed copy of the
// input Pixels and callers feed that to whichever draw surface they use.

/// @brief Allocate a Material2D with default state (no tint, full alpha, ALPHA blend).
void *rt_material2d_new(void) {
    rt_material2d_impl *material =
        (rt_material2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_material2d_impl));
    if (!material)
        return NULL;
    material->tint = -1;
    material->alpha = 255;
    material->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    return material;
}

/// @brief Set the multiplicative tint color (`0x00RRGGBB`); negative disables tinting.
void rt_material2d_set_tint(void *material, int64_t rgb) {
    if (material)
        ((rt_material2d_impl *)material)->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

/// @brief Get the current tint color, or -1 (no tint) for an invalid material.
int64_t rt_material2d_get_tint(void *material) {
    return material ? ((rt_material2d_impl *)material)->tint : -1;
}

/// @brief Set the alpha scale clamped to `[0, 255]`.
void rt_material2d_set_alpha(void *material, int64_t alpha) {
    if (material)
        ((rt_material2d_impl *)material)->alpha = clamp_u8_i64(alpha);
}

/// @brief Get the current alpha scale in `[0, 255]`, defaulting to 255 for invalid handles.
int64_t rt_material2d_get_alpha(void *material) {
    return material ? ((rt_material2d_impl *)material)->alpha : 255;
}

/// @brief Set the blend mode (`RT_GRAPHICS2D_BLEND_*`) clamped to `[0, 2]`.
void rt_material2d_set_blend_mode(void *material, int64_t blend_mode) {
    if (material)
        ((rt_material2d_impl *)material)->blend_mode = clamp_i64(blend_mode, 0, 2);
}

/// @brief Get the current blend mode, defaulting to ALPHA for invalid handles.
int64_t rt_material2d_get_blend_mode(void *material) {
    return material ? ((rt_material2d_impl *)material)->blend_mode : RT_GRAPHICS2D_BLEND_ALPHA;
}

/// @brief Produce a tint+alpha-applied copy of `pixels` using this material's state.
/// @details Clones the source and applies tint / alpha in place on the clone;
///          the original Pixels is untouched. Returns NULL if either argument is
///          NULL or the clone fails. Blend mode is NOT baked in — it's a
///          *compositing* choice made when the processed Pixels are drawn, not
///          when they're produced.
void *rt_material2d_apply(void *material, void *pixels) {
    if (!material || !pixels)
        return NULL;
    rt_material2d_impl *impl = (rt_material2d_impl *)material;
    void *out = rt_pixels_clone(pixels);
    if (out)
        apply_tint_alpha_in_place(out, impl->tint, impl->alpha);
    return out;
}

//=============================================================================
// Shader2D / PostProcess2D
//=============================================================================
// CPU image-effect wrapper. Five effects: NONE (passthrough clone), INVERT,
// GRAYSCALE, TINT (colorize toward `color`), BLUR (box blur with radius derived
// from `amount`). `PostProcess2D` is a type alias for `Shader2D` with the same
// impl struct — the two names let user code distinguish "applied as a material
// pass" from "applied as a full-screen post-effect" purely at the naming level.

/// @brief Allocate a Shader2D / PostProcess2D with the given initial effect.
/// @details Effect index is clamped to the valid range `[0, 4]`; defaults to
///          `amount = 1` (blur radius / tint intensity) and white `color`.
void *rt_shader2d_new(int64_t effect) {
    rt_shader2d_impl *shader =
        (rt_shader2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_shader2d_impl));
    if (!shader)
        return NULL;
    shader->effect = clamp_i64(effect, 0, 4);
    shader->amount = 1;
    shader->color = 0x00FFFFFF;
    return shader;
}

/// @brief Set the shader effect index clamped to `[0, 4]` (NONE/INVERT/GRAYSCALE/TINT/BLUR).
void rt_shader2d_set_effect(void *shader, int64_t effect) {
    if (shader)
        ((rt_shader2d_impl *)shader)->effect = clamp_i64(effect, 0, 4);
}

/// @brief Get the current effect index, defaulting to NONE for invalid handles.
int64_t rt_shader2d_get_effect(void *shader) {
    return shader ? ((rt_shader2d_impl *)shader)->effect : RT_GRAPHICS2D_EFFECT_NONE;
}

/// @brief Set the effect amount (e.g. blur radius) clamped to `[0, 10]`.
void rt_shader2d_set_amount(void *shader, int64_t amount) {
    if (shader)
        ((rt_shader2d_impl *)shader)->amount = clamp_i64(amount, 0, 10);
}

/// @brief Get the current effect amount, or 0 for invalid handles.
int64_t rt_shader2d_get_amount(void *shader) {
    return shader ? ((rt_shader2d_impl *)shader)->amount : 0;
}

/// @brief Set the effect color (`0x00RRGGBB`), used for the TINT effect.
void rt_shader2d_set_color(void *shader, int64_t rgb) {
    if (shader)
        ((rt_shader2d_impl *)shader)->color = rgb & 0x00FFFFFF;
}

/// @brief Get the current effect color, defaulting to white (0x00FFFFFF) for invalid handles.
int64_t rt_shader2d_get_color(void *shader) {
    return shader ? ((rt_shader2d_impl *)shader)->color : 0x00FFFFFF;
}

/// @brief Dispatch to the correct pixel-effect function for the given effect index.
/// @details Returns a new Pixels buffer with the effect applied. Effect NONE returns a plain clone.
///   INVERT, GRAYSCALE, TINT, and BLUR delegate to the corresponding `rt_pixels_*` functions.
static void *apply_effect(int64_t effect, int64_t amount, int64_t color, void *pixels) {
    if (!pixels)
        return NULL;
    switch (effect) {
        case RT_GRAPHICS2D_EFFECT_INVERT:
            return rt_pixels_invert(pixels);
        case RT_GRAPHICS2D_EFFECT_GRAYSCALE:
            return rt_pixels_grayscale(pixels);
        case RT_GRAPHICS2D_EFFECT_TINT:
            return rt_pixels_tint(pixels, color);
        case RT_GRAPHICS2D_EFFECT_BLUR:
            return rt_pixels_blur(pixels, amount);
        case RT_GRAPHICS2D_EFFECT_NONE:
        default:
            return rt_pixels_clone(pixels);
    }
}

/// @brief Apply the shader's effect to @p pixels and return a new Pixels buffer with the result.
/// @details A NULL shader degrades gracefully to a plain clone. Callers own the returned Pixels.
void *rt_shader2d_apply(void *shader, void *pixels) {
    if (!shader)
        return pixels ? rt_pixels_clone(pixels) : NULL;
    rt_shader2d_impl *impl = (rt_shader2d_impl *)shader;
    return apply_effect(impl->effect, impl->amount, impl->color, pixels);
}

/// @brief Allocate a PostProcess2D (a Shader2D starting with EFFECT_NONE).
void *rt_postprocess2d_new(void) {
    return rt_shader2d_new(RT_GRAPHICS2D_EFFECT_NONE);
}

/// @brief Set the post-process effect index (forwarded to Shader2D).
void rt_postprocess2d_set_effect(void *postprocess, int64_t effect) {
    rt_shader2d_set_effect(postprocess, effect);
}

/// @brief Set the post-process effect amount (forwarded to Shader2D).
void rt_postprocess2d_set_amount(void *postprocess, int64_t amount) {
    rt_shader2d_set_amount(postprocess, amount);
}

/// @brief Set the post-process effect color (forwarded to Shader2D).
void rt_postprocess2d_set_color(void *postprocess, int64_t rgb) {
    rt_shader2d_set_color(postprocess, rgb);
}

/// @brief Apply the post-process effect and return a new Pixels buffer (forwarded to Shader2D).
void *rt_postprocess2d_apply(void *postprocess, void *pixels) {
    return rt_shader2d_apply(postprocess, pixels);
}

//=============================================================================
// Viewport2D (aliased as ScreenScaler)
//=============================================================================
// Maps a fixed *virtual* resolution (the author's design size) onto the
// actual *screen* resolution with letterboxing. Scale is fixed-point with
// `1000 == 1.0x`. Consumers apply `Scale` + `OffsetX` / `OffsetY` to translate
// between world coordinates and screen coordinates. The class's superpower is
// *integer scaling* — retro pixel games want `2.0x`, `3.0x`, never `2.3x`,
// because non-integer scale produces non-uniform texel sizes and shimmer.

/// @brief Recompute `scale`, `offset_x`, `offset_y` from the four size fields.
/// @details Picks the *larger* possible uniform scale that still fits the
///          virtual canvas inside the screen on both axes (the smaller of
///          the X and Y scale factors — letterbox on the other axis). When
///          `integer_scaling` is enabled, truncates the scale to the nearest
///          whole multiple of `1000` (clamped to at least `1.0x`) so the
///          result is always `1x`, `2x`, `3x`, … — critical for crisp pixel-
///          art games. Offsets are centered on both axes so the remaining
///          letterbox / pillarbox space is split evenly.
///
///          Fixed-point representation: integer math with a 1000x multiplier
///          throughout, so `scale = 2300` means `2.3x`. All downstream
///          `world_to_screen` / `screen_to_world` calls use the same fixed-
///          point arithmetic so round-trips are stable within one-pixel
///          tolerance.
static void viewport2d_recalculate(rt_viewport2d_impl *viewport) {
    if (!viewport)
        return;
    long double scale_x_ld =
        ((long double)viewport->screen_width * 1000.0L) / (long double)viewport->virtual_width;
    long double scale_y_ld =
        ((long double)viewport->screen_height * 1000.0L) / (long double)viewport->virtual_height;
    int64_t scale_x = scale_x_ld >= (long double)INT64_MAX ? INT64_MAX : (int64_t)scale_x_ld;
    int64_t scale_y = scale_y_ld >= (long double)INT64_MAX ? INT64_MAX : (int64_t)scale_y_ld;
    viewport->scale = scale_x < scale_y ? scale_x : scale_y;
    if (viewport->scale < 1)
        viewport->scale = 1;
    if (viewport->integer_scaling) {
        int64_t whole = viewport->scale / 1000;
        if (whole >= 1)
            viewport->scale = whole > INT64_MAX / 1000 ? INT64_MAX : whole * 1000;
    }
    long double scaled_width_ld =
        ((long double)viewport->virtual_width * (long double)viewport->scale) / 1000.0L;
    long double scaled_height_ld =
        ((long double)viewport->virtual_height * (long double)viewport->scale) / 1000.0L;
    int64_t scaled_width =
        scaled_width_ld >= (long double)INT64_MAX ? INT64_MAX : (int64_t)scaled_width_ld;
    int64_t scaled_height =
        scaled_height_ld >= (long double)INT64_MAX ? INT64_MAX : (int64_t)scaled_height_ld;
    viewport->offset_x = (viewport->screen_width - scaled_width) / 2;
    viewport->offset_y = (viewport->screen_height - scaled_height) / 2;
}

/// @brief Allocate a Viewport2D with the given virtual and screen dimensions.
/// @details Dimensions are all clamped to `[1, RT2D_MAX_DIM]` by `normalized_dim`
///          so zero / negative inputs don't produce divide-by-zero in the scale
///          computation. Integer scaling is disabled by default. Scale + offsets
///          are computed immediately so the viewport is usable without a
///          separate `Recalculate` call.
void *rt_viewport2d_new(int64_t virtual_width,
                        int64_t virtual_height,
                        int64_t screen_width,
                        int64_t screen_height) {
    rt_viewport2d_impl *viewport =
        (rt_viewport2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_viewport2d_impl));
    if (!viewport)
        return NULL;
    viewport->virtual_width = normalized_dim(virtual_width);
    viewport->virtual_height = normalized_dim(virtual_height);
    viewport->screen_width = normalized_dim(screen_width);
    viewport->screen_height = normalized_dim(screen_height);
    viewport2d_recalculate(viewport);
    return viewport;
}

/// @brief Update the virtual (design) canvas size and recalculate scale/offsets.
void rt_viewport2d_set_virtual_size(void *viewport, int64_t width, int64_t height) {
    if (!viewport)
        return;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    impl->virtual_width = normalized_dim(width);
    impl->virtual_height = normalized_dim(height);
    viewport2d_recalculate(impl);
}

/// @brief Update the physical screen size and recalculate scale/offsets.
void rt_viewport2d_set_screen_size(void *viewport, int64_t width, int64_t height) {
    if (!viewport)
        return;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    impl->screen_width = normalized_dim(width);
    impl->screen_height = normalized_dim(height);
    viewport2d_recalculate(impl);
}

/// @brief Enable or disable integer-only scaling (whole-number scale factors only).
void rt_viewport2d_set_integer_scaling(void *viewport, int64_t enabled) {
    if (!viewport)
        return;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    impl->integer_scaling = enabled != 0;
    viewport2d_recalculate(impl);
}

/// @brief Return the current scale factor as a fixed-point integer (1000 = 1.0x).
int64_t rt_viewport2d_get_scale(void *viewport) {
    return viewport ? ((rt_viewport2d_impl *)viewport)->scale : 1000;
}

/// @brief Return the horizontal letterbox/pillarbox offset in screen pixels.
int64_t rt_viewport2d_get_offset_x(void *viewport) {
    return viewport ? ((rt_viewport2d_impl *)viewport)->offset_x : 0;
}

/// @brief Return the vertical letterbox/pillarbox offset in screen pixels.
int64_t rt_viewport2d_get_offset_y(void *viewport) {
    return viewport ? ((rt_viewport2d_impl *)viewport)->offset_y : 0;
}

/// @brief Convert a virtual-space X coordinate to a screen-space X pixel position.
/// @details Applies `screen_x = offset_x + (virtual_x * scale / 1000)` using `long double`
///   to preserve sub-pixel accuracy before rounding to the nearest integer.
int64_t rt_viewport2d_world_to_screen_x(void *viewport, int64_t x) {
    if (!viewport)
        return x;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    long double mapped =
        (long double)impl->offset_x + ((long double)x * (long double)impl->scale) / 1000.0L;
    return round_long_double_to_i64(mapped);
}

/// @brief Convert a virtual-space Y coordinate to a screen-space Y pixel position.
int64_t rt_viewport2d_world_to_screen_y(void *viewport, int64_t y) {
    if (!viewport)
        return y;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    long double mapped =
        (long double)impl->offset_y + ((long double)y * (long double)impl->scale) / 1000.0L;
    return round_long_double_to_i64(mapped);
}

/// @brief Convert a screen-space X pixel position back to a virtual-space X coordinate.
/// @details Inverse of `world_to_screen_x`: `virtual_x = (screen_x - offset_x) * 1000 / scale`.
int64_t rt_viewport2d_screen_to_world_x(void *viewport, int64_t x) {
    if (!viewport)
        return x;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    long double mapped =
        ((long double)x - (long double)impl->offset_x) * 1000.0L / (long double)impl->scale;
    return round_long_double_to_i64(mapped);
}

/// @brief Convert a screen-space Y pixel position back to a virtual-space Y coordinate.
int64_t rt_viewport2d_screen_to_world_y(void *viewport, int64_t y) {
    if (!viewport)
        return y;
    rt_viewport2d_impl *impl = (rt_viewport2d_impl *)viewport;
    long double mapped =
        ((long double)y - (long double)impl->offset_y) * 1000.0L / (long double)impl->scale;
    return round_long_double_to_i64(mapped);
}

//=============================================================================
// TileSet2D
//=============================================================================
// Uniform grid over a Pixels image. `columns × rows` is derived from the
// image size and per-tile dimensions; individual tiles can be extracted as
// their own Pixels for rendering or inspection.

/// @brief GC finalizer — releases the retained backing Pixels.
static void tileset2d_finalize(void *obj) {
    rt_tileset2d_impl *tileset = (rt_tileset2d_impl *)obj;
    release_ref_slot(&tileset->pixels);
}

/// @brief Wrap a Pixels image as a tileset with fixed `tile_width × tile_height`
///        cells.
/// @details Returns NULL (no trap) if `pixels` is null, either tile dimension is
///          non-positive, or the image is smaller than one tile — callers can
///          probe for "is this a usable tileset" without catching a trap. The
///          grid doesn't have to divide evenly; any partial column/row on the
///          right or bottom is inaccessible.
void *rt_tileset2d_new(void *pixels, int64_t tile_width, int64_t tile_height) {
    if (!pixels || tile_width <= 0 || tile_height <= 0)
        return NULL;
    if (rt_pixels_width(pixels) < tile_width || rt_pixels_height(pixels) < tile_height)
        return NULL;
    rt_tileset2d_impl *tileset =
        (rt_tileset2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tileset2d_impl));
    if (!tileset)
        return NULL;
    tileset->pixels = pixels;
    tileset->tile_width = tile_width;
    tileset->tile_height = tile_height;
    retain_ref(pixels);
    rt_obj_set_finalizer(tileset, tileset2d_finalize);
    return tileset;
}

/// @brief Return the number of whole tile columns in the tileset image.
int64_t rt_tileset2d_columns(void *tileset) {
    if (!tileset)
        return 0;
    rt_tileset2d_impl *impl = (rt_tileset2d_impl *)tileset;
    return rt_pixels_width(impl->pixels) / impl->tile_width;
}

/// @brief Return the number of whole tile rows in the tileset image.
int64_t rt_tileset2d_rows(void *tileset) {
    if (!tileset)
        return 0;
    rt_tileset2d_impl *impl = (rt_tileset2d_impl *)tileset;
    return rt_pixels_height(impl->pixels) / impl->tile_height;
}

/// @brief Return the total number of tiles (`columns * rows`) in the tileset.
int64_t rt_tileset2d_tile_count(void *tileset) {
    return rt_tileset2d_columns(tileset) * rt_tileset2d_rows(tileset);
}

/// @brief Extract one tile by index and return it as a new Pixels buffer.
/// @details Tiles are indexed left-to-right, top-to-bottom starting at 0. Returns NULL for
///   out-of-range indices; caller owns the returned Pixels.
void *rt_tileset2d_get_tile_pixels(void *tileset, int64_t tile_index) {
    if (!tileset || tile_index < 0 || tile_index >= rt_tileset2d_tile_count(tileset))
        return NULL;
    rt_tileset2d_impl *impl = (rt_tileset2d_impl *)tileset;
    int64_t columns = rt_tileset2d_columns(tileset);
    int64_t sx = (tile_index % columns) * impl->tile_width;
    int64_t sy = (tile_index / columns) * impl->tile_height;
    return copy_region_pixels(impl->pixels, sx, sy, impl->tile_width, impl->tile_height);
}

//=============================================================================
// TileLayer2D
//=============================================================================
// Dense `width × height` grid of int64 tile IDs, plus visibility flag and
// per-layer opacity. Zero IDs conventionally mean "empty cell," but the
// interpretation is ultimately the consumer's. Paired with `TileSet2D` at
// draw time to resolve an ID to a renderable tile.

/// @brief GC finalizer — frees the `tiles` buffer.
static void tilelayer2d_finalize(void *obj) {
    rt_tilelayer2d_impl *layer = (rt_tilelayer2d_impl *)obj;
    free(layer->tiles);
}

/// @brief Allocate a tile-grid layer `width × height`, zero-initialized and visible
///        at full opacity.
/// @details Validates dimensions through `checked_count` with an int64-sized
///          element cost (one int64 per tile ID) so an enormous grid can't
///          overflow the total byte count. Traps on invalid dimensions — the
///          caller gets a clear error rather than a silent NULL. On malloc
///          failure the partially-constructed impl is torn down cleanly.
void *rt_tilelayer2d_new(int64_t width, int64_t height) {
    int64_t count = 0;
    if (!checked_count(width, height, (int64_t)sizeof(int64_t), &count)) {
        rt_trap("TileLayer2D.New: invalid dimensions");
        return NULL;
    }
    rt_tilelayer2d_impl *layer =
        (rt_tilelayer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tilelayer2d_impl));
    if (!layer)
        return NULL;
    layer->tiles = (int64_t *)calloc((size_t)count, sizeof(int64_t));
    if (!layer->tiles) {
        if (rt_obj_release_check0(layer))
            rt_obj_free(layer);
        return NULL;
    }
    layer->width = width;
    layer->height = height;
    layer->visible = 1;
    layer->opacity = 255;
    rt_obj_set_finalizer(layer, tilelayer2d_finalize);
    return layer;
}

/// @brief Return the width (in tiles) of the layer.
int64_t rt_tilelayer2d_width(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->width : 0;
}

/// @brief Return the height (in tiles) of the layer.
int64_t rt_tilelayer2d_height(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->height : 0;
}

/// @brief Return non-zero if tile cell (x, y) is within the layer's bounds.
static int32_t tilelayer2d_in_bounds(rt_tilelayer2d_impl *layer, int64_t x, int64_t y) {
    return layer && x >= 0 && y >= 0 && x < layer->width && y < layer->height;
}

/// @brief Write a tile ID into a cell, using row-major flat indexing.
/// @details Bounds are checked via `tilelayer2d_in_bounds`; out-of-range writes
///          are silently dropped. Tile ID 0 conventionally means "empty".
void rt_tilelayer2d_set(void *layer, int64_t x, int64_t y, int64_t tile) {
    rt_tilelayer2d_impl *impl = (rt_tilelayer2d_impl *)layer;
    if (!tilelayer2d_in_bounds(impl, x, y))
        return;
    impl->tiles[y * impl->width + x] = tile;
}

/// @brief Read the tile ID at `(x, y)`, returning -1 for out-of-range coords.
int64_t rt_tilelayer2d_get(void *layer, int64_t x, int64_t y) {
    rt_tilelayer2d_impl *impl = (rt_tilelayer2d_impl *)layer;
    if (!tilelayer2d_in_bounds(impl, x, y))
        return -1;
    return impl->tiles[y * impl->width + x];
}

/// @brief Fill every cell in the layer with the same tile ID.
void rt_tilelayer2d_fill(void *layer, int64_t tile) {
    rt_tilelayer2d_impl *impl = (rt_tilelayer2d_impl *)layer;
    if (!impl)
        return;
    int64_t count = impl->width * impl->height;
    for (int64_t i = 0; i < count; i++)
        impl->tiles[i] = tile;
}

/// @brief Fill every cell with tile ID 0 (empty).
void rt_tilelayer2d_clear(void *layer) {
    rt_tilelayer2d_fill(layer, 0);
}

/// @brief Set whether the layer is included in tilemap renders.
void rt_tilelayer2d_set_visible(void *layer, int64_t visible) {
    if (layer)
        ((rt_tilelayer2d_impl *)layer)->visible = visible != 0;
}

/// @brief Return non-zero if the layer is currently marked visible.
int64_t rt_tilelayer2d_is_visible(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->visible : 0;
}

/// @brief Set the layer opacity in [0, 255]; values are clamped.
void rt_tilelayer2d_set_opacity(void *layer, int64_t opacity) {
    if (layer)
        ((rt_tilelayer2d_impl *)layer)->opacity = clamp_u8_i64(opacity);
}

/// @brief Return the current layer opacity in [0, 255].
int64_t rt_tilelayer2d_get_opacity(void *layer) {
    return layer ? ((rt_tilelayer2d_impl *)layer)->opacity : 0;
}

//=============================================================================
// ObjectLayer2D
//=============================================================================
// Dynamic list of rect objects — used for collision volumes, triggers, spawn
// points, and editor metadata. Each entry has `(x, y, width, height, type)`;
// interpretation of `type` is app-specific.

/// @brief GC finalizer — frees the dynamic `items` array.
static void objectlayer2d_finalize(void *obj) {
    rt_objectlayer2d_impl *layer = (rt_objectlayer2d_impl *)obj;
    free(layer->items);
}

/// @brief Allocate an object layer with the given initial capacity.
/// @details Capacity is clamped by `initial_capacity` (floor 16, ceiling 1Mi).
///          Returns NULL on allocation failure without trapping — the caller
///          can fall back to a smaller capacity or handle the error.
void *rt_objectlayer2d_new(int64_t capacity) {
    rt_objectlayer2d_impl *layer =
        (rt_objectlayer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_objectlayer2d_impl));
    if (!layer)
        return NULL;
    layer->capacity = initial_capacity(capacity);
    layer->items = (rt_objectlayer2d_entry *)calloc((size_t)layer->capacity, sizeof(*layer->items));
    if (!layer->items) {
        if (rt_obj_release_check0(layer))
            rt_obj_free(layer);
        return NULL;
    }
    rt_obj_set_finalizer(layer, objectlayer2d_finalize);
    return layer;
}

/// @brief Grow the object-entry array to fit at least `needed` entries.
/// @details Same doubling pattern as `renderer2d_reserve` with both element-count
///          and byte-count overflow guards. Returns 0 on failure so the caller
///          can decide whether to trap or drop-the-add — used by `Add` to
///          trap-on-overflow with a clear message.
static int32_t objectlayer2d_reserve(rt_objectlayer2d_impl *layer, int64_t needed) {
    if (!layer || needed <= layer->capacity)
        return 1;
    int64_t cap = layer->capacity > 0 ? layer->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_objectlayer2d_entry))
        return 0;
    rt_objectlayer2d_entry *items =
        (rt_objectlayer2d_entry *)realloc(layer->items, (size_t)cap * sizeof(*items));
    if (!items)
        return 0;
    memset(items + layer->capacity, 0, (size_t)(cap - layer->capacity) * sizeof(*items));
    layer->items = items;
    layer->capacity = cap;
    return 1;
}

/// @brief Append a new rect entry and return its index.
/// @details Grows the backing array if needed (doubling, checked). Traps via
///          `rt_trap` on capacity overflow. Returns -1 if `layer` is NULL.
///          The `type` field is opaque — callers use it as a category tag
///          (e.g. collision=1, trigger=2, spawn=3).
int64_t rt_objectlayer2d_add_rect(
    void *layer, int64_t x, int64_t y, int64_t width, int64_t height, int64_t type) {
    rt_objectlayer2d_impl *impl = (rt_objectlayer2d_impl *)layer;
    if (!impl)
        return -1;
    if (!objectlayer2d_reserve(impl, impl->count + 1)) {
        rt_trap("ObjectLayer2D: capacity overflow");
        return -1;
    }
    int64_t index = impl->count++;
    impl->items[index].x = x;
    impl->items[index].y = y;
    impl->items[index].width = width;
    impl->items[index].height = height;
    impl->items[index].type = type;
    return index;
}

/// @brief Return the number of rect entries currently in the layer.
int64_t rt_objectlayer2d_count(void *layer) {
    return layer ? ((rt_objectlayer2d_impl *)layer)->count : 0;
}

/// @brief Reset the entry count to zero without freeing the backing array.
void rt_objectlayer2d_clear(void *layer) {
    if (layer)
        ((rt_objectlayer2d_impl *)layer)->count = 0;
}

/// @brief Return a pointer to the entry at @p index in the object layer, or NULL if out of range.
static rt_objectlayer2d_entry *objectlayer2d_get_entry(void *layer, int64_t index) {
    rt_objectlayer2d_impl *impl = (rt_objectlayer2d_impl *)layer;
    if (!impl || index < 0 || index >= impl->count)
        return NULL;
    return &impl->items[index];
}

/// @brief Return the X position of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_x(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->x : 0;
}

/// @brief Return the Y position of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_y(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->y : 0;
}

/// @brief Return the width of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_width(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->width : 0;
}

/// @brief Return the height of the entry at `index`, or 0 if out of range.
int64_t rt_objectlayer2d_get_height(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->height : 0;
}

/// @brief Return the application-defined type tag of the entry at `index`,
///        or 0 if out of range.
int64_t rt_objectlayer2d_get_type(void *layer, int64_t index) {
    rt_objectlayer2d_entry *entry = objectlayer2d_get_entry(layer, index);
    return entry ? entry->type : 0;
}

//=============================================================================
// AutoTile2D
//=============================================================================
// 16-variant autotile resolver. Given a 4-bit neighbour mask (one bit per
// cardinal direction: N / E / S / W typically), returns the pre-configured
// tile ID to place so edges and corners join up seamlessly. The mask-to-ID
// table is filled via `SetVariant`; `Apply` writes the resolved ID directly
// into a TileLayer2D cell.

/// @brief Allocate a zeroed autotile variant table (16 entries, all 0).
void *rt_autotile2d_new(void) {
    return rt_obj_new_i64(0, (int64_t)sizeof(rt_autotile2d_impl));
}

/// @brief Register the tile ID to use when the neighbour mask equals `mask`.
/// @details Only the low 4 bits of `mask` are used (16 possible combinations).
void rt_autotile2d_set_variant(void *autotile, int64_t mask, int64_t tile) {
    if (!autotile)
        return;
    ((rt_autotile2d_impl *)autotile)->variants[mask & 15] = tile;
}

/// @brief Look up the tile ID for a given 4-bit neighbour `mask`.
/// @details Returns 0 if the autotile pointer is NULL or the variant was never
///          set (variants default to 0 on allocation).
int64_t rt_autotile2d_resolve(void *autotile, int64_t mask) {
    return autotile ? ((rt_autotile2d_impl *)autotile)->variants[mask & 15] : 0;
}

/// @brief Resolve `mask` to a tile ID and write it into the TileLayer2D cell at `(x, y)`.
/// @details Combines `rt_autotile2d_resolve` + `rt_tilelayer2d_set` so game
///          code can update a cell in one call without a temporary variable.
void rt_autotile2d_apply(void *autotile, void *layer, int64_t x, int64_t y, int64_t mask) {
    if (!autotile || !layer)
        return;
    rt_tilelayer2d_set(layer, x, y, rt_autotile2d_resolve(autotile, mask));
}

//=============================================================================
// Path2D
//=============================================================================
// Dynamic sequence of `(x, y, move)` points. `move == 1` marks a pen-up
// break — `MoveTo` followed by `LineTo` forms a subpath. `DrawToPixels`
// walks the list and renders a line between every consecutive non-move pair.

/// @brief GC finalizer — frees the points buffer.
static void path2d_finalize(void *obj) {
    rt_path2d_impl *path = (rt_path2d_impl *)obj;
    free(path->points);
}

/// @brief Allocate a Path2D with the given initial point capacity.
/// @details Capacity is clamped by `initial_capacity`. On allocation failure
///          the impl is torn down cleanly and NULL is returned.
void *rt_path2d_new(int64_t capacity) {
    rt_path2d_impl *path = (rt_path2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_path2d_impl));
    if (!path)
        return NULL;
    path->capacity = initial_capacity(capacity);
    path->points = (rt_path2d_point *)calloc((size_t)path->capacity, sizeof(*path->points));
    if (!path->points) {
        if (rt_obj_release_check0(path))
            rt_obj_free(path);
        return NULL;
    }
    rt_obj_set_finalizer(path, path2d_finalize);
    return path;
}

/// @brief Grow the point-buffer to fit at least `needed` entries.
/// @details Standard doubling-growth with element-count and byte-count overflow
///          guards. Tail is zero-initialized so a failed-mid-append sequence
///          doesn't leave stale data in unused slots.
static int32_t path2d_reserve(rt_path2d_impl *path, int64_t needed) {
    if (!path || needed <= path->capacity)
        return 1;
    int64_t cap = path->capacity > 0 ? path->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_path2d_point))
        return 0;
    rt_path2d_point *points =
        (rt_path2d_point *)realloc(path->points, (size_t)cap * sizeof(*points));
    if (!points)
        return 0;
    memset(points + path->capacity, 0, (size_t)(cap - path->capacity) * sizeof(*points));
    path->points = points;
    path->capacity = cap;
    return 1;
}

/// @brief Append a point to the path, growing the backing buffer as needed.
/// @param move  1 for a pen-up move (new subpath start); 0 for a pen-down line segment.
static void path2d_add(void *path, int64_t x, int64_t y, int32_t move) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl)
        return;
    if (!path2d_reserve(impl, impl->count + 1)) {
        rt_trap("Path2D: capacity overflow");
        return;
    }
    rt_path2d_point *point = &impl->points[impl->count++];
    point->x = x;
    point->y = y;
    point->move = move;
}

/// @brief Reset the path to empty without freeing the backing buffer.
void rt_path2d_clear(void *path) {
    if (path)
        ((rt_path2d_impl *)path)->count = 0;
}

/// @brief Append a pen-up move point — starts a new subpath at `(x, y)`.
/// @details No line is drawn from the previous point to this one; `DrawToPixels`
///          skips across move-flagged points when iterating.
void rt_path2d_move_to(void *path, int64_t x, int64_t y) {
    path2d_add(path, x, y, 1);
}

/// @brief Append a pen-down line point — a line will be drawn from the previous
///        point to `(x, y)` during `DrawToPixels`.
void rt_path2d_line_to(void *path, int64_t x, int64_t y) {
    path2d_add(path, x, y, 0);
}

/// @brief Return the number of points (both move and line) in the path.
int64_t rt_path2d_count(void *path) {
    return path ? ((rt_path2d_impl *)path)->count : 0;
}

/// @brief Return the X coordinate of the point at `index`, or 0 if out of range.
int64_t rt_path2d_get_x(void *path, int64_t index) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].x;
}

/// @brief Return the Y coordinate of the point at `index`, or 0 if out of range.
int64_t rt_path2d_get_y(void *path, int64_t index) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].y;
}

/// @brief Render the path as connected line segments into a Pixels buffer.
/// @details Walks the point list and draws a line for every non-move pair.
///          The first point is always treated as a move (no line drawn to it);
///          subsequent move points introduce pen-up breaks so you can emit
///          multiple disconnected subpaths from the same Path2D. Colors accept
///          either `0xRRGGBBAA` or `0x00RRGGBB` form via `draw_rgb`.
void rt_path2d_draw_to_pixels(void *path, void *pixels, int64_t rgba) {
    rt_path2d_impl *impl = (rt_path2d_impl *)path;
    if (!impl || !pixels || impl->count < 2)
        return;
    int64_t color = draw_rgb(rgba);
    for (int64_t i = 1; i < impl->count; i++) {
        if (impl->points[i].move)
            continue;
        rt_pixels_draw_line(pixels,
                            impl->points[i - 1].x,
                            impl->points[i - 1].y,
                            impl->points[i].x,
                            impl->points[i].y,
                            color);
    }
}

//=============================================================================
// ShapeRenderer2D
//=============================================================================
// Stateful line / rect / circle / path drawer. Holds current stroke + fill
// colors; drawing ops write directly into a Pixels buffer via the shared
// `rt_pixels_draw_*` primitives. A negative color (the default for `fill`)
// skips that half of the draw — e.g. rect with `fill = -1` is stroke-only.

/// @brief Allocate a ShapeRenderer2D with default colors (white stroke, no fill).
void *rt_shaperenderer2d_new(void) {
    rt_shaperenderer2d_impl *renderer =
        (rt_shaperenderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_shaperenderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->stroke = 0x00FFFFFF;
    renderer->fill = -1;
    return renderer;
}

/// @brief Set the stroke (outline) color; pass any negative value to disable stroke.
void rt_shaperenderer2d_set_stroke(void *renderer, int64_t rgba) {
    if (renderer)
        ((rt_shaperenderer2d_impl *)renderer)->stroke = rgba;
}

/// @brief Set the fill color; pass any negative value to disable fill (stroke-only mode).
void rt_shaperenderer2d_set_fill(void *renderer, int64_t rgba) {
    if (renderer)
        ((rt_shaperenderer2d_impl *)renderer)->fill = rgba;
}

/// @brief Draw a line from `(x0, y0)` to `(x1, y1)` using the current stroke color.
/// @details Uses `rt_pixels_draw_line`; no-op if stroke is negative or any pointer is NULL.
void rt_shaperenderer2d_line(
    void *renderer, void *pixels, int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    if (!renderer || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->stroke < 0)
        return;
    rt_pixels_draw_line(pixels, x0, y0, x1, y1, draw_rgb(impl->stroke));
}

/// @brief Draw a rectangle at `(x, y)` with `width × height`.
/// @details Draws a filled solid rect first (if fill >= 0), then a 1-px
///          outline frame on top (if stroke >= 0), so the stroke is always
///          visible over the fill color.
void rt_shaperenderer2d_rect(
    void *renderer, void *pixels, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!renderer || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->fill >= 0)
        rt_pixels_draw_box(pixels, x, y, width, height, draw_rgb(impl->fill));
    if (impl->stroke >= 0)
        rt_pixels_draw_frame(pixels, x, y, width, height, draw_rgb(impl->stroke));
}

/// @brief Draw a circle at `(x, y)` with the given `radius`.
/// @details Same fill-then-stroke ordering as `rt_shaperenderer2d_rect`.
///          Fill uses `rt_pixels_draw_disc`; stroke uses `rt_pixels_draw_ring`.
void rt_shaperenderer2d_circle(void *renderer, void *pixels, int64_t x, int64_t y, int64_t radius) {
    if (!renderer || !pixels)
        return;
    rt_shaperenderer2d_impl *impl = (rt_shaperenderer2d_impl *)renderer;
    if (impl->fill >= 0)
        rt_pixels_draw_disc(pixels, x, y, radius, draw_rgb(impl->fill));
    if (impl->stroke >= 0)
        rt_pixels_draw_ring(pixels, x, y, radius, draw_rgb(impl->stroke));
}

/// @brief Render a Path2D into `pixels` using the current stroke color.
/// @details Delegates entirely to `rt_path2d_draw_to_pixels`; fill is ignored
///          for paths (paths have no closed-region fill support).
void rt_shaperenderer2d_path(void *renderer, void *pixels, void *path) {
    if (!renderer || !pixels || !path)
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
    rt_textrenderer2d_impl *renderer = (rt_textrenderer2d_impl *)obj;
    release_ref_slot(&renderer->font);
}

/// @brief Allocate a TextRenderer2D with default state (no font, 1x scale, white).
void *rt_textrenderer2d_new(void) {
    rt_textrenderer2d_impl *renderer =
        (rt_textrenderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_textrenderer2d_impl));
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
    if (!renderer)
        return;
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    retain_ref(font);
    release_ref_slot(&impl->font);
    impl->font = font;
}

/// @brief Set the integer pixel scale factor; clamped to [1, 64].
void rt_textrenderer2d_set_scale(void *renderer, int64_t scale) {
    if (renderer)
        ((rt_textrenderer2d_impl *)renderer)->scale = clamp_i64(scale, 1, 64);
}

/// @brief Set the text color as a packed 0x00RRGGBB value (alpha bits are masked off).
void rt_textrenderer2d_set_color(void *renderer, int64_t rgb) {
    if (renderer)
        ((rt_textrenderer2d_impl *)renderer)->color = rgb & 0x00FFFFFF;
}

/// @brief Measure the pixel width of `text` using the bound font and scale.
/// @details Falls back to `rt_canvas_text_width` when no BitmapFont is bound.
///          Width is multiplied by scale using saturating arithmetic to avoid overflow.
int64_t rt_textrenderer2d_measure_width(void *renderer, rt_string text) {
    if (!renderer)
        return rt_canvas_text_width(text);
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    int64_t width =
        impl->font ? rt_bitmapfont_text_width(impl->font, text) : rt_canvas_text_width(text);
    return saturating_mul_i64(width, impl->scale);
}

/// @brief Measure the pixel height of one line of text with the bound font and scale.
/// @details The `text` argument is ignored — line height is font-uniform, not
///          string-dependent. Falls back to `rt_canvas_text_height` when no font is bound.
int64_t rt_textrenderer2d_measure_height(void *renderer, rt_string text) {
    (void)text;
    if (!renderer)
        return rt_canvas_text_height();
    rt_textrenderer2d_impl *impl = (rt_textrenderer2d_impl *)renderer;
    int64_t height = impl->font ? rt_bitmapfont_text_height(impl->font) : rt_canvas_text_height();
    return saturating_mul_i64(height, impl->scale);
}

/// @brief Draw `text` at `(x, y)` into `canvas` using the bound font, scale, and color.
/// @details If a BitmapFont is bound, uses `rt_canvas_text_font_scaled`; otherwise
///          falls back to `rt_canvas_text_scaled` with the built-in Canvas font.
void rt_textrenderer2d_draw(void *renderer, void *canvas, int64_t x, int64_t y, rt_string text) {
    if (!renderer || !canvas)
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
    rt_sdffont_impl *font = (rt_sdffont_impl *)obj;
    release_ref_slot(&font->bitmap_font);
}

/// @brief Wrap a BitmapFont as an SdfFont with the given SDF spread parameter.
/// @details `spread` is clamped to `[1, 64]`. Consumers that support real SDF
///          rendering will use `spread` directly; the current bitmap-backed
///          implementation records it but ignores it at draw time.
void *rt_sdffont_new(void *bitmap_font, int64_t spread) {
    rt_sdffont_impl *font = (rt_sdffont_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sdffont_impl));
    if (!font)
        return NULL;
    font->bitmap_font = bitmap_font;
    font->spread = clamp_i64(spread, 1, 64);
    retain_ref(bitmap_font);
    rt_obj_set_finalizer(font, sdffont_finalize);
    return font;
}

/// @brief Return the underlying BitmapFont pointer (not retained — caller must not release it).
void *rt_sdffont_get_bitmap_font(void *font) {
    return font ? ((rt_sdffont_impl *)font)->bitmap_font : NULL;
}

/// @brief Return the SDF spread value stored at construction time (range [1, 64]).
int64_t rt_sdffont_get_spread(void *font) {
    return font ? ((rt_sdffont_impl *)font)->spread : 0;
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
    rt_nineslice2d_impl *slice = (rt_nineslice2d_impl *)obj;
    release_ref_slot(&slice->pixels);
}

/// @brief Wrap a source Pixels image as a nine-slice with the given border widths.
/// @details Border widths are clamped to `[0, image_dim]` so passing e.g. a
///          border larger than the image falls back to the whole image edge.
///          The caller retains ownership of their `pixels` reference; this
///          constructor takes its own.
void *rt_nineslice2d_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom) {
    if (!pixels)
        return NULL;
    rt_nineslice2d_impl *slice =
        (rt_nineslice2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_nineslice2d_impl));
    if (!slice)
        return NULL;
    slice->pixels = pixels;
    slice->left = clamp_i64(left, 0, rt_pixels_width(pixels));
    slice->top = clamp_i64(top, 0, rt_pixels_height(pixels));
    slice->right = clamp_i64(right, 0, rt_pixels_width(pixels));
    slice->bottom = clamp_i64(bottom, 0, rt_pixels_height(pixels));
    retain_ref(pixels);
    rt_obj_set_finalizer(slice, nineslice2d_finalize);
    return slice;
}

/// @brief Copy one rectangular region from `source` into `target`, scaling as
///        needed. Used by the nine-slice draw to place each of the 9 sub-rects.
/// @details Fast path when source and destination sizes match — a direct
///          `blit_pixels` with no intermediate allocation. When they don't,
///          allocates a temporary region copy (via `copy_region_pixels`),
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
        blit_pixels(target, dx, dy, source, sx, sy, sw, sh, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
        return;
    }
    void *region = copy_region_pixels(source, sx, sy, sw, sh);
    if (!region)
        return;
    void *scaled = rt_pixels_scale(region, dw, dh);
    if (scaled)
        blit_pixels(target, dx, dy, scaled, 0, 0, dw, dh, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
    release_ref_slot(&scaled);
    release_ref_slot(&region);
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
    if (!slice || !target || width <= 0 || height <= 0)
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

    nineslice_copy_scaled(target, x, y, dl, dt, impl->pixels, 0, 0, sl, st);
    nineslice_copy_scaled(target, x + dl, y, dcw, dt, impl->pixels, sl, 0, scw, st);
    nineslice_copy_scaled(target, x + dl + dcw, y, dr, dt, impl->pixels, sl + scw, 0, sr, st);

    nineslice_copy_scaled(target, x, y + dt, dl, dch, impl->pixels, 0, st, sl, sch);
    nineslice_copy_scaled(target, x + dl, y + dt, dcw, dch, impl->pixels, sl, st, scw, sch);
    nineslice_copy_scaled(
        target, x + dl + dcw, y + dt, dr, dch, impl->pixels, sl + scw, st, sr, sch);

    nineslice_copy_scaled(target, x, y + dt + dch, dl, db, impl->pixels, 0, st + sch, sl, sb);
    nineslice_copy_scaled(
        target, x + dl, y + dt + dch, dcw, db, impl->pixels, sl, st + sch, scw, sb);
    nineslice_copy_scaled(
        target, x + dl + dcw, y + dt + dch, dr, db, impl->pixels, sl + scw, st + sch, sr, sb);
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
    rt_debugdraw2d_impl *debug_draw = (rt_debugdraw2d_impl *)obj;
    free(debug_draw->cmds);
}

/// @brief Allocate a DebugDraw2D with the given initial command-buffer capacity.
/// @details Capacity is clamped by `initial_capacity` (floor 16, ceiling 1Mi).
///          Returns NULL on allocation failure.
void *rt_debugdraw2d_new(int64_t capacity) {
    rt_debugdraw2d_impl *debug_draw =
        (rt_debugdraw2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_debugdraw2d_impl));
    if (!debug_draw)
        return NULL;
    debug_draw->capacity = initial_capacity(capacity);
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
    if (debug_draw)
        ((rt_debugdraw2d_impl *)debug_draw)->count = 0;
}

/// @brief Return the number of commands currently queued.
int64_t rt_debugdraw2d_count(void *debug_draw) {
    return debug_draw ? ((rt_debugdraw2d_impl *)debug_draw)->count : 0;
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
    debugdraw2d_add((rt_debugdraw2d_impl *)debug_draw, 1, x0, y0, x1, y1, 0, rgba);
}

/// @brief Queue a rectangle outline at `(x, y)` with `width × height` and `rgba` color (type=2).
void rt_debugdraw2d_rect(
    void *debug_draw, int64_t x, int64_t y, int64_t width, int64_t height, int64_t rgba) {
    debugdraw2d_add((rt_debugdraw2d_impl *)debug_draw, 2, x, y, width, height, 0, rgba);
}

/// @brief Queue a circle outline at `(x, y)` with the given `radius` and `rgba` color (type=3).
void rt_debugdraw2d_circle(void *debug_draw, int64_t x, int64_t y, int64_t radius, int64_t rgba) {
    debugdraw2d_add((rt_debugdraw2d_impl *)debug_draw, 3, x, y, 0, 0, radius, rgba);
}

/// @brief Flush all queued commands into `pixels`.
/// @details Iterates the command list and dispatches to the appropriate
///          `rt_pixels_draw_*` primitive by command type:
///          - type 1 → `rt_pixels_draw_line`
///          - type 2 → `rt_pixels_draw_frame` (outline rect)
///          - type 3 → `rt_pixels_draw_ring` (outline circle)
///          Unknown types are silently skipped.
void rt_debugdraw2d_draw_to_pixels(void *debug_draw, void *pixels) {
    rt_debugdraw2d_impl *impl = (rt_debugdraw2d_impl *)debug_draw;
    if (!impl || !pixels)
        return;
    for (int64_t i = 0; i < impl->count; i++) {
        rt_debugdraw2d_cmd *cmd = &impl->cmds[i];
        int64_t color = draw_rgb(cmd->color);
        if (cmd->type == 1)
            rt_pixels_draw_line(pixels, cmd->x0, cmd->y0, cmd->x1, cmd->y1, color);
        else if (cmd->type == 2)
            rt_pixels_draw_frame(pixels, cmd->x0, cmd->y0, cmd->x1, cmd->y1, color);
        else if (cmd->type == 3)
            rt_pixels_draw_ring(pixels, cmd->x0, cmd->y0, cmd->value, color);
    }
}

//=============================================================================
// Additional 2D Game Graphics Helpers
//=============================================================================

typedef struct {
    int64_t x;
    int64_t y;
    int64_t scale_x;
    int64_t scale_y;
    int64_t rotation;
    int64_t origin_x;
    int64_t origin_y;
} rt_transform2d_impl;

typedef struct {
    int64_t filter;
    int64_t wrap;
} rt_sampler2d_impl;

typedef struct {
    int64_t blend_mode;
    int64_t tint;
    int64_t alpha;
} rt_blendstate2d_impl;

typedef struct {
    void *material;
    void *sampler;
    void *blend_state;
} rt_spriterenderer2d_impl;

typedef struct {
    int64_t chunk_width;
    int64_t chunk_height;
    int64_t dirty_count;
} rt_tilechunkcache2d_impl;

typedef struct {
    void *cache;
    int64_t draw_count;
} rt_tilemaprenderer2d_impl;

typedef struct {
    int64_t start_frame;
    int64_t frame_count;
    int64_t frame_delay_ms;
    int64_t loop;
} rt_animationclip2d_impl;

typedef struct {
    void *sprite;
    void *clip;
    int64_t elapsed_ms;
    int64_t frame;
    int64_t playing;
} rt_animatedsprite2d_impl;

typedef struct {
    void *font;
    int64_t scale;
    int64_t wrap_width;
    int64_t alignment;
    int64_t color;
} rt_textlayout2d_impl;

typedef struct {
    void *source;
    void *target;
    void *shader;
    int64_t enabled;
} rt_renderpass2d_impl;

typedef struct {
    void **passes;
    int64_t count;
    int64_t capacity;
} rt_rendergraph2d_impl;

typedef struct {
    int64_t width;
    int64_t height;
    uint8_t *bits;
} rt_collisionmask2d_impl;

typedef struct {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
} rt_hitbox2d_impl;

typedef struct {
    uint32_t colors[256];
    int64_t count;
} rt_palette2d_impl;

typedef struct {
    uint32_t start;
    uint32_t end;
    int64_t steps;
} rt_gradient2d_impl;

typedef struct {
    void *camera;
    int64_t target_x;
    int64_t target_y;
    int64_t smoothing;
    int64_t shake_x;
    int64_t shake_y;
} rt_camerarig2d_impl;

typedef struct {
    void *atlas;
} rt_texturepackeratlas_impl;

typedef struct {
    int64_t frame_width;
    int64_t frame_height;
} rt_asepriteimporter_impl;

typedef struct {
    int64_t tile_width;
    int64_t tile_height;
} rt_tiledmaploader_impl;

//=============================================================================
// Second-tier classes: Transform2D / CollisionMask2D / HitBox2D / Palette2D /
// Gradient2D / CameraRig2D / Atlas importers / TileChunkCache2D /
// TilemapRenderer2D / AnimationClip2D / AnimatedSprite2D / TextLayout2D /
// RenderPass2D / RenderGraph2D
//=============================================================================
// These are lower-traffic utility classes layered on top of the primary 2D
// surface. Transform2D is a 2×3 affine builder (used by Renderer2D callers
// that want rotated / scaled draws); CollisionMask2D is a packed bit grid;
// the atlas importers are data-format adapters for external tooling; the
// CameraRig2D wraps Camera with shake/target-follow. Scale values use the
// same 100 == 1.0x fixed-point convention as Viewport2D's 1000 == 1.0x,
// except Transform2D scales are per-axis integers with 100 meaning "identity."

/// @brief Round a double to the nearest int64, away from zero.
static int64_t round_double_to_i64(double value) {
    if (!isfinite(value))
        return value < 0.0 ? INT64_MIN : INT64_MAX;
    if (value >= (double)INT64_MAX)
        return INT64_MAX;
    if (value <= (double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

/// @brief Apply a Transform2D to a point: translate, pivot around origin, scale,
///        rotate, translate back.
/// @details The transformation order matters. Each input point is:
///          1. Shifted into origin-relative space (`(x - origin_x, y - origin_y)`).
///          2. Scaled per-axis (fixed-point: `100` == 1.0x).
///          3. Rotated by `rotation` degrees.
///          4. Shifted back by `origin_x + x` (position plus origin).
///          Rotation uses double-precision `cos/sin` since the `PI/180`
///          conversion already forces a float round-trip. Final coordinates
///          are rounded away from zero via `round_double_to_i64` so a point
///          that lands *exactly* between pixels snaps consistently
///          regardless of sign. Passing a NULL transform is a no-op —
///          input point is copied straight to output (identity).
static void transform2d_point(
    rt_transform2d_impl *impl, int64_t x, int64_t y, int64_t *out_x, int64_t *out_y) {
    if (!impl) {
        if (out_x)
            *out_x = x;
        if (out_y)
            *out_y = y;
        return;
    }

    double px = ((double)x - (double)impl->origin_x) * (double)impl->scale_x / 100.0;
    double py = ((double)y - (double)impl->origin_y) * (double)impl->scale_y / 100.0;
    double radians = (double)impl->rotation * RT2D_PI / 180.0;
    double c = cos(radians);
    double s = sin(radians);
    double rx = px * c - py * s;
    double ry = px * s + py * c;

    if (out_x)
        *out_x = saturating_add_i64(saturating_add_i64(impl->x, impl->origin_x),
                                    round_double_to_i64(rx));
    if (out_y)
        *out_y = saturating_add_i64(saturating_add_i64(impl->y, impl->origin_y),
                                    round_double_to_i64(ry));
}

/// @brief Allocate a Transform2D with identity state (pos=0, scale=100, rotation=0, origin=0).
/// @details Scale is initialized to 100, which equals 1.0× in the fixed-point convention
///          used throughout (100 == 1.0×, 200 == 2.0×, 50 == 0.5×).
void *rt_transform2d_new(void) {
    rt_transform2d_impl *impl =
        (rt_transform2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_transform2d_impl));
    if (!impl)
        return NULL;
    impl->scale_x = 100;
    impl->scale_y = 100;
    return impl;
}

/// @brief Return the X world-space position of the transform.
int64_t rt_transform2d_get_x(void *transform) {
    return transform ? ((rt_transform2d_impl *)transform)->x : 0;
}

/// @brief Set the X world-space position of the transform.
void rt_transform2d_set_x(void *transform, int64_t x) {
    if (transform)
        ((rt_transform2d_impl *)transform)->x = x;
}

/// @brief Return the Y world-space position of the transform.
int64_t rt_transform2d_get_y(void *transform) {
    return transform ? ((rt_transform2d_impl *)transform)->y : 0;
}

/// @brief Set the Y world-space position of the transform.
void rt_transform2d_set_y(void *transform, int64_t y) {
    if (transform)
        ((rt_transform2d_impl *)transform)->y = y;
}

/// @brief Return the X scale in fixed-point units (100 == 1.0×).
int64_t rt_transform2d_get_scale_x(void *transform) {
    return transform ? ((rt_transform2d_impl *)transform)->scale_x : 100;
}

/// @brief Set the X scale; clamped to [1, 10000] (0.01× to 100×).
void rt_transform2d_set_scale_x(void *transform, int64_t scale_x) {
    if (transform)
        ((rt_transform2d_impl *)transform)->scale_x = clamp_i64(scale_x, 1, 10000);
}

/// @brief Return the Y scale in fixed-point units (100 == 1.0×).
int64_t rt_transform2d_get_scale_y(void *transform) {
    return transform ? ((rt_transform2d_impl *)transform)->scale_y : 100;
}

/// @brief Set the Y scale; clamped to [1, 10000].
void rt_transform2d_set_scale_y(void *transform, int64_t scale_y) {
    if (transform)
        ((rt_transform2d_impl *)transform)->scale_y = clamp_i64(scale_y, 1, 10000);
}

/// @brief Return the rotation in integer degrees (no clamping — can be any multiple of 1°).
int64_t rt_transform2d_get_rotation(void *transform) {
    return transform ? ((rt_transform2d_impl *)transform)->rotation : 0;
}

/// @brief Set the rotation in integer degrees.
void rt_transform2d_set_rotation(void *transform, int64_t degrees) {
    if (transform)
        ((rt_transform2d_impl *)transform)->rotation = degrees;
}

/// @brief Set both X and Y position in one call.
void rt_transform2d_set_position(void *transform, int64_t x, int64_t y) {
    if (!transform)
        return;
    rt_transform2d_impl *impl = (rt_transform2d_impl *)transform;
    impl->x = x;
    impl->y = y;
}

/// @brief Set X and Y scale in one call (each clamped to [1, 10000]).
void rt_transform2d_set_scale(void *transform, int64_t scale_x, int64_t scale_y) {
    rt_transform2d_set_scale_x(transform, scale_x);
    rt_transform2d_set_scale_y(transform, scale_y);
}

/// @brief Set the pivot / rotation origin as an offset from the transform position.
void rt_transform2d_set_origin(void *transform, int64_t x, int64_t y) {
    if (!transform)
        return;
    rt_transform2d_impl *impl = (rt_transform2d_impl *)transform;
    impl->origin_x = x;
    impl->origin_y = y;
}

/// @brief Translate (move) the transform by `(dx, dy)` using saturating addition.
void rt_transform2d_translate(void *transform, int64_t dx, int64_t dy) {
    if (!transform)
        return;
    rt_transform2d_impl *impl = (rt_transform2d_impl *)transform;
    impl->x = saturating_add_i64(impl->x, dx);
    impl->y = saturating_add_i64(impl->y, dy);
}

/// @brief Return the transformed X coordinate for input point `(x, y)`.
/// @details Delegates to `transform2d_point`; useful when callers only need the X component.
int64_t rt_transform2d_transform_x(void *transform, int64_t x, int64_t y) {
    int64_t out_x = x;
    transform2d_point((rt_transform2d_impl *)transform, x, y, &out_x, NULL);
    return out_x;
}

/// @brief Return the transformed Y coordinate for input point `(x, y)`.
/// @details Delegates to `transform2d_point`; useful when callers only need the Y component.
int64_t rt_transform2d_transform_y(void *transform, int64_t x, int64_t y) {
    int64_t out_y = y;
    transform2d_point((rt_transform2d_impl *)transform, x, y, NULL, &out_y);
    return out_y;
}

/// @brief Allocate a Sampler2D with default state (nearest filter, clamp wrap).
void *rt_sampler2d_new(void) {
    rt_sampler2d_impl *impl =
        (rt_sampler2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_sampler2d_impl));
    if (!impl)
        return NULL;
    impl->filter = RT_GRAPHICS2D_FILTER_NEAREST;
    impl->wrap = RT_GRAPHICS2D_WRAP_CLAMP;
    return impl;
}

/// @brief Set the filter mode; unrecognised values fall back to NEAREST.
void rt_sampler2d_set_filter(void *sampler, int64_t filter) {
    if (sampler)
        ((rt_sampler2d_impl *)sampler)->filter = filter == RT_GRAPHICS2D_FILTER_LINEAR
                                                     ? RT_GRAPHICS2D_FILTER_LINEAR
                                                     : RT_GRAPHICS2D_FILTER_NEAREST;
}

/// @brief Return the current filter mode (NEAREST or LINEAR).
int64_t rt_sampler2d_get_filter(void *sampler) {
    return sampler ? ((rt_sampler2d_impl *)sampler)->filter : RT_GRAPHICS2D_FILTER_NEAREST;
}

/// @brief Set the wrap mode; unrecognised values fall back to CLAMP.
void rt_sampler2d_set_wrap(void *sampler, int64_t wrap) {
    if (sampler)
        ((rt_sampler2d_impl *)sampler)->wrap = wrap == RT_GRAPHICS2D_WRAP_REPEAT
                                                   ? RT_GRAPHICS2D_WRAP_REPEAT
                                                   : RT_GRAPHICS2D_WRAP_CLAMP;
}

/// @brief Return the current wrap mode (CLAMP or REPEAT).
int64_t rt_sampler2d_get_wrap(void *sampler) {
    return sampler ? ((rt_sampler2d_impl *)sampler)->wrap : RT_GRAPHICS2D_WRAP_CLAMP;
}

/// @brief Push the sampler's filter and wrap settings into a Texture2D.
/// @details Convenience combinator — equivalent to calling `SetFilter` + `SetWrap`
///          on the texture directly, but expressed through the sampler's parameters.
void rt_sampler2d_apply_to_texture(void *sampler, void *texture) {
    if (!sampler || !texture)
        return;
    rt_texture2d_set_filter(texture, rt_sampler2d_get_filter(sampler));
    rt_texture2d_set_wrap(texture, rt_sampler2d_get_wrap(sampler));
}

/// @brief Allocate a BlendState2D with default state (alpha blend, no tint, full opacity).
/// @details `tint` defaults to -1 meaning "no tint override"; `alpha` defaults to 255.
void *rt_blendstate2d_new(void) {
    rt_blendstate2d_impl *impl =
        (rt_blendstate2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_blendstate2d_impl));
    if (!impl)
        return NULL;
    impl->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    impl->tint = -1;
    impl->alpha = 255;
    return impl;
}

/// @brief Set the blend mode; clamped to [0, 2] (NONE / ALPHA / ADDITIVE).
void rt_blendstate2d_set_blend_mode(void *state, int64_t blend_mode) {
    if (state)
        ((rt_blendstate2d_impl *)state)->blend_mode = clamp_i64(blend_mode, 0, 2);
}

/// @brief Return the current blend mode.
int64_t rt_blendstate2d_get_blend_mode(void *state) {
    return state ? ((rt_blendstate2d_impl *)state)->blend_mode : RT_GRAPHICS2D_BLEND_ALPHA;
}

/// @brief Set the tint as 0x00RRGGBB; pass a negative value to disable tint.
void rt_blendstate2d_set_tint(void *state, int64_t rgb) {
    if (state)
        ((rt_blendstate2d_impl *)state)->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

/// @brief Return the tint color (0x00RRGGBB), or -1 if no tint is set.
int64_t rt_blendstate2d_get_tint(void *state) {
    return state ? ((rt_blendstate2d_impl *)state)->tint : -1;
}

/// @brief Set the global alpha in [0, 255]; values are clamped.
void rt_blendstate2d_set_alpha(void *state, int64_t alpha) {
    if (state)
        ((rt_blendstate2d_impl *)state)->alpha = clamp_u8_i64(alpha);
}

/// @brief Return the current global alpha in [0, 255].
int64_t rt_blendstate2d_get_alpha(void *state) {
    return state ? ((rt_blendstate2d_impl *)state)->alpha : 255;
}

/// @brief Push blend mode, alpha, and tint from this state into a Renderer2D.
/// @details Equivalent to three separate `SetBlendMode` / `SetAlpha` / `SetTint`
///          calls, combined into a single apply step.
void rt_blendstate2d_apply_to_renderer(void *state, void *renderer) {
    if (!state || !renderer)
        return;
    rt_renderer2d_set_blend_mode(renderer, rt_blendstate2d_get_blend_mode(state));
    rt_renderer2d_set_alpha(renderer, rt_blendstate2d_get_alpha(state));
    rt_renderer2d_set_tint(renderer, rt_blendstate2d_get_tint(state));
}

/// @brief GC finalizer — releases retained references to material, sampler, and blend state.
static void spriterenderer2d_finalize(void *obj) {
    rt_spriterenderer2d_impl *impl = (rt_spriterenderer2d_impl *)obj;
    release_ref_slot(&impl->material);
    release_ref_slot(&impl->sampler);
    release_ref_slot(&impl->blend_state);
}

/// @brief Allocate a SpriteRenderer2D with no material, sampler, or blend state bound.
void *rt_spriterenderer2d_new(void) {
    rt_spriterenderer2d_impl *impl =
        (rt_spriterenderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_spriterenderer2d_impl));
    if (!impl)
        return NULL;
    rt_obj_set_finalizer(impl, spriterenderer2d_finalize);
    return impl;
}

/// @brief Retain-before-release helper for all three sprite-renderer reference slots.
static void spriterenderer2d_set_ref(void **slot, void *value) {
    retain_ref(value);
    release_ref_slot(slot);
    *slot = value;
}

/// @brief Bind a Material2D to this renderer, retaining a reference.
/// @details When set, the material's tint, alpha, and blend-mode override the
///          renderer state each time `DrawPixels` or `DrawTexture` is called.
void rt_spriterenderer2d_set_material(void *sprite_renderer, void *material) {
    if (sprite_renderer)
        spriterenderer2d_set_ref(&((rt_spriterenderer2d_impl *)sprite_renderer)->material,
                                 material);
}

/// @brief Bind a Sampler2D to this renderer, retaining a reference.
/// @details When set, the sampler's filter and wrap settings override the texture's
///          own settings during `DrawTexture`.
void rt_spriterenderer2d_set_sampler(void *sprite_renderer, void *sampler) {
    if (sprite_renderer)
        spriterenderer2d_set_ref(&((rt_spriterenderer2d_impl *)sprite_renderer)->sampler, sampler);
}

/// @brief Bind a BlendState2D to this renderer, retaining a reference.
/// @details BlendState overrides are applied *after* material overrides, so the
///          blend state wins if both are bound and specify the same property.
void rt_spriterenderer2d_set_blend_state(void *sprite_renderer, void *blend_state) {
    if (sprite_renderer)
        spriterenderer2d_set_ref(&((rt_spriterenderer2d_impl *)sprite_renderer)->blend_state,
                                 blend_state);
}

/// @brief Push material and blend-state overrides from the sprite renderer onto the renderer.
/// @details When a material is assigned, its tint, alpha, and blend mode are forwarded to
///          @p renderer. An explicit blend state then layers on top via
///          `rt_blendstate2d_apply_to_renderer`. Called before every draw operation so the
///          renderer reflects the sprite renderer's current configuration.
static void spriterenderer2d_apply_state(rt_spriterenderer2d_impl *impl, void *renderer) {
    if (!impl || !renderer)
        return;
    if (impl->material) {
        rt_renderer2d_set_tint(renderer, rt_material2d_get_tint(impl->material));
        rt_renderer2d_set_alpha(renderer, rt_material2d_get_alpha(impl->material));
        rt_renderer2d_set_blend_mode(renderer, rt_material2d_get_blend_mode(impl->material));
    }
    if (impl->blend_state)
        rt_blendstate2d_apply_to_renderer(impl->blend_state, renderer);
}

/// @brief Apply state and draw a Pixels buffer into the Renderer2D at `(x, y)`.
/// @details Pushes material/blend-state overrides onto the renderer, then issues
///          `rt_renderer2d_draw_pixels`. The renderer's prior tint/alpha/blend-mode
///          are replaced for this draw and remain changed until the caller resets them.
void rt_spriterenderer2d_draw_pixels(
    void *sprite_renderer, void *renderer, void *pixels, int64_t x, int64_t y) {
    spriterenderer2d_apply_state((rt_spriterenderer2d_impl *)sprite_renderer, renderer);
    rt_renderer2d_draw_pixels(renderer, pixels, x, y);
}

/// @brief Apply state and draw a Texture2D into the Renderer2D at `(x, y)`.
/// @details Same state-push as `DrawPixels`, but also resolves filter/wrap from
///          the bound sampler (if any) before queuing the texture draw command.
void rt_spriterenderer2d_draw_texture(
    void *sprite_renderer, void *renderer, void *texture, int64_t x, int64_t y) {
    rt_spriterenderer2d_impl *impl = (rt_spriterenderer2d_impl *)sprite_renderer;
    spriterenderer2d_apply_state(impl, renderer);
    int64_t filter = impl && impl->sampler ? rt_sampler2d_get_filter(impl->sampler)
                                           : rt_texture2d_get_filter(texture);
    int64_t wrap =
        impl && impl->sampler ? rt_sampler2d_get_wrap(impl->sampler) : rt_texture2d_get_wrap(texture);
    renderer2d_queue(renderer2d_checked(renderer),
                     1,
                     texture,
                     x,
                     y,
                     0,
                     0,
                     rt_texture2d_width(texture),
                     rt_texture2d_height(texture),
                     filter,
                     wrap);
}

/// @brief Allocate a TileChunkCache2D describing chunk dimensions for dirty-region tracking.
/// @details Chunk dimensions are sanitized via `normalized_dim`. Game code that modifies
///          tiles in a chunk calls `MarkDirty`; a renderer that caches per-chunk textures
///          checks `GetDirtyCount` and rebuilds stale chunks before drawing.
void *rt_tilechunkcache2d_new(int64_t chunk_width, int64_t chunk_height) {
    rt_tilechunkcache2d_impl *impl =
        (rt_tilechunkcache2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tilechunkcache2d_impl));
    if (!impl)
        return NULL;
    impl->chunk_width = normalized_dim(chunk_width);
    impl->chunk_height = normalized_dim(chunk_height);
    return impl;
}

/// @brief Return the chunk width in tiles.
int64_t rt_tilechunkcache2d_get_chunk_width(void *cache) {
    return cache ? ((rt_tilechunkcache2d_impl *)cache)->chunk_width : 0;
}

/// @brief Return the chunk height in tiles.
int64_t rt_tilechunkcache2d_get_chunk_height(void *cache) {
    return cache ? ((rt_tilechunkcache2d_impl *)cache)->chunk_height : 0;
}

/// @brief Increment the dirty counter — signals that at least one chunk needs rebuild.
void rt_tilechunkcache2d_mark_dirty(void *cache) {
    if (!cache)
        return;
    rt_tilechunkcache2d_impl *impl = (rt_tilechunkcache2d_impl *)cache;
    if (impl->dirty_count < INT64_MAX)
        impl->dirty_count++;
}

/// @brief Reset the dirty counter to zero after the renderer has processed all dirty chunks.
void rt_tilechunkcache2d_clear_dirty(void *cache) {
    if (cache)
        ((rt_tilechunkcache2d_impl *)cache)->dirty_count = 0;
}

/// @brief Return the number of pending dirty-chunk notifications.
int64_t rt_tilechunkcache2d_get_dirty_count(void *cache) {
    return cache ? ((rt_tilechunkcache2d_impl *)cache)->dirty_count : 0;
}

/// @brief GC finalizer — releases the retained TileChunkCache2D reference.
static void tilemaprenderer2d_finalize(void *obj) {
    rt_tilemaprenderer2d_impl *impl = (rt_tilemaprenderer2d_impl *)obj;
    release_ref_slot(&impl->cache);
}

/// @brief Allocate a TilemapRenderer2D with no chunk cache bound and zero draw count.
void *rt_tilemaprenderer2d_new(void) {
    rt_tilemaprenderer2d_impl *impl =
        (rt_tilemaprenderer2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tilemaprenderer2d_impl));
    if (!impl)
        return NULL;
    rt_obj_set_finalizer(impl, tilemaprenderer2d_finalize);
    return impl;
}

/// @brief Bind a TileChunkCache2D to this renderer, retaining a reference.
void rt_tilemaprenderer2d_set_chunk_cache(void *renderer, void *cache) {
    if (renderer)
        spriterenderer2d_set_ref(&((rt_tilemaprenderer2d_impl *)renderer)->cache, cache);
}

/// @brief Return the total number of draw calls issued since construction (or last reset).
int64_t rt_tilemaprenderer2d_get_draw_count(void *renderer) {
    return renderer ? ((rt_tilemaprenderer2d_impl *)renderer)->draw_count : 0;
}

/// @brief Draw the entire tilemap into `canvas` offset by `(offset_x, offset_y)`.
/// @details Delegates to `rt_tilemap_draw` and increments the internal draw counter.
void rt_tilemaprenderer2d_draw(
    void *renderer, void *tilemap, void *canvas, int64_t offset_x, int64_t offset_y) {
    if (!renderer || !tilemap || !canvas)
        return;
    rt_tilemap_draw(tilemap, canvas, offset_x, offset_y);
    ((rt_tilemaprenderer2d_impl *)renderer)->draw_count++;
}

/// @brief Draw a viewport-clipped region of the tilemap into `canvas`.
/// @details Delegates to `rt_tilemap_draw_region`, culling tiles outside the
///          `(view_x, view_y, view_w, view_h)` rectangle before drawing.
///          Increments the draw counter regardless.
void rt_tilemaprenderer2d_draw_region(void *renderer,
                                      void *tilemap,
                                      void *canvas,
                                      int64_t offset_x,
                                      int64_t offset_y,
                                      int64_t view_x,
                                      int64_t view_y,
                                      int64_t view_w,
                                      int64_t view_h) {
    if (!renderer || !tilemap || !canvas)
        return;
    rt_tilemap_draw_region(tilemap, canvas, offset_x, offset_y, view_x, view_y, view_w, view_h);
    ((rt_tilemaprenderer2d_impl *)renderer)->draw_count++;
}

/// @brief Allocate an AnimationClip2D — a frame-range description (starting
///        sprite frame, frame count, per-frame delay in ms, looping flag).
/// @details Defensive defaults for malformed inputs: negative `start_frame` → 0,
///          non-positive `frame_count` → 1 (single-frame clip), non-positive
///          `frame_delay_ms` → 100ms (~10 fps default). Clip data is immutable
///          after construction; callers that need different parameters build
///          a new clip rather than mutating.
void *rt_animationclip2d_new(int64_t start_frame,
                             int64_t frame_count,
                             int64_t frame_delay_ms,
                             int64_t loop) {
    rt_animationclip2d_impl *impl =
        (rt_animationclip2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_animationclip2d_impl));
    if (!impl)
        return NULL;
    impl->start_frame = start_frame < 0 ? 0 : start_frame;
    impl->frame_count = frame_count <= 0 ? 1 : frame_count;
    impl->frame_delay_ms = frame_delay_ms <= 0 ? 100 : frame_delay_ms;
    impl->loop = loop != 0;
    return impl;
}

/// @brief Return the first sprite frame index this clip plays from.
int64_t rt_animationclip2d_get_start_frame(void *clip) {
    return clip ? ((rt_animationclip2d_impl *)clip)->start_frame : 0;
}

/// @brief Return the number of sprite frames in this clip.
int64_t rt_animationclip2d_get_frame_count(void *clip) {
    return clip ? ((rt_animationclip2d_impl *)clip)->frame_count : 0;
}

/// @brief Return the per-frame delay in milliseconds.
int64_t rt_animationclip2d_get_frame_delay_ms(void *clip) {
    return clip ? ((rt_animationclip2d_impl *)clip)->frame_delay_ms : 0;
}

/// @brief Return non-zero if the clip loops back to its first frame after the last.
int64_t rt_animationclip2d_get_loop(void *clip) {
    return clip ? ((rt_animationclip2d_impl *)clip)->loop : 0;
}

/// @brief GC finalizer — releases retained references to the sprite and animation clip.
static void animatedsprite2d_finalize(void *obj) {
    rt_animatedsprite2d_impl *impl = (rt_animatedsprite2d_impl *)obj;
    release_ref_slot(&impl->sprite);
    release_ref_slot(&impl->clip);
}

/// @brief Allocate an AnimatedSprite2D wrapping `sprite`, ready to play clips.
/// @details Retains a reference to `sprite`. Starts with `playing = 1` so the
///          first `Update` call after `SetClip` immediately begins advancing frames.
void *rt_animatedsprite2d_new(void *sprite) {
    rt_animatedsprite2d_impl *impl =
        (rt_animatedsprite2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_animatedsprite2d_impl));
    if (!impl)
        return NULL;
    retain_ref(sprite);
    impl->sprite = sprite;
    impl->playing = 1;
    rt_obj_set_finalizer(impl, animatedsprite2d_finalize);
    return impl;
}

/// @brief Bind an AnimationClip2D and reset elapsed time and frame counter.
/// @details Calling `SetClip` with a non-NULL clip immediately seeks the bound
///          sprite to `clip->start_frame`, so the first drawn frame is always
///          consistent even before the first `Update`. Passing NULL stops playback.
void rt_animatedsprite2d_set_clip(void *animated_sprite, void *clip) {
    if (!animated_sprite)
        return;
    rt_animatedsprite2d_impl *impl = (rt_animatedsprite2d_impl *)animated_sprite;
    spriterenderer2d_set_ref(&impl->clip, clip);
    impl->elapsed_ms = 0;
    impl->frame = 0;
    impl->playing = clip != NULL;
    if (impl->sprite && clip)
        rt_sprite_set_frame(impl->sprite, rt_animationclip2d_get_start_frame(clip));
}

/// @brief Resume playback after `Stop` (or after a non-looping clip finished).
void rt_animatedsprite2d_play(void *animated_sprite) {
    if (animated_sprite)
        ((rt_animatedsprite2d_impl *)animated_sprite)->playing = 1;
}

/// @brief Pause playback; the current frame remains visible and elapsed time is preserved.
void rt_animatedsprite2d_stop(void *animated_sprite) {
    if (animated_sprite)
        ((rt_animatedsprite2d_impl *)animated_sprite)->playing = 0;
}

/// @brief Advance an AnimatedSprite2D by `delta_ms`, stepping through clip frames
///        and optionally looping at the end.
/// @details Accumulates `delta_ms` into `elapsed_ms` and consumes full `frame_delay`
///          chunks in a loop — this means a very long frame can skip ahead by
///          several frames in a single call rather than only advancing one, so
///          animation timing stays correct when the game stutters. Negative
///          `delta_ms` is clamped to 0 (no rewind).
///
///          End-of-clip behavior:
///          - **Looping clip:** wraps back to frame 0 and continues consuming
///            the remaining elapsed time.
///          - **Non-looping clip:** snaps to the last frame, clears `playing`,
///            and the `while` loop exits — any remaining elapsed time is
///            discarded. Replaying requires a fresh `SetClip` or `Play`.
///
///          The sprite's own frame index is updated via `rt_sprite_set_frame`
///          as we step, so consumers that just `Draw` the sprite see the
///          correct frame without having to query the AnimatedSprite2D.
void rt_animatedsprite2d_update(void *animated_sprite, int64_t delta_ms) {
    rt_animatedsprite2d_impl *impl = (rt_animatedsprite2d_impl *)animated_sprite;
    if (!impl || !impl->sprite || !impl->clip || !impl->playing)
        return;
    rt_animationclip2d_impl *clip = (rt_animationclip2d_impl *)impl->clip;
    int64_t sprite_frames = rt_sprite_get_frame_count(impl->sprite);
    if (sprite_frames <= 0 || clip->start_frame >= sprite_frames) {
        impl->playing = 0;
        impl->frame = 0;
        impl->elapsed_ms = 0;
        return;
    }
    int64_t effective_count = clip->frame_count;
    int64_t available = sprite_frames - clip->start_frame;
    if (effective_count > available)
        effective_count = available;
    if (effective_count <= 0) {
        impl->playing = 0;
        impl->frame = 0;
        impl->elapsed_ms = 0;
        return;
    }
    if (impl->frame < 0)
        impl->frame = 0;
    if (impl->frame >= effective_count)
        impl->frame = clip->loop ? impl->frame % effective_count : effective_count - 1;
    if (delta_ms < 0)
        delta_ms = 0;
    if (delta_ms > INT64_MAX - impl->elapsed_ms)
        impl->elapsed_ms = INT64_MAX;
    else
        impl->elapsed_ms += delta_ms;

    if (impl->elapsed_ms >= clip->frame_delay_ms) {
        int64_t steps = impl->elapsed_ms / clip->frame_delay_ms;
        impl->elapsed_ms %= clip->frame_delay_ms;
        if (clip->loop) {
            steps %= effective_count;
            impl->frame = (impl->frame + steps) % effective_count;
        } else {
            if (steps >= effective_count - impl->frame) {
                impl->frame = effective_count - 1;
                impl->playing = 0;
                impl->elapsed_ms = 0;
            } else {
                impl->frame += steps;
            }
        }
    }
    if (clip->start_frame <= INT64_MAX - impl->frame)
        rt_sprite_set_frame(impl->sprite, clip->start_frame + impl->frame);
}

/// @brief Return the current local frame index within the clip (not the sprite sheet index).
int64_t rt_animatedsprite2d_get_frame(void *animated_sprite) {
    return animated_sprite ? ((rt_animatedsprite2d_impl *)animated_sprite)->frame : 0;
}

/// @brief Return non-zero if the animation is currently playing.
int64_t rt_animatedsprite2d_is_playing(void *animated_sprite) {
    return animated_sprite ? ((rt_animatedsprite2d_impl *)animated_sprite)->playing : 0;
}

/// @brief GC finalizer — releases the retained BitmapFont reference.
static void textlayout2d_finalize(void *obj) {
    rt_textlayout2d_impl *impl = (rt_textlayout2d_impl *)obj;
    release_ref_slot(&impl->font);
}

/// @brief Allocate a TextLayout2D with defaults (scale=1, white, no wrap, left-align).
void *rt_textlayout2d_new(void) {
    rt_textlayout2d_impl *impl =
        (rt_textlayout2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_textlayout2d_impl));
    if (!impl)
        return NULL;
    impl->scale = 1;
    impl->color = 0x00FFFFFF;
    rt_obj_set_finalizer(impl, textlayout2d_finalize);
    return impl;
}

/// @brief Bind a BitmapFont, retaining a reference.
void rt_textlayout2d_set_font(void *layout, void *font) {
    if (layout)
        spriterenderer2d_set_ref(&((rt_textlayout2d_impl *)layout)->font, font);
}

/// @brief Set the integer pixel scale; clamped to [1, 64].
void rt_textlayout2d_set_scale(void *layout, int64_t scale) {
    if (layout)
        ((rt_textlayout2d_impl *)layout)->scale = clamp_i64(scale, 1, 64);
}

/// @brief Set the wrap width in pixels; 0 means no wrapping.
void rt_textlayout2d_set_wrap_width(void *layout, int64_t width) {
    if (layout)
        ((rt_textlayout2d_impl *)layout)->wrap_width = width < 0 ? 0 : width;
}

/// @brief Set text alignment: 0=left, 1=center, 2=right; clamped to [0, 2].
void rt_textlayout2d_set_alignment(void *layout, int64_t alignment) {
    if (layout)
        ((rt_textlayout2d_impl *)layout)->alignment = clamp_i64(alignment, 0, 2);
}

/// @brief Set the text color as 0x00RRGGBB (alpha bits are masked off).
void rt_textlayout2d_set_color(void *layout, int64_t rgb) {
    if (layout)
        ((rt_textlayout2d_impl *)layout)->color = rgb & 0x00FFFFFF;
}

/// @brief Compute the raw pixel width of @p text using the layout's font and scale, without wrap clamping.
/// @details Uses the layout's bitmap font if set; falls back to the canvas default font.
///          Width is multiplied by the layout's scale factor with saturation.
static int64_t textlayout2d_raw_width(rt_textlayout2d_impl *impl, rt_string text) {
    int64_t width = impl && impl->font ? rt_bitmapfont_text_width(impl->font, text)
                                       : rt_canvas_text_width(text);
    int64_t scale = impl ? impl->scale : 1;
    return saturating_mul_i64(width, scale);
}

/// @brief Measure the display width of `text` respecting the wrap width limit.
/// @details If wrapping is enabled and the raw pixel width exceeds `wrap_width`,
///          returns `wrap_width`; otherwise returns the unclipped pixel width.
int64_t rt_textlayout2d_measure_width(void *layout, rt_string text) {
    rt_textlayout2d_impl *impl = (rt_textlayout2d_impl *)layout;
    int64_t width = textlayout2d_raw_width(impl, text);
    if (impl && impl->wrap_width > 0 && width > impl->wrap_width)
        return impl->wrap_width;
    return width;
}

/// @brief Measure the total pixel height of `text`, accounting for line-wrapping.
/// @details Computes the number of wrapped lines as `ceil(raw_width / wrap_width)`
///          and multiplies by the scaled line height. Returns one line-height when
///          wrapping is off or the text fits on a single line.
int64_t rt_textlayout2d_measure_height(void *layout, rt_string text) {
    rt_textlayout2d_impl *impl = (rt_textlayout2d_impl *)layout;
    int64_t line_height =
        impl && impl->font ? rt_bitmapfont_text_height(impl->font) : rt_canvas_text_height();
    int64_t scale = impl ? impl->scale : 1;
    int64_t lines = 1;
    if (impl && impl->wrap_width > 0) {
        int64_t width = textlayout2d_raw_width(impl, text);
        int64_t numerator = saturating_add_i64(width, impl->wrap_width - 1);
        lines = numerator / impl->wrap_width;
        if (lines < 1)
            lines = 1;
    }
    return saturating_mul_i64(saturating_mul_i64(line_height, scale), lines);
}

/// @brief GC finalizer — releases retained references to source, target, and shader.
static void renderpass2d_finalize(void *obj) {
    rt_renderpass2d_impl *impl = (rt_renderpass2d_impl *)obj;
    release_ref_slot(&impl->source);
    release_ref_slot(&impl->target);
    release_ref_slot(&impl->shader);
}

/// @brief Allocate a RenderPass2D that reads from `source` and writes to `target`.
/// @details Both references are retained. The pass starts enabled. A Shader2D
///          can be attached later via `SetShader`; without one, source is
///          cloned directly into target (pixel-perfect copy).
void *rt_renderpass2d_new(void *source, void *target) {
    rt_renderpass2d_impl *impl =
        (rt_renderpass2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_renderpass2d_impl));
    if (!impl)
        return NULL;
    retain_ref(source);
    retain_ref(target);
    impl->source = source;
    impl->target = target;
    impl->enabled = 1;
    rt_obj_set_finalizer(impl, renderpass2d_finalize);
    return impl;
}

/// @brief Replace the source RenderTarget2D, retaining a reference to the new one.
void rt_renderpass2d_set_source(void *pass, void *source) {
    if (pass)
        spriterenderer2d_set_ref(&((rt_renderpass2d_impl *)pass)->source, source);
}

/// @brief Replace the target RenderTarget2D, retaining a reference to the new one.
void rt_renderpass2d_set_target(void *pass, void *target) {
    if (pass)
        spriterenderer2d_set_ref(&((rt_renderpass2d_impl *)pass)->target, target);
}

/// @brief Bind a Shader2D for this pass, retaining a reference.
void rt_renderpass2d_set_shader(void *pass, void *shader) {
    if (pass)
        spriterenderer2d_set_ref(&((rt_renderpass2d_impl *)pass)->shader, shader);
}

/// @brief Enable or disable this pass (disabled passes are skipped by `Execute`).
void rt_renderpass2d_set_enabled(void *pass, int64_t enabled) {
    if (pass)
        ((rt_renderpass2d_impl *)pass)->enabled = enabled != 0;
}

/// @brief Return non-zero if this pass is currently enabled.
int64_t rt_renderpass2d_get_enabled(void *pass) {
    return pass ? ((rt_renderpass2d_impl *)pass)->enabled : 0;
}

/// @brief Execute this pass: apply the optional shader and blit the result to target.
/// @details No-op if disabled, or if source/target are NULL or not valid RenderTarget2Ds.
///          If a Shader2D is bound, applies it via `rt_shader2d_apply`; otherwise
///          clones the source pixels directly. The target is cleared before writing.
void rt_renderpass2d_execute(void *pass) {
    rt_renderpass2d_impl *impl = (rt_renderpass2d_impl *)pass;
    if (!impl || !impl->enabled || !impl->source || !impl->target)
        return;
    if (!rendertarget2d_checked(impl->source) || !rendertarget2d_checked(impl->target))
        return;
    void *source_pixels = rt_rendertarget2d_get_pixels(impl->source);
    if (!source_pixels)
        return;
    void *processed = impl->shader ? rt_shader2d_apply(impl->shader, source_pixels)
                                   : rt_pixels_clone(source_pixels);
    if (!processed)
        return;
    rt_rendertarget2d_clear(impl->target, 0);
    rt_rendertarget2d_draw_pixels(impl->target, 0, 0, processed);
    release_ref_slot(&processed);
}

/// @brief GC finalizer — releases all retained pass references and frees the passes array.
static void rendergraph2d_finalize(void *obj) {
    rt_rendergraph2d_impl *impl = (rt_rendergraph2d_impl *)obj;
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        release_ref_slot(&impl->passes[i]);
    free(impl->passes);
}

/// @brief Allocate a RenderGraph2D with the given initial pass-array capacity.
void *rt_rendergraph2d_new(int64_t capacity) {
    rt_rendergraph2d_impl *impl =
        (rt_rendergraph2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_rendergraph2d_impl));
    if (!impl)
        return NULL;
    impl->capacity = initial_capacity(capacity);
    impl->passes = (void **)calloc((size_t)impl->capacity, sizeof(void *));
    if (!impl->passes) {
        if (rt_obj_release_check0(impl))
            rt_obj_free(impl);
        return NULL;
    }
    rt_obj_set_finalizer(impl, rendergraph2d_finalize);
    return impl;
}

/// @brief Grow the render-pass array to fit at least `needed` entries.
/// @details Same doubling-growth + overflow-guard pattern as the other `_reserve`
///          helpers in this file (renderer / path / object-layer). New tail is
///          zero-initialized so `rt_rendergraph2d_clear` can safely walk the
///          full capacity without stepping on uninitialized pointers.
/// @return 1 on success, 0 on overflow or allocation failure.
static int32_t rendergraph2d_reserve(rt_rendergraph2d_impl *impl, int64_t needed) {
    if (!impl || needed <= impl->capacity)
        return 1;
    int64_t cap = impl->capacity > 0 ? impl->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(void *))
        return 0;
    void **passes = (void **)realloc(impl->passes, (size_t)cap * sizeof(void *));
    if (!passes)
        return 0;
    memset(passes + impl->capacity, 0, (size_t)(cap - impl->capacity) * sizeof(void *));
    impl->passes = passes;
    impl->capacity = cap;
    return 1;
}

/// @brief Append a RenderPass2D to the graph, retaining a reference to it.
/// @details Grows the backing array if needed. Traps on capacity overflow.
void rt_rendergraph2d_add_pass(void *graph, void *pass) {
    rt_rendergraph2d_impl *impl = (rt_rendergraph2d_impl *)graph;
    if (!impl || !pass)
        return;
    if (!rendergraph2d_reserve(impl, impl->count + 1)) {
        rt_trap("RenderGraph2D: capacity overflow");
        return;
    }
    retain_ref(pass);
    impl->passes[impl->count++] = pass;
}

/// @brief Remove all passes from the graph, releasing their retained references.
void rt_rendergraph2d_clear(void *graph) {
    rt_rendergraph2d_impl *impl = (rt_rendergraph2d_impl *)graph;
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        release_ref_slot(&impl->passes[i]);
    impl->count = 0;
}

/// @brief Return the number of passes currently in the graph.
int64_t rt_rendergraph2d_get_count(void *graph) {
    return graph ? ((rt_rendergraph2d_impl *)graph)->count : 0;
}

/// @brief Execute every pass in insertion order.
/// @details Each pass is executed unconditionally here — individual passes skip
///          themselves via their `enabled` flag inside `rt_renderpass2d_execute`.
void rt_rendergraph2d_execute(void *graph) {
    rt_rendergraph2d_impl *impl = (rt_rendergraph2d_impl *)graph;
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        rt_renderpass2d_execute(impl->passes[i]);
}

/// @brief GC finalizer — frees the packed bit grid.
static void collisionmask2d_finalize(void *obj) {
    rt_collisionmask2d_impl *impl = (rt_collisionmask2d_impl *)obj;
    free(impl->bits);
}

/// @brief Allocate a CollisionMask2D with all cells initially clear (solid=0).
/// @details `checked_count` guards against dimension overflow; traps with a message
///          if `width * height` overflows. One byte per cell — not a true bit-pack,
///          but faster to read with no bit-shift math.
void *rt_collisionmask2d_new(int64_t width, int64_t height) {
    int64_t count = 0;
    if (!checked_count(width, height, 1, &count)) {
        rt_trap("CollisionMask2D.New: invalid dimensions");
        return NULL;
    }
    rt_collisionmask2d_impl *impl =
        (rt_collisionmask2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_collisionmask2d_impl));
    if (!impl)
        return NULL;
    impl->bits = (uint8_t *)calloc((size_t)count, 1);
    if (!impl->bits) {
        if (rt_obj_release_check0(impl))
            rt_obj_free(impl);
        return NULL;
    }
    impl->width = width;
    impl->height = height;
    rt_obj_set_finalizer(impl, collisionmask2d_finalize);
    return impl;
}

/// @brief Build a CollisionMask2D by classifying each pixel as solid/empty by alpha.
/// @details Pixels with alpha >= `alpha_threshold` (clamped to [0, 255]) are marked
///          solid. Useful for creating a per-pixel collision shape from a sprite's
///          alpha channel without any manual tilemap authoring.
void *rt_collisionmask2d_from_pixels(void *pixels, int64_t alpha_threshold) {
    if (!pixels)
        return NULL;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    void *mask = rt_collisionmask2d_new(width, height);
    if (!mask)
        return NULL;
    int64_t threshold = clamp_u8_i64(alpha_threshold);
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t rgba = (uint32_t)rt_pixels_get(pixels, x, y);
            rt_collisionmask2d_set(mask, x, y, (rgba & 255u) >= (uint32_t)threshold);
        }
    }
    return mask;
}

/// @brief Return the width of the mask in cells.
int64_t rt_collisionmask2d_get_width(void *mask) {
    return mask ? ((rt_collisionmask2d_impl *)mask)->width : 0;
}

/// @brief Return the height of the mask in cells.
int64_t rt_collisionmask2d_get_height(void *mask) {
    return mask ? ((rt_collisionmask2d_impl *)mask)->height : 0;
}

/// @brief Return non-zero if cell (x, y) is within the collision mask's bounds.
static int32_t collisionmask2d_in_bounds(rt_collisionmask2d_impl *impl, int64_t x, int64_t y) {
    return impl && x >= 0 && y >= 0 && x < impl->width && y < impl->height;
}

/// @brief Set or clear the solid flag at `(x, y)`; out-of-range calls are silently dropped.
void rt_collisionmask2d_set(void *mask, int64_t x, int64_t y, int64_t solid) {
    rt_collisionmask2d_impl *impl = (rt_collisionmask2d_impl *)mask;
    if (!collisionmask2d_in_bounds(impl, x, y))
        return;
    impl->bits[y * impl->width + x] = solid != 0;
}

/// @brief Return 1 if cell `(x, y)` is solid, 0 if empty or out of range.
int64_t rt_collisionmask2d_get(void *mask, int64_t x, int64_t y) {
    rt_collisionmask2d_impl *impl = (rt_collisionmask2d_impl *)mask;
    if (!collisionmask2d_in_bounds(impl, x, y))
        return 0;
    return impl->bits[y * impl->width + x] ? 1 : 0;
}

/// @brief Pixel-perfect overlap test between two masks at world positions `(ax, ay)` and `(bx, by)`.
/// @details Computes the axis-aligned intersection rectangle, then walks every cell
///          in that rectangle checking whether both masks are solid — returns 1 on the
///          first matching solid pair, 0 if no overlap exists. Early-exits if the AABBs
///          don't intersect at all.
int64_t rt_collisionmask2d_overlaps(
    void *a, int64_t ax, int64_t ay, void *b, int64_t bx, int64_t by) {
    rt_collisionmask2d_impl *ma = (rt_collisionmask2d_impl *)a;
    rt_collisionmask2d_impl *mb = (rt_collisionmask2d_impl *)b;
    if (!ma || !mb)
        return 0;
    int64_t left = ax > bx ? ax : bx;
    int64_t top = ay > by ? ay : by;
    if (!point_in_interval_i64(ax, ma->width, left) ||
        !point_in_interval_i64(bx, mb->width, left) ||
        !point_in_interval_i64(ay, ma->height, top) || !point_in_interval_i64(by, mb->height, top))
        return 0;

    uint64_t ax0 = (uint64_t)left - (uint64_t)ax;
    uint64_t bx0 = (uint64_t)left - (uint64_t)bx;
    uint64_t ay0 = (uint64_t)top - (uint64_t)ay;
    uint64_t by0 = (uint64_t)top - (uint64_t)by;
    uint64_t overlap_w = (uint64_t)ma->width - ax0;
    uint64_t b_rem_w = (uint64_t)mb->width - bx0;
    if (b_rem_w < overlap_w)
        overlap_w = b_rem_w;
    uint64_t overlap_h = (uint64_t)ma->height - ay0;
    uint64_t b_rem_h = (uint64_t)mb->height - by0;
    if (b_rem_h < overlap_h)
        overlap_h = b_rem_h;

    for (uint64_t dy = 0; dy < overlap_h; dy++) {
        for (uint64_t dx = 0; dx < overlap_w; dx++) {
            if (rt_collisionmask2d_get(a, (int64_t)(ax0 + dx), (int64_t)(ay0 + dy)) &&
                rt_collisionmask2d_get(b, (int64_t)(bx0 + dx), (int64_t)(by0 + dy)))
                return 1;
        }
    }
    return 0;
}

/// @brief Allocate a Hitbox2D at `(x, y)` with the given dimensions (negative sizes clamped to 0).
void *rt_hitbox2d_new(int64_t x, int64_t y, int64_t width, int64_t height) {
    rt_hitbox2d_impl *impl =
        (rt_hitbox2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_hitbox2d_impl));
    if (!impl)
        return NULL;
    rt_hitbox2d_set(impl, x, y, width, height);
    return impl;
}

/// @brief Reposition and resize an existing hitbox in one call.
void rt_hitbox2d_set(void *hitbox, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!hitbox)
        return;
    rt_hitbox2d_impl *impl = (rt_hitbox2d_impl *)hitbox;
    impl->x = x;
    impl->y = y;
    impl->width = width < 0 ? 0 : width;
    impl->height = height < 0 ? 0 : height;
}

/// @brief Return the X world-position of the hitbox's top-left corner.
int64_t rt_hitbox2d_get_x(void *hitbox) {
    return hitbox ? ((rt_hitbox2d_impl *)hitbox)->x : 0;
}

/// @brief Return the Y world-position of the hitbox's top-left corner.
int64_t rt_hitbox2d_get_y(void *hitbox) {
    return hitbox ? ((rt_hitbox2d_impl *)hitbox)->y : 0;
}

/// @brief Return the width of the hitbox in pixels.
int64_t rt_hitbox2d_get_width(void *hitbox) {
    return hitbox ? ((rt_hitbox2d_impl *)hitbox)->width : 0;
}

/// @brief Return the height of the hitbox in pixels.
int64_t rt_hitbox2d_get_height(void *hitbox) {
    return hitbox ? ((rt_hitbox2d_impl *)hitbox)->height : 0;
}

/// @brief Return non-zero if point `(x, y)` is inside this hitbox.
/// @details Uses `point_in_interval_i64` for both axes: [x, x+width) × [y, y+height).
int64_t rt_hitbox2d_contains(void *hitbox, int64_t x, int64_t y) {
    rt_hitbox2d_impl *impl = (rt_hitbox2d_impl *)hitbox;
    return impl && point_in_interval_i64(impl->x, impl->width, x) &&
           point_in_interval_i64(impl->y, impl->height, y);
}

/// @brief Return non-zero if two hitboxes overlap (AABB vs AABB test).
/// @details Uses `intervals_overlap_i64` on both axes — no separation means overlap.
int64_t rt_hitbox2d_intersects(void *a, void *b) {
    rt_hitbox2d_impl *ha = (rt_hitbox2d_impl *)a;
    rt_hitbox2d_impl *hb = (rt_hitbox2d_impl *)b;
    if (!ha || !hb)
        return 0;
    return intervals_overlap_i64(ha->x, ha->width, hb->x, hb->width) &&
           intervals_overlap_i64(ha->y, ha->height, hb->y, hb->height);
}

/// @brief Allocate an empty Palette2D (all 256 slots zeroed, count=0).
void *rt_palette2d_new(void) {
    rt_palette2d_impl *impl =
        (rt_palette2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_palette2d_impl));
    if (!impl)
        return NULL;
    return impl;
}

/// @brief Set a palette entry at `index` [0, 255] to `rgba`.
/// @details `count` is extended to `index+1` if the new index is the highest seen so far.
void rt_palette2d_set_color(void *palette, int64_t index, int64_t rgba) {
    rt_palette2d_impl *impl = (rt_palette2d_impl *)palette;
    if (!impl || index < 0 || index >= 256)
        return;
    impl->colors[index] = rt_pixels_rgba_or_tagged_color_to_rgba(rgba);
    if (index + 1 > impl->count)
        impl->count = index + 1;
}

/// @brief Return the RGBA color at `index`, or 0 if out of range or beyond `count`.
int64_t rt_palette2d_get_color(void *palette, int64_t index) {
    rt_palette2d_impl *impl = (rt_palette2d_impl *)palette;
    if (!impl || index < 0 || index >= 256 || index >= impl->count)
        return 0;
    return impl->colors[index];
}

/// @brief Return the number of palette entries set (highest set index + 1).
int64_t rt_palette2d_get_count(void *palette) {
    return palette ? ((rt_palette2d_impl *)palette)->count : 0;
}

/// @brief Remap a Pixels image through this palette and return a new Pixels.
/// @details For each pixel, the alpha byte (bits [7:0] in the RGBA word used here)
///          is taken as a palette index; the pixel is replaced with `colors[index]`.
///          Pixels with indices beyond `count` are copied unchanged. Returns NULL on
///          allocation failure.
void *rt_palette2d_apply(void *palette, void *pixels) {
    rt_palette2d_impl *impl = (rt_palette2d_impl *)palette;
    if (!impl || !pixels)
        return NULL;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    void *out = rt_pixels_new(width, height);
    if (!out)
        return NULL;
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t src = (uint32_t)rt_pixels_get(pixels, x, y);
            int64_t index = src & 255;
            uint32_t mapped = index < impl->count ? impl->colors[index] : src;
            rt_pixels_set(out, x, y, mapped);
        }
    }
    return out;
}

/// @brief Linear interpolation between two RGBA colors at `t_pct` percent [0, 100].
/// @details Each channel is interpolated independently in integer arithmetic:
///          `result_ch = a_ch + (b_ch - a_ch) * t / 100`. No gamma correction —
///          interpolation happens in gamma-encoded space, matching the simpler
///          expectation of the 2D tool palette.
static uint32_t lerp_rgba(uint32_t a, uint32_t b, int64_t t_pct) {
    int64_t t = clamp_i64(t_pct, 0, 100);
    int64_t ar = (a >> 24) & 255;
    int64_t ag = (a >> 16) & 255;
    int64_t ab = (a >> 8) & 255;
    int64_t aa = a & 255;
    int64_t br = (b >> 24) & 255;
    int64_t bg = (b >> 16) & 255;
    int64_t bb = (b >> 8) & 255;
    int64_t ba = b & 255;
    int64_t r = ar + (br - ar) * t / 100;
    int64_t g = ag + (bg - ag) * t / 100;
    int64_t blue = ab + (bb - ab) * t / 100;
    int64_t alpha = aa + (ba - aa) * t / 100;
    return (uint32_t)((r << 24) | (g << 16) | (blue << 8) | alpha);
}

/// @brief Allocate a Gradient2D with the given start/end RGBA colors and step count.
/// @details `steps` is the number of quantization levels; 2 means a smooth linear
///          gradient, higher values produce visible banding. Non-positive steps
///          default to 2. Colors are normalized via `rt_pixels_rgba_or_tagged_color_to_rgba`.
void *rt_gradient2d_new(int64_t start_rgba, int64_t end_rgba, int64_t steps) {
    rt_gradient2d_impl *impl =
        (rt_gradient2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_gradient2d_impl));
    if (!impl)
        return NULL;
    impl->start = rt_pixels_rgba_or_tagged_color_to_rgba(start_rgba);
    impl->end = rt_pixels_rgba_or_tagged_color_to_rgba(end_rgba);
    impl->steps = steps <= 0 ? 2 : steps;
    return impl;
}

/// @brief Update the start and end colors of an existing gradient.
void rt_gradient2d_set_colors(void *gradient, int64_t start_rgba, int64_t end_rgba) {
    if (!gradient)
        return;
    rt_gradient2d_impl *impl = (rt_gradient2d_impl *)gradient;
    impl->start = rt_pixels_rgba_or_tagged_color_to_rgba(start_rgba);
    impl->end = rt_pixels_rgba_or_tagged_color_to_rgba(end_rgba);
}

/// @brief Set the step count; non-positive values reset to 2 (smooth).
void rt_gradient2d_set_steps(void *gradient, int64_t steps) {
    if (gradient)
        ((rt_gradient2d_impl *)gradient)->steps = steps <= 0 ? 2 : steps;
}

/// @brief Sample the gradient at position `t_pct` percent [0, 100] and return RGBA.
/// @details Quantizes `t` into `steps` buckets first, then interpolates between
///          start and end via `lerp_rgba`. Steps above 101 are clamped to 101 to
///          keep the bucket arithmetic safe.
int64_t rt_gradient2d_sample(void *gradient, int64_t t_pct) {
    rt_gradient2d_impl *impl = (rt_gradient2d_impl *)gradient;
    if (!impl)
        return 0;
    int64_t t = clamp_i64(t_pct, 0, 100);
    int64_t steps = impl->steps < 2 ? 2 : impl->steps;
    if (steps > 101)
        steps = 101;
    int64_t bucket = (t * (steps - 1) + 50) / 100;
    int64_t stepped_t = steps <= 1 ? 0 : bucket * 100 / (steps - 1);
    return lerp_rgba(impl->start, impl->end, stepped_t);
}

/// @brief Fill `pixels` with a left-to-right horizontal gradient.
/// @details Maps each column X to a sample position `X * 100 / (width - 1)` and writes
///          the same sampled color to all rows in that column.
void rt_gradient2d_fill_horizontal(void *gradient, void *pixels) {
    rt_gradient2d_impl *impl = (rt_gradient2d_impl *)gradient;
    if (!impl || !pixels)
        return;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    int64_t denom = width > 1 ? width - 1 : 1;
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++)
            rt_pixels_set(pixels, x, y, rt_gradient2d_sample(gradient, x * 100 / denom));
    }
}

/// @brief Fill `pixels` with a top-to-bottom vertical gradient.
/// @details Maps each row Y to a sample position `Y * 100 / (height - 1)` and writes
///          the full row with a single color — avoids redundant samples per pixel.
void rt_gradient2d_fill_vertical(void *gradient, void *pixels) {
    rt_gradient2d_impl *impl = (rt_gradient2d_impl *)gradient;
    if (!impl || !pixels)
        return;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    int64_t denom = height > 1 ? height - 1 : 1;
    for (int64_t y = 0; y < height; y++) {
        uint32_t color = (uint32_t)rt_gradient2d_sample(gradient, y * 100 / denom);
        for (int64_t x = 0; x < width; x++)
            rt_pixels_set(pixels, x, y, color);
    }
}

/// @brief GC finalizer — releases the retained Camera2D reference.
static void camerarig2d_finalize(void *obj) {
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)obj;
    release_ref_slot(&impl->camera);
}

/// @brief Allocate a CameraRig2D wrapping `camera` with default smoothing (1000 = 100.0%).
/// @details `smoothing` of 1000 means instant snapping (camera jumps straight to target);
///          lower values produce lag-based smooth follow. Shake starts zeroed.
void *rt_camerarig2d_new(void *camera) {
    rt_camerarig2d_impl *impl =
        (rt_camerarig2d_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_camerarig2d_impl));
    if (!impl)
        return NULL;
    retain_ref(camera);
    impl->camera = camera;
    impl->smoothing = 1000;
    rt_obj_set_finalizer(impl, camerarig2d_finalize);
    return impl;
}

/// @brief Replace the Camera2D, retaining a reference to the new one.
void rt_camerarig2d_set_camera(void *rig, void *camera) {
    if (rig)
        spriterenderer2d_set_ref(&((rt_camerarig2d_impl *)rig)->camera, camera);
}

/// @brief Set the world-space target position the camera should track.
void rt_camerarig2d_set_target(void *rig, int64_t x, int64_t y) {
    if (!rig)
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    impl->target_x = x;
    impl->target_y = y;
}

/// @brief Set the smooth-follow strength in [0, 1000] (per-mille lerp factor).
/// @details 1000 = snap instantly, 0 = camera never moves, ~50 = gentle follow.
void rt_camerarig2d_set_smoothing(void *rig, int64_t lerp_pct) {
    if (rig)
        ((rt_camerarig2d_impl *)rig)->smoothing = clamp_i64(lerp_pct, 0, 1000);
}

/// @brief Set the camera's deadzone rectangle (camera doesn't move while target is inside).
void rt_camerarig2d_set_deadzone(void *rig, int64_t width, int64_t height) {
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    if (impl && impl->camera)
        rt_camera_set_deadzone(impl->camera, width, height);
}

/// @brief Add a shake displacement offset (accumulated via saturating addition).
void rt_camerarig2d_add_shake(void *rig, int64_t x, int64_t y) {
    if (!rig)
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    impl->shake_x = saturating_add_i64(impl->shake_x, x);
    impl->shake_y = saturating_add_i64(impl->shake_y, y);
}

/// @brief Reset the shake offset to zero.
void rt_camerarig2d_clear_shake(void *rig) {
    if (!rig)
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    impl->shake_x = 0;
    impl->shake_y = 0;
}

/// @brief Advance the rig by calling `rt_camera_smooth_follow` toward the current target.
/// @details Should be called once per frame before querying render positions.
void rt_camerarig2d_update(void *rig) {
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    if (impl && impl->camera)
        rt_camera_smooth_follow(impl->camera, impl->target_x, impl->target_y, impl->smoothing);
}

/// @brief Return the X render position: camera X plus current shake X offset.
int64_t rt_camerarig2d_get_render_x(void *rig) {
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    return impl && impl->camera ? saturating_add_i64(rt_camera_get_x(impl->camera), impl->shake_x)
                                : 0;
}

/// @brief Return the Y render position: camera Y plus current shake Y offset.
int64_t rt_camerarig2d_get_render_y(void *rig) {
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    return impl && impl->camera ? saturating_add_i64(rt_camera_get_y(impl->camera), impl->shake_y)
                                : 0;
}

/// @brief GC finalizer — releases the retained TexAtlas reference.
static void texturepackeratlas_finalize(void *obj) {
    rt_texturepackeratlas_impl *impl = (rt_texturepackeratlas_impl *)obj;
    release_ref_slot(&impl->atlas);
}

/// @brief Wrap a Pixels image as a TexturePackerAtlas, constructing a TexAtlas from it.
/// @details Traps with a message if `pixels` is NULL. The internal `rt_texatlas_new`
///          call may still return NULL on allocation failure, in which case this
///          function tears down the partially-constructed object cleanly.
void *rt_texturepackeratlas_new(void *pixels) {
    if (!pixels) {
        rt_trap("TexturePackerAtlas.New: null pixels");
        return NULL;
    }
    rt_texturepackeratlas_impl *impl = (rt_texturepackeratlas_impl *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_texturepackeratlas_impl));
    if (!impl)
        return NULL;
    impl->atlas = rt_texatlas_new(pixels);
    if (!impl->atlas) {
        if (rt_obj_release_check0(impl))
            rt_obj_free(impl);
        return NULL;
    }
    rt_obj_set_finalizer(impl, texturepackeratlas_finalize);
    return impl;
}

/// @brief Return the underlying TexAtlas pointer (not retained — caller must not release it).
void *rt_texturepackeratlas_get_atlas(void *packer) {
    return packer ? ((rt_texturepackeratlas_impl *)packer)->atlas : NULL;
}

/// @brief Register a named sub-region `(x, y, width, height)` in the atlas.
void rt_texturepackeratlas_add(
    void *packer, rt_string name, int64_t x, int64_t y, int64_t width, int64_t height) {
    void *atlas = rt_texturepackeratlas_get_atlas(packer);
    if (atlas)
        rt_texatlas_add(atlas, name, x, y, width, height);
}

/// @brief Return non-zero if a region with the given `name` exists in the atlas.
int64_t rt_texturepackeratlas_has(void *packer, rt_string name) {
    void *atlas = rt_texturepackeratlas_get_atlas(packer);
    return atlas ? rt_texatlas_has(atlas, name) : 0;
}

/// @brief Return the total number of named regions registered in the atlas.
int64_t rt_texturepackeratlas_region_count(void *packer) {
    void *atlas = rt_texturepackeratlas_get_atlas(packer);
    return atlas ? rt_texatlas_region_count(atlas) : 0;
}

/// @brief Allocate an AsepriteImporter with zeroed grid dimensions (must call SetGrid first).
void *rt_asepriteimporter_new(void) {
    rt_asepriteimporter_impl *impl =
        (rt_asepriteimporter_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_asepriteimporter_impl));
    if (!impl)
        return NULL;
    return impl;
}

/// @brief Set the frame cell dimensions for slicing a horizontal strip sprite sheet.
/// @details Dimensions are sanitized via `normalized_dim` (negative → 1).
void rt_asepriteimporter_set_grid(void *importer, int64_t frame_width, int64_t frame_height) {
    if (!importer)
        return;
    rt_asepriteimporter_impl *impl = (rt_asepriteimporter_impl *)importer;
    impl->frame_width = normalized_dim(frame_width);
    impl->frame_height = normalized_dim(frame_height);
}

/// @brief Return the configured frame width in pixels (0 if not set).
int64_t rt_asepriteimporter_get_frame_width(void *importer) {
    return importer ? ((rt_asepriteimporter_impl *)importer)->frame_width : 0;
}

/// @brief Return the configured frame height in pixels (0 if not set).
int64_t rt_asepriteimporter_get_frame_height(void *importer) {
    return importer ? ((rt_asepriteimporter_impl *)importer)->frame_height : 0;
}

/// @brief Slice `pixels` into a grid atlas using the configured frame dimensions.
/// @details Returns NULL if any required value is missing or zero. Delegates to
///          `rt_texatlas_load_grid`; the returned atlas owns its region data.
void *rt_asepriteimporter_to_atlas(void *importer, void *pixels) {
    rt_asepriteimporter_impl *impl = (rt_asepriteimporter_impl *)importer;
    if (!impl || !pixels || impl->frame_width <= 0 || impl->frame_height <= 0)
        return NULL;
    return rt_texatlas_load_grid(pixels, impl->frame_width, impl->frame_height);
}

/// @brief Allocate a TiledMapLoader with default tile size 16×16.
void *rt_tiledmaploader_new(void) {
    rt_tiledmaploader_impl *impl =
        (rt_tiledmaploader_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_tiledmaploader_impl));
    if (!impl)
        return NULL;
    impl->tile_width = 16;
    impl->tile_height = 16;
    return impl;
}

/// @brief Override the tile dimensions used when creating new tilemaps.
/// @details Dimensions are sanitized via `normalized_dim` (negative → 1).
void rt_tiledmaploader_set_tile_size(void *loader, int64_t tile_width, int64_t tile_height) {
    if (!loader)
        return;
    rt_tiledmaploader_impl *impl = (rt_tiledmaploader_impl *)loader;
    impl->tile_width = normalized_dim(tile_width);
    impl->tile_height = normalized_dim(tile_height);
}

/// @brief Return the configured tile width in pixels.
int64_t rt_tiledmaploader_get_tile_width(void *loader) {
    return loader ? ((rt_tiledmaploader_impl *)loader)->tile_width : 0;
}

/// @brief Return the configured tile height in pixels.
int64_t rt_tiledmaploader_get_tile_height(void *loader) {
    return loader ? ((rt_tiledmaploader_impl *)loader)->tile_height : 0;
}

/// @brief Allocate a blank Tilemap2D with `width × height` cells using this loader's tile size.
/// @details Delegates to `rt_tilemap_new` — the returned tilemap has all cells set to 0 (empty).
void *rt_tiledmaploader_new_tilemap(void *loader, int64_t width, int64_t height) {
    rt_tiledmaploader_impl *impl = (rt_tiledmaploader_impl *)loader;
    if (!impl)
        return NULL;
    return rt_tilemap_new(width, height, impl->tile_width, impl->tile_height);
}
