#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RT_GRAPHICS2D_FILTER_NEAREST = 0,
    RT_GRAPHICS2D_FILTER_LINEAR = 1,
};

enum {
    RT_GRAPHICS2D_WRAP_CLAMP = 0,
    RT_GRAPHICS2D_WRAP_REPEAT = 1,
};

enum {
    RT_GRAPHICS2D_BLEND_ALPHA = 0,
    RT_GRAPHICS2D_BLEND_OPAQUE = 1,
    RT_GRAPHICS2D_BLEND_ADD = 2,
};

enum {
    RT_GRAPHICS2D_EFFECT_NONE = 0,
    RT_GRAPHICS2D_EFFECT_INVERT = 1,
    RT_GRAPHICS2D_EFFECT_GRAYSCALE = 2,
    RT_GRAPHICS2D_EFFECT_TINT = 3,
    RT_GRAPHICS2D_EFFECT_BLUR = 4,
};

void *rt_rendertarget2d_new(int64_t width, int64_t height);
int64_t rt_rendertarget2d_width(void *target);
int64_t rt_rendertarget2d_height(void *target);
void *rt_rendertarget2d_get_pixels(void *target);
void rt_rendertarget2d_clear(void *target, int64_t rgba);
void rt_rendertarget2d_resize(void *target, int64_t width, int64_t height);
void rt_rendertarget2d_draw_pixels(void *target, int64_t x, int64_t y, void *pixels);
void rt_rendertarget2d_draw_region(void *target,
                                   int64_t x,
                                   int64_t y,
                                   void *pixels,
                                   int64_t sx,
                                   int64_t sy,
                                   int64_t width,
                                   int64_t height);

void *rt_texture2d_new(void *pixels);
void *rt_texture2d_from_file(rt_string path);
int64_t rt_texture2d_width(void *texture);
int64_t rt_texture2d_height(void *texture);
void *rt_texture2d_get_pixels(void *texture);
void *rt_texture2d_clone_pixels(void *texture);
void rt_texture2d_set_filter(void *texture, int64_t filter);
int64_t rt_texture2d_get_filter(void *texture);
void rt_texture2d_set_wrap(void *texture, int64_t wrap);
int64_t rt_texture2d_get_wrap(void *texture);

void *rt_renderer2d_new(int64_t capacity);
void rt_renderer2d_begin(void *renderer);
void rt_renderer2d_clear(void *renderer);
int64_t rt_renderer2d_count(void *renderer);
void rt_renderer2d_set_tint(void *renderer, int64_t rgb);
void rt_renderer2d_set_alpha(void *renderer, int64_t alpha);
void rt_renderer2d_set_blend_mode(void *renderer, int64_t blend_mode);
void rt_renderer2d_draw_pixels(void *renderer, void *pixels, int64_t x, int64_t y);
void rt_renderer2d_draw_texture(void *renderer, void *texture, int64_t x, int64_t y);
void rt_renderer2d_draw_texture_rotated(
    void *renderer, void *texture, int64_t x, int64_t y, double angle_deg);
void rt_renderer2d_draw_texture_rotated_at(void *renderer,
                                           void *texture,
                                           int64_t x,
                                           int64_t y,
                                           int64_t pivot_x,
                                           int64_t pivot_y,
                                           double angle_deg);
void rt_renderer2d_draw_texture_scaled(
    void *renderer, void *texture, int64_t x, int64_t y, int64_t width, int64_t height);
void rt_renderer2d_draw_texture_region(void *renderer,
                                       void *texture,
                                       int64_t x,
                                       int64_t y,
                                       int64_t sx,
                                       int64_t sy,
                                       int64_t width,
                                       int64_t height);
void rt_renderer2d_draw_region(void *renderer,
                               void *pixels,
                               int64_t x,
                               int64_t y,
                               int64_t sx,
                               int64_t sy,
                               int64_t width,
                               int64_t height);
void rt_renderer2d_flush_to_target(void *renderer, void *target);
void rt_renderer2d_end(void *renderer, void *canvas);

void *rt_material2d_new(void);
void rt_material2d_set_tint(void *material, int64_t rgb);
int64_t rt_material2d_get_tint(void *material);
void rt_material2d_set_alpha(void *material, int64_t alpha);
int64_t rt_material2d_get_alpha(void *material);
void rt_material2d_set_blend_mode(void *material, int64_t blend_mode);
int64_t rt_material2d_get_blend_mode(void *material);
void *rt_material2d_apply(void *material, void *pixels);

void *rt_shader2d_new(int64_t effect);
void rt_shader2d_set_effect(void *shader, int64_t effect);
int64_t rt_shader2d_get_effect(void *shader);
void rt_shader2d_set_amount(void *shader, int64_t amount);
int64_t rt_shader2d_get_amount(void *shader);
void rt_shader2d_set_color(void *shader, int64_t rgb);
int64_t rt_shader2d_get_color(void *shader);
void *rt_shader2d_apply(void *shader, void *pixels);

void *rt_postprocess2d_new(void);
void rt_postprocess2d_set_effect(void *postprocess, int64_t effect);
void rt_postprocess2d_set_amount(void *postprocess, int64_t amount);
void rt_postprocess2d_set_color(void *postprocess, int64_t rgb);
void *rt_postprocess2d_apply(void *postprocess, void *pixels);

void *rt_viewport2d_new(int64_t virtual_width,
                        int64_t virtual_height,
                        int64_t screen_width,
                        int64_t screen_height);
void rt_viewport2d_set_virtual_size(void *viewport, int64_t width, int64_t height);
void rt_viewport2d_set_screen_size(void *viewport, int64_t width, int64_t height);
void rt_viewport2d_set_integer_scaling(void *viewport, int64_t enabled);
int64_t rt_viewport2d_get_scale(void *viewport);
int64_t rt_viewport2d_get_offset_x(void *viewport);
int64_t rt_viewport2d_get_offset_y(void *viewport);
int64_t rt_viewport2d_world_to_screen_x(void *viewport, int64_t x);
int64_t rt_viewport2d_world_to_screen_y(void *viewport, int64_t y);
int64_t rt_viewport2d_screen_to_world_x(void *viewport, int64_t x);
int64_t rt_viewport2d_screen_to_world_y(void *viewport, int64_t y);

void *rt_tileset2d_new(void *pixels, int64_t tile_width, int64_t tile_height);
int64_t rt_tileset2d_columns(void *tileset);
int64_t rt_tileset2d_rows(void *tileset);
int64_t rt_tileset2d_tile_count(void *tileset);
void *rt_tileset2d_get_tile_pixels(void *tileset, int64_t tile_index);

void *rt_tilelayer2d_new(int64_t width, int64_t height);
int64_t rt_tilelayer2d_width(void *layer);
int64_t rt_tilelayer2d_height(void *layer);
void rt_tilelayer2d_set(void *layer, int64_t x, int64_t y, int64_t tile);
int64_t rt_tilelayer2d_get(void *layer, int64_t x, int64_t y);
void rt_tilelayer2d_fill(void *layer, int64_t tile);
void rt_tilelayer2d_clear(void *layer);
void rt_tilelayer2d_set_visible(void *layer, int64_t visible);
int64_t rt_tilelayer2d_is_visible(void *layer);
void rt_tilelayer2d_set_opacity(void *layer, int64_t opacity);
int64_t rt_tilelayer2d_get_opacity(void *layer);

void *rt_objectlayer2d_new(int64_t capacity);
int64_t rt_objectlayer2d_add_rect(
    void *layer, int64_t x, int64_t y, int64_t width, int64_t height, int64_t type);
int64_t rt_objectlayer2d_count(void *layer);
void rt_objectlayer2d_clear(void *layer);
int64_t rt_objectlayer2d_get_x(void *layer, int64_t index);
int64_t rt_objectlayer2d_get_y(void *layer, int64_t index);
int64_t rt_objectlayer2d_get_width(void *layer, int64_t index);
int64_t rt_objectlayer2d_get_height(void *layer, int64_t index);
int64_t rt_objectlayer2d_get_type(void *layer, int64_t index);

void *rt_autotile2d_new(void);
void rt_autotile2d_set_variant(void *autotile, int64_t mask, int64_t tile);
int64_t rt_autotile2d_resolve(void *autotile, int64_t mask);
void rt_autotile2d_apply(void *autotile, void *layer, int64_t x, int64_t y, int64_t mask);

void *rt_path2d_new(int64_t capacity);
void rt_path2d_clear(void *path);
void rt_path2d_move_to(void *path, int64_t x, int64_t y);
void rt_path2d_line_to(void *path, int64_t x, int64_t y);
int64_t rt_path2d_count(void *path);
int64_t rt_path2d_get_x(void *path, int64_t index);
int64_t rt_path2d_get_y(void *path, int64_t index);
void rt_path2d_draw_to_pixels(void *path, void *pixels, int64_t rgba);

void *rt_shaperenderer2d_new(void);
void rt_shaperenderer2d_set_stroke(void *renderer, int64_t rgba);
void rt_shaperenderer2d_set_fill(void *renderer, int64_t rgba);
void rt_shaperenderer2d_line(
    void *renderer, void *pixels, int64_t x0, int64_t y0, int64_t x1, int64_t y1);
void rt_shaperenderer2d_rect(
    void *renderer, void *pixels, int64_t x, int64_t y, int64_t width, int64_t height);
void rt_shaperenderer2d_circle(void *renderer, void *pixels, int64_t x, int64_t y, int64_t radius);
void rt_shaperenderer2d_path(void *renderer, void *pixels, void *path);

void *rt_textrenderer2d_new(void);
void rt_textrenderer2d_set_font(void *renderer, void *font);
void rt_textrenderer2d_set_scale(void *renderer, int64_t scale);
void rt_textrenderer2d_set_color(void *renderer, int64_t rgb);
int64_t rt_textrenderer2d_measure_width(void *renderer, rt_string text);
int64_t rt_textrenderer2d_measure_height(void *renderer, rt_string text);
void rt_textrenderer2d_draw(void *renderer, void *canvas, int64_t x, int64_t y, rt_string text);

void *rt_sdffont_new(void *bitmap_font, int64_t spread);
void *rt_sdffont_get_bitmap_font(void *font);
int64_t rt_sdffont_get_spread(void *font);

void *rt_nineslice2d_new(void *pixels, int64_t left, int64_t top, int64_t right, int64_t bottom);
void rt_nineslice2d_draw_to_pixels(
    void *slice, void *target, int64_t x, int64_t y, int64_t width, int64_t height);

void *rt_debugdraw2d_new(int64_t capacity);
void rt_debugdraw2d_clear(void *debug_draw);
int64_t rt_debugdraw2d_count(void *debug_draw);
void rt_debugdraw2d_line(
    void *debug_draw, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t rgba);
void rt_debugdraw2d_rect(
    void *debug_draw, int64_t x, int64_t y, int64_t width, int64_t height, int64_t rgba);
void rt_debugdraw2d_circle(void *debug_draw, int64_t x, int64_t y, int64_t radius, int64_t rgba);
void rt_debugdraw2d_draw_to_pixels(void *debug_draw, void *pixels);

void *rt_transform2d_new(void);
int64_t rt_transform2d_get_x(void *transform);
void rt_transform2d_set_x(void *transform, int64_t x);
int64_t rt_transform2d_get_y(void *transform);
void rt_transform2d_set_y(void *transform, int64_t y);
int64_t rt_transform2d_get_scale_x(void *transform);
void rt_transform2d_set_scale_x(void *transform, int64_t scale_x);
int64_t rt_transform2d_get_scale_y(void *transform);
void rt_transform2d_set_scale_y(void *transform, int64_t scale_y);
int64_t rt_transform2d_get_rotation(void *transform);
void rt_transform2d_set_rotation(void *transform, int64_t degrees);
void rt_transform2d_set_position(void *transform, int64_t x, int64_t y);
void rt_transform2d_set_scale(void *transform, int64_t scale_x, int64_t scale_y);
void rt_transform2d_set_origin(void *transform, int64_t x, int64_t y);
void rt_transform2d_translate(void *transform, int64_t dx, int64_t dy);
int64_t rt_transform2d_transform_x(void *transform, int64_t x, int64_t y);
int64_t rt_transform2d_transform_y(void *transform, int64_t x, int64_t y);

void *rt_sampler2d_new(void);
void rt_sampler2d_set_filter(void *sampler, int64_t filter);
int64_t rt_sampler2d_get_filter(void *sampler);
void rt_sampler2d_set_wrap(void *sampler, int64_t wrap);
int64_t rt_sampler2d_get_wrap(void *sampler);
void rt_sampler2d_apply_to_texture(void *sampler, void *texture);

void *rt_blendstate2d_new(void);
void rt_blendstate2d_set_blend_mode(void *state, int64_t blend_mode);
int64_t rt_blendstate2d_get_blend_mode(void *state);
void rt_blendstate2d_set_tint(void *state, int64_t rgb);
int64_t rt_blendstate2d_get_tint(void *state);
void rt_blendstate2d_set_alpha(void *state, int64_t alpha);
int64_t rt_blendstate2d_get_alpha(void *state);
void rt_blendstate2d_apply_to_renderer(void *state, void *renderer);

void *rt_spriterenderer2d_new(void);
void rt_spriterenderer2d_set_material(void *sprite_renderer, void *material);
void rt_spriterenderer2d_set_sampler(void *sprite_renderer, void *sampler);
void rt_spriterenderer2d_set_blend_state(void *sprite_renderer, void *blend_state);
void rt_spriterenderer2d_draw_pixels(
    void *sprite_renderer, void *renderer, void *pixels, int64_t x, int64_t y);
void rt_spriterenderer2d_draw_texture(
    void *sprite_renderer, void *renderer, void *texture, int64_t x, int64_t y);

void *rt_tilechunkcache2d_new(int64_t chunk_width, int64_t chunk_height);
int64_t rt_tilechunkcache2d_get_chunk_width(void *cache);
int64_t rt_tilechunkcache2d_get_chunk_height(void *cache);
void rt_tilechunkcache2d_mark_dirty(void *cache);
void rt_tilechunkcache2d_clear_dirty(void *cache);
int64_t rt_tilechunkcache2d_get_dirty_count(void *cache);

void *rt_tilemaprenderer2d_new(void);
void rt_tilemaprenderer2d_set_chunk_cache(void *renderer, void *cache);
int64_t rt_tilemaprenderer2d_get_draw_count(void *renderer);
void rt_tilemaprenderer2d_draw(
    void *renderer, void *tilemap, void *canvas, int64_t offset_x, int64_t offset_y);
void rt_tilemaprenderer2d_draw_region(void *renderer,
                                      void *tilemap,
                                      void *canvas,
                                      int64_t offset_x,
                                      int64_t offset_y,
                                      int64_t view_x,
                                      int64_t view_y,
                                      int64_t view_w,
                                      int64_t view_h);

void *rt_animationclip2d_new(
    int64_t start_frame, int64_t frame_count, int64_t frame_delay_ms, int64_t loop);
int64_t rt_animationclip2d_get_start_frame(void *clip);
int64_t rt_animationclip2d_get_frame_count(void *clip);
int64_t rt_animationclip2d_get_frame_delay_ms(void *clip);
int64_t rt_animationclip2d_get_loop(void *clip);

void *rt_animatedsprite2d_new(void *sprite);
void rt_animatedsprite2d_set_clip(void *animated_sprite, void *clip);
void rt_animatedsprite2d_play(void *animated_sprite);
void rt_animatedsprite2d_stop(void *animated_sprite);
void rt_animatedsprite2d_update(void *animated_sprite, int64_t delta_ms);
int64_t rt_animatedsprite2d_get_frame(void *animated_sprite);
int64_t rt_animatedsprite2d_is_playing(void *animated_sprite);

void *rt_textlayout2d_new(void);
void rt_textlayout2d_set_font(void *layout, void *font);
void rt_textlayout2d_set_scale(void *layout, int64_t scale);
void rt_textlayout2d_set_wrap_width(void *layout, int64_t width);
void rt_textlayout2d_set_alignment(void *layout, int64_t alignment);
void rt_textlayout2d_set_color(void *layout, int64_t rgb);
int64_t rt_textlayout2d_measure_width(void *layout, rt_string text);
int64_t rt_textlayout2d_measure_height(void *layout, rt_string text);

void *rt_renderpass2d_new(void *source, void *target);
void rt_renderpass2d_set_source(void *pass, void *source);
void rt_renderpass2d_set_target(void *pass, void *target);
void rt_renderpass2d_set_shader(void *pass, void *shader);
void rt_renderpass2d_set_enabled(void *pass, int64_t enabled);
int64_t rt_renderpass2d_get_enabled(void *pass);
void rt_renderpass2d_execute(void *pass);

void *rt_rendergraph2d_new(int64_t capacity);
void rt_rendergraph2d_add_pass(void *graph, void *pass);
void rt_rendergraph2d_clear(void *graph);
int64_t rt_rendergraph2d_get_count(void *graph);
void rt_rendergraph2d_execute(void *graph);

void *rt_collisionmask2d_new(int64_t width, int64_t height);
void *rt_collisionmask2d_from_pixels(void *pixels, int64_t alpha_threshold);
int64_t rt_collisionmask2d_get_width(void *mask);
int64_t rt_collisionmask2d_get_height(void *mask);
void rt_collisionmask2d_set(void *mask, int64_t x, int64_t y, int64_t solid);
int64_t rt_collisionmask2d_get(void *mask, int64_t x, int64_t y);
int64_t rt_collisionmask2d_overlaps(
    void *a, int64_t ax, int64_t ay, void *b, int64_t bx, int64_t by);

void *rt_hitbox2d_new(int64_t x, int64_t y, int64_t width, int64_t height);
void rt_hitbox2d_set(void *hitbox, int64_t x, int64_t y, int64_t width, int64_t height);
int64_t rt_hitbox2d_get_x(void *hitbox);
int64_t rt_hitbox2d_get_y(void *hitbox);
int64_t rt_hitbox2d_get_width(void *hitbox);
int64_t rt_hitbox2d_get_height(void *hitbox);
int64_t rt_hitbox2d_contains(void *hitbox, int64_t x, int64_t y);
int64_t rt_hitbox2d_intersects(void *a, void *b);

void *rt_palette2d_new(void);
void rt_palette2d_set_color(void *palette, int64_t index, int64_t rgba);
int64_t rt_palette2d_get_color(void *palette, int64_t index);
int64_t rt_palette2d_get_count(void *palette);
void *rt_palette2d_apply(void *palette, void *pixels);
void *rt_palette2d_apply_legacy(void *palette, void *pixels);

void *rt_gradient2d_new(int64_t start_rgba, int64_t end_rgba, int64_t steps);
void rt_gradient2d_set_colors(void *gradient, int64_t start_rgba, int64_t end_rgba);
void rt_gradient2d_set_steps(void *gradient, int64_t steps);
int64_t rt_gradient2d_sample(void *gradient, int64_t t_pct);
void rt_gradient2d_fill_horizontal(void *gradient, void *pixels);
void rt_gradient2d_fill_vertical(void *gradient, void *pixels);

void *rt_camerarig2d_new(void *camera);
void rt_camerarig2d_set_camera(void *rig, void *camera);
void rt_camerarig2d_set_target(void *rig, int64_t x, int64_t y);
void rt_camerarig2d_set_smoothing(void *rig, int64_t lerp_pct);
void rt_camerarig2d_set_deadzone(void *rig, int64_t width, int64_t height);
void rt_camerarig2d_add_shake(void *rig, int64_t x, int64_t y);
void rt_camerarig2d_clear_shake(void *rig);
void rt_camerarig2d_update(void *rig);
int64_t rt_camerarig2d_get_render_x(void *rig);
int64_t rt_camerarig2d_get_render_y(void *rig);

void *rt_texturepackeratlas_new(void *pixels);
void *rt_texturepackeratlas_get_atlas(void *packer);
void rt_texturepackeratlas_add(
    void *packer, rt_string name, int64_t x, int64_t y, int64_t width, int64_t height);
int64_t rt_texturepackeratlas_has(void *packer, rt_string name);
int64_t rt_texturepackeratlas_region_count(void *packer);

void *rt_asepriteimporter_new(void);
void rt_asepriteimporter_set_grid(void *importer, int64_t frame_width, int64_t frame_height);
int64_t rt_asepriteimporter_get_frame_width(void *importer);
int64_t rt_asepriteimporter_get_frame_height(void *importer);
void *rt_asepriteimporter_to_atlas(void *importer, void *pixels);

void *rt_tiledmaploader_new(void);
void rt_tiledmaploader_set_tile_size(void *loader, int64_t tile_width, int64_t tile_height);
int64_t rt_tiledmaploader_get_tile_width(void *loader);
int64_t rt_tiledmaploader_get_tile_height(void *loader);
void *rt_tiledmaploader_new_tilemap(void *loader, int64_t width, int64_t height);

#ifdef __cplusplus
}
#endif
