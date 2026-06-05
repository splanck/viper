//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_rendertarget3d.c
// Purpose: Viper.Graphics3D.RenderTarget3D — offscreen rendering target
//   with color + depth buffers. Enables render-to-texture for effects
//   like shadow maps, reflections, and post-processing.
//
// Key invariants:
//   - CPU readback buffer is always RGBA uint8_t (same format as Pixels)
//   - HDR RTT readback also keeps an optional linear RGBA32F CPU mirror
//   - Depth buffer is float (same as Z-buffer)
//   - CPU-side color/depth storage is allocated lazily on first CPU use
//     or when the software backend binds the target.
//   - AsPixels() returns a NEW Pixels object (fresh copy each call)
//   - GC finalizer frees both buffers
//
// Ownership/Lifetime:
//   - RenderTarget3D is GC-managed; finalizer frees the backend rendertarget
//     and its lazily-allocated color/depth buffers.
//   - Canvas3D retains a reference when the target is bound and releases it
//     on unbind / canvas destruction.
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, plans/3d/08-render-to-texture.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_platform.h"
#include "vgfx3d_backend.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#define RT_RENDERTARGET3D_DEFAULT_BUDGET_BYTES (1024ull * 1024ull * 1024ull)

static volatile size_t g_rendertarget3d_reserved_bytes = 0;
static volatile int64_t g_rendertarget3d_next_identity = 1;

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern void *rt_pixels_new(int64_t width, int64_t height);

//=============================================================================
// Render target allocation
//=============================================================================

/// @brief Multiply two size_t values with overflow detection, writing the product to *@p out.
/// @return 1 with *@p out set on success; 0 (with *@p out cleared) when @p out is NULL or @p a * @p
/// b
///   would exceed SIZE_MAX.
static int rt_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out)
        *out = 0u;
    if (!out)
        return 0;
    if (a != 0u && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

/// @brief Estimate the GPU memory (color + depth) of a @p w×@p h render target for the given
///   color format (8 bytes/texel for HDR16F, otherwise 4), using overflow-checked products.
/// @return 1 with @p out_bytes set on success; 0 for non-positive dimensions or on overflow.
static int rt_rendertarget_estimate_bytes(int32_t w,
                                          int32_t h,
                                          vgfx3d_rendertarget_color_format_t color_format,
                                          size_t *out_bytes) {
    size_t pixels;
    size_t color_bytes;
    size_t depth_bytes;
    size_t total;
    size_t color_stride = color_format == VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F ? 8u : 4u;
    if (out_bytes)
        *out_bytes = 0u;
    if (!out_bytes || w <= 0 || h <= 0)
        return 0;
    if (!rt_checked_mul_size((size_t)w, (size_t)h, &pixels) ||
        !rt_checked_mul_size(pixels, color_stride, &color_bytes) ||
        !rt_checked_mul_size(pixels, sizeof(float), &depth_bytes))
        return 0;
    if (SIZE_MAX - color_bytes < depth_bytes)
        return 0;
    total = color_bytes + depth_bytes;
    *out_bytes = total;
    return 1;
}

/// @brief Reserve @p bytes against the process-wide RenderTarget3D footprint budget.
/// @details Render-target CPU mirrors and GPU textures are allocated lazily by the active backend,
///   but the dimensions define a predictable maximum footprint. Reserving that estimate up front
///   prevents scripts from creating many huge targets and deferring failure to backend OOM.
/// @return 1 when the reservation was accepted; 0 when it would exceed the default budget.
static int rt_rendertarget_reserve_budget(uint64_t bytes) {
    const size_t budget_bytes = (size_t)RT_RENDERTARGET3D_DEFAULT_BUDGET_BYTES;
    size_t reserve_bytes;
    size_t old_value;
    if (bytes == 0u)
        return 1;
    if (bytes > RT_RENDERTARGET3D_DEFAULT_BUDGET_BYTES)
        return 0;
    reserve_bytes = (size_t)bytes;
    for (;;) {
        old_value = __atomic_load_n(&g_rendertarget3d_reserved_bytes, __ATOMIC_RELAXED);
        if (old_value > budget_bytes - reserve_bytes)
            return 0;
        if (__atomic_compare_exchange_n(&g_rendertarget3d_reserved_bytes,
                                        &old_value,
                                        old_value + reserve_bytes,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED))
            return 1;
    }
}

/// @brief Release a previous RenderTarget3D footprint reservation.
/// @details Saturates to zero on accounting mismatch so finalization remains idempotent even if a
///   partially constructed target is torn down during error recovery.
static void rt_rendertarget_release_budget(uint64_t bytes) {
    size_t release_bytes;
    size_t old_value;
    if (bytes == 0u)
        return;
    release_bytes = bytes > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)bytes;
    for (;;) {
        old_value = __atomic_load_n(&g_rendertarget3d_reserved_bytes, __ATOMIC_RELAXED);
        size_t new_value = old_value > release_bytes ? old_value - release_bytes : 0u;
        if (__atomic_compare_exchange_n(&g_rendertarget3d_reserved_bytes,
                                        &old_value,
                                        new_value,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED))
            return;
    }
}

/// @brief Allocate a monotonic non-zero cache identity for a render-target shell.
/// @details Backends use this instead of raw C pointers when caching native textures, avoiding stale
///   cache hits if the allocator reuses a recently freed target address.
static uint64_t rt_rendertarget_next_cache_identity(void) {
    uint64_t identity =
        (uint64_t)__atomic_fetch_add(&g_rendertarget3d_next_identity, (int64_t)1, __ATOMIC_RELAXED);
    if (identity == 0u)
        identity = (uint64_t)__atomic_fetch_add(
            &g_rendertarget3d_next_identity, (int64_t)1, __ATOMIC_RELAXED);
    return identity ? identity : 1u;
}

/// @brief Allocate the backend-side `vgfx3d_rendertarget_t` shell at `w × h` (RGBA8 stride =
/// w * 4), with `color_buf` / `depth_buf` left NULL. CPU-side buffers are allocated lazily
/// on first read or when the software backend binds the target — see
/// `vgfx3d_rendertarget_ensure_color` / `_ensure_depth` in `rt_canvas3d_internal.h`. Returns
/// NULL on calloc failure; the caller traps with a user-visible message.
static vgfx3d_rendertarget_t *rt_alloc(int32_t w,
                                       int32_t h,
                                       vgfx3d_rendertarget_color_format_t color_format) {
    size_t estimated_bytes = 0u;
    vgfx3d_rendertarget_t *rt = (vgfx3d_rendertarget_t *)calloc(1, sizeof(vgfx3d_rendertarget_t));
    if (!rt)
        return NULL;
    if (w > INT32_MAX / 4 ||
        !rt_rendertarget_estimate_bytes(w, h, color_format, &estimated_bytes)) {
        free(rt);
        return NULL;
    }
    if (!rt_rendertarget_reserve_budget((uint64_t)estimated_bytes)) {
        free(rt);
        return NULL;
    }

    rt->width = w;
    rt->height = h;
    rt->stride = w * 4;
    rt->color_format = (int32_t)color_format;
    rt->estimated_bytes = (uint64_t)estimated_bytes;
    rt->cache_identity = rt_rendertarget_next_cache_identity();

    return rt;
}

/// @brief Tear down a `vgfx3d_rendertarget_t`. Frees both CPU-side buffers (NULL-safe — they
/// may never have been allocated under the lazy-ensure model) and then the shell itself.
static void rt_free(vgfx3d_rendertarget_t *rt) {
    if (!rt)
        return;
    free(rt->color_buf);
    free(rt->hdr_color_buf);
    free(rt->depth_buf);
    rt_rendertarget_release_budget(rt->estimated_bytes);
    free(rt);
}

//=============================================================================
// Runtime type
//=============================================================================

/// @brief GC finalizer for `RenderTarget3D`. Frees the underlying backend rendertarget if
/// still live. The Canvas3D side releases its own retained reference separately, so the
/// finalizer only owns `target` itself, not the canvas pointer.
static void rt_rendertarget3d_finalize(void *obj) {
    rt_rendertarget3d *rtd = (rt_rendertarget3d *)obj;
    if (!rtd)
        return;
    if (rtd->target) {
        rt_free(rtd->target);
        rtd->target = NULL;
    }
}

/// @brief Validate @p obj as a RenderTarget3D handle and return its typed pointer (NULL on
/// mismatch).
static rt_rendertarget3d *rendertarget3d_checked(void *obj) {
    return (rt_rendertarget3d *)rt_g3d_checked_or_null(obj, RT_G3D_RENDERTARGET3D_CLASS_ID);
}

/// @brief Create an offscreen render target for render-to-texture effects.
/// @details Allocates a software framebuffer at the specified resolution. Once
///          bound to a Canvas3D via rt_canvas3d_bind_render_target, all subsequent
///          Begin/DrawMesh/End calls render to this target instead of the window.
///          The result can be read back as a Pixels object via as_pixels.
/// @param width  Target width in pixels (1–8192).
/// @param height Target height in pixels (1–8192).
/// @return Opaque render target handle, or NULL on failure.
static void *rt_rendertarget3d_new_with_format(int64_t width,
                                               int64_t height,
                                               vgfx3d_rendertarget_color_format_t color_format,
                                               const char *trap_name) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        rt_trap(trap_name);
        return NULL;
    }

    rt_rendertarget3d *rtd = (rt_rendertarget3d *)rt_obj_new_i64(
        RT_G3D_RENDERTARGET3D_CLASS_ID, (int64_t)sizeof(rt_rendertarget3d));
    if (!rtd) {
        rt_trap("RenderTarget3D: memory allocation failed");
        return NULL;
    }

    rtd->vptr = NULL;
    rtd->width = width;
    rtd->height = height;
    rtd->target = rt_alloc((int32_t)width, (int32_t)height, color_format);

    if (!rtd->target) {
        if (rt_obj_release_check0(rtd))
            rt_obj_free(rtd);
        rt_trap("RenderTarget3D: buffer allocation failed");
        return NULL;
    }

    rt_obj_set_finalizer(rtd, rt_rendertarget3d_finalize);
    return rtd;
}

/// @brief Allocate an LDR (8-bit per channel) off-screen render target.
/// @details Thin wrapper around the shared constructor that selects the
///   `UNORM8` color format. Dimensions outside 1-8192 trap with a descriptive
///   message rather than silently clamping.
/// @return Retained pointer to the new `rt_rendertarget3d`, or traps on failure.
void *rt_rendertarget3d_new(int64_t width, int64_t height) {
    return rt_rendertarget3d_new_with_format(width,
                                             height,
                                             VGFX3D_RENDERTARGET_COLOR_FORMAT_UNORM8,
                                             "RenderTarget3D.New: dimensions must be 1-8192");
}

/// @brief Allocate an HDR (16-bit float per channel) off-screen render target.
/// @details Same API as `rt_rendertarget3d_new`, but picks the `HDR16F` color
///   format so tone-mapping and bloom effects can work with values > 1.0
///   without early clamping. Callers that don't need HDR should use the plain
///   `new` variant — HDR targets consume 2x the VRAM per pixel.
void *rt_rendertarget3d_new_hdr(int64_t width, int64_t height) {
    return rt_rendertarget3d_new_with_format(width,
                                             height,
                                             VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F,
                                             "RenderTarget3D.NewHdr: dimensions must be 1-8192");
}

/// @brief Get the width of the render target in pixels.
int64_t rt_rendertarget3d_get_width(void *obj) {
    rt_rendertarget3d *rtd = rendertarget3d_checked(obj);
    if (!rtd || !rtd->target || rtd->width <= 0 || rtd->width > VGFX3D_RENDERTARGET_DIM_MAX ||
        rtd->target->width <= 0 || rtd->target->width > VGFX3D_RENDERTARGET_DIM_MAX ||
        rtd->target->width != rtd->width)
        return 0;
    return rtd->target->width;
}

/// @brief Get the height of the render target in pixels.
int64_t rt_rendertarget3d_get_height(void *obj) {
    rt_rendertarget3d *rtd = rendertarget3d_checked(obj);
    if (!rtd || !rtd->target || rtd->height <= 0 || rtd->height > VGFX3D_RENDERTARGET_DIM_MAX ||
        rtd->target->height <= 0 || rtd->target->height > VGFX3D_RENDERTARGET_DIM_MAX ||
        rtd->target->height != rtd->height)
        return 0;
    return rtd->target->height;
}

/// @brief Return whether the target stores HDR color on the GPU path.
int32_t rt_rendertarget3d_get_is_hdr(void *obj) {
    const rt_rendertarget3d *rtd = rendertarget3d_checked(obj);
    return (rtd && vgfx3d_rendertarget_is_hdr(rtd->target)) ? 1 : 0;
}

/// @brief Copy the render target contents into a new Pixels object for CPU access.
/// @details Converts the CPU readback buffer to Pixels' packed uint32_t format
///          (0xRRGGBBAA). HDR render targets are tonemapped into that buffer by
///          the active GPU backend before this copy occurs. This is a full copy
///          — the Pixels can be used as a texture, saved to disk, or processed
///          independently.
/// @param obj Render target handle.
/// @return New Pixels handle with the framebuffer contents, or NULL on failure.
void *rt_rendertarget3d_as_pixels(void *obj) {
    rt_rendertarget3d *rtd = rendertarget3d_checked(obj);
    if (!rtd)
        return NULL;
    if (!rtd->target)
        return NULL;
    size_t color_bytes = 0u;
    if (!vgfx3d_rendertarget_valid_color_layout(rtd->target, &color_bytes) || color_bytes == 0u)
        return NULL;
    if (!vgfx3d_rendertarget_ensure_color(rtd->target))
        return NULL;
    if (!vgfx3d_rendertarget_sync_color_if_needed(rtd->target))
        return NULL;

    int32_t w = rtd->target->width;
    int32_t h = rtd->target->height;
    int32_t stride = rtd->target->stride;
    if (w <= 0 || h <= 0 || (int64_t)stride < (int64_t)w * 4 || !rtd->target->color_buf)
        return NULL;
    if ((int64_t)w != rtd->width || (int64_t)h != rtd->height)
        return NULL;

    void *pixels = rt_pixels_new((int64_t)w, (int64_t)h);
    if (!pixels)
        return NULL;

    /* Copy render target RGBA bytes → Pixels 0xRRGGBBAA uint32_t */
    typedef struct {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view;

    px_view *pv = (px_view *)pixels;
    if (!pv->data || pv->w != (int64_t)w || pv->h != (int64_t)h) {
        if (rt_obj_release_check0(pixels))
            rt_obj_free(pixels);
        return NULL;
    }

    for (int32_t y = 0; y < h; y++) {
        const uint8_t *src = &rtd->target->color_buf[(size_t)y * (size_t)stride];
        uint32_t *dst = &pv->data[(size_t)y * (size_t)pv->w];
        for (int32_t x = 0; x < w; x++, src += 4) {
            dst[x] = ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) | ((uint32_t)src[2] << 8) |
                     (uint32_t)src[3];
        }
    }

    return pixels;
}

//=============================================================================
// Canvas3D render target binding
//=============================================================================

/// @brief Bind an offscreen render target. All subsequent Begin/DrawMesh/End
/// calls render to the target instead of the window. The active backend
/// (Metal, software, etc.) handles RTT natively — no backend switching needed.
void rt_canvas3d_set_render_target(void *canvas, void *target) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    if (c->in_frame) {
        rt_trap("Canvas3D.SetRenderTarget: cannot change render targets during a frame");
        return;
    }
    if (!target) {
        rt_canvas3d_reset_render_target(canvas);
        return;
    }
    rt_rendertarget3d *rtd =
        (rt_rendertarget3d *)rt_g3d_checked_or_null(target, RT_G3D_RENDERTARGET3D_CLASS_ID);
    if (!rtd || !rtd->target)
        return;
    if (c->backend == &vgfx3d_software_backend && !vgfx3d_rendertarget_ensure_color(rtd->target)) {
        rt_trap("Canvas3D.SetRenderTarget: buffer allocation failed");
        return;
    }
    if (!vgfx3d_rendertarget_ensure_depth(rtd->target)) {
        rt_trap("Canvas3D.SetRenderTarget: buffer allocation failed");
        return;
    }
    if (c->render_target_owner == rtd)
        return;
    rt_obj_retain_maybe(rtd);
    if (c->render_target_owner && rt_obj_release_check0(c->render_target_owner))
        rt_obj_free(c->render_target_owner);
    c->render_target_owner = rtd;
    c->render_target = rtd->target;

    if (c->backend && c->backend->set_render_target)
        c->backend->set_render_target(c->backend_ctx, rtd->target);
}

/// @brief Unbind the render target. Subsequent rendering goes to the window.
void rt_canvas3d_reset_render_target(void *canvas) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    if (c->in_frame) {
        rt_trap("Canvas3D.ResetRenderTarget: cannot change render targets during a frame");
        return;
    }

    if (c->backend && c->backend->set_render_target)
        c->backend->set_render_target(c->backend_ctx, NULL);

    if (c->render_target_owner && rt_obj_release_check0(c->render_target_owner))
        rt_obj_free(c->render_target_owner);
    c->render_target_owner = NULL;
    c->render_target = NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
