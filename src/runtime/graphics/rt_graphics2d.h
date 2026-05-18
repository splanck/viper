//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_graphics2d.h
// Purpose: Runtime bridge declarations for the Viper.Graphics 2D support layer —
// render targets, textures, batched/sprite renderers, materials, shaders and
// post-process effects, viewports, tile maps, paths, shape/text renderers,
// transforms, sampler/blend state, animation, render graphs, collision masks,
// palettes, gradients, camera rigs, and asset importers.
//
// Key invariants:
//   - Every object is a heap-allocated opaque `void *` handle; APIs never expose
//     concrete struct layouts to the caller.
//   - Colors are packed integers: 0xRRGGBBAA for raw pixel/RGBA parameters and
//     0x00RRGGBB for "rgb"/"tint" parameters, matching rt_pixels conventions.
//   - Coordinates are 0-based from the top-left; out-of-range tile/region access
//     is clipped or returns a zero/identity value rather than trapping.
//   - Percent-style parameters (lerp_pct, t_pct, opacity, alpha) are integers in
//     0..100 unless a wider range is documented on the specific function.
//
// Ownership/Lifetime:
//   - `*_new` constructors return owned handles; the caller manages lifetime.
//   - `*_apply` helpers that return a `void *` produce a new Pixels/handle owned
//     by the caller; inputs are not consumed.
//
// Links: src/runtime/graphics/rt_graphics2d.c (implementation),
//        src/runtime/graphics/rt_pixels.h (Pixels buffer type)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Texture sampling filter modes (nearest-neighbor vs bilinear).
enum {
    RT_GRAPHICS2D_FILTER_NEAREST = 0,
    RT_GRAPHICS2D_FILTER_LINEAR = 1,
};

/// @brief Texture coordinate wrap modes at sampling edges.
enum {
    RT_GRAPHICS2D_WRAP_CLAMP = 0,
    RT_GRAPHICS2D_WRAP_REPEAT = 1,
};

/// @brief Blend modes used when compositing source pixels onto a destination.
enum {
    RT_GRAPHICS2D_BLEND_ALPHA = 0,  ///< Standard source-over alpha blending.
    RT_GRAPHICS2D_BLEND_OPAQUE = 1, ///< Source replaces destination (no blend).
    RT_GRAPHICS2D_BLEND_ADD = 2,    ///< Additive blending (for glows/particles).
};

/// @brief Image post-process effect kinds for shader2d/postprocess2d.
enum {
    RT_GRAPHICS2D_EFFECT_NONE = 0,
    RT_GRAPHICS2D_EFFECT_INVERT = 1,
    RT_GRAPHICS2D_EFFECT_GRAYSCALE = 2,
    RT_GRAPHICS2D_EFFECT_TINT = 3,
    RT_GRAPHICS2D_EFFECT_BLUR = 4,
};

//===----------------------------------------------------------------------===//
// RenderTarget2D — an offscreen RGBA surface that can be drawn into.
//===----------------------------------------------------------------------===//

/// @brief Create a new offscreen render target of the given pixel size.
void *rt_rendertarget2d_new(int64_t width, int64_t height);
/// @brief Get the render target width in pixels.
int64_t rt_rendertarget2d_width(void *target);
/// @brief Get the render target height in pixels.
int64_t rt_rendertarget2d_height(void *target);
/// @brief Get the target's backing Pixels buffer (not owned by the caller).
void *rt_rendertarget2d_get_pixels(void *target);
/// @brief Clear the entire target to a packed RGBA color.
void rt_rendertarget2d_clear(void *target, int64_t rgba);
/// @brief Resize the target, reallocating its backing buffer.
void rt_rendertarget2d_resize(void *target, int64_t width, int64_t height);
/// @brief Blit a whole Pixels image into the target at (x, y).
void rt_rendertarget2d_draw_pixels(void *target, int64_t x, int64_t y, void *pixels);
/// @brief Blit a sub-rectangle of a Pixels image into the target at (x, y).
void rt_rendertarget2d_draw_region(void *target,
                                   int64_t x,
                                   int64_t y,
                                   void *pixels,
                                   int64_t sx,
                                   int64_t sy,
                                   int64_t width,
                                   int64_t height);

//===----------------------------------------------------------------------===//
// Texture2D — an immutable-ish image with filter/wrap sampling state.
//===----------------------------------------------------------------------===//

/// @brief Create a texture from an existing Pixels image.
void *rt_texture2d_new(void *pixels);
/// @brief Load a texture from an image file on disk.
void *rt_texture2d_from_file(rt_string path);
/// @brief Get the texture width in pixels.
int64_t rt_texture2d_width(void *texture);
/// @brief Get the texture height in pixels.
int64_t rt_texture2d_height(void *texture);
/// @brief Get the texture's backing Pixels buffer (not owned by the caller).
void *rt_texture2d_get_pixels(void *texture);
/// @brief Allocate and return an owned copy of the texture's pixels.
void *rt_texture2d_clone_pixels(void *texture);
/// @brief Set the sampling filter (RT_GRAPHICS2D_FILTER_*).
void rt_texture2d_set_filter(void *texture, int64_t filter);
/// @brief Get the current sampling filter.
int64_t rt_texture2d_get_filter(void *texture);
/// @brief Set the coordinate wrap mode (RT_GRAPHICS2D_WRAP_*).
void rt_texture2d_set_wrap(void *texture, int64_t wrap);
/// @brief Get the current coordinate wrap mode.
int64_t rt_texture2d_get_wrap(void *texture);

//===----------------------------------------------------------------------===//
// Renderer2D — an immediate/queued 2D draw list flushed to a target or canvas.
//===----------------------------------------------------------------------===//

/// @brief Create a renderer with an initial draw-call capacity.
void *rt_renderer2d_new(int64_t capacity);
/// @brief Begin a new frame, resetting per-frame state.
void rt_renderer2d_begin(void *renderer);
/// @brief Clear all queued draw calls without flushing.
void rt_renderer2d_clear(void *renderer);
/// @brief Number of draw calls currently queued.
int64_t rt_renderer2d_count(void *renderer);
/// @brief Set the tint color (0x00RRGGBB) applied to subsequent draws.
void rt_renderer2d_set_tint(void *renderer, int64_t rgb);
/// @brief Set the global alpha (0..255) applied to subsequent draws.
void rt_renderer2d_set_alpha(void *renderer, int64_t alpha);
/// @brief Set the blend mode (RT_GRAPHICS2D_BLEND_*) for subsequent draws.
void rt_renderer2d_set_blend_mode(void *renderer, int64_t blend_mode);
/// @brief Queue a draw of a raw Pixels image at (x, y).
void rt_renderer2d_draw_pixels(void *renderer, void *pixels, int64_t x, int64_t y);
/// @brief Queue a draw of a texture at (x, y).
void rt_renderer2d_draw_texture(void *renderer, void *texture, int64_t x, int64_t y);
/// @brief Queue a texture draw rotated about its top-left by angle_deg degrees.
void rt_renderer2d_draw_texture_rotated(
    void *renderer, void *texture, int64_t x, int64_t y, double angle_deg);
/// @brief Queue a texture draw rotated about an explicit pivot point.
void rt_renderer2d_draw_texture_rotated_at(void *renderer,
                                           void *texture,
                                           int64_t x,
                                           int64_t y,
                                           int64_t pivot_x,
                                           int64_t pivot_y,
                                           double angle_deg);
/// @brief Queue a texture draw scaled to the given destination width/height.
void rt_renderer2d_draw_texture_scaled(
    void *renderer, void *texture, int64_t x, int64_t y, int64_t width, int64_t height);
/// @brief Queue a draw of a sub-rectangle of a texture at (x, y).
void rt_renderer2d_draw_texture_region(void *renderer,
                                       void *texture,
                                       int64_t x,
                                       int64_t y,
                                       int64_t sx,
                                       int64_t sy,
                                       int64_t width,
                                       int64_t height);
/// @brief Queue a draw of a sub-rectangle of a raw Pixels image at (x, y).
void rt_renderer2d_draw_region(void *renderer,
                               void *pixels,
                               int64_t x,
                               int64_t y,
                               int64_t sx,
                               int64_t sy,
                               int64_t width,
                               int64_t height);
/// @brief Flush all queued draws into an offscreen render target.
void rt_renderer2d_flush_to_target(void *renderer, void *target);
/// @brief Flush all queued draws to the on-screen canvas and end the frame.
void rt_renderer2d_end(void *renderer, void *canvas);

//===----------------------------------------------------------------------===//
// Material2D — reusable tint/alpha/blend state applied to an image.
//===----------------------------------------------------------------------===//

/// @brief Create a material with default (identity) tint/alpha/blend.
void *rt_material2d_new(void);
/// @brief Set the material tint color (0x00RRGGBB).
void rt_material2d_set_tint(void *material, int64_t rgb);
/// @brief Get the material tint color.
int64_t rt_material2d_get_tint(void *material);
/// @brief Set the material alpha (0..255).
void rt_material2d_set_alpha(void *material, int64_t alpha);
/// @brief Get the material alpha.
int64_t rt_material2d_get_alpha(void *material);
/// @brief Set the material blend mode (RT_GRAPHICS2D_BLEND_*).
void rt_material2d_set_blend_mode(void *material, int64_t blend_mode);
/// @brief Get the material blend mode.
int64_t rt_material2d_get_blend_mode(void *material);
/// @brief Apply the material to a Pixels image, returning a new owned image.
void *rt_material2d_apply(void *material, void *pixels);

//===----------------------------------------------------------------------===//
// Shader2D — a single named image effect with amount/color parameters.
//===----------------------------------------------------------------------===//

/// @brief Create a shader configured for the given effect (RT_GRAPHICS2D_EFFECT_*).
void *rt_shader2d_new(int64_t effect);
/// @brief Set the active effect.
void rt_shader2d_set_effect(void *shader, int64_t effect);
/// @brief Get the active effect.
int64_t rt_shader2d_get_effect(void *shader);
/// @brief Set the effect strength/amount (effect-dependent units).
void rt_shader2d_set_amount(void *shader, int64_t amount);
/// @brief Get the effect strength/amount.
int64_t rt_shader2d_get_amount(void *shader);
/// @brief Set the effect color (0x00RRGGBB), e.g. for the tint effect.
void rt_shader2d_set_color(void *shader, int64_t rgb);
/// @brief Get the effect color.
int64_t rt_shader2d_get_color(void *shader);
/// @brief Apply the shader to a Pixels image, returning a new owned image.
void *rt_shader2d_apply(void *shader, void *pixels);

//===----------------------------------------------------------------------===//
// PostProcess2D — a configurable full-frame post-process effect.
//===----------------------------------------------------------------------===//

/// @brief Create a post-process effect (initially RT_GRAPHICS2D_EFFECT_NONE).
void *rt_postprocess2d_new(void);
/// @brief Set the post-process effect.
void rt_postprocess2d_set_effect(void *postprocess, int64_t effect);
/// @brief Set the post-process effect amount.
void rt_postprocess2d_set_amount(void *postprocess, int64_t amount);
/// @brief Set the post-process effect color (0x00RRGGBB).
void rt_postprocess2d_set_color(void *postprocess, int64_t rgb);
/// @brief Apply the post-process to a Pixels image, returning a new owned image.
void *rt_postprocess2d_apply(void *postprocess, void *pixels);

//===----------------------------------------------------------------------===//
// Viewport2D — virtual-resolution to screen mapping with optional integer scale.
//===----------------------------------------------------------------------===//

/// @brief Create a viewport mapping a virtual resolution onto a screen size.
void *rt_viewport2d_new(int64_t virtual_width,
                        int64_t virtual_height,
                        int64_t screen_width,
                        int64_t screen_height);
/// @brief Set the virtual (logical) resolution.
void rt_viewport2d_set_virtual_size(void *viewport, int64_t width, int64_t height);
/// @brief Set the physical screen size the viewport maps onto.
void rt_viewport2d_set_screen_size(void *viewport, int64_t width, int64_t height);
/// @brief Enable/disable integer-only scaling (avoids fractional pixel shimmer).
void rt_viewport2d_set_integer_scaling(void *viewport, int64_t enabled);
/// @brief Get the computed scale factor.
int64_t rt_viewport2d_get_scale(void *viewport);
/// @brief Get the horizontal letterbox offset in screen pixels.
int64_t rt_viewport2d_get_offset_x(void *viewport);
/// @brief Get the vertical letterbox offset in screen pixels.
int64_t rt_viewport2d_get_offset_y(void *viewport);
/// @brief Map a virtual-space X coordinate to screen space.
int64_t rt_viewport2d_world_to_screen_x(void *viewport, int64_t x);
/// @brief Map a virtual-space Y coordinate to screen space.
int64_t rt_viewport2d_world_to_screen_y(void *viewport, int64_t y);
/// @brief Map a screen-space X coordinate back to virtual space.
int64_t rt_viewport2d_screen_to_world_x(void *viewport, int64_t x);
/// @brief Map a screen-space Y coordinate back to virtual space.
int64_t rt_viewport2d_screen_to_world_y(void *viewport, int64_t y);

//===----------------------------------------------------------------------===//
// Tileset2D — a grid of equally sized tiles sliced from one Pixels image.
//===----------------------------------------------------------------------===//

/// @brief Create a tileset by slicing an image into tile_width x tile_height cells.
void *rt_tileset2d_new(void *pixels, int64_t tile_width, int64_t tile_height);
/// @brief Number of tile columns in the tileset.
int64_t rt_tileset2d_columns(void *tileset);
/// @brief Number of tile rows in the tileset.
int64_t rt_tileset2d_rows(void *tileset);
/// @brief Total number of tiles (columns * rows).
int64_t rt_tileset2d_tile_count(void *tileset);
/// @brief Get a new owned Pixels image for the tile at the given linear index.
void *rt_tileset2d_get_tile_pixels(void *tileset, int64_t tile_index);

//===----------------------------------------------------------------------===//
// TileLayer2D — a 2D grid of tile indices with visibility/opacity.
//===----------------------------------------------------------------------===//

/// @brief Create a tile layer of the given dimensions (all cells empty).
void *rt_tilelayer2d_new(int64_t width, int64_t height);
/// @brief Layer width in tiles.
int64_t rt_tilelayer2d_width(void *layer);
/// @brief Layer height in tiles.
int64_t rt_tilelayer2d_height(void *layer);
/// @brief Set the tile index at cell (x, y).
void rt_tilelayer2d_set(void *layer, int64_t x, int64_t y, int64_t tile);
/// @brief Get the tile index at cell (x, y).
int64_t rt_tilelayer2d_get(void *layer, int64_t x, int64_t y);
/// @brief Fill every cell with the given tile index.
void rt_tilelayer2d_fill(void *layer, int64_t tile);
/// @brief Clear every cell to empty.
void rt_tilelayer2d_clear(void *layer);
/// @brief Set whether the layer is rendered.
void rt_tilelayer2d_set_visible(void *layer, int64_t visible);
/// @brief Query whether the layer is visible.
int64_t rt_tilelayer2d_is_visible(void *layer);
/// @brief Set the layer opacity (0..100).
void rt_tilelayer2d_set_opacity(void *layer, int64_t opacity);
/// @brief Get the layer opacity.
int64_t rt_tilelayer2d_get_opacity(void *layer);

//===----------------------------------------------------------------------===//
// ObjectLayer2D — a list of typed rectangular objects (Tiled-style).
//===----------------------------------------------------------------------===//

/// @brief Create an object layer with an initial capacity.
void *rt_objectlayer2d_new(int64_t capacity);
/// @brief Add a typed rectangle object; returns its index.
int64_t rt_objectlayer2d_add_rect(
    void *layer, int64_t x, int64_t y, int64_t width, int64_t height, int64_t type);
/// @brief Number of objects in the layer.
int64_t rt_objectlayer2d_count(void *layer);
/// @brief Remove all objects.
void rt_objectlayer2d_clear(void *layer);
/// @brief Get object X by index.
int64_t rt_objectlayer2d_get_x(void *layer, int64_t index);
/// @brief Get object Y by index.
int64_t rt_objectlayer2d_get_y(void *layer, int64_t index);
/// @brief Get object width by index.
int64_t rt_objectlayer2d_get_width(void *layer, int64_t index);
/// @brief Get object height by index.
int64_t rt_objectlayer2d_get_height(void *layer, int64_t index);
/// @brief Get object type tag by index.
int64_t rt_objectlayer2d_get_type(void *layer, int64_t index);

//===----------------------------------------------------------------------===//
// AutoTile2D — bitmask-to-tile resolution for auto-tiling terrain edges.
//===----------------------------------------------------------------------===//

/// @brief Create an empty auto-tile rule set.
void *rt_autotile2d_new(void);
/// @brief Map a neighbor bitmask to a specific tile index.
void rt_autotile2d_set_variant(void *autotile, int64_t mask, int64_t tile);
/// @brief Resolve a neighbor bitmask to its configured tile index.
int64_t rt_autotile2d_resolve(void *autotile, int64_t mask);
/// @brief Resolve @p mask and write the tile into @p layer at (x, y).
void rt_autotile2d_apply(void *autotile, void *layer, int64_t x, int64_t y, int64_t mask);

//===----------------------------------------------------------------------===//
// Path2D — an ordered polyline that can be rasterized into pixels.
//===----------------------------------------------------------------------===//

/// @brief Create a path with an initial point capacity.
void *rt_path2d_new(int64_t capacity);
/// @brief Remove all points from the path.
void rt_path2d_clear(void *path);
/// @brief Start a new subpath at (x, y).
void rt_path2d_move_to(void *path, int64_t x, int64_t y);
/// @brief Add a line segment to (x, y) from the current point.
void rt_path2d_line_to(void *path, int64_t x, int64_t y);
/// @brief Number of points in the path.
int64_t rt_path2d_count(void *path);
/// @brief Get the X of the path point at @p index.
int64_t rt_path2d_get_x(void *path, int64_t index);
/// @brief Get the Y of the path point at @p index.
int64_t rt_path2d_get_y(void *path, int64_t index);
/// @brief Rasterize the path into a Pixels image with the given RGBA color.
void rt_path2d_draw_to_pixels(void *path, void *pixels, int64_t rgba);

//===----------------------------------------------------------------------===//
// ShapeRenderer2D — stroked/filled primitive rasterizer.
//===----------------------------------------------------------------------===//

/// @brief Create a shape renderer with default stroke/fill.
void *rt_shaperenderer2d_new(void);
/// @brief Set the stroke (outline) color (0xRRGGBBAA).
void rt_shaperenderer2d_set_stroke(void *renderer, int64_t rgba);
/// @brief Set the fill color (0xRRGGBBAA).
void rt_shaperenderer2d_set_fill(void *renderer, int64_t rgba);
/// @brief Draw a stroked line into @p pixels.
void rt_shaperenderer2d_line(
    void *renderer, void *pixels, int64_t x0, int64_t y0, int64_t x1, int64_t y1);
/// @brief Draw a filled/stroked rectangle into @p pixels.
void rt_shaperenderer2d_rect(
    void *renderer, void *pixels, int64_t x, int64_t y, int64_t width, int64_t height);
/// @brief Draw a filled/stroked circle into @p pixels.
void rt_shaperenderer2d_circle(void *renderer, void *pixels, int64_t x, int64_t y, int64_t radius);
/// @brief Stroke a Path2D into @p pixels using the current stroke color.
void rt_shaperenderer2d_path(void *renderer, void *pixels, void *path);

//===----------------------------------------------------------------------===//
// TextRenderer2D — font-based text drawing/measurement onto a canvas.
//===----------------------------------------------------------------------===//

/// @brief Create a text renderer with default font/scale/color.
void *rt_textrenderer2d_new(void);
/// @brief Set the font handle used for subsequent draws.
void rt_textrenderer2d_set_font(void *renderer, void *font);
/// @brief Set the integer text scale factor.
void rt_textrenderer2d_set_scale(void *renderer, int64_t scale);
/// @brief Set the text color (0x00RRGGBB).
void rt_textrenderer2d_set_color(void *renderer, int64_t rgb);
/// @brief Measure the rendered pixel width of @p text.
int64_t rt_textrenderer2d_measure_width(void *renderer, rt_string text);
/// @brief Measure the rendered pixel height of @p text.
int64_t rt_textrenderer2d_measure_height(void *renderer, rt_string text);
/// @brief Draw @p text onto @p canvas at (x, y).
void rt_textrenderer2d_draw(void *renderer, void *canvas, int64_t x, int64_t y, rt_string text);

//===----------------------------------------------------------------------===//
// SDFFont — signed-distance-field font wrapper over a bitmap font.
//===----------------------------------------------------------------------===//

/// @brief Create an SDF font from a bitmap font with the given spread.
void *rt_sdffont_new(void *bitmap_font, int64_t spread);
/// @brief Get the underlying bitmap font handle.
void *rt_sdffont_get_bitmap_font(void *font);
/// @brief Get the SDF spread (distance-field range in pixels).
int64_t rt_sdffont_get_spread(void *font);

//===----------------------------------------------------------------------===//
// NineSlice2D — 9-patch scalable UI image (fixed corners, stretched edges).
//===----------------------------------------------------------------------===//

/// @brief Create a 9-slice from an image with the given border insets.
void *rt_nineslice2d_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom);
/// @brief Draw the 9-slice scaled to @p width x @p height into @p target.
void rt_nineslice2d_draw_to_pixels(
    void *slice, void *target, int64_t x, int64_t y, int64_t width, int64_t height);

//===----------------------------------------------------------------------===//
// DebugDraw2D — queued debug overlay primitives (lines/rects/circles).
//===----------------------------------------------------------------------===//

/// @brief Create a debug-draw buffer with an initial capacity.
void *rt_debugdraw2d_new(int64_t capacity);
/// @brief Clear all queued debug primitives.
void rt_debugdraw2d_clear(void *debug_draw);
/// @brief Number of queued debug primitives.
int64_t rt_debugdraw2d_count(void *debug_draw);
/// @brief Queue a debug line in color @p rgba.
void rt_debugdraw2d_line(
    void *debug_draw, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t rgba);
/// @brief Queue a debug rectangle in color @p rgba.
void rt_debugdraw2d_rect(
    void *debug_draw, int64_t x, int64_t y, int64_t width, int64_t height, int64_t rgba);
/// @brief Queue a debug circle in color @p rgba.
void rt_debugdraw2d_circle(void *debug_draw, int64_t x, int64_t y, int64_t radius, int64_t rgba);
/// @brief Rasterize all queued debug primitives into @p pixels.
void rt_debugdraw2d_draw_to_pixels(void *debug_draw, void *pixels);

//===----------------------------------------------------------------------===//
// Transform2D — 2D affine transform (position/scale/rotation/origin).
//===----------------------------------------------------------------------===//

/// @brief Create an identity 2D transform.
void *rt_transform2d_new(void);
/// @brief Get the translation X.
int64_t rt_transform2d_get_x(void *transform);
/// @brief Set the translation X.
void rt_transform2d_set_x(void *transform, int64_t x);
/// @brief Get the translation Y.
int64_t rt_transform2d_get_y(void *transform);
/// @brief Set the translation Y.
void rt_transform2d_set_y(void *transform, int64_t y);
/// @brief Get the X scale (percent, 100 = 1.0).
int64_t rt_transform2d_get_scale_x(void *transform);
/// @brief Set the X scale (percent, 100 = 1.0).
void rt_transform2d_set_scale_x(void *transform, int64_t scale_x);
/// @brief Get the Y scale (percent, 100 = 1.0).
int64_t rt_transform2d_get_scale_y(void *transform);
/// @brief Set the Y scale (percent, 100 = 1.0).
void rt_transform2d_set_scale_y(void *transform, int64_t scale_y);
/// @brief Get the rotation in degrees.
int64_t rt_transform2d_get_rotation(void *transform);
/// @brief Set the rotation in degrees.
void rt_transform2d_set_rotation(void *transform, int64_t degrees);
/// @brief Set both translation components at once.
void rt_transform2d_set_position(void *transform, int64_t x, int64_t y);
/// @brief Set both scale components at once (percent, 100 = 1.0).
void rt_transform2d_set_scale(void *transform, int64_t scale_x, int64_t scale_y);
/// @brief Set the rotation/scale origin point.
void rt_transform2d_set_origin(void *transform, int64_t x, int64_t y);
/// @brief Translate the transform by (dx, dy).
void rt_transform2d_translate(void *transform, int64_t dx, int64_t dy);
/// @brief Apply the transform to a point and return the resulting X.
int64_t rt_transform2d_transform_x(void *transform, int64_t x, int64_t y);
/// @brief Apply the transform to a point and return the resulting Y.
int64_t rt_transform2d_transform_y(void *transform, int64_t x, int64_t y);

//===----------------------------------------------------------------------===//
// Sampler2D — reusable filter/wrap state applied to a texture.
//===----------------------------------------------------------------------===//

/// @brief Create a sampler with default filter/wrap.
void *rt_sampler2d_new(void);
/// @brief Set the sampler filter (RT_GRAPHICS2D_FILTER_*).
void rt_sampler2d_set_filter(void *sampler, int64_t filter);
/// @brief Get the sampler filter.
int64_t rt_sampler2d_get_filter(void *sampler);
/// @brief Set the sampler wrap mode (RT_GRAPHICS2D_WRAP_*).
void rt_sampler2d_set_wrap(void *sampler, int64_t wrap);
/// @brief Get the sampler wrap mode.
int64_t rt_sampler2d_get_wrap(void *sampler);
/// @brief Copy the sampler's filter/wrap state onto a texture.
void rt_sampler2d_apply_to_texture(void *sampler, void *texture);

//===----------------------------------------------------------------------===//
// BlendState2D — reusable blend/tint/alpha state applied to a renderer.
//===----------------------------------------------------------------------===//

/// @brief Create a blend state with default (alpha) blending.
void *rt_blendstate2d_new(void);
/// @brief Set the blend mode (RT_GRAPHICS2D_BLEND_*).
void rt_blendstate2d_set_blend_mode(void *state, int64_t blend_mode);
/// @brief Get the blend mode.
int64_t rt_blendstate2d_get_blend_mode(void *state);
/// @brief Set the tint color (0x00RRGGBB).
void rt_blendstate2d_set_tint(void *state, int64_t rgb);
/// @brief Get the tint color.
int64_t rt_blendstate2d_get_tint(void *state);
/// @brief Set the alpha (0..255).
void rt_blendstate2d_set_alpha(void *state, int64_t alpha);
/// @brief Get the alpha.
int64_t rt_blendstate2d_get_alpha(void *state);
/// @brief Copy this blend/tint/alpha state onto a Renderer2D.
void rt_blendstate2d_apply_to_renderer(void *state, void *renderer);

//===----------------------------------------------------------------------===//
// SpriteRenderer2D — draws sprites through a material/sampler/blend pipeline.
//===----------------------------------------------------------------------===//

/// @brief Create a sprite renderer with default pipeline state.
void *rt_spriterenderer2d_new(void);
/// @brief Set the material applied to drawn sprites.
void rt_spriterenderer2d_set_material(void *sprite_renderer, void *material);
/// @brief Set the sampler applied to drawn sprites.
void rt_spriterenderer2d_set_sampler(void *sprite_renderer, void *sampler);
/// @brief Set the blend state applied to drawn sprites.
void rt_spriterenderer2d_set_blend_state(void *sprite_renderer, void *blend_state);
/// @brief Draw a raw Pixels image through the pipeline via @p renderer.
void rt_spriterenderer2d_draw_pixels(
    void *sprite_renderer, void *renderer, void *pixels, int64_t x, int64_t y);
/// @brief Draw a texture through the pipeline via @p renderer.
void rt_spriterenderer2d_draw_texture(
    void *sprite_renderer, void *renderer, void *texture, int64_t x, int64_t y);

//===----------------------------------------------------------------------===//
// TileChunkCache2D — dirty-tracked chunk cache for large tilemap redraws.
//===----------------------------------------------------------------------===//

/// @brief Create a chunk cache with the given chunk dimensions.
void *rt_tilechunkcache2d_new(int64_t chunk_width, int64_t chunk_height);
/// @brief Get the cache chunk width in tiles.
int64_t rt_tilechunkcache2d_get_chunk_width(void *cache);
/// @brief Get the cache chunk height in tiles.
int64_t rt_tilechunkcache2d_get_chunk_height(void *cache);
/// @brief Mark all chunks dirty (force a full redraw).
void rt_tilechunkcache2d_mark_dirty(void *cache);
/// @brief Clear the dirty state on all chunks.
void rt_tilechunkcache2d_clear_dirty(void *cache);
/// @brief Number of chunks currently flagged dirty.
int64_t rt_tilechunkcache2d_get_dirty_count(void *cache);

//===----------------------------------------------------------------------===//
// TileMapRenderer2D — draws a tilemap (optionally chunk-cached) to a canvas.
//===----------------------------------------------------------------------===//

/// @brief Create a tilemap renderer.
void *rt_tilemaprenderer2d_new(void);
/// @brief Attach a chunk cache for incremental redraws.
void rt_tilemaprenderer2d_set_chunk_cache(void *renderer, void *cache);
/// @brief Number of tiles drawn in the last render call.
int64_t rt_tilemaprenderer2d_get_draw_count(void *renderer);
/// @brief Draw an entire tilemap onto @p canvas at the given offset.
void rt_tilemaprenderer2d_draw(
    void *renderer, void *tilemap, void *canvas, int64_t offset_x, int64_t offset_y);
/// @brief Draw only the tiles intersecting a view rectangle (culled draw).
void rt_tilemaprenderer2d_draw_region(void *renderer,
                                      void *tilemap,
                                      void *canvas,
                                      int64_t offset_x,
                                      int64_t offset_y,
                                      int64_t view_x,
                                      int64_t view_y,
                                      int64_t view_w,
                                      int64_t view_h);

//===----------------------------------------------------------------------===//
// AnimationClip2D / AnimatedSprite2D — frame-range sprite animation.
//===----------------------------------------------------------------------===//

/// @brief Create an animation clip over a contiguous frame range.
void *rt_animationclip2d_new(int64_t start_frame,
                             int64_t frame_count,
                             int64_t frame_delay_ms,
                             int64_t loop);
/// @brief Get the clip's first frame index.
int64_t rt_animationclip2d_get_start_frame(void *clip);
/// @brief Get the clip's frame count.
int64_t rt_animationclip2d_get_frame_count(void *clip);
/// @brief Get the per-frame delay in milliseconds.
int64_t rt_animationclip2d_get_frame_delay_ms(void *clip);
/// @brief Get whether the clip loops (non-zero = loop).
int64_t rt_animationclip2d_get_loop(void *clip);

/// @brief Create an animated sprite wrapping a base sprite.
void *rt_animatedsprite2d_new(void *sprite);
/// @brief Assign the active animation clip.
void rt_animatedsprite2d_set_clip(void *animated_sprite, void *clip);
/// @brief Start/resume playback.
void rt_animatedsprite2d_play(void *animated_sprite);
/// @brief Stop playback and reset to the first frame.
void rt_animatedsprite2d_stop(void *animated_sprite);
/// @brief Advance playback by @p delta_ms milliseconds.
void rt_animatedsprite2d_update(void *animated_sprite, int64_t delta_ms);
/// @brief Get the current frame index.
int64_t rt_animatedsprite2d_get_frame(void *animated_sprite);
/// @brief Query whether playback is active.
int64_t rt_animatedsprite2d_is_playing(void *animated_sprite);

//===----------------------------------------------------------------------===//
// TextLayout2D — wrapped/aligned multi-line text measurement.
//===----------------------------------------------------------------------===//

/// @brief Create a text layout with default font/scale/wrap/alignment.
void *rt_textlayout2d_new(void);
/// @brief Set the layout font.
void rt_textlayout2d_set_font(void *layout, void *font);
/// @brief Set the integer text scale.
void rt_textlayout2d_set_scale(void *layout, int64_t scale);
/// @brief Set the wrap width in pixels (0 disables wrapping).
void rt_textlayout2d_set_wrap_width(void *layout, int64_t width);
/// @brief Set the horizontal alignment mode.
void rt_textlayout2d_set_alignment(void *layout, int64_t alignment);
/// @brief Set the text color (0x00RRGGBB).
void rt_textlayout2d_set_color(void *layout, int64_t rgb);
/// @brief Measure the laid-out pixel width of @p text.
int64_t rt_textlayout2d_measure_width(void *layout, rt_string text);
/// @brief Measure the laid-out pixel height of @p text (after wrapping).
int64_t rt_textlayout2d_measure_height(void *layout, rt_string text);

//===----------------------------------------------------------------------===//
// RenderPass2D / RenderGraph2D — composable post-process pass pipeline.
//===----------------------------------------------------------------------===//

/// @brief Create a render pass from a source to a target surface.
void *rt_renderpass2d_new(void *source, void *target);
/// @brief Set the pass input surface.
void rt_renderpass2d_set_source(void *pass, void *source);
/// @brief Set the pass output surface.
void rt_renderpass2d_set_target(void *pass, void *target);
/// @brief Set the shader applied during the pass.
void rt_renderpass2d_set_shader(void *pass, void *shader);
/// @brief Enable/disable the pass.
void rt_renderpass2d_set_enabled(void *pass, int64_t enabled);
/// @brief Query whether the pass is enabled.
int64_t rt_renderpass2d_get_enabled(void *pass);
/// @brief Execute the pass (source -> shader -> target).
void rt_renderpass2d_execute(void *pass);

/// @brief Create a render graph with an initial pass capacity.
void *rt_rendergraph2d_new(int64_t capacity);
/// @brief Append a pass to the graph.
void rt_rendergraph2d_add_pass(void *graph, void *pass);
/// @brief Remove all passes.
void rt_rendergraph2d_clear(void *graph);
/// @brief Number of passes in the graph.
int64_t rt_rendergraph2d_get_count(void *graph);
/// @brief Execute all enabled passes in order.
void rt_rendergraph2d_execute(void *graph);

//===----------------------------------------------------------------------===//
// CollisionMask2D / HitBox2D — pixel-mask and AABB collision helpers.
//===----------------------------------------------------------------------===//

/// @brief Create an empty (all-clear) collision mask of the given size.
void *rt_collisionmask2d_new(int64_t width, int64_t height);
/// @brief Build a collision mask from a Pixels image using an alpha threshold.
void *rt_collisionmask2d_from_pixels(void *pixels, int64_t alpha_threshold);
/// @brief Get the mask width.
int64_t rt_collisionmask2d_get_width(void *mask);
/// @brief Get the mask height.
int64_t rt_collisionmask2d_get_height(void *mask);
/// @brief Set a cell solid/clear.
void rt_collisionmask2d_set(void *mask, int64_t x, int64_t y, int64_t solid);
/// @brief Query whether a cell is solid.
int64_t rt_collisionmask2d_get(void *mask, int64_t x, int64_t y);
/// @brief Test pixel-perfect overlap of two masks at the given offsets.
int64_t rt_collisionmask2d_overlaps(
    void *a, int64_t ax, int64_t ay, void *b, int64_t bx, int64_t by);

/// @brief Create an axis-aligned hit box.
void *rt_hitbox2d_new(int64_t x, int64_t y, int64_t width, int64_t height);
/// @brief Set the hit box rectangle.
void rt_hitbox2d_set(void *hitbox, int64_t x, int64_t y, int64_t width, int64_t height);
/// @brief Get the hit box X.
int64_t rt_hitbox2d_get_x(void *hitbox);
/// @brief Get the hit box Y.
int64_t rt_hitbox2d_get_y(void *hitbox);
/// @brief Get the hit box width.
int64_t rt_hitbox2d_get_width(void *hitbox);
/// @brief Get the hit box height.
int64_t rt_hitbox2d_get_height(void *hitbox);
/// @brief Test whether point (x, y) lies inside the hit box.
int64_t rt_hitbox2d_contains(void *hitbox, int64_t x, int64_t y);
/// @brief Test whether two hit boxes intersect.
int64_t rt_hitbox2d_intersects(void *a, void *b);

//===----------------------------------------------------------------------===//
// Palette2D / Gradient2D — indexed color and color-ramp helpers.
//===----------------------------------------------------------------------===//

/// @brief Create an empty palette.
void *rt_palette2d_new(void);
/// @brief Set the color at a palette index (0xRRGGBBAA).
void rt_palette2d_set_color(void *palette, int64_t index, int64_t rgba);
/// @brief Get the color at a palette index.
int64_t rt_palette2d_get_color(void *palette, int64_t index);
/// @brief Get the color at a palette index as a Viper.Graphics.Color-compatible value.
int64_t rt_palette2d_get_color_value(void *palette, int64_t index);
/// @brief Number of entries in the palette.
int64_t rt_palette2d_get_count(void *palette);
/// @brief Map an image to the nearest palette colors, returning a new image.
void *rt_palette2d_apply(void *palette, void *pixels);
/// @brief Legacy palette-apply variant kept for backward compatibility.
void *rt_palette2d_apply_legacy(void *palette, void *pixels);

/// @brief Create a gradient between two RGBA endpoints with @p steps bands.
void *rt_gradient2d_new(int64_t start_rgba, int64_t end_rgba, int64_t steps);
/// @brief Set the gradient endpoint colors.
void rt_gradient2d_set_colors(void *gradient, int64_t start_rgba, int64_t end_rgba);
/// @brief Set the number of quantization steps.
void rt_gradient2d_set_steps(void *gradient, int64_t steps);
/// @brief Sample the gradient at @p t_pct (0..100), returning an RGBA color.
int64_t rt_gradient2d_sample(void *gradient, int64_t t_pct);
/// @brief Sample the gradient as a Viper.Graphics.Color-compatible value.
int64_t rt_gradient2d_sample_color(void *gradient, int64_t t_pct);
/// @brief Fill a Pixels image with a left-to-right gradient.
void rt_gradient2d_fill_horizontal(void *gradient, void *pixels);
/// @brief Fill a Pixels image with a top-to-bottom gradient.
void rt_gradient2d_fill_vertical(void *gradient, void *pixels);

//===----------------------------------------------------------------------===//
// CameraRig2D — smoothed/dead-zoned camera follow with screen shake.
//===----------------------------------------------------------------------===//

/// @brief Create a camera rig wrapping a Camera2D.
void *rt_camerarig2d_new(void *camera);
/// @brief Replace the rig's underlying camera.
void rt_camerarig2d_set_camera(void *rig, void *camera);
/// @brief Set the world-space follow target.
void rt_camerarig2d_set_target(void *rig, int64_t x, int64_t y);
/// @brief Set the follow smoothing factor (lerp percent, 0..100).
void rt_camerarig2d_set_smoothing(void *rig, int64_t lerp_pct);
/// @brief Set the dead-zone box within which the camera does not move.
void rt_camerarig2d_set_deadzone(void *rig, int64_t width, int64_t height);
/// @brief Add a screen-shake impulse (x, y offset).
void rt_camerarig2d_add_shake(void *rig, int64_t x, int64_t y);
/// @brief Clear any active screen shake.
void rt_camerarig2d_clear_shake(void *rig);
/// @brief Advance the rig one step toward its target (apply smoothing/shake).
void rt_camerarig2d_update(void *rig);
/// @brief Get the final render X (post smoothing + shake).
int64_t rt_camerarig2d_get_render_x(void *rig);
/// @brief Get the final render Y (post smoothing + shake).
int64_t rt_camerarig2d_get_render_y(void *rig);

//===----------------------------------------------------------------------===//
// Asset importers — texture packing, Aseprite, and Tiled map loading.
//===----------------------------------------------------------------------===//

/// @brief Create a texture-packer atlas backed by a Pixels image.
void *rt_texturepackeratlas_new(void *pixels);
/// @brief Get the underlying TextureAtlas handle.
void *rt_texturepackeratlas_get_atlas(void *packer);
/// @brief Register a named sub-rectangle region in the atlas.
void rt_texturepackeratlas_add(
    void *packer, rt_string name, int64_t x, int64_t y, int64_t width, int64_t height);
/// @brief Query whether a named region exists.
int64_t rt_texturepackeratlas_has(void *packer, rt_string name);
/// @brief Number of registered regions.
int64_t rt_texturepackeratlas_region_count(void *packer);

/// @brief Create an Aseprite importer.
void *rt_asepriteimporter_new(void);
/// @brief Set the frame grid (frame width/height) for slicing.
void rt_asepriteimporter_set_grid(void *importer, int64_t frame_width, int64_t frame_height);
/// @brief Get the configured frame width.
int64_t rt_asepriteimporter_get_frame_width(void *importer);
/// @brief Get the configured frame height.
int64_t rt_asepriteimporter_get_frame_height(void *importer);
/// @brief Slice a source image into a frame atlas using the configured grid.
void *rt_asepriteimporter_to_atlas(void *importer, void *pixels);

/// @brief Create a Tiled-map loader.
void *rt_tiledmaploader_new(void);
/// @brief Set the tile dimensions used when constructing tilemaps.
void rt_tiledmaploader_set_tile_size(void *loader, int64_t tile_width, int64_t tile_height);
/// @brief Get the configured tile width.
int64_t rt_tiledmaploader_get_tile_width(void *loader);
/// @brief Get the configured tile height.
int64_t rt_tiledmaploader_get_tile_height(void *loader);
/// @brief Create a new empty tilemap of the given size using loader settings.
void *rt_tiledmaploader_new_tilemap(void *loader, int64_t width, int64_t height);

#ifdef __cplusplus
}
#endif
