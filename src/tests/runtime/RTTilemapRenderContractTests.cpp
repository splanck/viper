//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

extern "C" {
#include "rt_tilemap.h"
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

struct StubCanvas {
    int64_t width;
    int64_t height;
};

struct StubBody {
    double x;
    double y;
    double w;
    double h;
    double vx;
    double vy;
};

struct BlitCall {
    int64_t dx;
    int64_t dy;
    int64_t sx;
    int64_t sy;
    int64_t w;
    int64_t h;
    int64_t pixels_id;
};

BlitCall g_blits[16];
int g_blit_count = 0;

void reset_blits() {
    std::memset(g_blits, 0, sizeof(g_blits));
    g_blit_count = 0;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" void rt_trap(const char *msg) {
    std::fprintf(stderr, "unexpected rt_trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

extern "C" void rt_heap_retain(void *) {}

extern "C" void rt_heap_release(void *obj) {
    std::free(obj);
}

extern "C" void *rt_pixels_clone(void *pixels) {
    if (!pixels)
        return nullptr;
    auto *src = static_cast<StubPixels *>(pixels);
    auto *copy = static_cast<StubPixels *>(std::malloc(sizeof(StubPixels)));
    assert(copy != nullptr);
    *copy = *src;
    return copy;
}

extern "C" int64_t rt_pixels_width(void *pixels) {
    return pixels ? static_cast<StubPixels *>(pixels)->width : 0;
}

extern "C" int64_t rt_pixels_height(void *pixels) {
    return pixels ? static_cast<StubPixels *>(pixels)->height : 0;
}

extern "C" const char *rt_string_cstr(rt_string) {
    return "";
}

extern "C" int64_t rt_canvas_width(void *canvas) {
    return canvas ? static_cast<StubCanvas *>(canvas)->width : 0;
}

extern "C" int64_t rt_canvas_height(void *canvas) {
    return canvas ? static_cast<StubCanvas *>(canvas)->height : 0;
}

extern "C" void rt_canvas_blit_region(void *canvas,
                                      int64_t dx,
                                      int64_t dy,
                                      void *pixels,
                                      int64_t sx,
                                      int64_t sy,
                                      int64_t w,
                                      int64_t h) {
    (void)canvas;
    assert(g_blit_count < (int)(sizeof(g_blits) / sizeof(g_blits[0])));
    g_blits[g_blit_count++] = {dx, dy, sx, sy, w, h, static_cast<StubPixels *>(pixels)->id};
}

extern "C" double rt_physics2d_body_x(void *body) {
    return body ? static_cast<StubBody *>(body)->x : 0.0;
}

extern "C" double rt_physics2d_body_y(void *body) {
    return body ? static_cast<StubBody *>(body)->y : 0.0;
}

extern "C" double rt_physics2d_body_w(void *body) {
    return body ? static_cast<StubBody *>(body)->w : 0.0;
}

extern "C" double rt_physics2d_body_h(void *body) {
    return body ? static_cast<StubBody *>(body)->h : 0.0;
}

extern "C" double rt_physics2d_body_vx(void *body) {
    return body ? static_cast<StubBody *>(body)->vx : 0.0;
}

extern "C" double rt_physics2d_body_vy(void *body) {
    return body ? static_cast<StubBody *>(body)->vy : 0.0;
}

extern "C" void rt_physics2d_body_set_pos(void *body, double x, double y) {
    if (!body)
        return;
    static_cast<StubBody *>(body)->x = x;
    static_cast<StubBody *>(body)->y = y;
}

extern "C" void rt_physics2d_body_set_vel(void *body, double vx, double vy) {
    if (!body)
        return;
    static_cast<StubBody *>(body)->vx = vx;
    static_cast<StubBody *>(body)->vy = vy;
}

static void test_draw_uses_all_visible_layers_in_order() {
    StubPixels base_tileset{32, 16, 100};
    StubPixels overlay_tileset{16, 16, 200};
    StubCanvas canvas{16, 16};

    void *tm = rt_tilemap_new(2, 1, 16, 16);
    assert(tm != nullptr);
    rt_tilemap_set_tileset(tm, &base_tileset);
    assert(rt_tilemap_add_layer(tm, nullptr) == 1);
    rt_tilemap_set_tile_layer(tm, 0, 0, 0, 1);
    rt_tilemap_set_tile_layer(tm, 1, 0, 0, 1);
    rt_tilemap_set_layer_tileset(tm, 1, &overlay_tileset);

    reset_blits();
    rt_tilemap_draw(tm, &canvas, 0, 0);

    assert(g_blit_count == 2);
    assert(g_blits[0].pixels_id == 100);
    assert(g_blits[1].pixels_id == 200);
    assert(g_blits[0].dx == 0 && g_blits[0].dy == 0);
    assert(g_blits[1].dx == 0 && g_blits[1].dy == 0);
    assert(g_blits[0].sx == 0 && g_blits[1].sx == 0);
}

static void test_draw_region_can_render_layer_only_tilesets() {
    StubPixels overlay_tileset{16, 16, 300};
    StubCanvas canvas{16, 16};

    void *tm = rt_tilemap_new(1, 1, 16, 16);
    assert(tm != nullptr);
    assert(rt_tilemap_add_layer(tm, nullptr) == 1);
    rt_tilemap_set_tile_layer(tm, 1, 0, 0, 1);
    rt_tilemap_set_layer_tileset(tm, 1, &overlay_tileset);
    rt_tilemap_set_layer_visible(tm, 0, 0);

    reset_blits();
    rt_tilemap_draw_region(tm, &canvas, 0, 0, 0, 0, 1, 1);

    assert(g_blit_count == 1);
    assert(g_blits[0].pixels_id == 300);
    assert(g_blits[0].dx == 0 && g_blits[0].dy == 0);
}

int main() {
    test_draw_uses_all_visible_layers_in_order();
    test_draw_region_can_render_layer_only_tilesets();
    std::printf("RTTilemapRenderContractTests passed.\n");
    return 0;
}
