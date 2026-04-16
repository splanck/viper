//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"
#include "rt_videowidget.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct StubString {
    char *data;
};

struct StubPixels {
    int64_t width;
    int64_t height;
    uint32_t *data;
};

struct StubWidget {
    int child_count;
    int visible;
    double flex;
    int64_t width;
    int64_t height;
    double slider_value;
    int clicked;
    const void *last_pixels;
    int64_t last_pixels_w;
    int64_t last_pixels_h;
};

struct StubPlayer {
    int64_t width;
    int64_t height;
    double duration;
    double position;
    int64_t playing;
    double volume;
    StubPixels *frame;
    int play_calls;
    int pause_calls;
    int stop_calls;
    int update_calls;
    int seek_calls;
    double last_seek;
};

struct rt_videowidget_view {
    void *vptr;
    void *player;
    void *root_widget;
    void *image_widget;
    void *controls_widget;
    void *play_button;
    void *pause_button;
    void *stop_button;
    void *position_slider;
    int8_t show_controls;
    int8_t looping;
    double volume;
    double slider_last_value;
    int32_t video_width;
    int32_t video_height;
    uint8_t *rgba_buf;
    int32_t rgba_buf_size;
};

bool g_open_should_fail = false;
int64_t g_open_width = 64;
int64_t g_open_height = 32;
int g_release_count = 0;
int g_widget_destroy_count = 0;

void reset_open_state() {
    g_open_should_fail = false;
    g_open_width = 64;
    g_open_height = 32;
    g_release_count = 0;
    g_widget_destroy_count = 0;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" void rt_obj_set_finalizer(void *, void (*)(void *)) {}

extern "C" int rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    g_release_count++;
    std::free(obj);
}

extern "C" const char *rt_string_cstr(rt_string str) {
    return str ? reinterpret_cast<StubString *>(str)->data : nullptr;
}

extern "C" int64_t rt_pixels_width(void *pixels) {
    return pixels ? static_cast<StubPixels *>(pixels)->width : 0;
}

extern "C" int64_t rt_pixels_height(void *pixels) {
    return pixels ? static_cast<StubPixels *>(pixels)->height : 0;
}

extern "C" void *rt_image_new(void *) {
    return std::calloc(1, sizeof(StubWidget));
}

extern "C" void rt_image_set_pixels(void *image, void *pixels, int64_t w, int64_t h) {
    auto *widget = static_cast<StubWidget *>(image);
    widget->last_pixels = pixels;
    widget->last_pixels_w = w;
    widget->last_pixels_h = h;
}

extern "C" void rt_image_set_scale_mode(void *, int64_t) {}

extern "C" void rt_widget_set_size(void *widget, int64_t w, int64_t h) {
    auto *stub = static_cast<StubWidget *>(widget);
    stub->width = w;
    stub->height = h;
}

extern "C" void rt_widget_set_flex(void *widget, double flex) {
    static_cast<StubWidget *>(widget)->flex = flex;
}

extern "C" void rt_widget_set_visible(void *widget, int64_t visible) {
    static_cast<StubWidget *>(widget)->visible = visible ? 1 : 0;
}

extern "C" void rt_widget_destroy(void *widget) {
    g_widget_destroy_count++;
    std::free(widget);
}

extern "C" int64_t rt_widget_was_clicked(void *widget) {
    auto *stub = static_cast<StubWidget *>(widget);
    int clicked = stub->clicked;
    stub->clicked = 0;
    return clicked;
}

extern "C" void *rt_vbox_new(void) {
    return std::calloc(1, sizeof(StubWidget));
}

extern "C" void *rt_hbox_new(void) {
    return std::calloc(1, sizeof(StubWidget));
}

extern "C" void rt_container_set_spacing(void *, double) {}

extern "C" void rt_widget_add_child(void *parent, void *) {
    static_cast<StubWidget *>(parent)->child_count++;
}

extern "C" void *rt_button_new(void *parent, void *) {
    auto *button = static_cast<StubWidget *>(std::calloc(1, sizeof(StubWidget)));
    if (parent)
        static_cast<StubWidget *>(parent)->child_count++;
    return button;
}

extern "C" void *rt_slider_new(void *parent, int64_t) {
    auto *slider = static_cast<StubWidget *>(std::calloc(1, sizeof(StubWidget)));
    if (parent)
        static_cast<StubWidget *>(parent)->child_count++;
    return slider;
}

extern "C" void rt_slider_set_value(void *slider, double value) {
    static_cast<StubWidget *>(slider)->slider_value = value;
}

extern "C" double rt_slider_get_value(void *slider) {
    return static_cast<StubWidget *>(slider)->slider_value;
}

extern "C" rt_string rt_string_from_bytes(const char *data, size_t len) {
    auto *str = static_cast<StubString *>(std::calloc(1, sizeof(StubString)));
    str->data = static_cast<char *>(std::calloc(len + 1, 1));
    if (data && len > 0)
        std::memcpy(str->data, data, len);
    str->data[len] = '\0';
    return reinterpret_cast<rt_string>(str);
}

extern "C" void *rt_videoplayer_open(void *) {
    if (g_open_should_fail)
        return nullptr;
    auto *player = static_cast<StubPlayer *>(std::calloc(1, sizeof(StubPlayer)));
    player->width = g_open_width;
    player->height = g_open_height;
    player->duration = 8.0;
    player->volume = 1.0;
    auto *frame = static_cast<StubPixels *>(std::calloc(1, sizeof(StubPixels)));
    frame->width = g_open_width;
    frame->height = g_open_height;
    frame->data = static_cast<uint32_t *>(
        std::calloc(static_cast<size_t>(g_open_width * g_open_height), sizeof(uint32_t)));
    frame->data[0] = 0x11223344u;
    player->frame = frame;
    return player;
}

extern "C" void rt_videoplayer_play(void *vp) {
    auto *player = static_cast<StubPlayer *>(vp);
    player->playing = 1;
    player->play_calls++;
}

extern "C" void rt_videoplayer_pause(void *vp) {
    auto *player = static_cast<StubPlayer *>(vp);
    player->playing = 0;
    player->pause_calls++;
}

extern "C" void rt_videoplayer_stop(void *vp) {
    auto *player = static_cast<StubPlayer *>(vp);
    player->playing = 0;
    player->position = 0.0;
    player->stop_calls++;
}

extern "C" void rt_videoplayer_update(void *vp, double dt) {
    auto *player = static_cast<StubPlayer *>(vp);
    player->position += dt;
    player->update_calls++;
}

extern "C" void rt_videoplayer_seek(void *vp, double seconds) {
    auto *player = static_cast<StubPlayer *>(vp);
    player->position = seconds;
    player->last_seek = seconds;
    player->seek_calls++;
}

extern "C" void rt_videoplayer_set_volume(void *vp, double vol) {
    static_cast<StubPlayer *>(vp)->volume = vol;
}

extern "C" int64_t rt_videoplayer_get_width(void *vp) {
    return static_cast<StubPlayer *>(vp)->width;
}

extern "C" int64_t rt_videoplayer_get_height(void *vp) {
    return static_cast<StubPlayer *>(vp)->height;
}

extern "C" double rt_videoplayer_get_duration(void *vp) {
    return static_cast<StubPlayer *>(vp)->duration;
}

extern "C" double rt_videoplayer_get_position(void *vp) {
    return static_cast<StubPlayer *>(vp)->position;
}

extern "C" int64_t rt_videoplayer_get_is_playing(void *vp) {
    return static_cast<StubPlayer *>(vp)->playing;
}

extern "C" void *rt_videoplayer_get_frame(void *vp) {
    return static_cast<StubPlayer *>(vp)->frame;
}

static void test_constructor_validates_inputs() {
    StubWidget parent{};
    reset_open_state();

    assert(rt_videowidget_new(nullptr, reinterpret_cast<void *>(1)) == nullptr);
    assert(rt_videowidget_new(&parent, nullptr) == nullptr);

    g_open_should_fail = true;
    assert(rt_videowidget_new(&parent, reinterpret_cast<void *>(1)) == nullptr);

    g_open_should_fail = false;
    g_open_width = 0;
    assert(rt_videowidget_new(&parent, reinterpret_cast<void *>(1)) == nullptr);
    assert(g_release_count > 0);
}

static void test_successful_construction_sets_up_widgets() {
    StubWidget parent{};
    reset_open_state();

    auto *widget = static_cast<rt_videowidget_view *>(
        rt_videowidget_new(&parent, reinterpret_cast<void *>(1)));
    assert(widget != nullptr);
    assert(parent.child_count == 1);
    assert(widget->root_widget != nullptr);
    assert(widget->image_widget != nullptr);
    assert(widget->controls_widget != nullptr);
    assert(widget->rgba_buf != nullptr);
    assert(widget->video_width == g_open_width);
    assert(widget->video_height == g_open_height);

    auto *image = static_cast<StubWidget *>(widget->image_widget);
    assert(image->last_pixels != nullptr);
    assert(image->last_pixels_w == g_open_width);
    assert(image->last_pixels_h == g_open_height);
}

static void test_controls_drive_player_state() {
    StubWidget parent{};
    reset_open_state();

    auto *widget = static_cast<rt_videowidget_view *>(
        rt_videowidget_new(&parent, reinterpret_cast<void *>(1)));
    auto *player = static_cast<StubPlayer *>(widget->player);

    static_cast<StubWidget *>(widget->play_button)->clicked = 1;
    rt_videowidget_update(widget, 0.0);
    assert(player->play_calls == 1);

    static_cast<StubWidget *>(widget->pause_button)->clicked = 1;
    rt_videowidget_update(widget, 0.0);
    assert(player->pause_calls == 1);

    static_cast<StubWidget *>(widget->stop_button)->clicked = 1;
    rt_videowidget_update(widget, 0.0);
    assert(player->stop_calls == 1);
}

static void test_slider_seek_and_looping() {
    StubWidget parent{};
    reset_open_state();

    auto *widget = static_cast<rt_videowidget_view *>(
        rt_videowidget_new(&parent, reinterpret_cast<void *>(1)));
    auto *player = static_cast<StubPlayer *>(widget->player);
    auto *slider = static_cast<StubWidget *>(widget->position_slider);

    slider->slider_value = 0.5;
    rt_videowidget_update(widget, 0.0);
    assert(player->seek_calls == 1);
    assert(player->last_seek == 4.0);

    widget->looping = 1;
    player->playing = 0;
    player->position = 2.0;
    rt_videowidget_update(widget, 0.0);
    assert(player->stop_calls >= 1);
    assert(player->play_calls >= 1);
}

static void test_visibility_and_volume_are_clamped() {
    StubWidget parent{};
    reset_open_state();

    auto *widget = static_cast<rt_videowidget_view *>(
        rt_videowidget_new(&parent, reinterpret_cast<void *>(1)));
    auto *controls = static_cast<StubWidget *>(widget->controls_widget);
    auto *player = static_cast<StubPlayer *>(widget->player);

    rt_videowidget_set_show_controls(widget, 0);
    assert(controls->visible == 0);
    rt_videowidget_set_show_controls(widget, 1);
    assert(controls->visible == 1);

    rt_videowidget_set_volume(widget, -1.0);
    assert(player->volume == 0.0);
    rt_videowidget_set_volume(widget, 2.0);
    assert(player->volume == 1.0);
}

static void test_destroy_releases_player_and_widget_tree() {
    StubWidget parent{};
    reset_open_state();

    auto *widget = static_cast<rt_videowidget_view *>(
        rt_videowidget_new(&parent, reinterpret_cast<void *>(1)));
    assert(widget != nullptr);
    assert(widget->player != nullptr);
    assert(widget->root_widget != nullptr);
    assert(widget->rgba_buf != nullptr);

    rt_videowidget_destroy(widget);

    assert(widget->player == nullptr);
    assert(widget->root_widget == nullptr);
    assert(widget->image_widget == nullptr);
    assert(widget->controls_widget == nullptr);
    assert(widget->rgba_buf == nullptr);
    assert(widget->rgba_buf_size == 0);
    assert(g_release_count > 0);
    assert(g_widget_destroy_count == 1);

    rt_videowidget_destroy(widget);
    assert(g_widget_destroy_count == 1);
}

int main() {
    test_constructor_validates_inputs();
    test_successful_construction_sets_up_widgets();
    test_controls_drive_player_state();
    test_slider_seek_and_looping();
    test_visibility_and_volume_are_clamped();
    test_destroy_releases_player_and_widget_tree();
    std::printf("RTVideoWidgetContractTests passed.\n");
    return 0;
}
