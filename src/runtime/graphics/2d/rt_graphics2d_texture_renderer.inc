//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_graphics2d_texture_renderer.inc
/// @brief Texture2D, Sprite2D, Renderer2D, and BitmapFont2D runtime entry
/// points.
///
/// @details This include fragment is consumed only by rt_graphics2d.c and
/// keeps texture ownership, sprite drawing, renderer state, and bitmap font
/// behavior in one contiguous section of the split 2D graphics implementation.
///
// File: src/runtime/graphics/2d/rt_graphics2d_texture_renderer.inc
// Purpose: Texture2D, GpuTexture2D, and Renderer2D command-stream entry points.
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
// Texture2D (aliased as GpuTexture2D)
//=============================================================================
// Retained `Pixels` handle plus sampling state — filter mode (NEAREST / LINEAR)
// and wrap mode (CLAMP / REPEAT). CPU-backed in this implementation; the name
// is forward-compatible with a future GPU-texture handle.

/// @brief GC finalizer — releases the retained Pixels reference.
static void texture2d_finalize(void *obj) {
    rt_texture2d_impl *texture = (rt_texture2d_impl *)obj;
    texture->magic = 0;
    rt2d_release_ref_slot(&texture->pixels);
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
    rt2d_retain_ref(pixels);
    rt_texture2d_impl *texture =
        (rt_texture2d_impl *)rt_obj_new_i64(class_id, (int64_t)sizeof(rt_texture2d_impl));
    if (!texture) {
        void *owned_pixels = pixels;
        rt2d_release_ref_slot(&owned_pixels);
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
    rt2d_release_ref_slot(&pixels);
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
    rt2d_release_ref_slot(&cmd->source);
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
// each one through `rt2d_blit_pixels`. Works on any Pixels-compatible target.

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
///          `rt2d_initial_capacity` (floor 16, ceiling 1Mi).
void *rt_renderer2d_new(int64_t capacity) {
    rt_renderer2d_impl *renderer = (rt_renderer2d_impl *)rt_obj_new_i64(
        RT2D_RENDERER_CLASS_ID, (int64_t)sizeof(rt_renderer2d_impl));
    if (!renderer)
        return NULL;
    renderer->magic = RT2D_RENDERER_MAGIC;
    renderer->capacity = rt2d_initial_capacity(capacity);
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
    rt2d_retain_ref(source);
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
    /* Soft-skip an invalid handle (no trap) to match rt_renderer2d_draw_texture, so
     * a stale handle mid-frame drops the one draw rather than aborting the program. */
    rt_pixels_impl *p = rt_pixels_checked_impl_or_null(pixels);
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
///   `rt2d_blit_pixels` with the command's snapshot of tint, alpha, blend mode, and region.
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
    rt2d_blit_pixels(target_pixels,
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
///   via `rt_canvas_copy_rect` + `rt2d_blit_pixels(ADD)` + `rt_canvas_blit`. The additive
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
    rt2d_blit_pixels(dest, 0, 0, pixels, sx, sy, width, height, -1, 255, RT_GRAPHICS2D_BLEND_ADD);
    rt_canvas_blit(canvas, dx, dy, dest);
    rt2d_release_ref_slot(&dest);
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
                int64_t x0;
                int64_t x1;
                int64_t y0;
                int64_t y1;
                if (!rt2d_floor_i64_checked(min_x, &x0) || !rt2d_ceil_i64_checked(max_x, &x1) ||
                    !rt2d_floor_i64_checked(min_y, &y0) || !rt2d_ceil_i64_checked(max_y, &y1))
                    continue;
                int64_t rw = x1 - x0;
                int64_t rh = y1 - y0;
                if (rw <= 0 || rh <= 0 || !rt2d_checked_count(rw, rh, 4, NULL))
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
                blit_x = rt2d_saturating_add_i64(rt2d_saturating_add_i64(cmd->x, cmd->pivot_x), x0);
                blit_y = rt2d_saturating_add_i64(rt2d_saturating_add_i64(cmd->y, cmd->pivot_y), y0);
            } else if (cmd->source_kind == 1 &&
                       (cmd->sx != 0 || cmd->sy != 0 || cmd->width != rt_pixels_width(pixels) ||
                        cmd->height != rt_pixels_height(pixels) || cmd->dst_width != cmd->width ||
                        cmd->dst_height != cmd->height ||
                        cmd->sampler_wrap == RT_GRAPHICS2D_WRAP_REPEAT ||
                        cmd->sampler_filter == RT_GRAPHICS2D_FILTER_LINEAR)) {
                if (!rt2d_checked_count(cmd->dst_width, cmd->dst_height, 4, NULL))
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
                region = rt2d_copy_region_pixels(pixels, sx, sy, width, height);
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
            rt2d_release_ref_slot(&processed);
            rt2d_release_ref_slot(&region);
        }
    }
    impl->active = 0;
    rt_renderer2d_clear(renderer);
}
