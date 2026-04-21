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

#ifdef __cplusplus
}
#endif
