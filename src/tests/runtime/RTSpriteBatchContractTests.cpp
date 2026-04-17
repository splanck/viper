//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_spritebatch.h"
}

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct StubPixels {
    int64_t width;
    int64_t height;
    int64_t id;
};

struct DrawCall {
    int64_t pixels_id;
    int64_t x;
    int64_t y;
};

DrawCall g_alpha_calls[16];
DrawCall g_region_calls[16];
int g_alpha_call_count = 0;
int g_region_call_count = 0;

void reset_draw_calls() {
    std::memset(g_alpha_calls, 0, sizeof(g_alpha_calls));
    std::memset(g_region_calls, 0, sizeof(g_region_calls));
    g_alpha_call_count = 0;
    g_region_call_count = 0;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" int8_t rt_heap_is_payload(void *) {
    return 0;
}

extern "C" int32_t rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    std::free(obj);
}

extern "C" void rt_obj_retain_maybe(void *) {}

extern "C" void rt_trap(const char *msg) {
    std::fprintf(stderr, "unexpected rt_trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

extern "C" void *rt_pixels_tint(void *pixels, int64_t) {
    return pixels;
}

extern "C" void *rt_pixels_clone(void *pixels) {
    return pixels;
}

extern "C" void *rt_pixels_new(int64_t width, int64_t height) {
    auto *pixels = static_cast<StubPixels *>(std::calloc(1, sizeof(StubPixels)));
    assert(pixels != nullptr);
    pixels->width = width;
    pixels->height = height;
    return pixels;
}

extern "C" void rt_pixels_copy(
    void *, int64_t, int64_t, void *, int64_t, int64_t, int64_t, int64_t) {}

extern "C" void *rt_pixels_scale(void *pixels, int64_t, int64_t) {
    return pixels;
}

extern "C" void *rt_pixels_rotate(void *pixels, double) {
    return pixels;
}

extern "C" int64_t rt_pixels_width(void *pixels) {
    return pixels ? static_cast<StubPixels *>(pixels)->width : 0;
}

extern "C" int64_t rt_pixels_height(void *pixels) {
    return pixels ? static_cast<StubPixels *>(pixels)->height : 0;
}

extern "C" void rt_canvas_blit_alpha(void *canvas, int64_t x, int64_t y, void *pixels) {
    (void)canvas;
    assert(g_alpha_call_count < (int)(sizeof(g_alpha_calls) / sizeof(g_alpha_calls[0])));
    g_alpha_calls[g_alpha_call_count++] = {static_cast<StubPixels *>(pixels)->id, x, y};
}

extern "C" void rt_canvas_blit_region(void *canvas,
                                      int64_t x,
                                      int64_t y,
                                      void *pixels,
                                      int64_t,
                                      int64_t,
                                      int64_t,
                                      int64_t) {
    (void)canvas;
    assert(g_region_call_count < (int)(sizeof(g_region_calls) / sizeof(g_region_calls[0])));
    g_region_calls[g_region_call_count++] = {static_cast<StubPixels *>(pixels)->id, x, y};
}

extern "C" void rt_sprite_draw_transformed(
    void *, void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {}

extern "C" int64_t rt_sprite_get_depth(void *) {
    return 0;
}

static void test_equal_depth_pixels_preserve_submission_order() {
    StubPixels first{4, 4, 1};
    StubPixels second{4, 4, 2};

    void *batch = rt_spritebatch_new(0);
    assert(batch != nullptr);
    rt_spritebatch_set_sort_by_depth(batch, 1);
    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_pixels(batch, &first, 10, 20);
    rt_spritebatch_draw_pixels(batch, &second, 30, 40);

    reset_draw_calls();
    rt_spritebatch_end(batch, reinterpret_cast<void *>(1));

    assert(g_alpha_call_count == 2);
    assert(g_alpha_calls[0].pixels_id == 1);
    assert(g_alpha_calls[1].pixels_id == 2);
}

static void test_depth_sort_preserves_submission_order_within_equal_depth() {
    StubPixels back{8, 8, 10};
    StubPixels front_a{8, 8, 20};
    StubPixels front_b{8, 8, 30};

    void *batch = rt_spritebatch_new(0);
    assert(batch != nullptr);
    rt_spritebatch_set_sort_by_depth(batch, 1);
    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_region_ex(batch, &front_a, 0, 0, 0, 0, 8, 8, 100, 100, 0, 5);
    rt_spritebatch_draw_region_ex(batch, &back, 0, 0, 0, 0, 8, 8, 100, 100, 0, 1);
    rt_spritebatch_draw_region_ex(batch, &front_b, 0, 0, 0, 0, 8, 8, 100, 100, 0, 5);

    reset_draw_calls();
    rt_spritebatch_end(batch, reinterpret_cast<void *>(1));

    assert(g_region_call_count == 3);
    assert(g_region_calls[0].pixels_id == 10);
    assert(g_region_calls[1].pixels_id == 20);
    assert(g_region_calls[2].pixels_id == 30);
}

int main() {
    test_equal_depth_pixels_preserve_submission_order();
    test_depth_sort_preserves_submission_order_within_equal_depth();
    std::printf("RTSpriteBatchContractTests passed.\n");
    return 0;
}
