//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_graphics2d_material_fx_viewport.inc
/// @brief Material2D, Shader2D, BlendMode2D, PostFX2D, Viewport2D, Tilemap2D,
/// Camera2D, and Lighting2D entry points.
///
/// @details This include fragment is consumed only by rt_graphics2d.c and
/// groups the stateful rendering configuration and scene-view helpers that sit
/// above the lower-level surface and texture primitives.
///
// File: src/runtime/graphics/2d/rt_graphics2d_material_fx_viewport.inc
// Purpose: Material2D, Shader2D, PostProcess2D, Viewport2D, and ScreenScaler entry points.
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
