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
#include <cstdlib>
#include <cstring>

namespace {

struct FakeWindow {
    int32_t physical_width;
    int32_t physical_height;
    float scale_factor;
    float coord_scale;
    int coord_scale_calls;
    float last_coord_scale;
    int clip_set_calls;
    int clear_clip_calls;
    int32_t clip_x;
    int32_t clip_y;
    int32_t clip_w;
    int32_t clip_h;
    int update_calls;
    int close_requested;
    int fps;
    int32_t mouse_x;
    int32_t mouse_y;
};

static float g_initial_scale = 1.0f;
static int64_t g_clock_us = 0;

static FakeWindow *window_from(vgfx_window_t window) {
    return reinterpret_cast<FakeWindow *>(window);
}

static rt_canvas *new_canvas() {
    return static_cast<rt_canvas *>(rt_canvas_new(nullptr, 100, 50));
}

static void test_width_resyncs_coord_scale_after_display_move() {
    g_initial_scale = 1.0f;
    rt_canvas *canvas = new_canvas();
    assert(canvas != nullptr);

    auto *window = window_from(canvas->gfx_win);
    assert(window != nullptr);
    assert(window->last_coord_scale == 1.0f);

    window->scale_factor = 2.0f;
    window->physical_width = 200;
    window->physical_height = 100;

    assert(rt_canvas_width(canvas) == 100);
    assert(window->last_coord_scale == 2.0f);
    assert(window->coord_scale_calls >= 2);
}

static void test_poll_reapplies_clip_after_scale_change() {
    g_initial_scale = 1.0f;
    rt_canvas *canvas = new_canvas();
    assert(canvas != nullptr);

    auto *window = window_from(canvas->gfx_win);
    rt_canvas_set_clip_rect(canvas, 10, 20, 30, 40);
    int initial_clip_calls = window->clip_set_calls;

    window->scale_factor = 2.0f;
    window->physical_width = 200;
    window->physical_height = 100;

    int64_t event_type = rt_canvas_poll(canvas);
    assert(event_type == 0);
    assert(window->last_coord_scale == 2.0f);
    assert(window->clip_set_calls == initial_clip_calls + 1);
    assert(window->clip_x == 10);
    assert(window->clip_y == 20);
    assert(window->clip_w == 30);
    assert(window->clip_h == 40);
}

static void test_flip_rounds_positive_submillisecond_delta_up_to_one_ms() {
    g_initial_scale = 1.0f;
    rt_canvas *canvas = new_canvas();
    assert(canvas != nullptr);

    g_clock_us = 1000;
    rt_canvas_flip(canvas);
    assert(rt_canvas_get_delta_time(canvas) == 0);

    g_clock_us = 1500;
    rt_canvas_flip(canvas);
    assert(rt_canvas_get_delta_time(canvas) == 1);
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

extern "C" int64_t rt_str_len(rt_string) {
    return 0;
}

extern "C" const char *rt_string_cstr(rt_string) {
    return "";
}

extern "C" rt_string rt_string_from_bytes(const char *, size_t) {
    return nullptr;
}

extern "C" void rt_keyboard_clear_canvas_if_matches(void *) {}
extern "C" void rt_mouse_clear_canvas_if_matches(void *) {}
extern "C" void rt_keyboard_set_canvas(void *) {}
extern "C" void rt_mouse_set_canvas(void *) {}
extern "C" void rt_pad_init(void) {}
extern "C" void rt_keyboard_begin_frame(void) {}
extern "C" void rt_mouse_begin_frame(void) {}
extern "C" void rt_pad_begin_frame(void) {}
extern "C" void rt_pad_poll(void) {}
extern "C" void rt_keyboard_on_key_down(int64_t) {}
extern "C" void rt_keyboard_on_key_up(int64_t) {}
extern "C" void rt_keyboard_text_input(int32_t) {}
extern "C" void rt_mouse_update_pos(int64_t, int64_t) {}
extern "C" void rt_mouse_button_down(int64_t) {}
extern "C" void rt_mouse_button_up(int64_t) {}
extern "C" void rt_mouse_update_wheel(double, double) {}
extern "C" void rt_action_update(void) {}

extern "C" int64_t rt_clock_ticks_us(void) {
    return g_clock_us;
}

extern "C" void *rt_canvas_copy_rect(void *, int64_t, int64_t, int64_t, int64_t) {
    return nullptr;
}

extern "C" int64_t rt_pixels_save_bmp(void *, void *) {
    return 0;
}

extern "C" int64_t rt_pixels_save_png(void *, void *) {
    return 0;
}

extern "C" vgfx_window_params_t vgfx_window_params_default(void) {
    vgfx_window_params_t params;
    std::memset(&params, 0, sizeof(params));
    params.width = 640;
    params.height = 480;
    return params;
}

extern "C" vgfx_window_t vgfx_create_window(const vgfx_window_params_t *params) {
    auto *window = static_cast<FakeWindow *>(std::calloc(1, sizeof(FakeWindow)));
    assert(window != nullptr);
    int32_t logical_w = params ? params->width : 640;
    int32_t logical_h = params ? params->height : 480;
    window->scale_factor = g_initial_scale;
    window->coord_scale = 1.0f;
    window->physical_width = (int32_t)rtg_scale_up_i64(logical_w, window->scale_factor);
    window->physical_height = (int32_t)rtg_scale_up_i64(logical_h, window->scale_factor);
    window->fps = -1;
    return reinterpret_cast<vgfx_window_t>(window);
}

extern "C" void vgfx_destroy_window(vgfx_window_t window) {
    std::free(window_from(window));
}

extern "C" void vgfx_set_coord_scale(vgfx_window_t window, float scale) {
    auto *fake = window_from(window);
    assert(fake != nullptr);
    fake->coord_scale = scale;
    fake->last_coord_scale = scale;
    fake->coord_scale_calls++;
}

extern "C" float vgfx_window_get_scale(vgfx_window_t window) {
    auto *fake = window_from(window);
    return fake ? fake->scale_factor : 1.0f;
}

extern "C" int32_t vgfx_get_size(vgfx_window_t window, int32_t *width, int32_t *height) {
    auto *fake = window_from(window);
    if (!fake)
        return 0;
    if (width)
        *width = (int32_t)rtg_scale_down_i64(fake->physical_width, fake->coord_scale);
    if (height)
        *height = (int32_t)rtg_scale_down_i64(fake->physical_height, fake->coord_scale);
    return 1;
}

extern "C" void vgfx_set_clip(vgfx_window_t window, int32_t x, int32_t y, int32_t w, int32_t h) {
    auto *fake = window_from(window);
    assert(fake != nullptr);
    fake->clip_x = x;
    fake->clip_y = y;
    fake->clip_w = w;
    fake->clip_h = h;
    fake->clip_set_calls++;
}

extern "C" void vgfx_clear_clip(vgfx_window_t window) {
    auto *fake = window_from(window);
    assert(fake != nullptr);
    fake->clear_clip_calls++;
}

extern "C" int32_t vgfx_pump_events(vgfx_window_t) {
    return 1;
}

extern "C" int32_t vgfx_poll_event(vgfx_window_t, vgfx_event_t *) {
    return 0;
}

extern "C" int32_t vgfx_mouse_pos(vgfx_window_t window, int32_t *x, int32_t *y) {
    auto *fake = window_from(window);
    if (!fake)
        return 0;
    if (x)
        *x = (int32_t)rtg_scale_down_i64(fake->mouse_x, fake->coord_scale);
    if (y)
        *y = (int32_t)rtg_scale_down_i64(fake->mouse_y, fake->coord_scale);
    return 1;
}

extern "C" int32_t vgfx_update(vgfx_window_t window) {
    auto *fake = window_from(window);
    assert(fake != nullptr);
    fake->update_calls++;
    return 1;
}

extern "C" int32_t vgfx_close_requested(vgfx_window_t window) {
    auto *fake = window_from(window);
    return fake ? fake->close_requested : 0;
}

extern "C" void vgfx_cls(vgfx_window_t, vgfx_color_t) {}
extern "C" void vgfx_focus(vgfx_window_t) {}
extern "C" void vgfx_set_window_size(vgfx_window_t window, int32_t w, int32_t h) {
    auto *fake = window_from(window);
    assert(fake != nullptr);
    fake->physical_width = (int32_t)rtg_scale_up_i64(w, fake->scale_factor);
    fake->physical_height = (int32_t)rtg_scale_up_i64(h, fake->scale_factor);
}
extern "C" void vgfx_set_fullscreen(vgfx_window_t, int32_t) {}
extern "C" void vgfx_set_title(vgfx_window_t, const char *) {}
extern "C" void vgfx_set_fps(vgfx_window_t window, int32_t fps) {
    auto *fake = window_from(window);
    assert(fake != nullptr);
    fake->fps = fps;
}
extern "C" int32_t vgfx_get_fps(vgfx_window_t window) {
    auto *fake = window_from(window);
    return fake ? fake->fps : -1;
}
extern "C" int32_t vgfx_key_down(vgfx_window_t, vgfx_key_t) {
    return 0;
}
extern "C" void vgfx_get_position(vgfx_window_t, int32_t *x, int32_t *y) {
    if (x)
        *x = 0;
    if (y)
        *y = 0;
}
extern "C" void vgfx_set_position(vgfx_window_t, int32_t, int32_t) {}
extern "C" void vgfx_get_monitor_size(vgfx_window_t, int32_t *w, int32_t *h) {
    if (w)
        *w = 1920;
    if (h)
        *h = 1080;
}
extern "C" int32_t vgfx_is_focused(vgfx_window_t) {
    return 1;
}
extern "C" int32_t vgfx_is_minimized(vgfx_window_t) {
    return 0;
}
extern "C" int32_t vgfx_is_maximized(vgfx_window_t) {
    return 0;
}
extern "C" void vgfx_minimize(vgfx_window_t) {}
extern "C" void vgfx_maximize(vgfx_window_t) {}
extern "C" void vgfx_restore(vgfx_window_t) {}
extern "C" void vgfx_set_prevent_close(vgfx_window_t, int32_t) {}

int main() {
    test_width_resyncs_coord_scale_after_display_move();
    test_poll_reapplies_clip_after_scale_change();
    test_flip_rounds_positive_submillisecond_delta_up_to_one_ms();
    return 0;
}
