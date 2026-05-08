//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_graphics_internal.h"
#include "rt_heap.h"
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

static int g_line_calls = 0;
static void *g_object_payloads[32];
static int64_t g_object_class_ids[32];
static size_t g_object_count = 0;
static void *g_heap_payloads[32];
static rt_heap_hdr_t *g_heap_headers[32];
static size_t g_heap_count = 0;

static rt_canvas *make_canvas(int32_t logical_width, int32_t logical_height, float scale) {
    auto *canvas = static_cast<rt_canvas *>(rt_obj_new_i64(RT_CANVAS_CLASS_ID, sizeof(rt_canvas)));
    assert(canvas != nullptr);
    std::memset(canvas, 0, sizeof(rt_canvas));

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

static void test_canvas_handle_validation_rejects_non_heap_canvas() {
    rt_canvas stack_canvas{};
    stack_canvas.magic = RT_CANVAS_MAGIC;
    assert(rt_canvas_is_handle(&stack_canvas) == 0);
}

static void test_polyline_requires_sized_i64_heap_array() {
    rt_canvas *canvas = make_canvas(8, 8, 1.0f);
    auto *points = static_cast<int64_t *>(
        rt_heap_alloc(RT_HEAP_ARRAY, RT_ELEM_I64, sizeof(int64_t), 4, 4));
    assert(points != nullptr);
    points[0] = 0;
    points[1] = 0;
    points[2] = 7;
    points[3] = 7;

    g_line_calls = 0;
    rt_canvas_polyline(canvas, points, 3, 0x00FFFFFF);
    assert(g_line_calls == 0);

    rt_canvas_polyline(canvas, points, 2, 0x00FFFFFF);
    assert(g_line_calls == 1);

    rt_heap_release(points);
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

extern "C" void vgfx_line(vgfx_window_t, int32_t, int32_t, int32_t, int32_t, vgfx_color_t) {
    g_line_calls++;
}
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

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void *rt_obj_new_i64(int64_t class_id, int64_t byte_size) {
    assert(byte_size >= 0);
    assert(g_object_count < sizeof(g_object_payloads) / sizeof(g_object_payloads[0]));
    void *obj = std::calloc(1, static_cast<size_t>(byte_size));
    assert(obj != nullptr);
    g_object_payloads[g_object_count] = obj;
    g_object_class_ids[g_object_count] = class_id;
    g_object_count++;
    return obj;
}

extern "C" int64_t rt_obj_class_id(void *obj) {
    for (size_t i = 0; i < g_object_count; i++) {
        if (g_object_payloads[i] == obj)
            return g_object_class_ids[i];
    }
    return 0;
}

extern "C" void rt_obj_free(void *obj) {
    for (size_t i = 0; i < g_object_count; i++) {
        if (g_object_payloads[i] == obj) {
            g_object_payloads[i] = g_object_payloads[g_object_count - 1];
            g_object_class_ids[i] = g_object_class_ids[g_object_count - 1];
            g_object_count--;
            std::free(obj);
            return;
        }
    }

    auto *pixels = static_cast<rt_pixels_impl *>(obj);
    if (pixels)
        std::free(pixels->data);
    std::free(obj);
}

extern "C" void *rt_heap_alloc(rt_heap_kind_t kind,
                               rt_elem_kind_t elem_kind,
                               size_t elem_size,
                               size_t init_len,
                               size_t init_cap) {
    assert(init_len <= init_cap);
    assert(g_heap_count < sizeof(g_heap_payloads) / sizeof(g_heap_payloads[0]));
    size_t payload_size = elem_size * init_cap;
    auto *hdr = static_cast<rt_heap_hdr_t *>(std::calloc(1, sizeof(rt_heap_hdr_t) + payload_size));
    assert(hdr != nullptr);
    hdr->magic = RT_MAGIC;
    hdr->kind = kind;
    hdr->elem_kind = elem_kind;
    hdr->refcnt = 1;
    hdr->len = init_len;
    hdr->cap = init_cap;
    hdr->alloc_size = sizeof(rt_heap_hdr_t) + payload_size;
    void *payload = hdr + 1;
    g_heap_headers[g_heap_count] = hdr;
    g_heap_payloads[g_heap_count] = payload;
    g_heap_count++;
    return payload;
}

extern "C" size_t rt_heap_release(void *payload) {
    for (size_t i = 0; i < g_heap_count; i++) {
        if (g_heap_payloads[i] == payload) {
            rt_heap_hdr_t *hdr = g_heap_headers[i];
            assert(hdr->refcnt > 0);
            hdr->refcnt--;
            size_t refcnt = hdr->refcnt;
            if (refcnt == 0) {
                g_heap_payloads[i] = g_heap_payloads[g_heap_count - 1];
                g_heap_headers[i] = g_heap_headers[g_heap_count - 1];
                g_heap_count--;
                std::free(hdr);
            }
            return refcnt;
        }
    }
    return 0;
}

extern "C" int8_t rt_heap_try_get_header(void *payload, rt_heap_hdr_t **out_hdr) {
    if (out_hdr)
        *out_hdr = nullptr;
    for (size_t i = 0; i < g_heap_count; i++) {
        if (g_heap_payloads[i] == payload) {
            rt_heap_hdr_t *hdr = g_heap_headers[i];
            if (hdr->magic != RT_MAGIC || hdr->refcnt == 0)
                return 0;
            if (out_hdr)
                *out_hdr = hdr;
            return 1;
        }
    }
    return 0;
}

extern "C" int8_t rt_canvas_is_handle(void *canvas_ptr) {
    return rt_canvas_checked(canvas_ptr) ? 1 : 0;
}

int main() {
    test_blit_region_respects_clip_and_fractional_scale();
    test_flood_fill_stays_inside_clip_rect();
    test_gradient_h_stays_inside_clip_rect();
    test_gradient_h_preserves_full_gradient_when_clipped();
    test_gradient_v_preserves_full_gradient_when_clipped();
    test_canvas_handle_validation_rejects_non_heap_canvas();
    test_polyline_requires_sized_i64_heap_array();
    std::printf("RTCanvasContractTests passed.\n");
    return 0;
}
