//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_graphics_internal.h"
}

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct FakeWindow {
    int32_t logical_width;
    int32_t logical_height;
    float scale;
    uint8_t *pixels;
    vgfx_framebuffer_t framebuffer;
};

static rt_canvas *make_canvas(int32_t logical_width, int32_t logical_height, float scale) {
    auto *canvas = static_cast<rt_canvas *>(std::calloc(1, sizeof(rt_canvas)));
    assert(canvas != nullptr);

    auto *window = static_cast<FakeWindow *>(std::calloc(1, sizeof(FakeWindow)));
    assert(window != nullptr);
    window->logical_width = logical_width;
    window->logical_height = logical_height;
    window->scale = scale;
    window->framebuffer.width = (int32_t)rtg_scale_up_i64(logical_width, scale);
    window->framebuffer.height = (int32_t)rtg_scale_up_i64(logical_height, scale);
    window->framebuffer.stride = window->framebuffer.width * 4;
    window->pixels = static_cast<uint8_t *>(std::calloc(
        (size_t)window->framebuffer.stride * (size_t)window->framebuffer.height, 1));
    assert(window->pixels != nullptr);
    window->framebuffer.pixels = window->pixels;

    canvas->magic = RT_CANVAS_MAGIC;
    canvas->gfx_win = reinterpret_cast<vgfx_window_t>(window);
    canvas->clip_enabled = 0;
    return canvas;
}

static rt_pixels_impl *make_pixels(int64_t width, int64_t height) {
    auto *pixels = static_cast<rt_pixels_impl *>(std::calloc(1, sizeof(rt_pixels_impl)));
    assert(pixels != nullptr);
    pixels->width = width;
    pixels->height = height;
    pixels->data = static_cast<uint32_t *>(std::calloc((size_t)(width * height), sizeof(uint32_t)));
    assert(pixels->data != nullptr);
    return pixels;
}

static uint32_t framebuffer_pixel(rt_canvas *canvas, int32_t x, int32_t y) {
    auto *window = reinterpret_cast<FakeWindow *>(canvas->gfx_win);
    assert(window != nullptr);
    uint8_t *p = &window->pixels[(size_t)y * (size_t)window->framebuffer.stride + (size_t)x * 4u];
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint32_t opaque_rgb(int64_t rgb) {
    return ((uint32_t)(rgb & 0xFFFFFF) << 8) | 0xFFu;
}

static void test_blit_region_respects_clip_and_fractional_scale() {
    rt_canvas *canvas = make_canvas(4, 2, 1.5f);
    canvas->clip_enabled = 1;
    canvas->clip_x = 1;
    canvas->clip_y = 0;
    canvas->clip_w = 2;
    canvas->clip_h = 1;

    rt_pixels_impl *pixels = make_pixels(2, 1);
    pixels->data[0] = 0xFF0000FFu;
    pixels->data[1] = 0x00FF00FFu;

    rt_canvas_blit(canvas, 1, 0, pixels);

    assert(framebuffer_pixel(canvas, 0, 0) == 0);
    assert(framebuffer_pixel(canvas, 1, 0) == 0);
    assert(framebuffer_pixel(canvas, 2, 0) == 0xFF0000FFu);
    assert(framebuffer_pixel(canvas, 3, 0) == 0x00FF00FFu);
    assert(framebuffer_pixel(canvas, 4, 0) == 0x00FF00FFu);
    assert(framebuffer_pixel(canvas, 5, 0) == 0);
}

static void test_flood_fill_stays_inside_clip_rect() {
    rt_canvas *canvas = make_canvas(4, 2, 1.5f);
    canvas->clip_enabled = 1;
    canvas->clip_x = 0;
    canvas->clip_y = 0;
    canvas->clip_w = 2;
    canvas->clip_h = 2;

    rt_canvas_flood_fill(canvas, 0, 0, 0x0000FF00);

    for (int y = 0; y < 3; y++) {
        assert(framebuffer_pixel(canvas, 0, y) == 0x00FF00FFu);
        assert(framebuffer_pixel(canvas, 1, y) == 0x00FF00FFu);
        assert(framebuffer_pixel(canvas, 2, y) == 0x00FF00FFu);
        assert(framebuffer_pixel(canvas, 3, y) == 0);
        assert(framebuffer_pixel(canvas, 4, y) == 0);
        assert(framebuffer_pixel(canvas, 5, y) == 0);
    }
}

static void test_gradient_h_stays_inside_clip_rect() {
    rt_canvas *canvas = make_canvas(4, 1, 1.5f);
    canvas->clip_enabled = 1;
    canvas->clip_x = 1;
    canvas->clip_y = 0;
    canvas->clip_w = 1;
    canvas->clip_h = 1;

    rt_canvas_gradient_h(canvas, 0, 0, 4, 1, 0x00FF0000, 0x000000FF);

    assert(framebuffer_pixel(canvas, 0, 0) == 0);
    assert(framebuffer_pixel(canvas, 1, 0) == 0);
    assert(framebuffer_pixel(canvas, 2, 0) != 0);
    assert(framebuffer_pixel(canvas, 3, 0) == 0);
    assert(framebuffer_pixel(canvas, 4, 0) == 0);
    assert(framebuffer_pixel(canvas, 5, 0) == 0);
}

static void test_gradient_h_preserves_full_gradient_when_clipped() {
    rt_canvas *canvas = make_canvas(4, 1, 1.0f);
    canvas->clip_enabled = 1;
    canvas->clip_x = 1;
    canvas->clip_y = 0;
    canvas->clip_w = 2;
    canvas->clip_h = 1;

    rt_canvas_gradient_h(canvas, 0, 0, 4, 1, 0x00FF0000, 0x000000FF);

    assert(framebuffer_pixel(canvas, 0, 0) == 0);
    assert(framebuffer_pixel(canvas, 1, 0) == opaque_rgb(rt_color_lerp(0x00FF0000, 0x000000FF, 33)));
    assert(framebuffer_pixel(canvas, 2, 0) == opaque_rgb(rt_color_lerp(0x00FF0000, 0x000000FF, 66)));
    assert(framebuffer_pixel(canvas, 3, 0) == 0);
}

static void test_gradient_v_preserves_full_gradient_when_clipped() {
    rt_canvas *canvas = make_canvas(1, 4, 1.0f);
    canvas->clip_enabled = 1;
    canvas->clip_x = 0;
    canvas->clip_y = 1;
    canvas->clip_w = 1;
    canvas->clip_h = 2;

    rt_canvas_gradient_v(canvas, 0, 0, 1, 4, 0x00FF0000, 0x000000FF);

    assert(framebuffer_pixel(canvas, 0, 0) == 0);
    assert(framebuffer_pixel(canvas, 0, 1) == opaque_rgb(rt_color_lerp(0x00FF0000, 0x000000FF, 33)));
    assert(framebuffer_pixel(canvas, 0, 2) == opaque_rgb(rt_color_lerp(0x00FF0000, 0x000000FF, 66)));
    assert(framebuffer_pixel(canvas, 0, 3) == 0);
}

} // namespace

extern "C" int32_t vgfx_get_size(vgfx_window_t window, int32_t *width, int32_t *height) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    if (!fake)
        return 0;
    if (width)
        *width = fake->logical_width;
    if (height)
        *height = fake->logical_height;
    return 1;
}

extern "C" float vgfx_window_get_scale(vgfx_window_t window) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    return fake ? fake->scale : 1.0f;
}

extern "C" int32_t vgfx_get_framebuffer(vgfx_window_t window, vgfx_framebuffer_t *fb) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    if (!fake || !fb)
        return 0;
    *fb = fake->framebuffer;
    return 1;
}

extern "C" void vgfx_pset(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t color) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    if (!fake || x < 0 || y < 0 || x >= fake->framebuffer.width || y >= fake->framebuffer.height)
        return;
    uint8_t *p = &fake->pixels[(size_t)y * (size_t)fake->framebuffer.stride + (size_t)x * 4u];
    p[0] = (uint8_t)((color >> 16) & 0xFF);
    p[1] = (uint8_t)((color >> 8) & 0xFF);
    p[2] = (uint8_t)(color & 0xFF);
    p[3] = 0xFF;
}

extern "C" void vgfx_pset_alpha(vgfx_window_t window, int32_t x, int32_t y, uint32_t argb) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    if (!fake || x < 0 || y < 0 || x >= fake->framebuffer.width || y >= fake->framebuffer.height)
        return;
    uint8_t *p = &fake->pixels[(size_t)y * (size_t)fake->framebuffer.stride + (size_t)x * 4u];
    uint8_t a = (uint8_t)((argb >> 24) & 0xFF);
    uint8_t r = (uint8_t)((argb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((argb >> 8) & 0xFF);
    uint8_t b = (uint8_t)(argb & 0xFF);
    if (a == 255) {
        p[0] = r;
        p[1] = g;
        p[2] = b;
        p[3] = 0xFF;
        return;
    }
    uint16_t inv = (uint16_t)(255 - a);
    p[0] = (uint8_t)((r * a + p[0] * inv) / 255);
    p[1] = (uint8_t)((g * a + p[1] * inv) / 255);
    p[2] = (uint8_t)((b * a + p[2] * inv) / 255);
    p[3] = 0xFF;
}

extern "C" int32_t vgfx_point(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t *color) {
    auto *fake = reinterpret_cast<FakeWindow *>(window);
    if (!fake || !color || x < 0 || y < 0 || x >= fake->framebuffer.width ||
        y >= fake->framebuffer.height)
        return 0;
    uint8_t *p = &fake->pixels[(size_t)y * (size_t)fake->framebuffer.stride + (size_t)x * 4u];
    *color = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    return 1;
}

extern "C" void vgfx_line(vgfx_window_t, int32_t, int32_t, int32_t, int32_t, vgfx_color_t) {}
extern "C" void vgfx_fill_rect(vgfx_window_t, int32_t, int32_t, int32_t, int32_t, vgfx_color_t) {}
extern "C" void vgfx_rect(vgfx_window_t, int32_t, int32_t, int32_t, int32_t, vgfx_color_t) {}
extern "C" void vgfx_fill_circle(vgfx_window_t, int32_t, int32_t, int32_t, vgfx_color_t) {}
extern "C" void vgfx_circle(vgfx_window_t, int32_t, int32_t, int32_t, vgfx_color_t) {}
extern "C" void vgfx_set_coord_scale(vgfx_window_t, float) {}
extern "C" void vgfx_set_clip(vgfx_window_t, int32_t, int32_t, int32_t, int32_t) {}
extern "C" void vgfx_clear_clip(vgfx_window_t) {}

extern "C" const uint8_t *rt_font_get_glyph(int) {
    static const uint8_t kEmptyGlyph[8] = {0};
    return kEmptyGlyph;
}

extern "C" const char *rt_string_cstr(rt_string) {
    return "";
}

extern "C" int64_t rt_str_len(rt_string) {
    return 0;
}

extern "C" rt_string rt_string_from_bytes(const char *, size_t) {
    return nullptr;
}

extern "C" int64_t rt_canvas_width(void *canvas_ptr) {
    auto *canvas = static_cast<rt_canvas *>(canvas_ptr);
    auto *fake = canvas ? reinterpret_cast<FakeWindow *>(canvas->gfx_win) : nullptr;
    return fake ? fake->logical_width : 0;
}

extern "C" int64_t rt_canvas_height(void *canvas_ptr) {
    auto *canvas = static_cast<rt_canvas *>(canvas_ptr);
    auto *fake = canvas ? reinterpret_cast<FakeWindow *>(canvas->gfx_win) : nullptr;
    return fake ? fake->logical_height : 0;
}

extern "C" void *rt_pixels_new(int64_t width, int64_t height) {
    return make_pixels(width, height);
}

extern "C" int64_t rt_pixels_save_bmp(void *, void *) {
    return 0;
}

extern "C" int64_t rt_pixels_save_png(void *, void *) {
    return 0;
}

int main() {
    test_blit_region_respects_clip_and_fractional_scale();
    test_flood_fill_stays_inside_clip_rect();
    test_gradient_h_stays_inside_clip_rect();
    test_gradient_h_preserves_full_gradient_when_clipped();
    test_gradient_v_preserves_full_gradient_when_clipped();
    std::printf("RTCanvasContractTests passed.\n");
    return 0;
}
