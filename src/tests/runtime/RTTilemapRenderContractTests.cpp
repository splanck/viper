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

#ifndef RT_PIXELS_CLASS_ID
#define RT_PIXELS_CLASS_ID INT64_C(-0x600201)
#endif

namespace {

struct ObjHeader {
    int64_t class_id;
    void (*finalizer)(void *);
};

struct StubPixels {
    int64_t width;
    int64_t height;
    int64_t id;
    int refcount;
    bool freed;
};

struct StubCanvas {
    int64_t width;
    int64_t height;
};

struct StubBody {
    double x;
    double y;
    double prev_y;
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
StubPixels *g_clones[16];
int g_clone_count = 0;
int g_pixels_freed = 0;

void reset_blits() {
    std::memset(g_blits, 0, sizeof(g_blits));
    g_blit_count = 0;
}

void reset_pixels_tracking() {
    std::memset(g_clones, 0, sizeof(g_clones));
    g_clone_count = 0;
    g_pixels_freed = 0;
}

ObjHeader *header_from_payload(void *obj) {
    return reinterpret_cast<ObjHeader *>(obj) - 1;
}

void destroy_rt_object(void *obj) {
    if (!obj)
        return;
    ObjHeader *header = header_from_payload(obj);
    if (header->finalizer)
        header->finalizer(obj);
    std::free(header);
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t class_id, int64_t byte_size) {
    auto *header =
        static_cast<ObjHeader *>(std::calloc(1, sizeof(ObjHeader) + static_cast<size_t>(byte_size)));
    assert(header != nullptr);
    header->class_id = class_id;
    return header + 1;
}

extern "C" int64_t rt_obj_class_id(void *obj) {
    return obj ? header_from_payload(obj)->class_id : 0;
}

extern "C" int8_t rt_obj_is_instance(void *obj, int64_t class_id, size_t) {
    if (!obj)
        return 0;
    if (class_id == RT_PIXELS_CLASS_ID)
        return 1;
    return rt_obj_class_id(obj) == class_id;
}

extern "C" void rt_obj_set_finalizer(void *obj, void (*finalizer)(void *)) {
    if (!obj)
        return;
    header_from_payload(obj)->finalizer = finalizer;
}

extern "C" void rt_trap(const char *msg) {
    std::fprintf(stderr, "unexpected rt_trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

extern "C" void rt_heap_retain(void *obj) {
    if (!obj)
        return;
    auto *pixels = static_cast<StubPixels *>(obj);
    pixels->refcount++;
}

extern "C" void rt_heap_release(void *obj) {
    if (!obj)
        return;
    auto *pixels = static_cast<StubPixels *>(obj);
    assert(pixels->refcount > 0);
    pixels->refcount--;
    if (pixels->refcount == 0 && !pixels->freed) {
        pixels->freed = true;
        g_pixels_freed++;
    }
}

extern "C" void *rt_pixels_clone(void *pixels) {
    if (!pixels)
        return nullptr;
    auto *src = static_cast<StubPixels *>(pixels);
    auto *copy = static_cast<StubPixels *>(std::malloc(sizeof(StubPixels)));
    assert(copy != nullptr);
    *copy = *src;
    copy->refcount = 1;
    copy->freed = false;
    assert(g_clone_count < (int)(sizeof(g_clones) / sizeof(g_clones[0])));
    g_clones[g_clone_count++] = copy;
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

extern "C" double rt_physics2d_body_prev_y(void *body) {
    return body ? static_cast<StubBody *>(body)->prev_y : 0.0;
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
    reset_pixels_tracking();
    StubPixels base_tileset{32, 16, 100, 0, false};
    StubPixels overlay_tileset{16, 16, 200, 0, false};
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
    reset_pixels_tracking();
    StubPixels overlay_tileset{16, 16, 300, 0, false};
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

static void test_tileset_clone_keeps_single_owned_reference() {
    reset_pixels_tracking();
    StubPixels base_tileset{32, 16, 400, 0, false};

    void *tm = rt_tilemap_new(2, 1, 16, 16);
    assert(tm != nullptr);
    rt_tilemap_set_tileset(tm, &base_tileset);

    assert(g_clone_count == 1);
    assert(g_clones[0]->refcount == 1);
}

static void test_layer_tileset_clone_keeps_single_owned_reference() {
    reset_pixels_tracking();
    StubPixels overlay_tileset{16, 16, 500, 0, false};

    void *tm = rt_tilemap_new(1, 1, 16, 16);
    assert(tm != nullptr);
    assert(rt_tilemap_add_layer(tm, nullptr) == 1);
    rt_tilemap_set_layer_tileset(tm, 1, &overlay_tileset);

    assert(g_clone_count == 1);
    assert(g_clones[0]->refcount == 1);
}

static void test_positive_offset_culls_to_visible_tiles() {
    reset_pixels_tracking();
    StubPixels base_tileset{64, 16, 550, 0, false};
    StubCanvas canvas{32, 16};

    void *tm = rt_tilemap_new(4, 1, 16, 16);
    assert(tm != nullptr);
    rt_tilemap_set_tileset(tm, &base_tileset);
    for (int64_t x = 0; x < 4; x++)
        rt_tilemap_set_tile(tm, x, 0, 1);

    reset_blits();
    rt_tilemap_draw(tm, &canvas, 8, 0);

    assert(g_blit_count == 2);
    assert(g_blits[0].dx == 8);
    assert(g_blits[1].dx == 24);
}

static void test_one_way_platform_tolerance_rejects_starting_inside_surface() {
    void *tm = rt_tilemap_new(1, 2, 16, 16);
    assert(tm != nullptr);
    rt_tilemap_set_tile(tm, 0, 1, 1);
    rt_tilemap_set_collision(tm, 1, RT_TILE_COLLISION_ONE_WAY_UP);

    StubBody body{0.0, 13.0, 12.5, 8.0, 4.0, 0.0, 1.0};
    assert(rt_tilemap_collide_body(tm, &body) == 0);
    assert(body.y == 13.0);
    assert(body.vy == 1.0);
}

static void test_finalizer_releases_owned_tilesets() {
    reset_pixels_tracking();
    StubPixels base_tileset{32, 16, 600, 0, false};
    StubPixels overlay_tileset{16, 16, 700, 0, false};

    void *tm = rt_tilemap_new(2, 1, 16, 16);
    assert(tm != nullptr);
    rt_tilemap_set_tileset(tm, &base_tileset);
    assert(rt_tilemap_add_layer(tm, nullptr) == 1);
    rt_tilemap_set_layer_tileset(tm, 1, &overlay_tileset);

    destroy_rt_object(tm);

    assert(g_clone_count == 2);
    assert(g_pixels_freed == 2);
    assert(g_clones[0]->freed);
    assert(g_clones[1]->freed);
}

int main() {
    test_draw_uses_all_visible_layers_in_order();
    test_draw_region_can_render_layer_only_tilesets();
    test_tileset_clone_keeps_single_owned_reference();
    test_layer_tileset_clone_keeps_single_owned_reference();
    test_positive_offset_culls_to_visible_tiles();
    test_one_way_platform_tolerance_rejects_starting_inside_surface();
    test_finalizer_releases_owned_tilesets();
    std::printf("RTTilemapRenderContractTests passed.\n");
    return 0;
}
