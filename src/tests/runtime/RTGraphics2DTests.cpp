//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGraphics2DTests.cpp
// Purpose: Tests for production-oriented Viper.Graphics 2D runtime classes.
//
//===----------------------------------------------------------------------===//

#include "rt_graphics2d.h"
#include "rt_internal.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include <cassert>
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

    void *shape = rt_shaperenderer2d_new();
    rt_shaperenderer2d_set_stroke(shape, 0x0000FF00);
    rt_shaperenderer2d_line(shape, pixels, 0, 1, 3, 1);
    assert(rt_pixels_get(pixels, 3, 1) == 0x00FF00FF);

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
    assert(rt_debugdraw2d_count(debug) == 2);
    rt_debugdraw2d_draw_to_pixels(debug, pixels);
    assert(rt_pixels_get(pixels, 0, 2) == 0x0000FFFF);
    rt_debugdraw2d_clear(debug);
    assert(rt_debugdraw2d_count(debug) == 0);
    printf("test_paths_shapes_text_nineslice_and_debugdraw: PASSED\n");
}

int main() {
    test_render_target_alpha_blend();
    test_texture_renderer_material_and_effects();
    test_viewport_tiles_and_objects();
    test_paths_shapes_text_nineslice_and_debugdraw();
    printf("RTGraphics2DTests: ALL PASSED\n");
    return 0;
}
