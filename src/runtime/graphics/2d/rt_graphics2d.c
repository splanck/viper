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
//     Canvas-style `0x00RRGGBB`, raw `0xRRGGBBAA`, and Color.RGBA values.
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
#include "rt_graphics2d_internal.h"

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
#define RT2D_PI 3.14159265358979323846
#define RT2D_RENDERTARGET_MAGIC 0x525432445247544cLL /* "RT2DRGTL" */
#define RT2D_TEXTURE_MAGIC 0x525432445445584cLL      /* "RT2DTEXL" */
#define RT2D_RENDERER_MAGIC 0x52543244524e444cLL     /* "RT2DRNDL" */
#define RT2D_RENDERTARGET_CLASS_ID INT64_C(-0x620100)
#define RT2D_TEXTURE_CLASS_ID INT64_C(-0x620101)
#define RT2D_RENDERER_CLASS_ID INT64_C(-0x620102)
#define RT2D_MATERIAL_CLASS_ID INT64_C(-0x620103)
#define RT2D_SHADER_CLASS_ID INT64_C(-0x620104)
#define RT2D_VIEWPORT_CLASS_ID INT64_C(-0x620105)
#define RT2D_PATH_CLASS_ID INT64_C(-0x62010A)
#define RT2D_TRANSFORM_CLASS_ID INT64_C(-0x620110)
#define RT2D_SAMPLER_CLASS_ID INT64_C(-0x620111)
#define RT2D_BLENDSTATE_CLASS_ID INT64_C(-0x620112)
#define RT2D_SPRITERENDERER_CLASS_ID INT64_C(-0x620113)
#define RT2D_TILECHUNKCACHE_CLASS_ID INT64_C(-0x620114)
#define RT2D_TILEMAPRENDERER_CLASS_ID INT64_C(-0x620115)
#define RT2D_ANIMATIONCLIP_CLASS_ID INT64_C(-0x620116)
#define RT2D_ANIMATEDSPRITE_CLASS_ID INT64_C(-0x620117)
#define RT2D_TEXTLAYOUT_CLASS_ID INT64_C(-0x620118)
#define RT2D_RENDERPASS_CLASS_ID INT64_C(-0x620119)
#define RT2D_RENDERGRAPH_CLASS_ID INT64_C(-0x62011A)
#define RT2D_COLLISIONMASK_CLASS_ID INT64_C(-0x62011B)
#define RT2D_HITBOX_CLASS_ID INT64_C(-0x62011C)
#define RT2D_PALETTE_CLASS_ID INT64_C(-0x62011D)
#define RT2D_GRADIENT_CLASS_ID INT64_C(-0x62011E)
#define RT2D_CAMERARIG_CLASS_ID INT64_C(-0x62011F)
#define RT2D_ASEPRITEIMPORTER_CLASS_ID INT64_C(-0x620120)
#define RT2D_TILEDMAPLOADER_CLASS_ID INT64_C(-0x620121)
#define RT2D_TEXTUREPACKERATLAS_CLASS_ID INT64_C(-0x620122)
#define RT2D_POSTPROCESS_CLASS_ID INT64_C(-0x620123)
#define RT2D_SURFACE_CLASS_ID INT64_C(-0x620124)
#define RT2D_GPUTEXTURE_CLASS_ID INT64_C(-0x620125)
#define RT2D_SCREENSCALER_CLASS_ID INT64_C(-0x620126)

static int rt2d_normalize_angle_degrees(double *angle_deg) {
    if (!angle_deg || !isfinite(*angle_deg))
        return 0;
    *angle_deg = fmod(*angle_deg, 360.0);
    return isfinite(*angle_deg);
}

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
    int64_t dst_width;
    int64_t dst_height;
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
    int64_t sampler_filter;
    int64_t sampler_wrap;
    double angle_deg;
    int64_t pivot_x;
    int64_t pivot_y;
    int8_t has_rotation;
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
    int64_t x;
    int64_t y;
    int32_t move;
} rt_path2d_point;

typedef struct {
    rt_path2d_point *points;
    int64_t count;
    int64_t capacity;
} rt_path2d_impl;


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
int64_t saturating_add_i64(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Saturating integer multiplication using `long double` to detect overflow before
/// truncation.
int64_t saturating_mul_i64(int64_t a, int64_t b) {
    long double value = (long double)a * (long double)b;
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)value;
}

static int32_t rt2d_is_instance_of_either(void *obj,
                                          int64_t primary_class_id,
                                          int64_t alias_class_id,
                                          size_t min_size) {
    return obj && (rt_obj_is_instance(obj, primary_class_id, min_size) ||
                   rt_obj_is_instance(obj, alias_class_id, min_size));
}

static rt_rendertarget2d_impl *rendertarget2d_checked(void *target) {
    if (!target)
        return NULL;
    if (!rt2d_is_instance_of_either(target,
                                    RT2D_RENDERTARGET_CLASS_ID,
                                    RT2D_SURFACE_CLASS_ID,
                                    sizeof(rt_rendertarget2d_impl)))
        return NULL;
    rt_rendertarget2d_impl *impl = (rt_rendertarget2d_impl *)target;
    return impl->magic == RT2D_RENDERTARGET_MAGIC ? impl : NULL;
}

/// @brief Validate and cast a `void *` to `rt_texture2d_impl *` by checking the magic cookie.
static rt_texture2d_impl *texture2d_checked(void *texture) {
    if (!texture)
        return NULL;
    if (!rt2d_is_instance_of_either(
            texture, RT2D_TEXTURE_CLASS_ID, RT2D_GPUTEXTURE_CLASS_ID, sizeof(rt_texture2d_impl)))
        return NULL;
    rt_texture2d_impl *impl = (rt_texture2d_impl *)texture;
    return impl->magic == RT2D_TEXTURE_MAGIC ? impl : NULL;
}

/// @brief Validate and cast a `void *` to `rt_renderer2d_impl *` by checking the magic cookie.
static rt_renderer2d_impl *renderer2d_checked(void *renderer) {
    if (!renderer)
        return NULL;
    if (!rt_obj_is_instance(renderer, RT2D_RENDERER_CLASS_ID, sizeof(rt_renderer2d_impl)))
        return NULL;
    rt_renderer2d_impl *impl = (rt_renderer2d_impl *)renderer;
    return impl->magic == RT2D_RENDERER_MAGIC ? impl : NULL;
}

/// @brief Test whether @p obj is non-NULL and has the runtime class id @p class_id.
/// @details Used by every public Renderer2D / Material2D / TileLayer2D entry
///          point as a one-line type guard before downcasting an opaque
///          handle to its concrete impl pointer.
int32_t rt2d_has_class(void *obj, int64_t class_id) {
    return obj && rt_obj_is_instance(obj, class_id, 0);
}

/// @brief Like rt2d_has_class but also requires the payload be at least
///        @p min_size bytes (guards struct-layout mismatches before a cast).
int32_t rt2d_has_class_min(void *obj, int64_t class_id, size_t min_size) {
    return obj && rt_obj_is_instance(obj, class_id, min_size);
}

int32_t rt2d_is_bitmap_font_handle(void *font) {
    return font && (rt_obj_is_instance(font, RT_BITMAPFONT_CLASS_ID, 0) ||
                    rt_obj_is_instance(font, RT_SPRITEFONT_CLASS_ID, 0));
}

/// @brief Safe-cast an opaque handle to rt_shader2d_impl, or NULL if not one.
static rt_shader2d_impl *shader2d_checked(void *shader) {
    return rt2d_has_class_min(shader, RT2D_SHADER_CLASS_ID, sizeof(rt_shader2d_impl))
               ? (rt_shader2d_impl *)shader
               : NULL;
}

/// @brief Safe-cast an opaque handle to rt_postprocess2d_impl, or NULL.
static rt_postprocess2d_impl *postprocess2d_checked(void *postprocess) {
    return rt2d_has_class_min(postprocess, RT2D_POSTPROCESS_CLASS_ID, sizeof(rt_postprocess2d_impl))
               ? (rt_postprocess2d_impl *)postprocess
               : NULL;
}

/// @brief True if @p shader is either a Shader2D or a PostProcess2D (both
///        accept the same apply-to-pixels usage).
static int32_t rt2d_is_shader_like(void *shader) {
    return shader2d_checked(shader) || postprocess2d_checked(shader);
}

/// @brief Safe-cast an opaque handle to rt_viewport2d_impl, or NULL.
static rt_viewport2d_impl *viewport2d_checked(void *viewport) {
    return rt2d_is_instance_of_either(viewport,
                                      RT2D_VIEWPORT_CLASS_ID,
                                      RT2D_SCREENSCALER_CLASS_ID,
                                      sizeof(rt_viewport2d_impl))
               ? (rt_viewport2d_impl *)viewport
               : NULL;
}

/// @brief Safe-cast an opaque handle to rt_tileset2d_impl, or NULL.
/// @details Uses an unsigned subtraction trick to collapse the "negative start" and
///   "value < start" cases into a single bounds check without signed-overflow UB.
static int32_t point_in_interval_i64(int64_t start, int64_t length, int64_t value) {
    if (length <= 0 || value < start)
        return 0;
    return ((uint64_t)value - (uint64_t)start) < (uint64_t)length;
}

/// @brief Test whether two intervals `[a_start, a_start+a_length)` and `[b_start,
/// b_start+b_length)` overlap.
/// @details Implemented as "does either interval contain the other's start?" — one containment
/// implies
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
/// @details Negative values use `(uint64_t)(-(value+1))+1u` so INT64_MIN is handled
///          without signed-overflow UB.
static uint64_t distance_to_zero_i64(int64_t value) {
    if (value >= 0)
        return (uint64_t)value;
    return (uint64_t)(-(value + 1)) + 1u;
}

/// @brief Clip a blit operation along one axis, adjusting destination, source, and length in place.
/// @details Handles the four cases where the destination or source position is negative or runs
/// past
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
int32_t checked_count(int64_t width, int64_t height, int64_t elem_size, int64_t *out_count) {
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
int64_t initial_capacity(int64_t requested) {
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
void retain_ref(void *obj) {
    if (obj && rt_heap_is_payload(obj))
        rt_obj_retain_maybe(obj);
}

/// @brief Release one reference through a slot and zero the slot atomically.
/// @details Used by every `_finalize` and every "replace slot" setter in this file.
///          The slot is cleared *before* the potential free so observers can't see
///          a dangling pointer mid-release. Non-payload handles are skipped just
///          like `retain_ref`.
void release_ref_slot(void **slot) {
    if (!slot || !*slot)
        return;
    void *obj = *slot;
    *slot = NULL;
    if (rt_heap_is_payload(obj) && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Normalize a user-supplied color to `0x00RRGGBB` form.
/// @details Shape/debug APIs accept Canvas-style `0x00RRGGBB`, raw
///          `0xRRGGBBAA`, or tagged Color.RGBA values. Alpha is ignored by
///          these RGB-only drawing primitives.
int64_t draw_rgb(int64_t color) {
    uint64_t full = (uint64_t)color;
    uint64_t c = full & 0xFFFFFFFFu;
    if ((full & (uint64_t)RT_PIXELS_COLOR_EXPLICIT_ALPHA_FLAG) != 0)
        return (int64_t)(c & 0x00FFFFFFu);
    if (c <= 0x00FFFFFFu)
        return (int64_t)c;
    return (int64_t)((c >> 8) & 0x00FFFFFFu);
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
void blit_pixels(void *dst,
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

    void *snapshot = NULL;
    void *read_src = src;
    void *read_dst = dst;
    if (dst == src &&
        !(dx + width <= sx || sx + width <= dx || dy + height <= sy || sy + height <= dy)) {
        snapshot = rt_pixels_clone(src);
        if (!snapshot)
            return;
        read_src = snapshot;
        read_dst = snapshot;
    }

    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t source = (uint32_t)rt_pixels_get(read_src, sx + x, sy + y);
            source = apply_tint_alpha(source, tint, alpha);
            if ((source & 255u) == 0)
                continue;
            uint32_t dest = (uint32_t)rt_pixels_get(read_dst, dx + x, dy + y);
            rt_pixels_set(dst, dx + x, dy + y, (int64_t)blend_pixel(dest, source, blend_mode));
        }
    }

    release_ref_slot(&snapshot);
}

/// @brief Wrap or clamp a 1D source-pixel coordinate per the requested wrap mode.
/// @details RT_GRAPHICS2D_WRAP_REPEAT performs a positive-modulo wrap (so
///          negative coords land in [0, limit)); other wrap modes clamp into
///          [0, limit-1]. Used by both nearest- and linear-filter samplers.
/// @return A coordinate in [0, limit). Returns 0 for limit <= 0.
static int64_t sampler_coord_i64(int64_t coord, int64_t limit, int64_t wrap) {
    if (limit <= 0)
        return 0;
    if (wrap == RT_GRAPHICS2D_WRAP_REPEAT) {
        int64_t wrapped = coord % limit;
        return wrapped < 0 ? wrapped + limit : wrapped;
    }
    if (coord < 0)
        return 0;
    if (coord >= limit)
        return limit - 1;
    return coord;
}

/// @brief Resolve a source coordinate inside a texture-region span.
/// @details Linear filtering for atlas regions must never borrow texels from
///          neighboring regions. The local coordinate is therefore wrapped or
///          clamped against the requested span first, then translated by the
///          region origin and finally resolved against the backing Pixels
///          bounds using the texture wrap mode.
static int64_t sampler_region_coord_i64(
    int64_t origin, int64_t local_coord, int64_t span, int64_t source_limit, int64_t wrap) {
    if (source_limit <= 0 || span <= 0)
        return 0;
    int64_t local = 0;
    if (wrap == RT_GRAPHICS2D_WRAP_REPEAT) {
        local = local_coord % span;
        if (local < 0)
            local += span;
    } else {
        local = clamp_i64(local_coord, 0, span - 1);
    }
    return sampler_coord_i64(saturating_add_i64(origin, local), source_limit, wrap);
}

/// @brief Round and clamp a long-double color channel to a 0..255 byte.
static uint8_t clamp_channel_ld(long double value) {
    if (value <= 0.0L)
        return 0;
    if (value >= 255.0L)
        return 255;
    return (uint8_t)(value + 0.5L);
}

/// @brief Alpha-premultiply one color channel of a packed RGBA value.
/// @param rgba  Packed 0xRRGGBBAA pixel.
/// @param shift Bit shift selecting the channel (24=R, 16=G, 8=B).
/// @return channel * alpha / 255 as a long double (premultiplied channel).
static long double premul_channel(uint32_t rgba, int shift) {
    long double c = (long double)((rgba >> shift) & 255u);
    long double a = (long double)(rgba & 255u);
    return c * a / 255.0L;
}

/// @brief Bilinearly interpolate four RGBA texels in premultiplied-alpha space.
/// @details Weights the four corner texels by (tx, ty), blends in
///          alpha-premultiplied space (correct edge behaviour against
///          transparent texels), then un-premultiplies back to straight RGBA.
///          Returns fully transparent (0) when the blended alpha is zero.
/// @param c00,c10,c01,c11 Corner texels (TL, TR, BL, BR) as 0xRRGGBBAA.
/// @param tx,ty Fractional sample position in [0,1] within the texel quad.
/// @return The interpolated 0xRRGGBBAA color.
static uint32_t bilerp_premul_rgba(
    uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, long double tx, long double ty) {
    long double w00 = (1.0L - tx) * (1.0L - ty);
    long double w10 = tx * (1.0L - ty);
    long double w01 = (1.0L - tx) * ty;
    long double w11 = tx * ty;
    long double a = (long double)(c00 & 255u) * w00 + (long double)(c10 & 255u) * w10 +
                    (long double)(c01 & 255u) * w01 + (long double)(c11 & 255u) * w11;
    uint32_t alpha = clamp_channel_ld(a);
    if (alpha == 0)
        return 0;

    long double r_pm = premul_channel(c00, 24) * w00 + premul_channel(c10, 24) * w10 +
                       premul_channel(c01, 24) * w01 + premul_channel(c11, 24) * w11;
    long double g_pm = premul_channel(c00, 16) * w00 + premul_channel(c10, 16) * w10 +
                       premul_channel(c01, 16) * w01 + premul_channel(c11, 16) * w11;
    long double b_pm = premul_channel(c00, 8) * w00 + premul_channel(c10, 8) * w10 +
                       premul_channel(c01, 8) * w01 + premul_channel(c11, 8) * w11;
    long double scale = 255.0L / (long double)alpha;
    uint32_t r = clamp_channel_ld(r_pm * scale);
    uint32_t g = clamp_channel_ld(g_pm * scale);
    uint32_t b = clamp_channel_ld(b_pm * scale);
    return (r << 24) | (g << 16) | (b << 8) | alpha;
}

/// @brief Sample one pixel from @p src using nearest-neighbor filtering.
/// @details Maps the destination position (dst_x, dst_y) within a
///          (dst_span_w × dst_span_h) blit region back to a source position
///          inside the (src_span_w × src_span_h) source region anchored at
///          (sx, sy). Picks the nearest source texel, applying the wrap mode
///          for out-of-range coordinates. Returns 0 (transparent black) on
///          degenerate input.
static uint32_t sample_pixels_nearest(void *src,
                                      int64_t sx,
                                      int64_t sy,
                                      int64_t src_span_w,
                                      int64_t src_span_h,
                                      int64_t dst_span_w,
                                      int64_t dst_span_h,
                                      int64_t dst_x,
                                      int64_t dst_y,
                                      int64_t wrap) {
    int64_t src_width = rt_pixels_width(src);
    int64_t src_height = rt_pixels_height(src);
    if (src_width <= 0 || src_height <= 0 || src_span_w <= 0 || src_span_h <= 0 ||
        dst_span_w <= 0 || dst_span_h <= 0)
        return 0;
    long double ux =
        (((long double)dst_x + 0.5L) * (long double)src_span_w) / (long double)dst_span_w;
    long double uy =
        (((long double)dst_y + 0.5L) * (long double)src_span_h) / (long double)dst_span_h;
    int64_t off_x = (int64_t)floorl(ux);
    int64_t off_y = (int64_t)floorl(uy);
    if (off_x < 0)
        off_x = 0;
    if (off_y < 0)
        off_y = 0;
    if (off_x >= src_span_w)
        off_x = src_span_w - 1;
    if (off_y >= src_span_h)
        off_y = src_span_h - 1;
    int64_t sample_x = sampler_region_coord_i64(sx, off_x, src_span_w, src_width, wrap);
    int64_t sample_y = sampler_region_coord_i64(sy, off_y, src_span_h, src_height, wrap);
    return (uint32_t)rt_pixels_get(src, sample_x, sample_y);
}

/// @brief Sample one pixel from @p src using bilinear filtering (4-tap blend).
/// @details Same coordinate space as sample_pixels_nearest, but performs a
///          bilinear blend of the four texels surrounding the back-mapped
///          fractional source position. RGB is interpolated in premultiplied
///          alpha space, then converted back to straight RGBA, so transparent
///          edge texels do not darken partially transparent samples.
static uint32_t sample_pixels_linear(void *src,
                                     int64_t sx,
                                     int64_t sy,
                                     int64_t src_span_w,
                                     int64_t src_span_h,
                                     int64_t dst_span_w,
                                     int64_t dst_span_h,
                                     int64_t dst_x,
                                     int64_t dst_y,
                                     int64_t wrap) {
    int64_t src_width = rt_pixels_width(src);
    int64_t src_height = rt_pixels_height(src);
    if (src_width <= 0 || src_height <= 0 || src_span_w <= 0 || src_span_h <= 0 ||
        dst_span_w <= 0 || dst_span_h <= 0)
        return 0;
    long double ux =
        (((long double)dst_x + 0.5L) * (long double)src_span_w) / (long double)dst_span_w - 0.5L;
    long double uy =
        (((long double)dst_y + 0.5L) * (long double)src_span_h) / (long double)dst_span_h - 0.5L;
    int64_t base_x = (int64_t)floorl(ux);
    int64_t base_y = (int64_t)floorl(uy);
    long double tx = ux - (long double)base_x;
    long double ty = uy - (long double)base_y;

    int64_t x0 = sampler_region_coord_i64(sx, base_x, src_span_w, src_width, wrap);
    int64_t x1 =
        sampler_region_coord_i64(sx, saturating_add_i64(base_x, 1), src_span_w, src_width, wrap);
    int64_t y0 = sampler_region_coord_i64(sy, base_y, src_span_h, src_height, wrap);
    int64_t y1 =
        sampler_region_coord_i64(sy, saturating_add_i64(base_y, 1), src_span_h, src_height, wrap);

    uint32_t c00 = (uint32_t)rt_pixels_get(src, x0, y0);
    uint32_t c10 = (uint32_t)rt_pixels_get(src, x1, y0);
    uint32_t c01 = (uint32_t)rt_pixels_get(src, x0, y1);
    uint32_t c11 = (uint32_t)rt_pixels_get(src, x1, y1);

    return bilerp_premul_rgba(c00, c10, c01, c11, tx, ty);
}

/// @brief Dispatch a pixel sample to either the nearest or linear filter.
/// @details Single entry point so the inner blit loops don't have to check
///          the filter mode per pixel — only the dispatch sees the filter
///          argument. Defaults to nearest for unknown filter values.
static uint32_t sample_pixels(void *src,
                              int64_t sx,
                              int64_t sy,
                              int64_t src_span_w,
                              int64_t src_span_h,
                              int64_t dst_span_w,
                              int64_t dst_span_h,
                              int64_t dst_x,
                              int64_t dst_y,
                              int64_t filter,
                              int64_t wrap) {
    if (filter == RT_GRAPHICS2D_FILTER_LINEAR)
        return sample_pixels_linear(
            src, sx, sy, src_span_w, src_span_h, dst_span_w, dst_span_h, dst_x, dst_y, wrap);
    return sample_pixels_nearest(
        src, sx, sy, src_span_w, src_span_h, dst_span_w, dst_span_h, dst_x, dst_y, wrap);
}

/// @brief Blit a sampled, scaled, optionally tinted region from @p src into @p dst.
/// @details Walks the destination rect (dx, dy, dst_width, dst_height), invokes
///          sample_pixels for each pixel to fetch the (possibly filtered)
///          source color, applies the tint and per-call alpha, and composites
///          via blend_pixel. This is the workhorse for Renderer2D.DrawTexture*
///          and post-process passes.
/// @param dst        Destination Pixels (RGBA8). Required.
/// @param dx,dy      Top-left of the destination rect.
/// @param src        Source Pixels (RGBA8). Required.
/// @param sx,sy      Top-left of the source region.
/// @param src_width  Width of the source region (the (sx,sy)..(sx+w,sy+h) span).
/// @param src_height Height of the source region.
/// @param dst_width  Width of the destination rect (scaling).
/// @param dst_height Height of the destination rect (scaling).
/// @param tint       0xAARRGGBB tint multiplier (-1 = no tint).
/// @param alpha      Extra opacity multiplier in [0, 255].
/// @param blend_mode RT_GRAPHICS2D_BLEND_* (alpha / additive / replace …).
/// @param filter     RT_GRAPHICS2D_FILTER_NEAREST or LINEAR.
/// @param wrap       RT_GRAPHICS2D_WRAP_* for source-coord out-of-range.
static void blit_pixels_sampled_scaled(void *dst,
                                       int64_t dx,
                                       int64_t dy,
                                       void *src,
                                       int64_t sx,
                                       int64_t sy,
                                       int64_t src_width,
                                       int64_t src_height,
                                       int64_t dst_width,
                                       int64_t dst_height,
                                       int64_t tint,
                                       int64_t alpha,
                                       int64_t blend_mode,
                                       int64_t filter,
                                       int64_t wrap) {
    if (!dst || !src || src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0)
        return;
    int64_t target_width = rt_pixels_width(dst);
    int64_t target_height = rt_pixels_height(dst);
    if (target_width <= 0 || target_height <= 0)
        return;

    int64_t clipped_x = 0;
    int64_t clipped_y = 0;
    int64_t copy_width = dst_width;
    int64_t copy_height = dst_height;
    if (dx < 0) {
        uint64_t skip = distance_to_zero_i64(dx);
        if (skip >= (uint64_t)copy_width)
            return;
        clipped_x = (int64_t)skip;
        copy_width -= clipped_x;
        dx = 0;
    }
    if (dy < 0) {
        uint64_t skip = distance_to_zero_i64(dy);
        if (skip >= (uint64_t)copy_height)
            return;
        clipped_y = (int64_t)skip;
        copy_height -= clipped_y;
        dy = 0;
    }
    if (dx >= target_width || dy >= target_height)
        return;
    if (copy_width > target_width - dx)
        copy_width = target_width - dx;
    if (copy_height > target_height - dy)
        copy_height = target_height - dy;
    if (copy_width <= 0 || copy_height <= 0)
        return;

    wrap = wrap == RT_GRAPHICS2D_WRAP_REPEAT ? RT_GRAPHICS2D_WRAP_REPEAT : RT_GRAPHICS2D_WRAP_CLAMP;
    filter = filter == RT_GRAPHICS2D_FILTER_LINEAR ? RT_GRAPHICS2D_FILTER_LINEAR
                                                   : RT_GRAPHICS2D_FILTER_NEAREST;

    for (int64_t y = 0; y < copy_height; y++) {
        int64_t dst_local_y = clipped_y + y;
        for (int64_t x = 0; x < copy_width; x++) {
            int64_t dst_local_x = clipped_x + x;
            uint32_t source = sample_pixels(src,
                                            sx,
                                            sy,
                                            src_width,
                                            src_height,
                                            dst_width,
                                            dst_height,
                                            dst_local_x,
                                            dst_local_y,
                                            filter,
                                            wrap);
            source = apply_tint_alpha(source, tint, alpha);
            if ((source & 255u) == 0)
                continue;
            uint32_t dest = (uint32_t)rt_pixels_get(dst, dx + x, dy + y);
            rt_pixels_set(dst, dx + x, dy + y, (int64_t)blend_pixel(dest, source, blend_mode));
        }
    }
}

/// @brief Blit a full source image rotated around a pivot inside the source rect.
/// @details The destination anchor `(dx,dy)` is the unrotated top-left; `pivot_x/y`
///          are source-local coordinates. Rotation is clockwise in screen space.
static void blit_pixels_rotated(void *dst,
                                int64_t dx,
                                int64_t dy,
                                void *src,
                                int64_t pivot_x,
                                int64_t pivot_y,
                                double angle_deg,
                                int64_t tint,
                                int64_t alpha,
                                int64_t blend_mode,
                                int64_t filter,
                                int64_t wrap) {
    if (!dst || !src || !rt2d_normalize_angle_degrees(&angle_deg))
        return;
    int64_t sw = rt_pixels_width(src);
    int64_t sh = rt_pixels_height(src);
    int64_t dw = rt_pixels_width(dst);
    int64_t dh = rt_pixels_height(dst);
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;

    double radians = angle_deg * (double)RT2D_PI / 180.0;
    double cos_a = cos(radians);
    double sin_a = sin(radians);
    double corners[4][2] = {{-(double)pivot_x, -(double)pivot_y},
                            {(double)sw - (double)pivot_x, -(double)pivot_y},
                            {(double)sw - (double)pivot_x, (double)sh - (double)pivot_y},
                            {-(double)pivot_x, (double)sh - (double)pivot_y}};
    double min_x = 1.0e30;
    double min_y = 1.0e30;
    double max_x = -1.0e30;
    double max_y = -1.0e30;
    for (int i = 0; i < 4; i++) {
        double rx = corners[i][0] * cos_a - corners[i][1] * sin_a;
        double ry = corners[i][0] * sin_a + corners[i][1] * cos_a;
        if (rx < min_x)
            min_x = rx;
        if (rx > max_x)
            max_x = rx;
        if (ry < min_y)
            min_y = ry;
        if (ry > max_y)
            max_y = ry;
    }

    int64_t x0 = (int64_t)floor(min_x);
    int64_t x1 = (int64_t)ceil(max_x);
    int64_t y0 = (int64_t)floor(min_y);
    int64_t y1 = (int64_t)ceil(max_y);
    wrap = wrap == RT_GRAPHICS2D_WRAP_REPEAT ? RT_GRAPHICS2D_WRAP_REPEAT : RT_GRAPHICS2D_WRAP_CLAMP;
    filter = filter == RT_GRAPHICS2D_FILTER_LINEAR ? RT_GRAPHICS2D_FILTER_LINEAR
                                                   : RT_GRAPHICS2D_FILTER_NEAREST;

    for (int64_t oy = y0; oy < y1; oy++) {
        int64_t out_y = dy + pivot_y + oy;
        if (out_y < 0 || out_y >= dh)
            continue;
        for (int64_t ox = x0; ox < x1; ox++) {
            int64_t out_x = dx + pivot_x + ox;
            if (out_x < 0 || out_x >= dw)
                continue;
            double sx = (double)ox * cos_a + (double)oy * sin_a + (double)pivot_x;
            double sy = -(double)ox * sin_a + (double)oy * cos_a + (double)pivot_y;
            if (wrap != RT_GRAPHICS2D_WRAP_REPEAT &&
                (sx < 0.0 || sy < 0.0 || sx >= (double)sw || sy >= (double)sh))
                continue;
            uint32_t source;
            if (filter == RT_GRAPHICS2D_FILTER_LINEAR) {
                int64_t base_x = (int64_t)floor(sx);
                int64_t base_y = (int64_t)floor(sy);
                long double tx = (long double)sx - (long double)base_x;
                long double ty = (long double)sy - (long double)base_y;
                int64_t ix0 = sampler_coord_i64(base_x, sw, wrap);
                int64_t ix1 = sampler_coord_i64(base_x + 1, sw, wrap);
                int64_t iy0 = sampler_coord_i64(base_y, sh, wrap);
                int64_t iy1 = sampler_coord_i64(base_y + 1, sh, wrap);
                source = bilerp_premul_rgba((uint32_t)rt_pixels_get(src, ix0, iy0),
                                            (uint32_t)rt_pixels_get(src, ix1, iy0),
                                            (uint32_t)rt_pixels_get(src, ix0, iy1),
                                            (uint32_t)rt_pixels_get(src, ix1, iy1),
                                            tx,
                                            ty);
            } else {
                int64_t ix = (int64_t)floor(sx);
                int64_t iy = (int64_t)floor(sy);
                ix = sampler_coord_i64(ix, sw, wrap);
                iy = sampler_coord_i64(iy, sh, wrap);
                source = (uint32_t)rt_pixels_get(src, ix, iy);
            }
            source = apply_tint_alpha(source, tint, alpha);
            if ((source & 255u) == 0)
                continue;
            uint32_t dest = (uint32_t)rt_pixels_get(dst, out_x, out_y);
            rt_pixels_set(dst, out_x, out_y, (int64_t)blend_pixel(dest, source, blend_mode));
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
void *copy_region_pixels(
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

static void *rendertarget2d_new_with_class(int64_t width, int64_t height, int64_t class_id) {
    if (!checked_count(width, height, 4, NULL)) {
        rt_trap("RenderTarget2D.New: invalid dimensions");
        return NULL;
    }
    rt_rendertarget2d_impl *target =
        (rt_rendertarget2d_impl *)rt_obj_new_i64(class_id, (int64_t)sizeof(rt_rendertarget2d_impl));
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

/// @brief Allocate a new offscreen render target sized `width × height` (RGBA).
/// @details Validates the dimensions through `checked_count` (caps at RT2D_MAX_DIM
///          and rejects overflowing byte counts) and traps on invalid inputs — the
///          caller gets a clear error instead of a silent NULL return for a bad
///          size. The owned `Pixels` buffer is allocated separately; if that
///          allocation fails, the partially-constructed impl is released cleanly.
/// @return RenderTarget2D handle, or NULL on allocation failure (trap already
///         fired for invalid dimensions).
void *rt_rendertarget2d_new(int64_t width, int64_t height) {
    return rendertarget2d_new_with_class(width, height, RT2D_RENDERTARGET_CLASS_ID);
}

/// @brief Allocate a Surface2D with distinct runtime type identity and RenderTarget2D behavior.
void *rt_surface2d_new(int64_t width, int64_t height) {
    return rendertarget2d_new_with_class(width, height, RT2D_SURFACE_CLASS_ID);
}

/// @brief Return the width of the render target's backing Pixels buffer in pixels, or 0 if invalid.
int64_t rt_rendertarget2d_width(void *target) {
    rt_rendertarget2d_impl *impl = rendertarget2d_checked(target);
    return impl ? rt_pixels_width(impl->pixels) : 0;
}

/// @brief Return the height of the render target's backing Pixels buffer in pixels, or 0 if
/// invalid.
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
    blit_pixels(
        impl->pixels, x, y, pixels, sx, sy, width, height, -1, 255, RT_GRAPHICS2D_BLEND_ALPHA);
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
static void *texture2d_new_with_class(void *pixels, int64_t class_id) {
    if (!pixels)
        return NULL;
    if (!rt_obj_is_instance(pixels, RT_PIXELS_CLASS_ID, sizeof(rt_pixels_impl)))
        return NULL;
    retain_ref(pixels);
    rt_texture2d_impl *texture =
        (rt_texture2d_impl *)rt_obj_new_i64(class_id, (int64_t)sizeof(rt_texture2d_impl));
    if (!texture) {
        void *owned_pixels = pixels;
        release_ref_slot(&owned_pixels);
        return NULL;
    }
    texture->magic = RT2D_TEXTURE_MAGIC;
    texture->pixels = pixels;
    texture->filter = RT_GRAPHICS2D_FILTER_NEAREST;
    texture->wrap = RT_GRAPHICS2D_WRAP_CLAMP;
    rt_obj_set_finalizer(texture, texture2d_finalize);
    return texture;
}

void *rt_texture2d_new(void *pixels) {
    return texture2d_new_with_class(pixels, RT2D_TEXTURE_CLASS_ID);
}

void *rt_gputexture2d_new(void *pixels) {
    return texture2d_new_with_class(pixels, RT2D_GPUTEXTURE_CLASS_ID);
}

/// @brief Load a Pixels buffer from disk and wrap it as a Texture2D.
/// @details The transient Pixels reference is released after the Texture2D takes its own
///   retain, so the caller gets a Texture2D with a net-zero effect on the Pixels refcount.
///   Returns NULL if the file cannot be loaded or the Texture2D allocation fails.
static void *texture2d_from_file_with_class(rt_string path, int64_t class_id) {
    void *pixels = rt_pixels_load(path);
    if (!pixels)
        return NULL;
    void *texture = texture2d_new_with_class(pixels, class_id);
    release_ref_slot(&pixels);
    return texture;
}

void *rt_texture2d_from_file(rt_string path) {
    return texture2d_from_file_with_class(path, RT2D_TEXTURE_CLASS_ID);
}

void *rt_gputexture2d_from_file(rt_string path) {
    return texture2d_from_file_with_class(path, RT2D_GPUTEXTURE_CLASS_ID);
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
    rt_renderer2d_impl *renderer = (rt_renderer2d_impl *)rt_obj_new_i64(
        RT2D_RENDERER_CLASS_ID, (int64_t)sizeof(rt_renderer2d_impl));
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
                             int64_t dst_width,
                             int64_t dst_height,
                             int64_t sampler_filter,
                             int64_t sampler_wrap) {
    if (!renderer || renderer->magic != RT2D_RENDERER_MAGIC || !renderer->active || !source)
        return;
    if (width <= 0 || height <= 0 || dst_width <= 0 || dst_height <= 0)
        return;
    if (!renderer2d_reserve(renderer, renderer->count + 1)) {
        rt_trap("Renderer2D: command capacity overflow");
        return;
    }
    retain_ref(source);
    rt_renderer2d_cmd *cmd = &renderer->cmds[renderer->count];
    cmd->source = source;
    cmd->source_kind = source_kind;
    cmd->x = x;
    cmd->y = y;
    cmd->sx = sx;
    cmd->sy = sy;
    cmd->width = width;
    cmd->height = height;
    cmd->dst_width = dst_width;
    cmd->dst_height = dst_height;
    cmd->tint = renderer->tint;
    cmd->alpha = renderer->alpha;
    cmd->blend_mode = renderer->blend_mode;
    cmd->sampler_filter = sampler_filter;
    cmd->sampler_wrap = sampler_wrap;
    cmd->angle_deg = 0.0;
    cmd->pivot_x = 0;
    cmd->pivot_y = 0;
    cmd->has_rotation = 0;
    renderer->count++;
}

/// @brief Queue a draw command to blit the full @p pixels buffer at `(x, y)` with current render
/// state.
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
                     p->width,
                     p->height,
                     RT_GRAPHICS2D_FILTER_NEAREST,
                     RT_GRAPHICS2D_WRAP_CLAMP);
}

/// @brief Queue a draw command to blit the full Texture2D at `(x, y)`, propagating its sampler
/// state.
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
                     rt_texture2d_width(texture),
                     rt_texture2d_height(texture),
                     rt_texture2d_get_filter(texture),
                     rt_texture2d_get_wrap(texture));
}

void rt_renderer2d_draw_texture_rotated(
    void *renderer, void *texture, int64_t x, int64_t y, double angle_deg) {
    if (!texture || !texture2d_checked(texture) || !rt2d_normalize_angle_degrees(&angle_deg))
        return;
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (!impl)
        return;
    int64_t width = rt_texture2d_width(texture);
    int64_t height = rt_texture2d_height(texture);
    int64_t before = impl->count;
    renderer2d_queue(impl,
                     1,
                     texture,
                     x,
                     y,
                     0,
                     0,
                     width,
                     height,
                     width,
                     height,
                     rt_texture2d_get_filter(texture),
                     rt_texture2d_get_wrap(texture));
    if (impl->count > before) {
        rt_renderer2d_cmd *cmd = &impl->cmds[impl->count - 1];
        cmd->has_rotation = 1;
        cmd->pivot_x = width / 2;
        cmd->pivot_y = height / 2;
        cmd->angle_deg = angle_deg;
    }
}

void rt_renderer2d_draw_texture_rotated_at(void *renderer,
                                           void *texture,
                                           int64_t x,
                                           int64_t y,
                                           int64_t pivot_x,
                                           int64_t pivot_y,
                                           double angle_deg) {
    if (!texture || !texture2d_checked(texture) || !rt2d_normalize_angle_degrees(&angle_deg))
        return;
    rt_renderer2d_impl *impl = renderer2d_checked(renderer);
    if (!impl)
        return;
    int64_t width = rt_texture2d_width(texture);
    int64_t height = rt_texture2d_height(texture);
    int64_t before = impl->count;
    renderer2d_queue(impl,
                     1,
                     texture,
                     x,
                     y,
                     0,
                     0,
                     width,
                     height,
                     width,
                     height,
                     rt_texture2d_get_filter(texture),
                     rt_texture2d_get_wrap(texture));
    if (impl->count > before) {
        rt_renderer2d_cmd *cmd = &impl->cmds[impl->count - 1];
        cmd->has_rotation = 1;
        cmd->pivot_x = pivot_x;
        cmd->pivot_y = pivot_y;
        cmd->angle_deg = angle_deg;
    }
}

/// @brief Queue a scaled full-texture draw, using the texture's filter and wrap state.
void rt_renderer2d_draw_texture_scaled(
    void *renderer, void *texture, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!texture || !texture2d_checked(texture) || width <= 0 || height <= 0)
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
                     width,
                     height,
                     rt_texture2d_get_filter(texture),
                     rt_texture2d_get_wrap(texture));
}

/// @brief Queue a texture-region draw. Out-of-bounds source texels obey the texture wrap mode.
void rt_renderer2d_draw_texture_region(void *renderer,
                                       void *texture,
                                       int64_t x,
                                       int64_t y,
                                       int64_t sx,
                                       int64_t sy,
                                       int64_t width,
                                       int64_t height) {
    if (!texture || !texture2d_checked(texture) || width <= 0 || height <= 0)
        return;
    renderer2d_queue(renderer2d_checked(renderer),
                     1,
                     texture,
                     x,
                     y,
                     sx,
                     sy,
                     width,
                     height,
                     width,
                     height,
                     rt_texture2d_get_filter(texture),
                     rt_texture2d_get_wrap(texture));
}

/// @brief Queue a draw command to blit the sub-region `(sx, sy, width, height)` of @p pixels at
/// `(x, y)`.
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
    if (cmd->has_rotation) {
        blit_pixels_rotated(target_pixels,
                            cmd->x,
                            cmd->y,
                            pixels,
                            cmd->pivot_x,
                            cmd->pivot_y,
                            cmd->angle_deg,
                            cmd->tint,
                            cmd->alpha,
                            cmd->blend_mode,
                            cmd->sampler_filter,
                            cmd->sampler_wrap);
        return;
    }
    if (cmd->source_kind == 1) {
        blit_pixels_sampled_scaled(target_pixels,
                                   cmd->x,
                                   cmd->y,
                                   pixels,
                                   cmd->sx,
                                   cmd->sy,
                                   cmd->width,
                                   cmd->height,
                                   cmd->dst_width,
                                   cmd->dst_height,
                                   cmd->tint,
                                   cmd->alpha,
                                   cmd->blend_mode,
                                   cmd->sampler_filter,
                                   cmd->sampler_wrap);
        return;
    }
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

    int64_t dx = x;
    int64_t dy = y;
    int64_t sx = 0;
    int64_t sy = 0;
    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    if (width <= 0 || height <= 0)
        return;
    int64_t canvas_width = rt_canvas_width(canvas);
    int64_t canvas_height = rt_canvas_height(canvas);
    if (canvas_width <= 0 || canvas_height <= 0)
        return;
    if (dx < 0) {
        int64_t shift = dx == INT64_MIN ? INT64_MAX : -dx;
        if (shift >= width)
            return;
        sx = shift;
        width -= shift;
        dx = 0;
    }
    if (dy < 0) {
        int64_t shift = dy == INT64_MIN ? INT64_MAX : -dy;
        if (shift >= height)
            return;
        sy = shift;
        height -= shift;
        dy = 0;
    }
    if (dx >= canvas_width || dy >= canvas_height)
        return;
    if (width > canvas_width - dx)
        width = canvas_width - dx;
    if (height > canvas_height - dy)
        height = canvas_height - dy;
    if (width <= 0 || height <= 0)
        return;

    void *dest = rt_canvas_copy_rect(canvas, dx, dy, width, height);
    if (!dest)
        return;
    blit_pixels(dest, 0, 0, pixels, sx, sy, width, height, -1, 255, RT_GRAPHICS2D_BLEND_ADD);
    rt_canvas_blit(canvas, dx, dy, dest);
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
            if (cmd->has_rotation) {
                double angle_deg = cmd->angle_deg;
                if (!rt2d_normalize_angle_degrees(&angle_deg))
                    continue;
                int64_t sw = rt_pixels_width(pixels);
                int64_t sh = rt_pixels_height(pixels);
                double radians = angle_deg * (double)RT2D_PI / 180.0;
                double cos_a = cos(radians);
                double sin_a = sin(radians);
                double corners[4][2] = {
                    {-(double)cmd->pivot_x, -(double)cmd->pivot_y},
                    {(double)sw - (double)cmd->pivot_x, -(double)cmd->pivot_y},
                    {(double)sw - (double)cmd->pivot_x, (double)sh - (double)cmd->pivot_y},
                    {-(double)cmd->pivot_x, (double)sh - (double)cmd->pivot_y}};
                double min_x = 1.0e30;
                double min_y = 1.0e30;
                double max_x = -1.0e30;
                double max_y = -1.0e30;
                for (int j = 0; j < 4; j++) {
                    double rx = corners[j][0] * cos_a - corners[j][1] * sin_a;
                    double ry = corners[j][0] * sin_a + corners[j][1] * cos_a;
                    if (rx < min_x)
                        min_x = rx;
                    if (rx > max_x)
                        max_x = rx;
                    if (ry < min_y)
                        min_y = ry;
                    if (ry > max_y)
                        max_y = ry;
                }
                int64_t x0 = (int64_t)floor(min_x);
                int64_t x1 = (int64_t)ceil(max_x);
                int64_t y0 = (int64_t)floor(min_y);
                int64_t y1 = (int64_t)ceil(max_y);
                int64_t rw = x1 - x0;
                int64_t rh = y1 - y0;
                if (rw <= 0 || rh <= 0 || !checked_count(rw, rh, 4, NULL))
                    continue;
                region = rt_pixels_new(rw, rh);
                if (!region)
                    continue;
                blit_pixels_rotated(region,
                                    -x0 - cmd->pivot_x,
                                    -y0 - cmd->pivot_y,
                                    pixels,
                                    cmd->pivot_x,
                                    cmd->pivot_y,
                                    angle_deg,
                                    cmd->tint,
                                    cmd->alpha,
                                    RT_GRAPHICS2D_BLEND_OPAQUE,
                                    cmd->sampler_filter,
                                    cmd->sampler_wrap);
                draw_pixels = region;
                blit_x = cmd->x + cmd->pivot_x + x0;
                blit_y = cmd->y + cmd->pivot_y + y0;
            } else if (cmd->source_kind == 1 &&
                       (cmd->sx != 0 || cmd->sy != 0 || cmd->width != rt_pixels_width(pixels) ||
                        cmd->height != rt_pixels_height(pixels) || cmd->dst_width != cmd->width ||
                        cmd->dst_height != cmd->height ||
                        cmd->sampler_wrap == RT_GRAPHICS2D_WRAP_REPEAT ||
                        cmd->sampler_filter == RT_GRAPHICS2D_FILTER_LINEAR)) {
                if (!checked_count(cmd->dst_width, cmd->dst_height, 4, NULL))
                    continue;
                region = rt_pixels_new(cmd->dst_width, cmd->dst_height);
                if (!region)
                    continue;
                blit_pixels_sampled_scaled(region,
                                           0,
                                           0,
                                           pixels,
                                           cmd->sx,
                                           cmd->sy,
                                           cmd->width,
                                           cmd->height,
                                           cmd->dst_width,
                                           cmd->dst_height,
                                           -1,
                                           255,
                                           RT_GRAPHICS2D_BLEND_OPAQUE,
                                           cmd->sampler_filter,
                                           cmd->sampler_wrap);
                draw_pixels = region;
            } else if (cmd->sx != 0 || cmd->sy != 0 || cmd->width != rt_pixels_width(pixels) ||
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
            void *processed = processed_pixels_or_null(draw_pixels,
                                                       cmd->has_rotation ? -1 : cmd->tint,
                                                       cmd->has_rotation ? 255 : cmd->alpha);
            if (processed)
                draw_pixels = processed;
            if (draw_pixels)
                renderer2d_blit_pixels_to_canvas(
                    canvas, blit_x, blit_y, draw_pixels, cmd->blend_mode);
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
    rt_material2d_impl *material = (rt_material2d_impl *)rt_obj_new_i64(
        RT2D_MATERIAL_CLASS_ID, (int64_t)sizeof(rt_material2d_impl));
    if (!material)
        return NULL;
    material->tint = -1;
    material->alpha = 255;
    material->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    return material;
}

/// @brief Set the multiplicative tint color (`0x00RRGGBB`); negative disables tinting.
void rt_material2d_set_tint(void *material, int64_t rgb) {
    if (rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID))
        ((rt_material2d_impl *)material)->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

/// @brief Get the current tint color, or -1 (no tint) for an invalid material.
int64_t rt_material2d_get_tint(void *material) {
    return rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID) ? ((rt_material2d_impl *)material)->tint
                                                            : -1;
}

/// @brief Set the alpha scale clamped to `[0, 255]`.
void rt_material2d_set_alpha(void *material, int64_t alpha) {
    if (rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID))
        ((rt_material2d_impl *)material)->alpha = clamp_u8_i64(alpha);
}

/// @brief Get the current alpha scale in `[0, 255]`, defaulting to 255 for invalid handles.
int64_t rt_material2d_get_alpha(void *material) {
    return rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID)
               ? ((rt_material2d_impl *)material)->alpha
               : 255;
}

/// @brief Set the blend mode (`RT_GRAPHICS2D_BLEND_*`) clamped to `[0, 2]`.
void rt_material2d_set_blend_mode(void *material, int64_t blend_mode) {
    if (rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID))
        ((rt_material2d_impl *)material)->blend_mode = clamp_i64(blend_mode, 0, 2);
}

/// @brief Get the current blend mode, defaulting to ALPHA for invalid handles.
int64_t rt_material2d_get_blend_mode(void *material) {
    return rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID)
               ? ((rt_material2d_impl *)material)->blend_mode
               : RT_GRAPHICS2D_BLEND_ALPHA;
}

/// @brief Produce a tint+alpha-applied copy of `pixels` using this material's state.
/// @details Clones the source and applies tint / alpha in place on the clone;
///          the original Pixels is untouched. Returns NULL if either argument is
///          NULL or the clone fails. Blend mode is NOT baked in — it's a
///          *compositing* choice made when the processed Pixels are drawn, not
///          when they're produced.
void *rt_material2d_apply(void *material, void *pixels) {
    if (!rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID) || !pixels)
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
        (rt_shader2d_impl *)rt_obj_new_i64(RT2D_SHADER_CLASS_ID, (int64_t)sizeof(rt_shader2d_impl));
    if (!shader)
        return NULL;
    shader->effect = clamp_i64(effect, 0, 4);
    shader->amount = 1;
    shader->color = 0x00FFFFFF;
    return shader;
}

/// @brief Set the shader effect index clamped to `[0, 4]` (NONE/INVERT/GRAYSCALE/TINT/BLUR).
void rt_shader2d_set_effect(void *shader, int64_t effect) {
    rt_shader2d_impl *impl = shader2d_checked(shader);
    if (impl)
        impl->effect = clamp_i64(effect, 0, 4);
}

/// @brief Get the current effect index, defaulting to NONE for invalid handles.
int64_t rt_shader2d_get_effect(void *shader) {
    rt_shader2d_impl *impl = shader2d_checked(shader);
    return impl ? impl->effect : RT_GRAPHICS2D_EFFECT_NONE;
}

/// @brief Set the effect amount (e.g. blur radius) clamped to `[0, 10]`.
void rt_shader2d_set_amount(void *shader, int64_t amount) {
    rt_shader2d_impl *impl = shader2d_checked(shader);
    if (impl)
        impl->amount = clamp_i64(amount, 0, 10);
}

/// @brief Get the current effect amount, or 0 for invalid handles.
int64_t rt_shader2d_get_amount(void *shader) {
    rt_shader2d_impl *impl = shader2d_checked(shader);
    return impl ? impl->amount : 0;
}

/// @brief Set the effect color (`0x00RRGGBB`), used for the TINT effect.
void rt_shader2d_set_color(void *shader, int64_t rgb) {
    rt_shader2d_impl *impl = shader2d_checked(shader);
    if (impl)
        impl->color = rgb & 0x00FFFFFF;
}

/// @brief Get the current effect color, defaulting to white (0x00FFFFFF) for invalid handles.
int64_t rt_shader2d_get_color(void *shader) {
    rt_shader2d_impl *impl = shader2d_checked(shader);
    return impl ? impl->color : 0x00FFFFFF;
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
    if (!shader2d_checked(shader))
        return NULL;
    rt_shader2d_impl *impl = (rt_shader2d_impl *)shader;
    return apply_effect(impl->effect, impl->amount, impl->color, pixels);
}

/// @brief Allocate a PostProcess2D (a Shader2D starting with EFFECT_NONE).
void *rt_postprocess2d_new(void) {
    rt_postprocess2d_impl *postprocess = (rt_postprocess2d_impl *)rt_obj_new_i64(
        RT2D_POSTPROCESS_CLASS_ID, (int64_t)sizeof(rt_postprocess2d_impl));
    if (!postprocess)
        return NULL;
    postprocess->effect = RT_GRAPHICS2D_EFFECT_NONE;
    postprocess->amount = 1;
    postprocess->color = 0x00FFFFFF;
    return postprocess;
}

/// @brief Set the post-process effect index (forwarded to Shader2D).
void rt_postprocess2d_set_effect(void *postprocess, int64_t effect) {
    rt_postprocess2d_impl *impl = postprocess2d_checked(postprocess);
    if (impl)
        impl->effect = clamp_i64(effect, 0, 4);
}

/// @brief Set the post-process effect amount (forwarded to Shader2D).
void rt_postprocess2d_set_amount(void *postprocess, int64_t amount) {
    rt_postprocess2d_impl *impl = postprocess2d_checked(postprocess);
    if (impl)
        impl->amount = clamp_i64(amount, 0, 10);
}

/// @brief Set the post-process effect color (forwarded to Shader2D).
void rt_postprocess2d_set_color(void *postprocess, int64_t rgb) {
    rt_postprocess2d_impl *impl = postprocess2d_checked(postprocess);
    if (impl)
        impl->color = rgb & 0x00FFFFFF;
}

/// @brief Apply the post-process effect and return a new Pixels buffer (forwarded to Shader2D).
void *rt_postprocess2d_apply(void *postprocess, void *pixels) {
    if (!postprocess)
        return pixels ? rt_pixels_clone(pixels) : NULL;
    rt_postprocess2d_impl *impl = postprocess2d_checked(postprocess);
    if (!impl)
        return NULL;
    return apply_effect(impl->effect, impl->amount, impl->color, pixels);
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
    if (viewport->integer_scaling && viewport->scale >= 1000) {
        int64_t whole = viewport->scale / 1000;
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

static void *viewport2d_new_with_class(int64_t virtual_width,
                                       int64_t virtual_height,
                                       int64_t screen_width,
                                       int64_t screen_height,
                                       int64_t class_id) {
    rt_viewport2d_impl *viewport =
        (rt_viewport2d_impl *)rt_obj_new_i64(class_id, (int64_t)sizeof(rt_viewport2d_impl));
    if (!viewport)
        return NULL;
    viewport->virtual_width = normalized_dim(virtual_width);
    viewport->virtual_height = normalized_dim(virtual_height);
    viewport->screen_width = normalized_dim(screen_width);
    viewport->screen_height = normalized_dim(screen_height);
    viewport2d_recalculate(viewport);
    return viewport;
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
    return viewport2d_new_with_class(
        virtual_width, virtual_height, screen_width, screen_height, RT2D_VIEWPORT_CLASS_ID);
}

/// @brief Allocate a ScreenScaler with distinct runtime type identity and Viewport2D behavior.
void *rt_screenscaler_new(int64_t virtual_width,
                          int64_t virtual_height,
                          int64_t screen_width,
                          int64_t screen_height) {
    return viewport2d_new_with_class(
        virtual_width, virtual_height, screen_width, screen_height, RT2D_SCREENSCALER_CLASS_ID);
}

/// @brief Update the virtual (design) canvas size and recalculate scale/offsets.
void rt_viewport2d_set_virtual_size(void *viewport, int64_t width, int64_t height) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return;
    impl->virtual_width = normalized_dim(width);
    impl->virtual_height = normalized_dim(height);
    viewport2d_recalculate(impl);
}

/// @brief Update the physical screen size and recalculate scale/offsets.
void rt_viewport2d_set_screen_size(void *viewport, int64_t width, int64_t height) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return;
    impl->screen_width = normalized_dim(width);
    impl->screen_height = normalized_dim(height);
    viewport2d_recalculate(impl);
}

/// @brief Enable or disable integer-only scaling (whole-number scale factors only).
void rt_viewport2d_set_integer_scaling(void *viewport, int64_t enabled) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return;
    impl->integer_scaling = enabled != 0;
    viewport2d_recalculate(impl);
}

/// @brief Return the current scale factor as a fixed-point integer (1000 = 1.0x).
int64_t rt_viewport2d_get_scale(void *viewport) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    return impl ? impl->scale : 1000;
}

/// @brief Return the horizontal letterbox/pillarbox offset in screen pixels.
int64_t rt_viewport2d_get_offset_x(void *viewport) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    return impl ? impl->offset_x : 0;
}

/// @brief Return the vertical letterbox/pillarbox offset in screen pixels.
int64_t rt_viewport2d_get_offset_y(void *viewport) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    return impl ? impl->offset_y : 0;
}

/// @brief Convert a virtual-space X coordinate to a screen-space X pixel position.
/// @details Applies `screen_x = offset_x + (virtual_x * scale / 1000)` using `long double`
///   to preserve sub-pixel accuracy before rounding to the nearest integer.
int64_t rt_viewport2d_world_to_screen_x(void *viewport, int64_t x) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return x;
    long double mapped =
        (long double)impl->offset_x + ((long double)x * (long double)impl->scale) / 1000.0L;
    return round_long_double_to_i64(mapped);
}

/// @brief Convert a virtual-space Y coordinate to a screen-space Y pixel position.
int64_t rt_viewport2d_world_to_screen_y(void *viewport, int64_t y) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return y;
    long double mapped =
        (long double)impl->offset_y + ((long double)y * (long double)impl->scale) / 1000.0L;
    return round_long_double_to_i64(mapped);
}

/// @brief Convert a screen-space X pixel position back to a virtual-space X coordinate.
/// @details Inverse of `world_to_screen_x`: `virtual_x = (screen_x - offset_x) * 1000 / scale`.
int64_t rt_viewport2d_screen_to_world_x(void *viewport, int64_t x) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return x;
    long double mapped =
        ((long double)x - (long double)impl->offset_x) * 1000.0L / (long double)impl->scale;
    return round_long_double_to_i64(mapped);
}

/// @brief Convert a screen-space Y pixel position back to a virtual-space Y coordinate.
int64_t rt_viewport2d_screen_to_world_y(void *viewport, int64_t y) {
    rt_viewport2d_impl *impl = viewport2d_checked(viewport);
    if (!impl)
        return y;
    long double mapped =
        ((long double)y - (long double)impl->offset_y) * 1000.0L / (long double)impl->scale;
    return round_long_double_to_i64(mapped);
}

//=============================================================================
// Path2D
//=============================================================================
// Dynamic sequence of `(x, y, move)` points. `move == 1` marks a pen-up
// break — `MoveTo` followed by `LineTo` forms a subpath. `DrawToPixels`
// walks the list and renders a line between every consecutive non-move pair.

/// @brief GC finalizer — frees the points buffer.
static void path2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_PATH_CLASS_ID))
        return;
    rt_path2d_impl *path = (rt_path2d_impl *)obj;
    free(path->points);
}

/// @brief Allocate a Path2D with the given initial point capacity.
/// @details Capacity is clamped by `initial_capacity`. On allocation failure
///          the impl is torn down cleanly and NULL is returned.
void *rt_path2d_new(int64_t capacity) {
    rt_path2d_impl *path =
        (rt_path2d_impl *)rt_obj_new_i64(RT2D_PATH_CLASS_ID, (int64_t)sizeof(rt_path2d_impl));
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
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
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
    if (rt2d_has_class(path, RT2D_PATH_CLASS_ID))
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
    return rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? ((rt_path2d_impl *)path)->count : 0;
}

/// @brief Return the X coordinate of the point at `index`, or 0 if out of range.
int64_t rt_path2d_get_x(void *path, int64_t index) {
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].x;
}

/// @brief Return the Y coordinate of the point at `index`, or 0 if out of range.
int64_t rt_path2d_get_y(void *path, int64_t index) {
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
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
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
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
    uint8_t defined[256];
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
    rt_transform2d_impl *impl = (rt_transform2d_impl *)rt_obj_new_i64(
        RT2D_TRANSFORM_CLASS_ID, (int64_t)sizeof(rt_transform2d_impl));
    if (!impl)
        return NULL;
    impl->scale_x = 100;
    impl->scale_y = 100;
    return impl;
}

/// @brief Return the X world-space position of the transform.
int64_t rt_transform2d_get_x(void *transform) {
    return rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
               ? ((rt_transform2d_impl *)transform)->x
               : 0;
}

/// @brief Set the X world-space position of the transform.
void rt_transform2d_set_x(void *transform, int64_t x) {
    if (rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        ((rt_transform2d_impl *)transform)->x = x;
}

/// @brief Return the Y world-space position of the transform.
int64_t rt_transform2d_get_y(void *transform) {
    return rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
               ? ((rt_transform2d_impl *)transform)->y
               : 0;
}

/// @brief Set the Y world-space position of the transform.
void rt_transform2d_set_y(void *transform, int64_t y) {
    if (rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        ((rt_transform2d_impl *)transform)->y = y;
}

/// @brief Return the X scale in fixed-point units (100 == 1.0×).
int64_t rt_transform2d_get_scale_x(void *transform) {
    return rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
               ? ((rt_transform2d_impl *)transform)->scale_x
               : 100;
}

/// @brief Set the X scale; clamped to [1, 10000] (0.01× to 100×).
void rt_transform2d_set_scale_x(void *transform, int64_t scale_x) {
    if (rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        ((rt_transform2d_impl *)transform)->scale_x = clamp_i64(scale_x, 1, 10000);
}

/// @brief Return the Y scale in fixed-point units (100 == 1.0×).
int64_t rt_transform2d_get_scale_y(void *transform) {
    return rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
               ? ((rt_transform2d_impl *)transform)->scale_y
               : 100;
}

/// @brief Set the Y scale; clamped to [1, 10000].
void rt_transform2d_set_scale_y(void *transform, int64_t scale_y) {
    if (rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        ((rt_transform2d_impl *)transform)->scale_y = clamp_i64(scale_y, 1, 10000);
}

/// @brief Return the rotation in integer degrees (no clamping — can be any multiple of 1°).
int64_t rt_transform2d_get_rotation(void *transform) {
    return rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
               ? ((rt_transform2d_impl *)transform)->rotation
               : 0;
}

/// @brief Set the rotation in integer degrees.
void rt_transform2d_set_rotation(void *transform, int64_t degrees) {
    if (rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        ((rt_transform2d_impl *)transform)->rotation = degrees;
}

/// @brief Set both X and Y position in one call.
void rt_transform2d_set_position(void *transform, int64_t x, int64_t y) {
    if (!rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
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
    if (!rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        return;
    rt_transform2d_impl *impl = (rt_transform2d_impl *)transform;
    impl->origin_x = x;
    impl->origin_y = y;
}

/// @brief Translate (move) the transform by `(dx, dy)` using saturating addition.
void rt_transform2d_translate(void *transform, int64_t dx, int64_t dy) {
    if (!rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID))
        return;
    rt_transform2d_impl *impl = (rt_transform2d_impl *)transform;
    impl->x = saturating_add_i64(impl->x, dx);
    impl->y = saturating_add_i64(impl->y, dy);
}

/// @brief Return the transformed X coordinate for input point `(x, y)`.
/// @details Delegates to `transform2d_point`; useful when callers only need the X component.
int64_t rt_transform2d_transform_x(void *transform, int64_t x, int64_t y) {
    int64_t out_x = x;
    transform2d_point(rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
                          ? (rt_transform2d_impl *)transform
                          : NULL,
                      x,
                      y,
                      &out_x,
                      NULL);
    return out_x;
}

/// @brief Return the transformed Y coordinate for input point `(x, y)`.
/// @details Delegates to `transform2d_point`; useful when callers only need the Y component.
int64_t rt_transform2d_transform_y(void *transform, int64_t x, int64_t y) {
    int64_t out_y = y;
    transform2d_point(rt2d_has_class(transform, RT2D_TRANSFORM_CLASS_ID)
                          ? (rt_transform2d_impl *)transform
                          : NULL,
                      x,
                      y,
                      NULL,
                      &out_y);
    return out_y;
}

/// @brief Allocate a Sampler2D with default state (nearest filter, clamp wrap).
void *rt_sampler2d_new(void) {
    rt_sampler2d_impl *impl = (rt_sampler2d_impl *)rt_obj_new_i64(
        RT2D_SAMPLER_CLASS_ID, (int64_t)sizeof(rt_sampler2d_impl));
    if (!impl)
        return NULL;
    impl->filter = RT_GRAPHICS2D_FILTER_NEAREST;
    impl->wrap = RT_GRAPHICS2D_WRAP_CLAMP;
    return impl;
}

/// @brief Set the filter mode; unrecognised values fall back to NEAREST.
void rt_sampler2d_set_filter(void *sampler, int64_t filter) {
    if (rt2d_has_class(sampler, RT2D_SAMPLER_CLASS_ID))
        ((rt_sampler2d_impl *)sampler)->filter = filter == RT_GRAPHICS2D_FILTER_LINEAR
                                                     ? RT_GRAPHICS2D_FILTER_LINEAR
                                                     : RT_GRAPHICS2D_FILTER_NEAREST;
}

/// @brief Return the current filter mode (NEAREST or LINEAR).
int64_t rt_sampler2d_get_filter(void *sampler) {
    return rt2d_has_class(sampler, RT2D_SAMPLER_CLASS_ID) ? ((rt_sampler2d_impl *)sampler)->filter
                                                          : RT_GRAPHICS2D_FILTER_NEAREST;
}

/// @brief Set the wrap mode; unrecognised values fall back to CLAMP.
void rt_sampler2d_set_wrap(void *sampler, int64_t wrap) {
    if (rt2d_has_class(sampler, RT2D_SAMPLER_CLASS_ID))
        ((rt_sampler2d_impl *)sampler)->wrap = wrap == RT_GRAPHICS2D_WRAP_REPEAT
                                                   ? RT_GRAPHICS2D_WRAP_REPEAT
                                                   : RT_GRAPHICS2D_WRAP_CLAMP;
}

/// @brief Return the current wrap mode (CLAMP or REPEAT).
int64_t rt_sampler2d_get_wrap(void *sampler) {
    return rt2d_has_class(sampler, RT2D_SAMPLER_CLASS_ID) ? ((rt_sampler2d_impl *)sampler)->wrap
                                                          : RT_GRAPHICS2D_WRAP_CLAMP;
}

/// @brief Push the sampler's filter and wrap settings into a Texture2D.
/// @details Convenience combinator — equivalent to calling `SetFilter` + `SetWrap`
///          on the texture directly, but expressed through the sampler's parameters.
void rt_sampler2d_apply_to_texture(void *sampler, void *texture) {
    if (!rt2d_has_class(sampler, RT2D_SAMPLER_CLASS_ID) || !texture)
        return;
    rt_texture2d_set_filter(texture, rt_sampler2d_get_filter(sampler));
    rt_texture2d_set_wrap(texture, rt_sampler2d_get_wrap(sampler));
}

/// @brief Allocate a BlendState2D with default state (alpha blend, no tint, full opacity).
/// @details `tint` defaults to -1 meaning "no tint override"; `alpha` defaults to 255.
void *rt_blendstate2d_new(void) {
    rt_blendstate2d_impl *impl = (rt_blendstate2d_impl *)rt_obj_new_i64(
        RT2D_BLENDSTATE_CLASS_ID, (int64_t)sizeof(rt_blendstate2d_impl));
    if (!impl)
        return NULL;
    impl->blend_mode = RT_GRAPHICS2D_BLEND_ALPHA;
    impl->tint = -1;
    impl->alpha = 255;
    return impl;
}

/// @brief Set the blend mode; clamped to [0, 2] (NONE / ALPHA / ADDITIVE).
void rt_blendstate2d_set_blend_mode(void *state, int64_t blend_mode) {
    if (rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID))
        ((rt_blendstate2d_impl *)state)->blend_mode = clamp_i64(blend_mode, 0, 2);
}

/// @brief Return the current blend mode.
int64_t rt_blendstate2d_get_blend_mode(void *state) {
    return rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID)
               ? ((rt_blendstate2d_impl *)state)->blend_mode
               : RT_GRAPHICS2D_BLEND_ALPHA;
}

/// @brief Set the tint as 0x00RRGGBB; pass a negative value to disable tint.
void rt_blendstate2d_set_tint(void *state, int64_t rgb) {
    if (rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID))
        ((rt_blendstate2d_impl *)state)->tint = rgb < 0 ? -1 : (rgb & 0x00FFFFFF);
}

/// @brief Return the tint color (0x00RRGGBB), or -1 if no tint is set.
int64_t rt_blendstate2d_get_tint(void *state) {
    return rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID) ? ((rt_blendstate2d_impl *)state)->tint
                                                           : -1;
}

/// @brief Set the global alpha in [0, 255]; values are clamped.
void rt_blendstate2d_set_alpha(void *state, int64_t alpha) {
    if (rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID))
        ((rt_blendstate2d_impl *)state)->alpha = clamp_u8_i64(alpha);
}

/// @brief Return the current global alpha in [0, 255].
int64_t rt_blendstate2d_get_alpha(void *state) {
    return rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID) ? ((rt_blendstate2d_impl *)state)->alpha
                                                           : 255;
}

/// @brief Push blend mode, alpha, and tint from this state into a Renderer2D.
/// @details Equivalent to three separate `SetBlendMode` / `SetAlpha` / `SetTint`
///          calls, combined into a single apply step.
void rt_blendstate2d_apply_to_renderer(void *state, void *renderer) {
    if (!rt2d_has_class(state, RT2D_BLENDSTATE_CLASS_ID) || !renderer)
        return;
    rt_renderer2d_set_blend_mode(renderer, rt_blendstate2d_get_blend_mode(state));
    rt_renderer2d_set_alpha(renderer, rt_blendstate2d_get_alpha(state));
    rt_renderer2d_set_tint(renderer, rt_blendstate2d_get_tint(state));
}

/// @brief GC finalizer — releases retained references to material, sampler, and blend state.
static void spriterenderer2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_SPRITERENDERER_CLASS_ID))
        return;
    rt_spriterenderer2d_impl *impl = (rt_spriterenderer2d_impl *)obj;
    release_ref_slot(&impl->material);
    release_ref_slot(&impl->sampler);
    release_ref_slot(&impl->blend_state);
}

/// @brief Allocate a SpriteRenderer2D with no material, sampler, or blend state bound.
void *rt_spriterenderer2d_new(void) {
    rt_spriterenderer2d_impl *impl = (rt_spriterenderer2d_impl *)rt_obj_new_i64(
        RT2D_SPRITERENDERER_CLASS_ID, (int64_t)sizeof(rt_spriterenderer2d_impl));
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
    if (rt2d_has_class(sprite_renderer, RT2D_SPRITERENDERER_CLASS_ID) &&
        (!material || rt2d_has_class(material, RT2D_MATERIAL_CLASS_ID)))
        spriterenderer2d_set_ref(&((rt_spriterenderer2d_impl *)sprite_renderer)->material,
                                 material);
}

/// @brief Bind a Sampler2D to this renderer, retaining a reference.
/// @details When set, the sampler's filter and wrap settings override the texture's
///          own settings during `DrawTexture`.
void rt_spriterenderer2d_set_sampler(void *sprite_renderer, void *sampler) {
    if (rt2d_has_class(sprite_renderer, RT2D_SPRITERENDERER_CLASS_ID) &&
        (!sampler || rt2d_has_class(sampler, RT2D_SAMPLER_CLASS_ID)))
        spriterenderer2d_set_ref(&((rt_spriterenderer2d_impl *)sprite_renderer)->sampler, sampler);
}

/// @brief Bind a BlendState2D to this renderer, retaining a reference.
/// @details BlendState overrides are applied *after* material overrides, so the
///          blend state wins if both are bound and specify the same property.
void rt_spriterenderer2d_set_blend_state(void *sprite_renderer, void *blend_state) {
    if (rt2d_has_class(sprite_renderer, RT2D_SPRITERENDERER_CLASS_ID) &&
        (!blend_state || rt2d_has_class(blend_state, RT2D_BLENDSTATE_CLASS_ID)))
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

typedef struct {
    int64_t tint;
    int64_t alpha;
    int64_t blend_mode;
} rt_renderer2d_saved_state;

/// @brief Snapshot a renderer's tint/alpha/blend mode into @p state so it can
///        be restored after a temporary override (see renderer2d_restore_state).
static void renderer2d_save_state(rt_renderer2d_impl *renderer, rt_renderer2d_saved_state *state) {
    if (!renderer || !state)
        return;
    state->tint = renderer->tint;
    state->alpha = renderer->alpha;
    state->blend_mode = renderer->blend_mode;
}

/// @brief Restore a renderer's tint/alpha/blend mode previously captured by
///        renderer2d_save_state.
static void renderer2d_restore_state(rt_renderer2d_impl *renderer,
                                     const rt_renderer2d_saved_state *state) {
    if (!renderer || !state)
        return;
    renderer->tint = state->tint;
    renderer->alpha = state->alpha;
    renderer->blend_mode = state->blend_mode;
}

/// @brief Apply state and draw a Pixels buffer into the Renderer2D at `(x, y)`.
/// @details Pushes material/blend-state overrides onto the renderer, then issues
///          `rt_renderer2d_draw_pixels`. The renderer's prior tint/alpha/blend-mode
///          are restored after the draw is queued.
void rt_spriterenderer2d_draw_pixels(
    void *sprite_renderer, void *renderer, void *pixels, int64_t x, int64_t y) {
    rt_spriterenderer2d_impl *impl = rt2d_has_class(sprite_renderer, RT2D_SPRITERENDERER_CLASS_ID)
                                         ? (rt_spriterenderer2d_impl *)sprite_renderer
                                         : NULL;
    rt_renderer2d_impl *renderer_impl = renderer2d_checked(renderer);
    if (!impl || !renderer_impl)
        return;
    rt_renderer2d_saved_state state;
    renderer2d_save_state(renderer_impl, &state);
    spriterenderer2d_apply_state(impl, renderer);
    rt_renderer2d_draw_pixels(renderer, pixels, x, y);
    renderer2d_restore_state(renderer_impl, &state);
}

/// @brief Apply state and draw a Texture2D into the Renderer2D at `(x, y)`.
/// @details Same state-push as `DrawPixels`, but also resolves filter/wrap from
///          the bound sampler (if any) before queuing the texture draw command.
void rt_spriterenderer2d_draw_texture(
    void *sprite_renderer, void *renderer, void *texture, int64_t x, int64_t y) {
    rt_spriterenderer2d_impl *impl = rt2d_has_class(sprite_renderer, RT2D_SPRITERENDERER_CLASS_ID)
                                         ? (rt_spriterenderer2d_impl *)sprite_renderer
                                         : NULL;
    rt_renderer2d_impl *renderer_impl = renderer2d_checked(renderer);
    if (!impl || !renderer_impl || !texture2d_checked(texture))
        return;
    rt_renderer2d_saved_state state;
    renderer2d_save_state(renderer_impl, &state);
    spriterenderer2d_apply_state(impl, renderer);
    int64_t filter = impl && impl->sampler ? rt_sampler2d_get_filter(impl->sampler)
                                           : rt_texture2d_get_filter(texture);
    int64_t wrap = impl && impl->sampler ? rt_sampler2d_get_wrap(impl->sampler)
                                         : rt_texture2d_get_wrap(texture);
    renderer2d_queue(renderer_impl,
                     1,
                     texture,
                     x,
                     y,
                     0,
                     0,
                     rt_texture2d_width(texture),
                     rt_texture2d_height(texture),
                     rt_texture2d_width(texture),
                     rt_texture2d_height(texture),
                     filter,
                     wrap);
    renderer2d_restore_state(renderer_impl, &state);
}

/// @brief Allocate a TileChunkCache2D describing chunk dimensions for dirty-region tracking.
/// @details Chunk dimensions are sanitized via `normalized_dim`. Game code that modifies
///          tiles in a chunk calls `MarkDirty`; a renderer that caches per-chunk textures
///          checks `GetDirtyCount` and rebuilds stale chunks before drawing.
void *rt_tilechunkcache2d_new(int64_t chunk_width, int64_t chunk_height) {
    rt_tilechunkcache2d_impl *impl = (rt_tilechunkcache2d_impl *)rt_obj_new_i64(
        RT2D_TILECHUNKCACHE_CLASS_ID, (int64_t)sizeof(rt_tilechunkcache2d_impl));
    if (!impl)
        return NULL;
    impl->chunk_width = normalized_dim(chunk_width);
    impl->chunk_height = normalized_dim(chunk_height);
    return impl;
}

/// @brief Return the chunk width in tiles.
int64_t rt_tilechunkcache2d_get_chunk_width(void *cache) {
    return rt2d_has_class(cache, RT2D_TILECHUNKCACHE_CLASS_ID)
               ? ((rt_tilechunkcache2d_impl *)cache)->chunk_width
               : 0;
}

/// @brief Return the chunk height in tiles.
int64_t rt_tilechunkcache2d_get_chunk_height(void *cache) {
    return rt2d_has_class(cache, RT2D_TILECHUNKCACHE_CLASS_ID)
               ? ((rt_tilechunkcache2d_impl *)cache)->chunk_height
               : 0;
}

/// @brief Increment the dirty counter — signals that at least one chunk needs rebuild.
void rt_tilechunkcache2d_mark_dirty(void *cache) {
    if (!rt2d_has_class(cache, RT2D_TILECHUNKCACHE_CLASS_ID))
        return;
    rt_tilechunkcache2d_impl *impl = (rt_tilechunkcache2d_impl *)cache;
    if (impl->dirty_count < INT64_MAX)
        impl->dirty_count++;
}

/// @brief Reset the dirty counter to zero after the renderer has processed all dirty chunks.
void rt_tilechunkcache2d_clear_dirty(void *cache) {
    if (rt2d_has_class(cache, RT2D_TILECHUNKCACHE_CLASS_ID))
        ((rt_tilechunkcache2d_impl *)cache)->dirty_count = 0;
}

/// @brief Return the number of pending dirty-chunk notifications.
int64_t rt_tilechunkcache2d_get_dirty_count(void *cache) {
    return rt2d_has_class(cache, RT2D_TILECHUNKCACHE_CLASS_ID)
               ? ((rt_tilechunkcache2d_impl *)cache)->dirty_count
               : 0;
}

/// @brief GC finalizer — releases the retained TileChunkCache2D reference.
static void tilemaprenderer2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_TILEMAPRENDERER_CLASS_ID))
        return;
    rt_tilemaprenderer2d_impl *impl = (rt_tilemaprenderer2d_impl *)obj;
    release_ref_slot(&impl->cache);
}

/// @brief Allocate a TilemapRenderer2D with no chunk cache bound and zero draw count.
void *rt_tilemaprenderer2d_new(void) {
    rt_tilemaprenderer2d_impl *impl = (rt_tilemaprenderer2d_impl *)rt_obj_new_i64(
        RT2D_TILEMAPRENDERER_CLASS_ID, (int64_t)sizeof(rt_tilemaprenderer2d_impl));
    if (!impl)
        return NULL;
    rt_obj_set_finalizer(impl, tilemaprenderer2d_finalize);
    return impl;
}

/// @brief Bind a TileChunkCache2D to this renderer, retaining a reference.
void rt_tilemaprenderer2d_set_chunk_cache(void *renderer, void *cache) {
    if (rt2d_has_class(renderer, RT2D_TILEMAPRENDERER_CLASS_ID) &&
        (!cache || rt2d_has_class(cache, RT2D_TILECHUNKCACHE_CLASS_ID)))
        spriterenderer2d_set_ref(&((rt_tilemaprenderer2d_impl *)renderer)->cache, cache);
}

/// @brief Return the number of tiles drawn by the last Draw/DrawRegion call.
int64_t rt_tilemaprenderer2d_get_draw_count(void *renderer) {
    return rt2d_has_class(renderer, RT2D_TILEMAPRENDERER_CLASS_ID)
               ? ((rt_tilemaprenderer2d_impl *)renderer)->draw_count
               : 0;
}

/// @brief Draw the entire tilemap into `canvas` offset by `(offset_x, offset_y)`.
/// @details Delegates to `rt_tilemap_draw` and records the number of visible
///          non-empty tiles issued by that draw.
void rt_tilemaprenderer2d_draw(
    void *renderer, void *tilemap, void *canvas, int64_t offset_x, int64_t offset_y) {
    if (!rt2d_has_class(renderer, RT2D_TILEMAPRENDERER_CLASS_ID) || !tilemap || !canvas)
        return;
    rt_tilemaprenderer2d_impl *impl = (rt_tilemaprenderer2d_impl *)renderer;
    impl->draw_count = rt_tilemap_count_drawn_visible(tilemap, canvas, offset_x, offset_y);
    rt_tilemap_draw(tilemap, canvas, offset_x, offset_y);
}

/// @brief Draw a viewport-clipped region of the tilemap into `canvas`.
/// @details Delegates to `rt_tilemap_draw_region`, culling tiles outside the
///          `(view_x, view_y, view_w, view_h)` rectangle before drawing and
///          records how many non-empty tiles were visible.
void rt_tilemaprenderer2d_draw_region(void *renderer,
                                      void *tilemap,
                                      void *canvas,
                                      int64_t offset_x,
                                      int64_t offset_y,
                                      int64_t view_x,
                                      int64_t view_y,
                                      int64_t view_w,
                                      int64_t view_h) {
    if (!rt2d_has_class(renderer, RT2D_TILEMAPRENDERER_CLASS_ID) || !tilemap || !canvas)
        return;
    rt_tilemaprenderer2d_impl *impl = (rt_tilemaprenderer2d_impl *)renderer;
    impl->draw_count = rt_tilemap_count_drawn_region(tilemap, view_x, view_y, view_w, view_h);
    rt_tilemap_draw_region(tilemap, canvas, offset_x, offset_y, view_x, view_y, view_w, view_h);
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
    rt_animationclip2d_impl *impl = (rt_animationclip2d_impl *)rt_obj_new_i64(
        RT2D_ANIMATIONCLIP_CLASS_ID, (int64_t)sizeof(rt_animationclip2d_impl));
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
    return rt2d_has_class(clip, RT2D_ANIMATIONCLIP_CLASS_ID)
               ? ((rt_animationclip2d_impl *)clip)->start_frame
               : 0;
}

/// @brief Return the number of sprite frames in this clip.
int64_t rt_animationclip2d_get_frame_count(void *clip) {
    return rt2d_has_class(clip, RT2D_ANIMATIONCLIP_CLASS_ID)
               ? ((rt_animationclip2d_impl *)clip)->frame_count
               : 0;
}

/// @brief Return the per-frame delay in milliseconds.
int64_t rt_animationclip2d_get_frame_delay_ms(void *clip) {
    return rt2d_has_class(clip, RT2D_ANIMATIONCLIP_CLASS_ID)
               ? ((rt_animationclip2d_impl *)clip)->frame_delay_ms
               : 0;
}

/// @brief Return non-zero if the clip loops back to its first frame after the last.
int64_t rt_animationclip2d_get_loop(void *clip) {
    return rt2d_has_class(clip, RT2D_ANIMATIONCLIP_CLASS_ID)
               ? ((rt_animationclip2d_impl *)clip)->loop
               : 0;
}

/// @brief GC finalizer — releases retained references to the sprite and animation clip.
static void animatedsprite2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_ANIMATEDSPRITE_CLASS_ID))
        return;
    rt_animatedsprite2d_impl *impl = (rt_animatedsprite2d_impl *)obj;
    release_ref_slot(&impl->sprite);
    release_ref_slot(&impl->clip);
}

/// @brief Allocate an AnimatedSprite2D wrapping `sprite`, ready to play clips.
/// @details Retains a reference to `sprite`. Playback remains stopped until a
///          valid clip is attached or `Play` is called with a valid clip.
void *rt_animatedsprite2d_new(void *sprite) {
    if (!sprite || !rt_obj_is_instance(sprite, RT_SPRITE_CLASS_ID, 0))
        return NULL;
    retain_ref(sprite);
    rt_animatedsprite2d_impl *impl = (rt_animatedsprite2d_impl *)rt_obj_new_i64(
        RT2D_ANIMATEDSPRITE_CLASS_ID, (int64_t)sizeof(rt_animatedsprite2d_impl));
    if (!impl) {
        void *owned_sprite = sprite;
        release_ref_slot(&owned_sprite);
        return NULL;
    }
    impl->sprite = sprite;
    impl->clip = NULL;
    impl->elapsed_ms = 0;
    impl->frame = 0;
    impl->playing = 0;
    rt_obj_set_finalizer(impl, animatedsprite2d_finalize);
    return impl;
}

/// @brief Bind an AnimationClip2D and reset elapsed time and frame counter.
/// @details Calling `SetClip` with a non-NULL clip immediately seeks the bound
///          sprite to `clip->start_frame`, so the first drawn frame is always
///          consistent even before the first `Update`. Passing NULL stops playback.
void rt_animatedsprite2d_set_clip(void *animated_sprite, void *clip) {
    if (!rt2d_has_class(animated_sprite, RT2D_ANIMATEDSPRITE_CLASS_ID) ||
        (clip && !rt2d_has_class(clip, RT2D_ANIMATIONCLIP_CLASS_ID)))
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
    rt_animatedsprite2d_impl *impl = rt2d_has_class(animated_sprite, RT2D_ANIMATEDSPRITE_CLASS_ID)
                                         ? (rt_animatedsprite2d_impl *)animated_sprite
                                         : NULL;
    if (!impl)
        return;
    if (!impl->sprite || !rt2d_has_class(impl->clip, RT2D_ANIMATIONCLIP_CLASS_ID)) {
        impl->playing = 0;
        return;
    }

    rt_animationclip2d_impl *clip = (rt_animationclip2d_impl *)impl->clip;
    int64_t sprite_frames = rt_sprite_get_frame_count(impl->sprite);
    int64_t available = sprite_frames > clip->start_frame ? sprite_frames - clip->start_frame : 0;
    int64_t effective_count = clip->frame_count < available ? clip->frame_count : available;
    if (effective_count <= 0) {
        impl->playing = 0;
        impl->frame = 0;
        impl->elapsed_ms = 0;
        return;
    }

    if (!impl->playing && impl->frame >= effective_count - 1) {
        impl->frame = 0;
        impl->elapsed_ms = 0;
        rt_sprite_set_frame(impl->sprite, clip->start_frame);
    }
    impl->playing = 1;
}

/// @brief Stop playback and reset to the clip's first frame.
void rt_animatedsprite2d_stop(void *animated_sprite) {
    if (!rt2d_has_class(animated_sprite, RT2D_ANIMATEDSPRITE_CLASS_ID))
        return;
    rt_animatedsprite2d_impl *impl = (rt_animatedsprite2d_impl *)animated_sprite;
    impl->playing = 0;
    impl->elapsed_ms = 0;
    impl->frame = 0;
    if (impl->sprite && rt2d_has_class(impl->clip, RT2D_ANIMATIONCLIP_CLASS_ID))
        rt_sprite_set_frame(impl->sprite, ((rt_animationclip2d_impl *)impl->clip)->start_frame);
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
    rt_animatedsprite2d_impl *impl = rt2d_has_class(animated_sprite, RT2D_ANIMATEDSPRITE_CLASS_ID)
                                         ? (rt_animatedsprite2d_impl *)animated_sprite
                                         : NULL;
    if (!impl || !impl->sprite || !impl->clip || !impl->playing)
        return;
    if (!rt2d_has_class(impl->clip, RT2D_ANIMATIONCLIP_CLASS_ID))
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
    return rt2d_has_class(animated_sprite, RT2D_ANIMATEDSPRITE_CLASS_ID)
               ? ((rt_animatedsprite2d_impl *)animated_sprite)->frame
               : 0;
}

/// @brief Return non-zero if the animation is currently playing.
int64_t rt_animatedsprite2d_is_playing(void *animated_sprite) {
    rt_animatedsprite2d_impl *impl = rt2d_has_class(animated_sprite, RT2D_ANIMATEDSPRITE_CLASS_ID)
                                         ? (rt_animatedsprite2d_impl *)animated_sprite
                                         : NULL;
    return impl && impl->playing && impl->sprite &&
                   rt2d_has_class(impl->clip, RT2D_ANIMATIONCLIP_CLASS_ID)
               ? 1
               : 0;
}

/// @brief GC finalizer — releases the retained BitmapFont reference.
static void textlayout2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_TEXTLAYOUT_CLASS_ID))
        return;
    rt_textlayout2d_impl *impl = (rt_textlayout2d_impl *)obj;
    release_ref_slot(&impl->font);
}

/// @brief Allocate a TextLayout2D with defaults (scale=1, white, no wrap, left-align).
void *rt_textlayout2d_new(void) {
    rt_textlayout2d_impl *impl = (rt_textlayout2d_impl *)rt_obj_new_i64(
        RT2D_TEXTLAYOUT_CLASS_ID, (int64_t)sizeof(rt_textlayout2d_impl));
    if (!impl)
        return NULL;
    impl->scale = 1;
    impl->color = 0x00FFFFFF;
    rt_obj_set_finalizer(impl, textlayout2d_finalize);
    return impl;
}

/// @brief Bind a BitmapFont, retaining a reference.
void rt_textlayout2d_set_font(void *layout, void *font) {
    if (rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID) &&
        (!font || rt2d_is_bitmap_font_handle(font)))
        spriterenderer2d_set_ref(&((rt_textlayout2d_impl *)layout)->font, font);
}

/// @brief Set the integer pixel scale; clamped to [1, 64].
void rt_textlayout2d_set_scale(void *layout, int64_t scale) {
    if (rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID))
        ((rt_textlayout2d_impl *)layout)->scale = clamp_i64(scale, 1, 64);
}

/// @brief Set the wrap width in pixels; 0 means no wrapping.
void rt_textlayout2d_set_wrap_width(void *layout, int64_t width) {
    if (rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID))
        ((rt_textlayout2d_impl *)layout)->wrap_width = width < 0 ? 0 : width;
}

/// @brief Set text alignment: 0=left, 1=center, 2=right; clamped to [0, 2].
void rt_textlayout2d_set_alignment(void *layout, int64_t alignment) {
    if (rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID))
        ((rt_textlayout2d_impl *)layout)->alignment = clamp_i64(alignment, 0, 2);
}

/// @brief Set the text color as 0x00RRGGBB (alpha bits are masked off).
void rt_textlayout2d_set_color(void *layout, int64_t rgb) {
    if (rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID))
        ((rt_textlayout2d_impl *)layout)->color = rgb & 0x00FFFFFF;
}

/// @brief Compute the raw pixel width of @p text using the layout's font and scale, without wrap
/// clamping.
/// @details Uses the layout's bitmap font if set; falls back to the canvas default font.
///          Width is multiplied by the layout's scale factor with saturation.
static int64_t textlayout2d_raw_width(rt_textlayout2d_impl *impl, rt_string text) {
    if (!text)
        return 0;
    int64_t width = impl && impl->font ? rt_bitmapfont_text_width(impl->font, text)
                                       : rt_canvas_text_width(text);
    int64_t scale = impl ? impl->scale : 1;
    return saturating_mul_i64(width, scale);
}

/// @brief Scaled height of one text line for the layout's font (px).
static int64_t textlayout2d_line_height(rt_textlayout2d_impl *impl) {
    int64_t line_height =
        impl && impl->font ? rt_bitmapfont_text_height(impl->font) : rt_canvas_text_height();
    int64_t scale = impl ? impl->scale : 1;
    return saturating_mul_i64(line_height, scale);
}

/// @brief Measure the scaled pixel width of a raw byte segment.
/// @details Wraps the bytes in a temporary rt_string, measures via
///          textlayout2d_raw_width, and releases it. Returns 0 for empty input.
static int64_t textlayout2d_segment_width(rt_textlayout2d_impl *impl,
                                          const char *data,
                                          size_t len) {
    if (!data || len == 0)
        return 0;
    rt_string segment = rt_string_from_bytes(data, len);
    if (!segment)
        return 0;
    int64_t width = textlayout2d_raw_width(impl, segment);
    rt_string_unref(segment);
    return width;
}

typedef struct {
    int64_t max_width;
    int64_t lines;
} rt_textlayout2d_measure;

/// @brief Fold a completed line's width into the running max-width accumulator.
static void textlayout2d_commit_line(rt_textlayout2d_measure *m, int64_t line_width) {
    if (!m)
        return;
    if (line_width > m->max_width)
        m->max_width = line_width;
}

/// @brief Word-wrap accumulator: fit one word onto the current line or wrap.
/// @details Adds @p word (plus any pending inter-word space) to @p line_width.
///          If it would exceed @p wrap_width, commits the current line and
///          starts a new one. Words wider than @p wrap_width are split across
///          multiple lines. Updates @p line_width, @p pending_space_width and
///          the @p measure (max width / line count) in place.
static void textlayout2d_commit_word(rt_textlayout2d_impl *impl,
                                     const char *word,
                                     size_t word_len,
                                     int64_t wrap_width,
                                     int64_t *line_width,
                                     int64_t *pending_space_width,
                                     rt_textlayout2d_measure *measure) {
    if (!word || word_len == 0 || !line_width || !pending_space_width || !measure)
        return;
    int64_t word_width = textlayout2d_segment_width(impl, word, word_len);
    int64_t join_width = *line_width > 0 ? *pending_space_width : 0;
    int64_t candidate = saturating_add_i64(*line_width, saturating_add_i64(join_width, word_width));
    if (wrap_width > 0 && *line_width > 0 && candidate > wrap_width) {
        textlayout2d_commit_line(measure, *line_width);
        measure->lines = saturating_add_i64(measure->lines, 1);
        *line_width = 0;
        join_width = 0;
    }

    if (wrap_width > 0 && word_width > wrap_width) {
        int64_t chunks = saturating_add_i64(word_width, wrap_width - 1) / wrap_width;
        if (chunks < 1)
            chunks = 1;
        if (chunks > 1)
            measure->lines = saturating_add_i64(measure->lines, chunks - 1);
        if (wrap_width > measure->max_width)
            measure->max_width = wrap_width;
        int64_t rem = word_width % wrap_width;
        *line_width = rem == 0 ? wrap_width : rem;
    } else {
        *line_width = saturating_add_i64(*line_width, saturating_add_i64(join_width, word_width));
    }
    *pending_space_width = 0;
}

/// @brief Measure wrapped text: compute max line width and total line count.
/// @details Tokenizes @p text on spaces/tabs/newlines, feeding words through
///          textlayout2d_commit_word with the layout's wrap width. Newlines
///          force a line break. Returns {max_width, lines} (lines >= 1).
static rt_textlayout2d_measure textlayout2d_measure(rt_textlayout2d_impl *impl, rt_string text) {
    rt_textlayout2d_measure measure = {0, 1};
    if (!text)
        return measure;

    const char *data = rt_string_cstr(text);
    size_t len = (size_t)rt_str_len(text);
    int64_t wrap_width = impl ? impl->wrap_width : 0;
    int64_t line_width = 0;
    int64_t pending_space_width = 0;
    size_t word_start = 0;
    size_t word_len = 0;

    for (size_t i = 0; i <= len; i++) {
        char ch = i < len ? data[i] : '\0';
        int is_end = i == len;
        int is_newline = ch == '\n' || ch == '\r';
        int is_space = ch == ' ' || ch == '\t';
        if (!is_end && !is_newline && !is_space) {
            if (word_len == 0)
                word_start = i;
            word_len++;
            continue;
        }

        if (word_len > 0) {
            textlayout2d_commit_word(impl,
                                     data + word_start,
                                     word_len,
                                     wrap_width,
                                     &line_width,
                                     &pending_space_width,
                                     &measure);
            word_len = 0;
        }

        if (is_space) {
            if (line_width > 0)
                pending_space_width = saturating_add_i64(
                    pending_space_width, textlayout2d_segment_width(impl, data + i, 1));
            continue;
        }

        if (is_newline) {
            textlayout2d_commit_line(&measure, line_width);
            measure.lines = saturating_add_i64(measure.lines, 1);
            line_width = 0;
            pending_space_width = 0;
            if (ch == '\r' && i + 1 < len && data[i + 1] == '\n')
                i++;
        }
    }

    textlayout2d_commit_line(&measure, line_width);
    return measure;
}

/// @brief Measure the display width of `text` respecting explicit newlines and wrap width.
int64_t rt_textlayout2d_measure_width(void *layout, rt_string text) {
    rt_textlayout2d_impl *impl =
        rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID) ? (rt_textlayout2d_impl *)layout : NULL;
    rt_textlayout2d_measure measure = textlayout2d_measure(impl, text);
    return measure.max_width;
}

/// @brief Measure the total pixel height of `text`, accounting for newlines and word wrapping.
int64_t rt_textlayout2d_measure_height(void *layout, rt_string text) {
    rt_textlayout2d_impl *impl =
        rt2d_has_class(layout, RT2D_TEXTLAYOUT_CLASS_ID) ? (rt_textlayout2d_impl *)layout : NULL;
    rt_textlayout2d_measure measure = textlayout2d_measure(impl, text);
    return saturating_mul_i64(textlayout2d_line_height(impl), measure.lines);
}

/// @brief GC finalizer — releases retained references to source, target, and shader.
static void renderpass2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_RENDERPASS_CLASS_ID))
        return;
    rt_renderpass2d_impl *impl = (rt_renderpass2d_impl *)obj;
    release_ref_slot(&impl->source);
    release_ref_slot(&impl->target);
    release_ref_slot(&impl->shader);
}

/// @brief Allocate a RenderPass2D that reads from `source` and writes to `target`.
/// @details Both references must be valid RenderTarget2D or Surface2D handles
///          and are retained. The pass starts enabled. A Shader2D can be
///          attached later via `SetShader`; without one, source is cloned
///          directly into target (pixel-perfect copy).
void *rt_renderpass2d_new(void *source, void *target) {
    if (!rendertarget2d_checked(source) || !rendertarget2d_checked(target))
        return NULL;
    rt_renderpass2d_impl *impl = (rt_renderpass2d_impl *)rt_obj_new_i64(
        RT2D_RENDERPASS_CLASS_ID, (int64_t)sizeof(rt_renderpass2d_impl));
    if (!impl)
        return NULL;
    impl->source = NULL;
    impl->target = NULL;
    impl->shader = NULL;
    impl->enabled = 0;
    rt_obj_set_finalizer(impl, renderpass2d_finalize);
    if (source) {
        retain_ref(source);
        impl->source = source;
    }
    if (target) {
        retain_ref(target);
        impl->target = target;
    }
    impl->enabled = 1;
    return impl;
}

/// @brief Replace the source RenderTarget2D, retaining a reference to the new one.
void rt_renderpass2d_set_source(void *pass, void *source) {
    if (rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID) &&
        (!source || rendertarget2d_checked(source)))
        spriterenderer2d_set_ref(&((rt_renderpass2d_impl *)pass)->source, source);
}

/// @brief Replace the target RenderTarget2D, retaining a reference to the new one.
void rt_renderpass2d_set_target(void *pass, void *target) {
    if (rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID) &&
        (!target || rendertarget2d_checked(target)))
        spriterenderer2d_set_ref(&((rt_renderpass2d_impl *)pass)->target, target);
}

/// @brief Bind a Shader2D for this pass, retaining a reference.
void rt_renderpass2d_set_shader(void *pass, void *shader) {
    if (rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID) && (!shader || rt2d_is_shader_like(shader)))
        spriterenderer2d_set_ref(&((rt_renderpass2d_impl *)pass)->shader, shader);
}

/// @brief Enable or disable this pass (disabled passes are skipped by `Execute`).
void rt_renderpass2d_set_enabled(void *pass, int64_t enabled) {
    if (rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID))
        ((rt_renderpass2d_impl *)pass)->enabled = enabled != 0;
}

/// @brief Return non-zero if this pass is currently enabled.
int64_t rt_renderpass2d_get_enabled(void *pass) {
    return rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID) ? ((rt_renderpass2d_impl *)pass)->enabled
                                                          : 0;
}

/// @brief Execute this pass: apply the optional shader and blit the result to target.
/// @details No-op if disabled, or if source/target are NULL or not valid render targets.
///          If a Shader2D is bound, applies it via `rt_shader2d_apply`; otherwise
///          clones the source pixels directly. The target is cleared before writing.
void rt_renderpass2d_execute(void *pass) {
    rt_renderpass2d_impl *impl =
        rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID) ? (rt_renderpass2d_impl *)pass : NULL;
    if (!impl || !impl->enabled || !impl->source || !impl->target)
        return;
    if (!rendertarget2d_checked(impl->source) || !rendertarget2d_checked(impl->target))
        return;
    if (impl->shader && !rt2d_is_shader_like(impl->shader))
        return;
    void *source_pixels = rt_rendertarget2d_get_pixels(impl->source);
    if (!source_pixels)
        return;
    void *processed = NULL;
    if (impl->shader) {
        if (shader2d_checked(impl->shader))
            processed = rt_shader2d_apply(impl->shader, source_pixels);
        else
            processed = rt_postprocess2d_apply(impl->shader, source_pixels);
    } else {
        processed = rt_pixels_clone(source_pixels);
    }
    if (!processed)
        return;
    rt_rendertarget2d_clear(impl->target, 0);
    rt_rendertarget2d_draw_pixels(impl->target, 0, 0, processed);
    release_ref_slot(&processed);
}

/// @brief GC finalizer — releases all retained pass references and frees the passes array.
static void rendergraph2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_RENDERGRAPH_CLASS_ID))
        return;
    rt_rendergraph2d_impl *impl = (rt_rendergraph2d_impl *)obj;
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        release_ref_slot(&impl->passes[i]);
    free(impl->passes);
}

/// @brief Allocate a RenderGraph2D with the given initial pass-array capacity.
void *rt_rendergraph2d_new(int64_t capacity) {
    rt_rendergraph2d_impl *impl = (rt_rendergraph2d_impl *)rt_obj_new_i64(
        RT2D_RENDERGRAPH_CLASS_ID, (int64_t)sizeof(rt_rendergraph2d_impl));
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
    rt_rendergraph2d_impl *impl =
        rt2d_has_class(graph, RT2D_RENDERGRAPH_CLASS_ID) ? (rt_rendergraph2d_impl *)graph : NULL;
    if (!impl || !rt2d_has_class(pass, RT2D_RENDERPASS_CLASS_ID))
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
    rt_rendergraph2d_impl *impl =
        rt2d_has_class(graph, RT2D_RENDERGRAPH_CLASS_ID) ? (rt_rendergraph2d_impl *)graph : NULL;
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        release_ref_slot(&impl->passes[i]);
    impl->count = 0;
}

/// @brief Return the number of passes currently in the graph.
int64_t rt_rendergraph2d_get_count(void *graph) {
    return rt2d_has_class(graph, RT2D_RENDERGRAPH_CLASS_ID)
               ? ((rt_rendergraph2d_impl *)graph)->count
               : 0;
}

/// @brief Execute every pass in insertion order.
/// @details Each pass is executed unconditionally here — individual passes skip
///          themselves via their `enabled` flag inside `rt_renderpass2d_execute`.
void rt_rendergraph2d_execute(void *graph) {
    rt_rendergraph2d_impl *impl =
        rt2d_has_class(graph, RT2D_RENDERGRAPH_CLASS_ID) ? (rt_rendergraph2d_impl *)graph : NULL;
    if (!impl)
        return;
    for (int64_t i = 0; i < impl->count; i++)
        rt_renderpass2d_execute(impl->passes[i]);
}

/// @brief GC finalizer — frees the packed bit grid.
static void collisionmask2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_COLLISIONMASK_CLASS_ID))
        return;
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
    rt_collisionmask2d_impl *impl = (rt_collisionmask2d_impl *)rt_obj_new_i64(
        RT2D_COLLISIONMASK_CLASS_ID, (int64_t)sizeof(rt_collisionmask2d_impl));
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
///          alpha channel without any manual tilemap authoring. A threshold of 0
///          means "any non-zero alpha" so fully transparent pixels stay empty.
void *rt_collisionmask2d_from_pixels(void *pixels, int64_t alpha_threshold) {
    rt_pixels_impl *pix = rt_pixels_checked_impl_or_null(pixels);
    if (!pix || pix->width <= 0 || pix->height <= 0 || !pix->data)
        return NULL;
    int64_t width = pix->width;
    int64_t height = pix->height;
    void *mask = rt_collisionmask2d_new(width, height);
    if (!mask)
        return NULL;
    int64_t threshold = clamp_u8_i64(alpha_threshold);
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t alpha = pix->data[y * width + x] & 255u;
            int64_t solid = threshold <= 0 ? alpha > 0u : alpha >= (uint32_t)threshold;
            rt_collisionmask2d_set(mask, x, y, solid);
        }
    }
    return mask;
}

/// @brief Return the width of the mask in cells.
int64_t rt_collisionmask2d_get_width(void *mask) {
    return rt2d_has_class(mask, RT2D_COLLISIONMASK_CLASS_ID)
               ? ((rt_collisionmask2d_impl *)mask)->width
               : 0;
}

/// @brief Return the height of the mask in cells.
int64_t rt_collisionmask2d_get_height(void *mask) {
    return rt2d_has_class(mask, RT2D_COLLISIONMASK_CLASS_ID)
               ? ((rt_collisionmask2d_impl *)mask)->height
               : 0;
}

/// @brief Return non-zero if cell (x, y) is within the collision mask's bounds.
static int32_t collisionmask2d_in_bounds(rt_collisionmask2d_impl *impl, int64_t x, int64_t y) {
    return impl && x >= 0 && y >= 0 && x < impl->width && y < impl->height;
}

/// @brief Set or clear the solid flag at `(x, y)`; out-of-range calls are silently dropped.
void rt_collisionmask2d_set(void *mask, int64_t x, int64_t y, int64_t solid) {
    rt_collisionmask2d_impl *impl =
        rt2d_has_class(mask, RT2D_COLLISIONMASK_CLASS_ID) ? (rt_collisionmask2d_impl *)mask : NULL;
    if (!collisionmask2d_in_bounds(impl, x, y))
        return;
    impl->bits[y * impl->width + x] = solid != 0;
}

/// @brief Return 1 if cell `(x, y)` is solid, 0 if empty or out of range.
int64_t rt_collisionmask2d_get(void *mask, int64_t x, int64_t y) {
    rt_collisionmask2d_impl *impl =
        rt2d_has_class(mask, RT2D_COLLISIONMASK_CLASS_ID) ? (rt_collisionmask2d_impl *)mask : NULL;
    if (!collisionmask2d_in_bounds(impl, x, y))
        return 0;
    return impl->bits[y * impl->width + x] ? 1 : 0;
}

/// @brief Pixel-perfect overlap test between two masks at world positions `(ax, ay)` and `(bx,
/// by)`.
/// @details Computes the axis-aligned intersection rectangle, then walks every cell
///          in that rectangle checking whether both masks are solid — returns 1 on the
///          first matching solid pair, 0 if no overlap exists. Early-exits if the AABBs
///          don't intersect at all.
int64_t rt_collisionmask2d_overlaps(
    void *a, int64_t ax, int64_t ay, void *b, int64_t bx, int64_t by) {
    rt_collisionmask2d_impl *ma =
        rt2d_has_class(a, RT2D_COLLISIONMASK_CLASS_ID) ? (rt_collisionmask2d_impl *)a : NULL;
    rt_collisionmask2d_impl *mb =
        rt2d_has_class(b, RT2D_COLLISIONMASK_CLASS_ID) ? (rt_collisionmask2d_impl *)b : NULL;
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
        (rt_hitbox2d_impl *)rt_obj_new_i64(RT2D_HITBOX_CLASS_ID, (int64_t)sizeof(rt_hitbox2d_impl));
    if (!impl)
        return NULL;
    rt_hitbox2d_set(impl, x, y, width, height);
    return impl;
}

/// @brief Reposition and resize an existing hitbox in one call.
void rt_hitbox2d_set(void *hitbox, int64_t x, int64_t y, int64_t width, int64_t height) {
    if (!rt2d_has_class(hitbox, RT2D_HITBOX_CLASS_ID))
        return;
    rt_hitbox2d_impl *impl = (rt_hitbox2d_impl *)hitbox;
    impl->x = x;
    impl->y = y;
    impl->width = width < 0 ? 0 : width;
    impl->height = height < 0 ? 0 : height;
}

/// @brief Return the X world-position of the hitbox's top-left corner.
int64_t rt_hitbox2d_get_x(void *hitbox) {
    return rt2d_has_class(hitbox, RT2D_HITBOX_CLASS_ID) ? ((rt_hitbox2d_impl *)hitbox)->x : 0;
}

/// @brief Return the Y world-position of the hitbox's top-left corner.
int64_t rt_hitbox2d_get_y(void *hitbox) {
    return rt2d_has_class(hitbox, RT2D_HITBOX_CLASS_ID) ? ((rt_hitbox2d_impl *)hitbox)->y : 0;
}

/// @brief Return the width of the hitbox in pixels.
int64_t rt_hitbox2d_get_width(void *hitbox) {
    return rt2d_has_class(hitbox, RT2D_HITBOX_CLASS_ID) ? ((rt_hitbox2d_impl *)hitbox)->width : 0;
}

/// @brief Return the height of the hitbox in pixels.
int64_t rt_hitbox2d_get_height(void *hitbox) {
    return rt2d_has_class(hitbox, RT2D_HITBOX_CLASS_ID) ? ((rt_hitbox2d_impl *)hitbox)->height : 0;
}

/// @brief Return non-zero if point `(x, y)` is inside this hitbox.
/// @details Uses `point_in_interval_i64` for both axes: [x, x+width) × [y, y+height).
int64_t rt_hitbox2d_contains(void *hitbox, int64_t x, int64_t y) {
    rt_hitbox2d_impl *impl =
        rt2d_has_class(hitbox, RT2D_HITBOX_CLASS_ID) ? (rt_hitbox2d_impl *)hitbox : NULL;
    return impl && point_in_interval_i64(impl->x, impl->width, x) &&
           point_in_interval_i64(impl->y, impl->height, y);
}

/// @brief Return non-zero if two hitboxes overlap (AABB vs AABB test).
/// @details Uses `intervals_overlap_i64` on both axes — no separation means overlap.
int64_t rt_hitbox2d_intersects(void *a, void *b) {
    rt_hitbox2d_impl *ha = rt2d_has_class(a, RT2D_HITBOX_CLASS_ID) ? (rt_hitbox2d_impl *)a : NULL;
    rt_hitbox2d_impl *hb = rt2d_has_class(b, RT2D_HITBOX_CLASS_ID) ? (rt_hitbox2d_impl *)b : NULL;
    if (!ha || !hb)
        return 0;
    return intervals_overlap_i64(ha->x, ha->width, hb->x, hb->width) &&
           intervals_overlap_i64(ha->y, ha->height, hb->y, hb->height);
}

/// @brief Allocate an empty Palette2D (all 256 slots zeroed, count=0).
void *rt_palette2d_new(void) {
    rt_palette2d_impl *impl = (rt_palette2d_impl *)rt_obj_new_i64(
        RT2D_PALETTE_CLASS_ID, (int64_t)sizeof(rt_palette2d_impl));
    if (!impl)
        return NULL;
    return impl;
}

/// @brief Set a palette entry at `index` [0, 255] to `rgba`.
/// @details `count` is extended to `index+1` if the new index is the highest seen so far.
void rt_palette2d_set_color(void *palette, int64_t index, int64_t rgba) {
    rt_palette2d_impl *impl =
        rt2d_has_class(palette, RT2D_PALETTE_CLASS_ID) ? (rt_palette2d_impl *)palette : NULL;
    if (!impl || index < 0 || index >= 256)
        return;
    impl->colors[index] = rt_pixels_rgba_or_tagged_color_to_rgba(rgba);
    impl->defined[index] = 1;
    if (index + 1 > impl->count)
        impl->count = index + 1;
}

/// @brief Return the RGBA color at `index`, or 0 if out of range or beyond `count`.
int64_t rt_palette2d_get_color(void *palette, int64_t index) {
    rt_palette2d_impl *impl =
        rt2d_has_class(palette, RT2D_PALETTE_CLASS_ID) ? (rt_palette2d_impl *)palette : NULL;
    if (!impl || index < 0 || index >= 256 || !impl->defined[index])
        return 0;
    return impl->colors[index];
}

/// @brief Return the palette entry as a Color.RGBA-compatible value.
int64_t rt_palette2d_get_color_value(void *palette, int64_t index) {
    return rt_pixels_rgba_to_color((uint32_t)rt_palette2d_get_color(palette, index));
}

/// @brief Return the number of palette entries set (highest set index + 1).
int64_t rt_palette2d_get_count(void *palette) {
    return rt2d_has_class(palette, RT2D_PALETTE_CLASS_ID) ? ((rt_palette2d_impl *)palette)->count
                                                          : 0;
}

/// @brief Remap indexed pixels through a palette using the red byte as the index.
/// @details Shared implementation behind the compatibility ApplyLegacy path;
///          returns a new owned Pixels image.
static void *palette2d_apply_indexed(void *palette, void *pixels, int8_t legacy_alpha_index) {
    rt_palette2d_impl *impl =
        rt2d_has_class(palette, RT2D_PALETTE_CLASS_ID) ? (rt_palette2d_impl *)palette : NULL;
    rt_pixels_impl *src_pixels = rt_pixels_checked_impl_or_null(pixels);
    if (!impl || !src_pixels || !src_pixels->data)
        return NULL;
    int64_t width = src_pixels->width;
    int64_t height = src_pixels->height;
    void *out = rt_pixels_new(width, height);
    if (!out)
        return NULL;
    rt_pixels_impl *dst_pixels = rt_pixels_checked_impl_or_null(out);
    if (!dst_pixels || !dst_pixels->data) {
        release_ref_slot(&out);
        return NULL;
    }
    for (int64_t y = 0; y < height; y++) {
        for (int64_t x = 0; x < width; x++) {
            uint32_t src = src_pixels->data[y * width + x];
            uint8_t alpha = (uint8_t)(src & 255u);
            int64_t index = (legacy_alpha_index && alpha != 255u && (src & 0xFFFFFF00u) == 0u)
                                ? (int64_t)alpha
                                : (int64_t)((src >> 24) & 255u);
            uint32_t mapped =
                (index >= 0 && index < 256 && impl->defined[index]) ? impl->colors[index] : src;
            dst_pixels->data[y * width + x] = mapped;
        }
    }
    return out;
}

/// @brief Remap a Pixels image through this palette and return a new Pixels.
/// @details For each pixel, selects the nearest defined palette color in RGBA
///          space. Unset palette slots are ignored; an empty palette returns a
///          clone. Returns NULL on invalid input/allocation failure.
void *rt_palette2d_apply(void *palette, void *pixels) {
    rt_palette2d_impl *impl =
        rt2d_has_class(palette, RT2D_PALETTE_CLASS_ID) ? (rt_palette2d_impl *)palette : NULL;
    rt_pixels_impl *src_pixels = rt_pixels_checked_impl_or_null(pixels);
    if (!impl || !src_pixels || !src_pixels->data)
        return NULL;

    void *out = rt_pixels_new(src_pixels->width, src_pixels->height);
    if (!out)
        return NULL;
    rt_pixels_impl *dst_pixels = rt_pixels_checked_impl_or_null(out);
    if (!dst_pixels || !dst_pixels->data) {
        release_ref_slot(&out);
        return NULL;
    }

    for (int64_t y = 0; y < src_pixels->height; y++) {
        for (int64_t x = 0; x < src_pixels->width; x++) {
            uint32_t src = src_pixels->data[y * src_pixels->width + x];
            uint32_t best = src;
            uint64_t best_distance = UINT64_MAX;
            for (int64_t i = 0; i < impl->count && i < 256; i++) {
                if (!impl->defined[i])
                    continue;
                uint32_t candidate = impl->colors[i];
                int64_t dr = (int64_t)((src >> 24) & 255u) - (int64_t)((candidate >> 24) & 255u);
                int64_t dg = (int64_t)((src >> 16) & 255u) - (int64_t)((candidate >> 16) & 255u);
                int64_t db = (int64_t)((src >> 8) & 255u) - (int64_t)((candidate >> 8) & 255u);
                int64_t da = (int64_t)(src & 255u) - (int64_t)(candidate & 255u);
                uint64_t distance = (uint64_t)(dr * dr) + (uint64_t)(dg * dg) +
                                    (uint64_t)(db * db) + (uint64_t)(da * da);
                if (distance < best_distance) {
                    best_distance = distance;
                    best = candidate;
                }
            }
            dst_pixels->data[y * src_pixels->width + x] = best;
        }
    }
    return out;
}

/// @brief Legacy indexed remap that also accepts `0x000000II` alpha-byte indices.
/// @details Kept separate from Apply so normal transparent black pixels are no longer
///          interpreted as palette index 128 or similar by accident.
void *rt_palette2d_apply_legacy(void *palette, void *pixels) {
    return palette2d_apply_indexed(palette, pixels, 1);
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
    rt_gradient2d_impl *impl = (rt_gradient2d_impl *)rt_obj_new_i64(
        RT2D_GRADIENT_CLASS_ID, (int64_t)sizeof(rt_gradient2d_impl));
    if (!impl)
        return NULL;
    impl->start = rt_pixels_rgba_or_tagged_color_to_rgba(start_rgba);
    impl->end = rt_pixels_rgba_or_tagged_color_to_rgba(end_rgba);
    impl->steps = steps <= 0 ? 2 : steps;
    return impl;
}

/// @brief Update the start and end colors of an existing gradient.
void rt_gradient2d_set_colors(void *gradient, int64_t start_rgba, int64_t end_rgba) {
    if (!rt2d_has_class(gradient, RT2D_GRADIENT_CLASS_ID))
        return;
    rt_gradient2d_impl *impl = (rt_gradient2d_impl *)gradient;
    impl->start = rt_pixels_rgba_or_tagged_color_to_rgba(start_rgba);
    impl->end = rt_pixels_rgba_or_tagged_color_to_rgba(end_rgba);
}

/// @brief Set the step count; non-positive values reset to 2 (smooth).
void rt_gradient2d_set_steps(void *gradient, int64_t steps) {
    if (rt2d_has_class(gradient, RT2D_GRADIENT_CLASS_ID))
        ((rt_gradient2d_impl *)gradient)->steps = steps <= 0 ? 2 : steps;
}

/// @brief Sample the gradient at position `t_pct` percent [0, 100] and return RGBA.
/// @details `steps <= 2` means a smooth gradient. Larger values quantize `t`
///          into that many buckets before interpolation. Steps above 101 are
///          clamped to keep the bucket arithmetic safe.
int64_t rt_gradient2d_sample(void *gradient, int64_t t_pct) {
    rt_gradient2d_impl *impl =
        rt2d_has_class(gradient, RT2D_GRADIENT_CLASS_ID) ? (rt_gradient2d_impl *)gradient : NULL;
    if (!impl)
        return 0;
    int64_t t = clamp_i64(t_pct, 0, 100);
    int64_t steps = impl->steps < 2 ? 2 : impl->steps;
    if (steps > 101)
        steps = 101;
    if (steps > 2) {
        int64_t bucket = (t * (steps - 1) + 50) / 100;
        t = bucket * 100 / (steps - 1);
    }
    return lerp_rgba(impl->start, impl->end, t);
}

/// @brief Sample the gradient and return a Color.RGBA-compatible value.
int64_t rt_gradient2d_sample_color(void *gradient, int64_t t_pct) {
    return rt_pixels_rgba_to_color((uint32_t)rt_gradient2d_sample(gradient, t_pct));
}

/// @brief Fill `pixels` with a left-to-right horizontal gradient.
/// @details Maps each column X to a sample position `X * 100 / (width - 1)` and writes
///          the same sampled color to all rows in that column.
void rt_gradient2d_fill_horizontal(void *gradient, void *pixels) {
    rt_gradient2d_impl *impl =
        rt2d_has_class(gradient, RT2D_GRADIENT_CLASS_ID) ? (rt_gradient2d_impl *)gradient : NULL;
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
    rt_gradient2d_impl *impl =
        rt2d_has_class(gradient, RT2D_GRADIENT_CLASS_ID) ? (rt_gradient2d_impl *)gradient : NULL;
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
    if (!rt2d_has_class(obj, RT2D_CAMERARIG_CLASS_ID))
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)obj;
    release_ref_slot(&impl->camera);
}

/// @brief Allocate a CameraRig2D wrapping `camera` with default smoothing (100%).
/// @details `smoothing` of 100 means instant snapping (camera jumps straight to target);
///          lower values produce lag-based smooth follow. Shake starts zeroed.
void *rt_camerarig2d_new(void *camera) {
    if (camera && !rt_obj_is_instance(camera, RT_CAMERA_CLASS_ID, 0))
        return NULL;
    retain_ref(camera);
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rt_obj_new_i64(
        RT2D_CAMERARIG_CLASS_ID, (int64_t)sizeof(rt_camerarig2d_impl));
    if (!impl) {
        void *owned_camera = camera;
        release_ref_slot(&owned_camera);
        return NULL;
    }
    impl->camera = camera;
    impl->target_x = 0;
    impl->target_y = 0;
    impl->smoothing = 100;
    impl->shake_x = 0;
    impl->shake_y = 0;
    rt_obj_set_finalizer(impl, camerarig2d_finalize);
    return impl;
}

/// @brief Replace the Camera2D, retaining a reference to the new one.
void rt_camerarig2d_set_camera(void *rig, void *camera) {
    if (rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID) &&
        (!camera || rt_obj_is_instance(camera, RT_CAMERA_CLASS_ID, 0)))
        spriterenderer2d_set_ref(&((rt_camerarig2d_impl *)rig)->camera, camera);
}

/// @brief Set the world-space target position the camera should track.
void rt_camerarig2d_set_target(void *rig, int64_t x, int64_t y) {
    if (!rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID))
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    impl->target_x = x;
    impl->target_y = y;
}

/// @brief Set the smooth-follow strength in percent [0, 100].
/// @details 100 = snap instantly, 0 = camera never moves, ~5 = gentle follow.
void rt_camerarig2d_set_smoothing(void *rig, int64_t lerp_pct) {
    if (rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID))
        ((rt_camerarig2d_impl *)rig)->smoothing = clamp_i64(lerp_pct, 0, 100);
}

/// @brief Set the camera's deadzone rectangle (camera doesn't move while target is inside).
void rt_camerarig2d_set_deadzone(void *rig, int64_t width, int64_t height) {
    rt_camerarig2d_impl *impl =
        rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID) ? (rt_camerarig2d_impl *)rig : NULL;
    if (impl && impl->camera)
        rt_camera_set_deadzone(impl->camera, width, height);
}

/// @brief Add a shake displacement offset (accumulated via saturating addition).
void rt_camerarig2d_add_shake(void *rig, int64_t x, int64_t y) {
    if (!rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID))
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    impl->shake_x = saturating_add_i64(impl->shake_x, x);
    impl->shake_y = saturating_add_i64(impl->shake_y, y);
}

/// @brief Reset the shake offset to zero.
void rt_camerarig2d_clear_shake(void *rig) {
    if (!rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID))
        return;
    rt_camerarig2d_impl *impl = (rt_camerarig2d_impl *)rig;
    impl->shake_x = 0;
    impl->shake_y = 0;
}

/// @brief Advance the rig by calling `rt_camera_smooth_follow` toward the current target.
/// @details Should be called once per frame before querying render positions.
void rt_camerarig2d_update(void *rig) {
    rt_camerarig2d_impl *impl =
        rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID) ? (rt_camerarig2d_impl *)rig : NULL;
    if (impl && impl->camera)
        rt_camera_smooth_follow(
            impl->camera, impl->target_x, impl->target_y, clamp_i64(impl->smoothing, 0, 100) * 10);
}

/// @brief Return the X render position: camera X plus current shake X offset.
int64_t rt_camerarig2d_get_render_x(void *rig) {
    rt_camerarig2d_impl *impl =
        rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID) ? (rt_camerarig2d_impl *)rig : NULL;
    return impl && impl->camera ? saturating_add_i64(rt_camera_get_x(impl->camera), impl->shake_x)
                                : 0;
}

/// @brief Return the Y render position: camera Y plus current shake Y offset.
int64_t rt_camerarig2d_get_render_y(void *rig) {
    rt_camerarig2d_impl *impl =
        rt2d_has_class(rig, RT2D_CAMERARIG_CLASS_ID) ? (rt_camerarig2d_impl *)rig : NULL;
    return impl && impl->camera ? saturating_add_i64(rt_camera_get_y(impl->camera), impl->shake_y)
                                : 0;
}

/// @brief GC finalizer — releases the retained TexAtlas reference.
static void texturepackeratlas_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_TEXTUREPACKERATLAS_CLASS_ID))
        return;
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
    void *atlas = rt_texatlas_new(pixels);
    if (!atlas)
        return NULL;
    rt_texturepackeratlas_impl *impl = (rt_texturepackeratlas_impl *)rt_obj_new_i64(
        RT2D_TEXTUREPACKERATLAS_CLASS_ID, (int64_t)sizeof(rt_texturepackeratlas_impl));
    if (!impl) {
        release_ref_slot(&atlas);
        return NULL;
    }
    impl->atlas = atlas;
    rt_obj_set_finalizer(impl, texturepackeratlas_finalize);
    return impl;
}

/// @brief Return the underlying TexAtlas pointer (not retained — caller must not release it).
void *rt_texturepackeratlas_get_atlas(void *packer) {
    return rt2d_has_class(packer, RT2D_TEXTUREPACKERATLAS_CLASS_ID)
               ? ((rt_texturepackeratlas_impl *)packer)->atlas
               : NULL;
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
    rt_asepriteimporter_impl *impl = (rt_asepriteimporter_impl *)rt_obj_new_i64(
        RT2D_ASEPRITEIMPORTER_CLASS_ID, (int64_t)sizeof(rt_asepriteimporter_impl));
    if (!impl)
        return NULL;
    return impl;
}

/// @brief Set the frame cell dimensions for slicing a horizontal strip sprite sheet.
/// @details Positive dimensions are sanitized via `normalized_dim`; zero or negative
///          dimensions leave the grid unset so ToAtlas fails cleanly.
void rt_asepriteimporter_set_grid(void *importer, int64_t frame_width, int64_t frame_height) {
    if (!rt2d_has_class(importer, RT2D_ASEPRITEIMPORTER_CLASS_ID))
        return;
    rt_asepriteimporter_impl *impl = (rt_asepriteimporter_impl *)importer;
    impl->frame_width = frame_width > 0 ? normalized_dim(frame_width) : 0;
    impl->frame_height = frame_height > 0 ? normalized_dim(frame_height) : 0;
}

/// @brief Return the configured frame width in pixels (0 if not set).
int64_t rt_asepriteimporter_get_frame_width(void *importer) {
    return rt2d_has_class(importer, RT2D_ASEPRITEIMPORTER_CLASS_ID)
               ? ((rt_asepriteimporter_impl *)importer)->frame_width
               : 0;
}

/// @brief Return the configured frame height in pixels (0 if not set).
int64_t rt_asepriteimporter_get_frame_height(void *importer) {
    return rt2d_has_class(importer, RT2D_ASEPRITEIMPORTER_CLASS_ID)
               ? ((rt_asepriteimporter_impl *)importer)->frame_height
               : 0;
}

/// @brief Slice `pixels` into a grid atlas using the configured frame dimensions.
/// @details Returns NULL if any required value is missing or zero. Delegates to
///          `rt_texatlas_load_grid`; the returned atlas owns its region data.
void *rt_asepriteimporter_to_atlas(void *importer, void *pixels) {
    rt_asepriteimporter_impl *impl = rt2d_has_class(importer, RT2D_ASEPRITEIMPORTER_CLASS_ID)
                                         ? (rt_asepriteimporter_impl *)importer
                                         : NULL;
    if (!impl || !pixels || impl->frame_width <= 0 || impl->frame_height <= 0)
        return NULL;
    return rt_texatlas_load_grid(pixels, impl->frame_width, impl->frame_height);
}

/// @brief Allocate a TiledMapLoader with default tile size 16×16.
void *rt_tiledmaploader_new(void) {
    rt_tiledmaploader_impl *impl = (rt_tiledmaploader_impl *)rt_obj_new_i64(
        RT2D_TILEDMAPLOADER_CLASS_ID, (int64_t)sizeof(rt_tiledmaploader_impl));
    if (!impl)
        return NULL;
    impl->tile_width = 16;
    impl->tile_height = 16;
    return impl;
}

/// @brief Override the tile dimensions used when creating new tilemaps.
/// @details Dimensions are sanitized via `normalized_dim` (negative → 1).
void rt_tiledmaploader_set_tile_size(void *loader, int64_t tile_width, int64_t tile_height) {
    if (!rt2d_has_class(loader, RT2D_TILEDMAPLOADER_CLASS_ID))
        return;
    rt_tiledmaploader_impl *impl = (rt_tiledmaploader_impl *)loader;
    impl->tile_width = normalized_dim(tile_width);
    impl->tile_height = normalized_dim(tile_height);
}

/// @brief Return the configured tile width in pixels.
int64_t rt_tiledmaploader_get_tile_width(void *loader) {
    return rt2d_has_class(loader, RT2D_TILEDMAPLOADER_CLASS_ID)
               ? ((rt_tiledmaploader_impl *)loader)->tile_width
               : 0;
}

/// @brief Return the configured tile height in pixels.
int64_t rt_tiledmaploader_get_tile_height(void *loader) {
    return rt2d_has_class(loader, RT2D_TILEDMAPLOADER_CLASS_ID)
               ? ((rt_tiledmaploader_impl *)loader)->tile_height
               : 0;
}

/// @brief Allocate a blank Tilemap2D with `width × height` cells using this loader's tile size.
/// @details Delegates to `rt_tilemap_new` — the returned tilemap has all cells set to 0 (empty).
void *rt_tiledmaploader_new_tilemap(void *loader, int64_t width, int64_t height) {
    rt_tiledmaploader_impl *impl = rt2d_has_class(loader, RT2D_TILEDMAPLOADER_CLASS_ID)
                                       ? (rt_tiledmaploader_impl *)loader
                                       : NULL;
    if (!impl)
        return NULL;
    return rt_tilemap_new(width, height, impl->tile_width, impl->tile_height);
}
