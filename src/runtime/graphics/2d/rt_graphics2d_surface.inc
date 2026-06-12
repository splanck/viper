//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_graphics2d_surface.inc
/// @brief RenderTarget2D and Surface2D offscreen pixel-surface entry points.
///
/// @details This include fragment is consumed only by rt_graphics2d.c and
/// keeps surface allocation, finalization, draw, and sample behavior grouped
/// apart from renderer, material, path, and extended utility classes.
///
// File: src/runtime/graphics/2d/rt_graphics2d_surface.inc
// Purpose: RenderTarget2D and Surface2D offscreen pixel-surface entry points.
//
// Key invariants:
//   - Included only by src/runtime/graphics/2d/rt_graphics2d.c.
//   - Keeps the original rt_graphics2d.c declaration order and static helper
//     visibility; this is a physical source split, not a behavior split.
//   - Public runtime entry points preserve their existing names and signatures.
//
// Ownership/Lifetime:
//   - Ownership rules are unchanged from the original monolithic source file;
//     GC finalizers release retained handles and heap buffers for each class.
//
// Links: src/runtime/graphics/2d/rt_graphics2d.c,
//        src/runtime/graphics/2d/rt_graphics2d_internal.h
//
//===----------------------------------------------------------------------===//

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
