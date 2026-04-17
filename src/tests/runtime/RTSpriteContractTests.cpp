//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_gif.h"
#include "rt_sprite.h"
}

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

struct StubPixels {
    int64_t width;
    int64_t height;
    int64_t id;
};

static int64_t g_next_pixels_id = 1000;
static int64_t g_last_blit_pixels_id = 0;
static int64_t g_last_blit_x = 0;
static int64_t g_last_blit_y = 0;

static StubPixels *make_pixels(int64_t width, int64_t height, int64_t id) {
    auto *pixels = static_cast<StubPixels *>(std::calloc(1, sizeof(StubPixels)));
    assert(pixels != nullptr);
    pixels->width = width;
    pixels->height = height;
    pixels->id = id;
    return pixels;
}

static void test_flip_x_uses_returned_pixels_buffer() {
    StubPixels source{8, 6, 10};
    void *sprite = rt_sprite_new(&source);
    assert(sprite != nullptr);

    rt_sprite_set_flip_x(sprite, 1);
    rt_sprite_draw(sprite, reinterpret_cast<void *>(1));

    assert(g_last_blit_pixels_id == 110);
    assert(g_last_blit_x == 0);
    assert(g_last_blit_y == 0);
}

static void test_scale_setters_clamp_to_positive_values() {
    StubPixels source{10, 10, 20};
    void *sprite = rt_sprite_new(&source);
    assert(sprite != nullptr);

    rt_sprite_set_scale_x(sprite, -50);
    rt_sprite_set_scale_y(sprite, 0);

    assert(rt_sprite_get_scale_x(sprite) == 1);
    assert(rt_sprite_get_scale_y(sprite) == 1);
}

static void test_tiny_positive_scale_still_has_nonzero_collision_bounds() {
    StubPixels a_src{10, 10, 30};
    StubPixels b_src{10, 10, 40};
    void *a = rt_sprite_new(&a_src);
    void *b = rt_sprite_new(&b_src);
    assert(a != nullptr);
    assert(b != nullptr);

    rt_sprite_set_scale_x(a, 1);
    rt_sprite_set_scale_y(a, 1);
    rt_sprite_set_scale_x(b, 1);
    rt_sprite_set_scale_y(b, 1);

    rt_sprite_set_x(a, 5);
    rt_sprite_set_y(a, 7);
    rt_sprite_set_x(b, 5);
    rt_sprite_set_y(b, 7);

    assert(rt_sprite_contains(a, 5, 7) == 1);
    assert(rt_sprite_overlaps(a, b) == 1);
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    std::free(obj);
}

extern "C" void rt_trap(const char *) {
    std::abort();
}

extern "C" void rt_heap_release(void *obj) {
    std::free(obj);
}

extern "C" int64_t rt_timer_ms(void) {
    return 0;
}

extern "C" const char *rt_string_cstr(rt_string) {
    return "";
}

extern "C" rt_string rt_string_from_bytes(const char *, size_t) {
    return nullptr;
}

extern "C" rt_string rt_str_empty(void) {
    return nullptr;
}

extern "C" int gif_decode_file(const char *, gif_frame_t **out_frames, int *out_frame_count, int *out_width, int *out_height) {
    if (out_frames)
        *out_frames = nullptr;
    if (out_frame_count)
        *out_frame_count = 0;
    if (out_width)
        *out_width = 0;
    if (out_height)
        *out_height = 0;
    return 0;
}

extern "C" void *rt_pixels_clone(void *pixels) {
    auto *src = static_cast<StubPixels *>(pixels);
    return src ? make_pixels(src->width, src->height, src->id) : nullptr;
}

extern "C" void *rt_pixels_flip_h(void *pixels) {
    auto *src = static_cast<StubPixels *>(pixels);
    return src ? make_pixels(src->width, src->height, src->id + 100) : nullptr;
}

extern "C" void *rt_pixels_flip_v(void *pixels) {
    auto *src = static_cast<StubPixels *>(pixels);
    return src ? make_pixels(src->width, src->height, src->id + 200) : nullptr;
}

extern "C" void *rt_pixels_scale(void *pixels, int64_t width, int64_t height) {
    auto *src = static_cast<StubPixels *>(pixels);
    return src ? make_pixels(width, height, g_next_pixels_id++) : nullptr;
}

extern "C" void *rt_pixels_rotate(void *pixels, double) {
    auto *src = static_cast<StubPixels *>(pixels);
    return src ? make_pixels(src->width, src->height, g_next_pixels_id++) : nullptr;
}

extern "C" void *rt_pixels_tint(void *pixels, int64_t) {
    auto *src = static_cast<StubPixels *>(pixels);
    return src ? make_pixels(src->width, src->height, g_next_pixels_id++) : nullptr;
}

extern "C" void *rt_pixels_new(int64_t width, int64_t height) {
    return make_pixels(width, height, g_next_pixels_id++);
}

extern "C" void rt_pixels_copy(void *, int64_t, int64_t, void *, int64_t, int64_t, int64_t, int64_t) {}

extern "C" int64_t rt_pixels_width(void *pixels) {
    auto *p = static_cast<StubPixels *>(pixels);
    return p ? p->width : 0;
}

extern "C" int64_t rt_pixels_height(void *pixels) {
    auto *p = static_cast<StubPixels *>(pixels);
    return p ? p->height : 0;
}

extern "C" void *rt_pixels_subimage(void *, int64_t, int64_t, int64_t, int64_t) {
    return nullptr;
}

extern "C" void *rt_pixels_load(rt_string) {
    return nullptr;
}

extern "C" void *rt_pixels_load_bmp(rt_string) {
    return nullptr;
}

extern "C" void *rt_pixels_load_png(rt_string) {
    return nullptr;
}

extern "C" void *rt_pixels_load_jpeg(rt_string) {
    return nullptr;
}

extern "C" void rt_canvas_blit_alpha(void *, int64_t x, int64_t y, void *pixels) {
    auto *p = static_cast<StubPixels *>(pixels);
    g_last_blit_pixels_id = p ? p->id : 0;
    g_last_blit_x = x;
    g_last_blit_y = y;
}

int main() {
    test_flip_x_uses_returned_pixels_buffer();
    test_scale_setters_clamp_to_positive_values();
    test_tiny_positive_scale_still_has_nonzero_collision_bounds();
    return 0;
}
