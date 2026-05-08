//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGraphics2DTests.cpp
// Purpose: Tests for Viper.Graphics 2D rendering, tilemap, UI, and game helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_camera.h"
#include "rt_graphics.h"
#include "rt_graphics2d.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_sprite.h"
#include "rt_string.h"
#include "rt_tilemap.h"

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static int64_t red_of(int64_t rgba) {
    return ((uint32_t)rgba >> 24) & 255;
}

static int64_t green_of(int64_t rgba) {
    return ((uint32_t)rgba >> 16) & 255;
}

static int64_t blue_of(int64_t rgba) {
    return ((uint32_t)rgba >> 8) & 255;
}

static void test_graphics2d_handles_have_unique_classes_and_reject_wrong_types() {
    void *target = rt_rendertarget2d_new(1, 1);
    void *pixels = rt_pixels_new(1, 1);
    void *texture = rt_texture2d_new(pixels);
    void *renderer = rt_renderer2d_new(1);
    void *sprite = rt_sprite_new(pixels);

    assert(target != nullptr);
    assert(texture != nullptr);
    assert(renderer != nullptr);
    assert(sprite != nullptr);

    int64_t target_class = rt_obj_class_id(target);
    int64_t texture_class = rt_obj_class_id(texture);
    int64_t renderer_class = rt_obj_class_id(renderer);
    int64_t pixels_class = rt_obj_class_id(pixels);
    int64_t sprite_class = rt_obj_class_id(sprite);

    assert(target_class != 0);
    assert(texture_class != 0);
    assert(renderer_class != 0);
    assert(target_class != texture_class);
    assert(target_class != renderer_class);
    assert(texture_class != renderer_class);
    assert(texture_class != pixels_class);
    assert(texture_class != sprite_class);

    assert(rt_texture2d_new(sprite) == nullptr);
    assert(rt_rendertarget2d_get_pixels(sprite) == nullptr);
    assert(rt_texture2d_get_pixels(sprite) == nullptr);
    printf("test_graphics2d_handles_have_unique_classes_and_reject_wrong_types: PASSED\n");
}

static void test_render_target_alpha_blend() {
    void *target = rt_rendertarget2d_new(4, 4);
    assert(target != nullptr);
    assert(rt_rendertarget2d_width(target) == 4);
    assert(rt_rendertarget2d_height(target) == 4);

    rt_rendertarget2d_clear(target, 0x000000FF);
    void *src = rt_pixels_new(1, 1);
    rt_pixels_set(src, 0, 0, 0xFF000080);
    rt_rendertarget2d_draw_pixels(target, 1, 1, src);

    int64_t blended = rt_pixels_get(rt_rendertarget2d_get_pixels(target), 1, 1);
    assert(red_of(blended) >= 126 && red_of(blended) <= 129);
    assert(green_of(blended) == 0);
    assert(blue_of(blended) == 0);

    rt_rendertarget2d_draw_region(target, INT64_MAX - 1, 0, src, -1, 0, INT64_MAX, 1);
    assert(rt_pixels_get(rt_rendertarget2d_get_pixels(target), 0, 0) == 0x000000FF);

    rt_rendertarget2d_clear(target, 0x00000000);
    rt_rendertarget2d_draw_pixels(target, 2, 2, src);
    int64_t over_transparent = rt_pixels_get(rt_rendertarget2d_get_pixels(target), 2, 2);
    assert(red_of(over_transparent) == 255);
    assert(green_of(over_transparent) == 0);
    assert(blue_of(over_transparent) == 0);
    assert((over_transparent & 255) >= 127 && (over_transparent & 255) <= 128);
    printf("test_render_target_alpha_blend: PASSED\n");
}

static void test_texture_renderer_material_and_effects() {
    void *src = rt_pixels_new(2, 2);
    rt_pixels_set(src, 0, 0, 0x808080FF);
    rt_pixels_set(src, 1, 0, 0x112233FF);

    void *texture = rt_texture2d_new(src);
    assert(texture != nullptr);
    assert(rt_texture2d_width(texture) == 2);
    assert(rt_texture2d_height(texture) == 2);
    rt_texture2d_set_filter(texture, RT_GRAPHICS2D_FILTER_LINEAR);
    rt_texture2d_set_wrap(texture, RT_GRAPHICS2D_WRAP_REPEAT);
    assert(rt_texture2d_get_filter(texture) == RT_GRAPHICS2D_FILTER_LINEAR);
    assert(rt_texture2d_get_wrap(texture) == RT_GRAPHICS2D_WRAP_REPEAT);

    void *material = rt_material2d_new();
    rt_material2d_set_tint(material, 0x00FF0000);
    rt_material2d_set_alpha(material, 128);
    void *tinted = rt_material2d_apply(material, src);
    int64_t tinted_pixel = rt_pixels_get(tinted, 0, 0);
    assert(red_of(tinted_pixel) >= 127 && red_of(tinted_pixel) <= 129);
    assert(green_of(tinted_pixel) == 0);
    assert((tinted_pixel & 255) >= 127 && (tinted_pixel & 255) <= 129);

    void *shader = rt_shader2d_new(RT_GRAPHICS2D_EFFECT_INVERT);
    void *inverted = rt_shader2d_apply(shader, src);
    assert(rt_pixels_get(inverted, 1, 0) == 0xEEDDCCFF);

    void *post = rt_postprocess2d_new();
    rt_postprocess2d_set_effect(post, RT_GRAPHICS2D_EFFECT_GRAYSCALE);
    void *gray = rt_postprocess2d_apply(post, src);
    int64_t g = rt_pixels_get(gray, 1, 0);
    assert(red_of(g) == green_of(g));
    assert(green_of(g) == blue_of(g));

    void *target = rt_rendertarget2d_new(4, 4);
    void *renderer = rt_renderer2d_new(1);
    rt_renderer2d_begin(renderer);
    rt_renderer2d_draw_texture(renderer, texture, 1, 1);
    assert(rt_renderer2d_count(renderer) == 1);
    rt_renderer2d_flush_to_target(renderer, target);
    assert(rt_pixels_get(rt_rendertarget2d_get_pixels(target), 1, 1) == 0x808080FF);
    rt_renderer2d_end(renderer, nullptr);
    assert(rt_renderer2d_count(renderer) == 0);
    rt_renderer2d_end(renderer, nullptr);
    assert(rt_renderer2d_count(renderer) == 0);

    void *sample_src = rt_pixels_new(2, 1);
    rt_pixels_set(sample_src, 0, 0, 0xFF0000FF);
    rt_pixels_set(sample_src, 1, 0, 0x00FF00FF);
    void *sample_texture = rt_texture2d_new(sample_src);
    rt_texture2d_set_wrap(sample_texture, RT_GRAPHICS2D_WRAP_REPEAT);
    rt_rendertarget2d_resize(target, 4, 1);
    rt_rendertarget2d_clear(target, 0x00000000);
    rt_renderer2d_begin(renderer);
    rt_renderer2d_draw_texture_region(renderer, sample_texture, 0, 0, 0, 0, 4, 1);
    rt_renderer2d_flush_to_target(renderer, target);
    void *target_pixels = rt_rendertarget2d_get_pixels(target);
    assert(rt_pixels_get(target_pixels, 0, 0) == 0xFF0000FF);
    assert(rt_pixels_get(target_pixels, 1, 0) == 0x00FF00FF);
    assert(rt_pixels_get(target_pixels, 2, 0) == 0xFF0000FF);
    assert(rt_pixels_get(target_pixels, 3, 0) == 0x00FF00FF);
    rt_renderer2d_end(renderer, nullptr);

    rt_texture2d_set_filter(sample_texture, RT_GRAPHICS2D_FILTER_LINEAR);
    rt_texture2d_set_wrap(sample_texture, RT_GRAPHICS2D_WRAP_CLAMP);
    rt_rendertarget2d_resize(target, 3, 1);
    rt_rendertarget2d_clear(target, 0x00000000);
    rt_renderer2d_begin(renderer);
    rt_renderer2d_draw_texture_scaled(renderer, sample_texture, 0, 0, 3, 1);
    rt_renderer2d_flush_to_target(renderer, target);
    target_pixels = rt_rendertarget2d_get_pixels(target);
    int64_t middle = rt_pixels_get(target_pixels, 1, 0);
    assert(red_of(middle) >= 126 && red_of(middle) <= 129);
    assert(green_of(middle) >= 126 && green_of(middle) <= 129);
    rt_renderer2d_end(renderer, nullptr);

    rt_rendertarget2d_clear(target, 0x101010FF);
    void *add_src = rt_pixels_new(1, 1);
    rt_pixels_set(add_src, 0, 0, 0x202000FF);
    rt_renderer2d_begin(renderer);
    rt_renderer2d_set_blend_mode(renderer, RT_GRAPHICS2D_BLEND_ADD);
    rt_renderer2d_draw_pixels(renderer, add_src, 0, 0);
    rt_renderer2d_flush_to_target(renderer, target);
    assert(rt_pixels_get(rt_rendertarget2d_get_pixels(target), 0, 0) == 0x303010FF);
    rt_renderer2d_end(renderer, nullptr);
    printf("test_texture_renderer_material_and_effects: PASSED\n");
}

static void test_viewport_tiles_and_objects() {
    void *viewport = rt_viewport2d_new(320, 180, 1280, 720);
    assert(rt_viewport2d_get_scale(viewport) == 4000);
    assert(rt_viewport2d_world_to_screen_x(viewport, 10) == 40);
    assert(rt_viewport2d_screen_to_world_y(viewport, 80) == 20);
    rt_viewport2d_set_screen_size(viewport, 1000, 720);
    rt_viewport2d_set_integer_scaling(viewport, 1);
    assert(rt_viewport2d_get_scale(viewport) == 3000);
    assert(rt_viewport2d_get_offset_x(viewport) == 20);
    rt_viewport2d_set_screen_size(viewport, 160, 90);
    assert(rt_viewport2d_get_scale(viewport) == 500);
    assert(rt_viewport2d_get_offset_x(viewport) == 0);
    assert(rt_viewport2d_get_offset_y(viewport) == 0);

    void *tiles = rt_pixels_new(4, 2);
    for (int64_t y = 0; y < 2; y++) {
        for (int64_t x = 0; x < 2; x++)
            rt_pixels_set(tiles, x, y, 0xFF0000FF);
        for (int64_t x = 2; x < 4; x++)
            rt_pixels_set(tiles, x, y, 0x00FF00FF);
    }
    void *tileset = rt_tileset2d_new(tiles, 2, 2);
    assert(rt_tileset2d_columns(tileset) == 2);
    assert(rt_tileset2d_rows(tileset) == 1);
    assert(rt_tileset2d_tile_count(tileset) == 2);
    void *tile = rt_tileset2d_get_tile_pixels(tileset, 1);
    assert(rt_pixels_get(tile, 0, 0) == 0x00FF00FF);

    void *layer = rt_tilelayer2d_new(3, 2);
    rt_tilelayer2d_set(layer, 1, 1, 7);
    assert(rt_tilelayer2d_get(layer, 1, 1) == 7);
    rt_tilelayer2d_set_opacity(layer, 300);
    assert(rt_tilelayer2d_get_opacity(layer) == 255);

    void *objects = rt_objectlayer2d_new(1);
    int64_t index = rt_objectlayer2d_add_rect(objects, 10, 20, 30, 40, 5);
    assert(index == 0);
    assert(rt_objectlayer2d_count(objects) == 1);
    assert(rt_objectlayer2d_get_x(objects, 0) == 10);
    assert(rt_objectlayer2d_get_height(objects, 0) == 40);
    assert(rt_objectlayer2d_get_type(objects, 0) == 5);
    int64_t normalized = rt_objectlayer2d_add_rect(objects, 40, 50, -10, -20, 6);
    assert(normalized == 1);
    assert(rt_objectlayer2d_get_x(objects, 1) == 30);
    assert(rt_objectlayer2d_get_y(objects, 1) == 30);
    assert(rt_objectlayer2d_get_width(objects, 1) == 10);
    assert(rt_objectlayer2d_get_height(objects, 1) == 20);
    assert(rt_objectlayer2d_add_rect(objects, 0, 0, 0, 1, 7) == -1);
    assert(rt_objectlayer2d_add_rect(objects, 0, 0, INT64_MIN, 1, 7) == -1);
    assert(rt_objectlayer2d_count(objects) == 2);

    void *autotile = rt_autotile2d_new();
    rt_autotile2d_set_variant(autotile, 5, 42);
    assert(rt_autotile2d_resolve(autotile, 5) == 42);
    rt_autotile2d_apply(autotile, layer, 2, 0, 5);
    assert(rt_tilelayer2d_get(layer, 2, 0) == 42);
    printf("test_viewport_tiles_and_objects: PASSED\n");
}

static void test_paths_shapes_text_nineslice_and_debugdraw() {
    void *pixels = rt_pixels_new(8, 8);
    void *path = rt_path2d_new(2);
    rt_path2d_move_to(path, 0, 0);
    rt_path2d_line_to(path, 3, 0);
    assert(rt_path2d_count(path) == 2);
    rt_path2d_draw_to_pixels(path, pixels, 0x00FF0000);
    assert(rt_pixels_get(pixels, 3, 0) == 0xFF0000FF);
    rt_path2d_draw_to_pixels(path, pixels, rt_color_rgba(0, 0, 255, 255));
    assert(rt_pixels_get(pixels, 3, 0) == 0x0000FFFF);

    void *shape = rt_shaperenderer2d_new();
    rt_shaperenderer2d_set_stroke(shape, 0x0000FF00);
    rt_shaperenderer2d_line(shape, pixels, 0, 1, 3, 1);
    assert(rt_pixels_get(pixels, 3, 1) == 0x00FF00FF);
    rt_shaperenderer2d_set_stroke(shape, rt_color_rgba(255, 0, 0, 255));
    rt_shaperenderer2d_line(shape, pixels, 0, 3, 3, 3);
    assert(rt_pixels_get(pixels, 3, 3) == 0xFF0000FF);
    rt_shaperenderer2d_set_stroke(shape, -1);
    rt_shaperenderer2d_line(shape, pixels, 0, 7, 7, 7);
    assert(rt_pixels_get(pixels, 7, 7) == 0);
    void *stroke_disabled_path = rt_path2d_new(2);
    rt_path2d_move_to(stroke_disabled_path, 6, 6);
    rt_path2d_line_to(stroke_disabled_path, 7, 7);
    rt_shaperenderer2d_path(shape, pixels, stroke_disabled_path);
    assert(rt_pixels_get(pixels, 7, 7) == 0);

    rt_string text = rt_str_from_lit("Hi", 2);
    void *text_renderer = rt_textrenderer2d_new();
    rt_textrenderer2d_set_scale(text_renderer, 2);
    assert(rt_textrenderer2d_measure_width(text_renderer, text) == 32);
    assert(rt_textrenderer2d_measure_height(text_renderer, text) == 16);
    void *sdf = rt_sdffont_new(nullptr, 6);
    assert(rt_sdffont_get_bitmap_font(sdf) == nullptr);
    assert(rt_sdffont_get_spread(sdf) == 6);

    void *source = rt_pixels_new(3, 3);
    rt_pixels_set(source, 0, 0, 0xFF0000FF);
    rt_pixels_set(source, 2, 0, 0x00FF00FF);
    rt_pixels_set(source, 0, 2, 0x0000FFFF);
    rt_pixels_set(source, 2, 2, 0xFFFF00FF);
    void *slice = rt_nineslice2d_new(source, 1, 1, 1, 1);
    void *target = rt_pixels_new(5, 5);
    rt_nineslice2d_draw_to_pixels(slice, target, 0, 0, 5, 5);
    assert(rt_pixels_get(target, 0, 0) == 0xFF0000FF);
    assert(rt_pixels_get(target, 4, 0) == 0x00FF00FF);
    assert(rt_pixels_get(target, 0, 4) == 0x0000FFFF);
    assert(rt_pixels_get(target, 4, 4) == 0xFFFF00FF);

    void *debug = rt_debugdraw2d_new(1);
    rt_debugdraw2d_line(debug, 0, 2, 3, 2, 0x000000FF);
    rt_debugdraw2d_rect(debug, 1, 1, 3, 3, 0x00FFFFFF);
    rt_debugdraw2d_line(debug, 0, 4, 3, 4, rt_color_rgba(0, 255, 0, 255));
    assert(rt_debugdraw2d_count(debug) == 3);
    rt_debugdraw2d_draw_to_pixels(debug, pixels);
    assert(rt_pixels_get(pixels, 0, 2) == 0x0000FFFF);
    assert(rt_pixels_get(pixels, 3, 4) == 0x00FF00FF);
    rt_debugdraw2d_clear(debug);
    assert(rt_debugdraw2d_count(debug) == 0);
    printf("test_paths_shapes_text_nineslice_and_debugdraw: PASSED\n");
}

static void test_transform_sampler_blend_and_sprite_renderer() {
    void *transform = rt_transform2d_new();
    assert(transform != nullptr);
    rt_transform2d_set_position(transform, 10, 20);
    rt_transform2d_set_scale(transform, 200, 200);
    rt_transform2d_translate(transform, 1, -2);
    assert(rt_transform2d_get_x(transform) == 11);
    assert(rt_transform2d_get_y(transform) == 18);
    assert(rt_transform2d_transform_x(transform, 2, 3) == 15);
    assert(rt_transform2d_transform_y(transform, 2, 3) == 24);
    rt_transform2d_set_position(transform, INT64_MAX, INT64_MAX);
    rt_transform2d_set_origin(transform, 0, 0);
    rt_transform2d_set_scale(transform, 100, 100);
    assert(rt_transform2d_transform_x(transform, 1, 1) == INT64_MAX);
    assert(rt_transform2d_transform_y(transform, 1, 1) == INT64_MAX);

    void *pixels = rt_pixels_new(1, 1);
    rt_pixels_set(pixels, 0, 0, 0x336699FF);
    void *texture = rt_texture2d_new(pixels);
    void *sampler = rt_sampler2d_new();
    rt_sampler2d_set_filter(sampler, RT_GRAPHICS2D_FILTER_LINEAR);
    rt_sampler2d_set_wrap(sampler, RT_GRAPHICS2D_WRAP_REPEAT);
    rt_sampler2d_apply_to_texture(sampler, texture);
    assert(rt_texture2d_get_filter(texture) == RT_GRAPHICS2D_FILTER_LINEAR);
    assert(rt_texture2d_get_wrap(texture) == RT_GRAPHICS2D_WRAP_REPEAT);

    void *blend = rt_blendstate2d_new();
    rt_blendstate2d_set_blend_mode(blend, RT_GRAPHICS2D_BLEND_OPAQUE);
    rt_blendstate2d_set_alpha(blend, 300);
    assert(rt_blendstate2d_get_alpha(blend) == 255);

    void *renderer = rt_renderer2d_new(1);
    void *sprite_renderer = rt_spriterenderer2d_new();
    rt_spriterenderer2d_set_sampler(sprite_renderer, sampler);
    rt_spriterenderer2d_set_blend_state(sprite_renderer, blend);
    rt_renderer2d_begin(renderer);
    rt_spriterenderer2d_draw_texture(sprite_renderer, renderer, texture, 0, 0);
    assert(rt_renderer2d_count(renderer) == 1);

    void *texture_default = rt_texture2d_new(pixels);
    assert(rt_texture2d_get_filter(texture_default) == RT_GRAPHICS2D_FILTER_NEAREST);
    assert(rt_texture2d_get_wrap(texture_default) == RT_GRAPHICS2D_WRAP_CLAMP);
    rt_spriterenderer2d_draw_texture(sprite_renderer, renderer, texture_default, 0, 0);
    assert(rt_texture2d_get_filter(texture_default) == RT_GRAPHICS2D_FILTER_NEAREST);
    assert(rt_texture2d_get_wrap(texture_default) == RT_GRAPHICS2D_WRAP_CLAMP);
    printf("test_transform_sampler_blend_and_sprite_renderer: PASSED\n");
}

static void test_animation_collision_palette_gradient_and_rig() {
    void *frame0 = rt_pixels_new(2, 2);
    void *frame1 = rt_pixels_new(2, 2);
    rt_pixels_fill(frame0, 0x000000FF);
    rt_pixels_fill(frame1, 0xFFFFFFFF);
    void *sprite = rt_sprite_new(frame0);
    rt_sprite_add_frame(sprite, frame1);

    void *clip = rt_animationclip2d_new(0, 2, 50, 1);
    void *animated = rt_animatedsprite2d_new(sprite);
    rt_animatedsprite2d_set_clip(animated, clip);
    rt_animatedsprite2d_update(animated, 50);
    assert(rt_animatedsprite2d_get_frame(animated) == 1);
    assert(rt_sprite_get_frame(sprite) == 1);
    rt_animatedsprite2d_update(animated, INT64_MAX);
    assert(rt_sprite_get_frame(sprite) >= 0);
    assert(rt_sprite_get_frame(sprite) < rt_sprite_get_frame_count(sprite));

    void *bad_clip = rt_animationclip2d_new(99, 5, 10, 0);
    rt_animatedsprite2d_set_clip(animated, bad_clip);
    rt_animatedsprite2d_update(animated, 10);
    assert(rt_animatedsprite2d_is_playing(animated) == 0);

    void *mask_a = rt_collisionmask2d_new(2, 2);
    void *mask_b = rt_collisionmask2d_new(2, 2);
    rt_collisionmask2d_set(mask_a, 1, 1, 1);
    rt_collisionmask2d_set(mask_b, 0, 0, 1);
    assert(rt_collisionmask2d_overlaps(mask_a, 0, 0, mask_b, 1, 1) == 1);
    rt_collisionmask2d_set(mask_a, 1, 0, 1);
    rt_collisionmask2d_set(mask_b, 0, 0, 1);
    assert(rt_collisionmask2d_overlaps(mask_a, INT64_MAX - 1, 0, mask_b, INT64_MAX, 0) == 1);
    void *alpha_pixels = rt_pixels_new(2, 1);
    rt_pixels_set(alpha_pixels, 0, 0, 0xFFFFFFFF);
    rt_pixels_set(alpha_pixels, 1, 0, 0xFFFFFF00);
    void *alpha_mask = rt_collisionmask2d_from_pixels(alpha_pixels, 0);
    assert(rt_collisionmask2d_get(alpha_mask, 0, 0) == 1);
    assert(rt_collisionmask2d_get(alpha_mask, 1, 0) == 0);

    void *hit_a = rt_hitbox2d_new(0, 0, 10, 10);
    void *hit_b = rt_hitbox2d_new(5, 5, 2, 2);
    assert(rt_hitbox2d_contains(hit_a, 9, 9) == 1);
    assert(rt_hitbox2d_intersects(hit_a, hit_b) == 1);
    void *hit_max = rt_hitbox2d_new(INT64_MAX - 1, 0, 2, 2);
    void *hit_edge = rt_hitbox2d_new(INT64_MAX, 1, 1, 1);
    assert(rt_hitbox2d_contains(hit_max, INT64_MAX, 1) == 1);
    assert(rt_hitbox2d_intersects(hit_max, hit_edge) == 1);

    void *palette = rt_palette2d_new();
    rt_palette2d_set_color(palette, 3, 0xFF0000FF);
    rt_palette2d_set_color(palette, 4, rt_color_rgba(0, 0, 255, 128));
    void *indexed = rt_pixels_new(1, 1);
    rt_pixels_set(indexed, 0, 0, 0x00000003);
    void *mapped = rt_palette2d_apply(palette, indexed);
    assert(rt_pixels_get(mapped, 0, 0) == 0xFF0000FF);
    rt_pixels_set(indexed, 0, 0, 0x00000004);
    void *mapped_tagged = rt_palette2d_apply(palette, indexed);
    assert(rt_pixels_get(mapped_tagged, 0, 0) == 0x0000FF80);
    rt_pixels_set(indexed, 0, 0, 0x030000FF);
    void *mapped_red_channel = rt_palette2d_apply(palette, indexed);
    assert(rt_pixels_get(mapped_red_channel, 0, 0) == 0xFF0000FF);

    void *gradient = rt_gradient2d_new(0x000000FF, 0xFFFFFFFF, 2);
    assert(rt_gradient2d_sample(gradient, 100) == 0xFFFFFFFF);
    int64_t smooth_mid = rt_gradient2d_sample(gradient, 50);
    assert(red_of(smooth_mid) >= 126 && red_of(smooth_mid) <= 128);
    void *stepped_gradient = rt_gradient2d_new(0x000000FF, 0xFFFFFFFF, 3);
    assert(rt_gradient2d_sample(stepped_gradient, 20) == 0x000000FF);
    assert(rt_gradient2d_sample(stepped_gradient, 50) == 0x7F7F7FFF);
    assert(rt_gradient2d_sample(stepped_gradient, 80) == 0xFFFFFFFF);
    void *tagged_gradient =
        rt_gradient2d_new(rt_color_rgba(0, 255, 0, 128), rt_color_rgba(255, 0, 0, 255), 2);
    assert(rt_gradient2d_sample(tagged_gradient, 0) == 0x00FF0080);
    void *grad_pixels = rt_pixels_new(2, 1);
    rt_gradient2d_fill_horizontal(gradient, grad_pixels);
    assert(rt_pixels_get(grad_pixels, 0, 0) == 0x000000FF);
    assert(rt_pixels_get(grad_pixels, 1, 0) == 0xFFFFFFFF);

    void *camera = rt_camera_new(100, 100);
    void *rig = rt_camerarig2d_new(camera);
    rt_camerarig2d_set_target(rig, 50, 50);
    rt_camerarig2d_update(rig);
    rt_camerarig2d_add_shake(rig, 3, -2);
    assert(rt_camerarig2d_get_render_x(rig) == rt_camera_get_x(camera) + 3);
    assert(rt_camerarig2d_get_render_y(rig) == rt_camera_get_y(camera) - 2);
    rt_camerarig2d_add_shake(rig, INT64_MAX, INT64_MIN);
    assert(rt_camerarig2d_get_render_x(rig) == INT64_MAX);
    assert(rt_camerarig2d_get_render_y(rig) <= rt_camera_get_y(camera));
    assert(rt_material2d_get_alpha(alpha_pixels) == 255);
    assert(rt_sampler2d_get_wrap(alpha_pixels) == RT_GRAPHICS2D_WRAP_CLAMP);
    assert(rt_palette2d_apply(alpha_pixels, indexed) == nullptr);
    assert(rt_collisionmask2d_get_width(alpha_pixels) == 0);
    printf("test_animation_collision_palette_gradient_and_rig: PASSED\n");
}

static void test_camera_extreme_arithmetic_saturates() {
    void *camera = rt_camera_new(INT64_MAX, INT64_MAX);
    assert(camera != nullptr);
    rt_camera_follow(camera, INT64_MAX, INT64_MAX);
    rt_camera_move(camera, INT64_MAX, INT64_MAX);
    assert(rt_camera_get_x(camera) == INT64_MAX);
    assert(rt_camera_get_y(camera) == INT64_MAX);

    rt_camera_set_bounds(camera, INT64_MIN, INT64_MIN, INT64_MAX, INT64_MAX);
    rt_camera_smooth_follow(camera, INT64_MIN, INT64_MIN, 500);
    assert(rt_camera_get_x(camera) >= INT64_MIN);
    assert(rt_camera_get_y(camera) >= INT64_MIN);
    printf("test_camera_extreme_arithmetic_saturates: PASSED\n");
}

static void test_layout_rendergraph_tile_helpers_and_importers() {
    rt_string text = rt_str_from_lit("Hello", 5);
    void *layout = rt_textlayout2d_new();
    rt_textlayout2d_set_scale(layout, 2);
    rt_textlayout2d_set_wrap_width(layout, 16);
    assert(rt_textlayout2d_measure_width(layout, text) == 16);
    assert(rt_textlayout2d_measure_height(layout, text) >= 16);

    void *src = rt_rendertarget2d_new(1, 1);
    void *dst = rt_rendertarget2d_new(1, 1);
    rt_rendertarget2d_clear(src, 0x112233FF);
    void *shader = rt_shader2d_new(RT_GRAPHICS2D_EFFECT_INVERT);
    void *pass = rt_renderpass2d_new(src, dst);
    rt_renderpass2d_set_shader(pass, shader);
    void *graph = rt_rendergraph2d_new(1);
    rt_rendergraph2d_add_pass(graph, pass);
    assert(rt_rendergraph2d_get_count(graph) == 1);
    rt_rendergraph2d_execute(graph);
    assert(rt_pixels_get(rt_rendertarget2d_get_pixels(dst), 0, 0) == 0xEEDDCCFF);

    void *wrong_source_pass = rt_renderpass2d_new(rt_rendertarget2d_get_pixels(src), dst);
    rt_rendertarget2d_clear(dst, 0x010203FF);
    rt_renderpass2d_execute(wrong_source_pass);
    assert(rt_pixels_get(rt_rendertarget2d_get_pixels(dst), 0, 0) == 0x010203FF);
    assert(rt_rendertarget2d_get_pixels(rt_rendertarget2d_get_pixels(src)) == nullptr);

    void *cache = rt_tilechunkcache2d_new(8, 8);
    rt_tilechunkcache2d_mark_dirty(cache);
    assert(rt_tilechunkcache2d_get_dirty_count(cache) == 1);
    for (int i = 0; i < 3; i++)
        rt_tilechunkcache2d_mark_dirty(cache);
    assert(rt_tilechunkcache2d_get_dirty_count(cache) == 4);
    rt_tilechunkcache2d_clear_dirty(cache);
    assert(rt_tilechunkcache2d_get_dirty_count(cache) == 0);
    void *tile_renderer = rt_tilemaprenderer2d_new();
    rt_tilemaprenderer2d_set_chunk_cache(tile_renderer, cache);
    assert(rt_tilemaprenderer2d_get_draw_count(tile_renderer) == 0);

    void *atlas_pixels = rt_pixels_new(4, 4);
    void *packer = rt_texturepackeratlas_new(atlas_pixels);
    if (rt_texturepackeratlas_get_atlas(packer)) {
        rt_string hero = rt_str_from_lit("hero", 4);
        rt_texturepackeratlas_add(packer, hero, 0, 0, 2, 2);
        assert(rt_texturepackeratlas_has(packer, hero) == 1);
        assert(rt_texturepackeratlas_region_count(packer) == 1);
    }

    void *ase = rt_asepriteimporter_new();
    rt_asepriteimporter_set_grid(ase, 2, 2);
    assert(rt_asepriteimporter_get_frame_width(ase) == 2);
    assert(rt_asepriteimporter_get_frame_height(ase) == 2);

    void *tiled = rt_tiledmaploader_new();
    rt_tiledmaploader_set_tile_size(tiled, 4, 5);
    assert(rt_tiledmaploader_get_tile_width(tiled) == 4);
    assert(rt_tiledmaploader_get_tile_height(tiled) == 5);
    void *tilemap = rt_tiledmaploader_new_tilemap(tiled, 3, 2);
    assert(tilemap != nullptr);
    assert(rt_tilemap_get_tile_width(tilemap) == 4);
    assert(rt_tilemap_get_tile_height(tilemap) == 5);
    printf("test_layout_rendergraph_tile_helpers_and_importers: PASSED\n");
}

int main() {
    test_graphics2d_handles_have_unique_classes_and_reject_wrong_types();
    test_render_target_alpha_blend();
    test_texture_renderer_material_and_effects();
    test_viewport_tiles_and_objects();
    test_paths_shapes_text_nineslice_and_debugdraw();
    test_transform_sampler_blend_and_sprite_renderer();
    test_animation_collision_palette_gradient_and_rig();
    test_camera_extreme_arithmetic_saturates();
    test_layout_rendergraph_tile_helpers_and_importers();
    printf("RTGraphics2DTests: ALL PASSED\n");
    return 0;
}
