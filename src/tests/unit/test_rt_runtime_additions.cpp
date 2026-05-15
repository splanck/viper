//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_runtime_additions.cpp
// Purpose: Focused tests for runtime additions from baseball/plans/24.
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_animstate.h"
#include "rt_animtimeline.h"
#include "rt_audio.h"
#include "rt_camera.h"
#include "rt_gameui.h"
#include "rt_graphics2d.h"
#include "rt_gui.h"
#include "rt_internal.h"
#include "rt_mixgroup.h"
#include "rt_object.h"
#include "rt_physics2d.h"
#include "rt_pixels.h"
#include "rt_string.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

enum {
    KEY_ESCAPE = 256,
    KEY_ENTER = 257,
    KEY_BACKSPACE = 259,
    KEY_RIGHT = 262,
    KEY_DOWN = 264,
    KEY_HOME = 268,
    KEY_END = 269,
};

static bool str_eq(rt_string s, const char *expected) {
    return strcmp(rt_string_cstr(s), expected) == 0;
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_textinput_unicode_selection_and_limits() {
    TEST("TextInput handles UTF-8 cursoring, selection, and length limits");
    void *input = rt_uitextinput_new(0, 0, 160, 24);
    assert(input != nullptr);

    rt_uitextinput_set_text(input, rt_const_cstr("a\303\251b"));
    assert(rt_uitextinput_text_length(input) == 3);
    assert(rt_uitextinput_get_cursor(input) == 3);

    rt_uitextinput_set_cursor(input, 1);
    rt_uitextinput_set_focused(input, 1);
    rt_uitextinput_handle_key(input, KEY_RIGHT, 1);
    assert(rt_uitextinput_has_selection(input) == 1);
    assert(str_eq(rt_uitextinput_get_selected_text(input), "\303\251"));

    rt_uitextinput_handle_key(input, KEY_BACKSPACE, 0);
    assert(str_eq(rt_uitextinput_get_text(input), "ab"));
    assert(rt_uitextinput_get_cursor(input) == 1);

    rt_uitextinput_set_text(input, rt_const_cstr(""));
    rt_uitextinput_set_focused(input, 1);
    rt_uitextinput_set_max_codepoints(input, 3);
    assert(rt_uitextinput_handle_text(input, rt_const_cstr("abcd")) == 1);
    assert(str_eq(rt_uitextinput_get_text(input), "abc"));
    assert(rt_uitextinput_handle_text(input, rt_const_cstr("z")) == 0);
    rt_uitextinput_select_all(input);
    rt_uitextinput_delete_selection(input);
    assert(str_eq(rt_uitextinput_get_text(input), ""));

    release_obj(input);
    PASS();
}

static void test_table_sorting_selection_and_header_clicks() {
    TEST("Table sorts numeric columns and tracks selection/header clicks");
    void *table = rt_uitable_new(0, 0, 220, 96);
    assert(table != nullptr);
    assert(rt_uitable_add_column(table, rt_const_cstr("Name"), 100, 0) == 0);
    assert(rt_uitable_add_column(table, rt_const_cstr("Score"), 80, 0) == 1);
    rt_uitable_set_column_sortable(table, 1, 1, 1);

    int64_t row_a = rt_uitable_add_row(table);
    int64_t row_b = rt_uitable_add_row(table);
    int64_t row_c = rt_uitable_add_row(table);
    rt_uitable_set_cell(table, row_a, 0, rt_const_cstr("A"));
    rt_uitable_set_cell(table, row_a, 1, rt_const_cstr("10"));
    rt_uitable_set_cell(table, row_b, 0, rt_const_cstr("B"));
    rt_uitable_set_cell(table, row_b, 1, rt_const_cstr("2"));
    rt_uitable_set_cell(table, row_c, 0, rt_const_cstr("C"));
    rt_uitable_set_cell(table, row_c, 1, rt_const_cstr("30"));

    rt_uitable_sort_by(table, 1, 0);
    assert(str_eq(rt_uitable_get_cell(table, 0, 0), "B"));
    assert(str_eq(rt_uitable_get_cell(table, 1, 0), "A"));
    assert(str_eq(rt_uitable_get_cell(table, 2, 0), "C"));

    assert(rt_uitable_handle_click(table, 120, 4) == -2);
    assert(rt_uitable_last_header_click(table) == 1);
    assert(rt_uitable_get_sort_descending(table) == 1);
    assert(str_eq(rt_uitable_get_cell(table, 0, 0), "C"));

    rt_uitable_set_selected_row(table, 0);
    rt_uitable_handle_key(table, KEY_DOWN);
    assert(rt_uitable_get_selected_row(table) == 1);
    assert(rt_uitable_handle_click(table, 10, 40) >= 0);
    rt_uitable_handle_scroll(table, 10);
    assert(rt_uitable_get_scroll(table) >= 0);
    rt_uitable_remove_row(table, 1);
    assert(rt_uitable_row_count(table) == 2);
    rt_uitable_clear_rows(table);
    assert(rt_uitable_row_count(table) == 0);

    release_obj(table);
    PASS();
}

static void test_slider_dropdown_modal_and_tooltip() {
    TEST("Slider, Dropdown, Modal, and Tooltip state transitions");
    void *slider = rt_uislider_new(0, 0, 100, 24, 0, 10);
    assert(slider != nullptr);
    rt_uislider_set_step(slider, 2);
    assert(rt_uislider_handle_key(slider, KEY_RIGHT) == 1);
    assert(rt_uislider_get_value(slider) == 2);
    rt_uislider_set_value(slider, 99);
    assert(rt_uislider_get_value(slider) == 10);
    assert(rt_uislider_handle_key(slider, KEY_HOME) == 1);
    assert(rt_uislider_get_value(slider) == 0);
    assert(rt_uislider_handle_mouse_down(slider, 90, 10) == 1);
    assert(rt_uislider_get_value(slider) >= 8);
    assert(rt_uislider_handle_mouse_up(slider) == 1);

    void *dropdown = rt_uidropdown_new(0, 0, 100, 20);
    assert(dropdown != nullptr);
    rt_uidropdown_add_option(dropdown, rt_const_cstr("One"));
    rt_uidropdown_add_option(dropdown, rt_const_cstr("Two"));
    assert(rt_uidropdown_get_selected(dropdown) == 0);
    assert(rt_uidropdown_handle_key(dropdown, KEY_DOWN) == 1);
    assert(rt_uidropdown_get_selected(dropdown) == 1);
    assert(str_eq(rt_uidropdown_get_selected_text(dropdown), "Two"));
    assert(rt_uidropdown_handle_key(dropdown, KEY_ENTER) == 1);
    assert(rt_uidropdown_is_open(dropdown) == 1);
    assert(rt_uidropdown_handle_key(dropdown, KEY_ESCAPE) == 1);
    assert(rt_uidropdown_is_open(dropdown) == 0);

    void *modal = rt_uimodal_new_at(0, 0, 220, 120);
    assert(modal != nullptr);
    assert(rt_uimodal_add_button(modal, rt_const_cstr("OK"), 7) == 0);
    assert(rt_uimodal_add_button(modal, rt_const_cstr("Cancel"), 9) == 1);
    rt_uimodal_set_default_button(modal, 0);
    rt_uimodal_set_cancel_button(modal, 1);
    rt_uimodal_open(modal);
    assert(rt_uimodal_handle_key(modal, KEY_ENTER, 0) == 7);
    assert(rt_uimodal_is_open(modal) == 0);
    assert(rt_uimodal_get_result(modal) == 7);
    rt_uimodal_open(modal);
    assert(rt_uimodal_handle_key(modal, KEY_ESCAPE, 0) == 9);
    assert(rt_uimodal_get_result(modal) == 9);

    void *tooltip = rt_uitooltip_new();
    assert(tooltip != nullptr);
    rt_uitooltip_set_text(tooltip, rt_const_cstr("Tip"));
    rt_uitooltip_set_hover_delay_ms(tooltip, 50);
    rt_uitooltip_update(tooltip, 5, 6, 1, 25);
    rt_uitooltip_update(tooltip, 5, 6, 1, 25);
    rt_uitooltip_draw(tooltip, nullptr);

    release_obj(tooltip);
    release_obj(modal);
    release_obj(dropdown);
    release_obj(slider);
    PASS();
}

static void test_renderer2d_rotated_texture_paths() {
    TEST("Renderer2D rotated texture draws zero-degree and pivoted rotations");
    void *src = rt_pixels_new(2, 2);
    assert(src != nullptr);
    rt_pixels_set(src, 0, 0, 0xFF0000FF);
    rt_pixels_set(src, 1, 0, 0x00FF00FF);
    rt_pixels_set(src, 0, 1, 0x0000FFFF);
    rt_pixels_set(src, 1, 1, 0xFFFFFFFF);
    void *texture = rt_texture2d_new(src);
    void *target = rt_rendertarget2d_new(4, 4);
    void *renderer = rt_renderer2d_new(4);
    assert(texture != nullptr);
    assert(target != nullptr);
    assert(renderer != nullptr);

    rt_rendertarget2d_clear(target, 0x00000000);
    rt_renderer2d_begin(renderer);
    rt_renderer2d_draw_texture_rotated(renderer, texture, 1, 1, 0.0);
    assert(rt_renderer2d_count(renderer) == 1);
    rt_renderer2d_flush_to_target(renderer, target);
    void *out = rt_rendertarget2d_get_pixels(target);
    assert(rt_pixels_get(out, 1, 1) == 0xFF0000FF);
    assert(rt_pixels_get(out, 2, 1) == 0x00FF00FF);
    assert(rt_pixels_get(out, 1, 2) == 0x0000FFFF);
    assert(rt_pixels_get(out, 2, 2) == 0xFFFFFFFF);
    rt_renderer2d_end(renderer, nullptr);

    rt_rendertarget2d_clear(target, 0x00000000);
    rt_renderer2d_begin(renderer);
    rt_renderer2d_draw_texture_rotated_at(renderer, texture, 1, 1, 1, 1, 90.0);
    rt_renderer2d_flush_to_target(renderer, target);
    int non_empty = 0;
    for (int64_t y = 0; y < 4; y++) {
        for (int64_t x = 0; x < 4; x++) {
            if (rt_pixels_get(out, x, y) != 0)
                non_empty++;
        }
    }
    assert(non_empty >= 2);
    rt_renderer2d_end(renderer, nullptr);

    release_obj(renderer);
    release_obj(target);
    release_obj(texture);
    release_obj(src);
    PASS();
}

static void test_animstate_events_and_timeline() {
    TEST("AnimState events and AnimTimeline markers fire deterministically");
    void *state = rt_animstate_new();
    assert(state != nullptr);
    rt_animstate_add_state(state, 1, 0, 3, 1, 0);
    assert(rt_animstate_add_event(state, 1, 1, 101) == 1);
    assert(rt_animstate_add_event(state, 1, 2, 102) == 1);
    assert(rt_animstate_add_event(state, 99, 1, 999) == 0);
    assert(rt_animstate_set_initial(state, 1) == 1);
    rt_animstate_update(state);
    assert(rt_animstate_events_fired_count(state) == 1);
    assert(rt_animstate_event_fired_id(state, 0) == 101);
    rt_animstate_update(state);
    assert(rt_animstate_events_fired_count(state) == 1);
    assert(rt_animstate_event_fired_id(state, 0) == 102);
    rt_animstate_clear_events(state, 1);
    rt_animstate_transition(state, 1);
    rt_animstate_update(state);
    assert(rt_animstate_events_fired_count(state) == 0);

    void *timeline = rt_animtimeline_new(10);
    assert(timeline != nullptr);
    int64_t anim_track = rt_animtimeline_add_anim_track(timeline, rt_const_cstr("walk"), 2, 4, 33);
    int64_t tween_track =
        rt_animtimeline_add_tween_track(timeline, rt_const_cstr("alpha"), 0, 10, 5, 15);
    assert(anim_track == 0);
    assert(tween_track == 1);
    assert(rt_animtimeline_add_marker(timeline, 3, 77) == 0);
    assert(rt_animtimeline_track_payload_a(timeline, anim_track) == 33);
    assert(rt_animtimeline_track_payload_a(timeline, tween_track) == 5);
    assert(rt_animtimeline_track_payload_b(timeline, tween_track) == 15);

    rt_animtimeline_play(timeline);
    rt_animtimeline_advance(timeline, 2);
    assert(rt_animtimeline_track_is_active(timeline, anim_track) == 1);
    assert(std::fabs(rt_animtimeline_track_progress(timeline, tween_track) - 0.2) < 0.001);
    rt_animtimeline_advance(timeline, 1);
    assert(rt_animtimeline_events_fired_count(timeline) == 1);
    assert(rt_animtimeline_event_fired_id(timeline, 0) == 77);
    rt_animtimeline_set_looping(timeline, 1);
    rt_animtimeline_advance(timeline, 7);
    assert(rt_animtimeline_get_current_frame(timeline) == 0);
    rt_animtimeline_advance(timeline, 3);
    assert(rt_animtimeline_events_fired_count(timeline) == 1);
    assert(rt_animtimeline_event_fired_id(timeline, 0) == 77);

    release_obj(timeline);
    release_obj(state);
    PASS();
}

static void test_projectile2d_and_named_audio_groups() {
    TEST("Projectile2D math and named audio mixer groups");
    void *projectile = rt_projectile2d_new(0.0, 0.0, 10.0, 0.0, 0.0, 10.0);
    assert(projectile != nullptr);
    rt_projectile2d_set_ground_y(projectile, 20.0);
    assert(std::fabs(rt_projectile2d_x_at(projectile, 2.0) - 20.0) < 0.001);
    assert(std::fabs(rt_projectile2d_y_at(projectile, 2.0) - 20.0) < 0.001);
    assert(std::fabs(rt_projectile2d_vy_at(projectile, 2.0) - 20.0) < 0.001);
    assert(std::fabs(rt_projectile2d_time_to_ground(projectile) - 2.0) < 0.001);
    rt_projectile2d_advance(projectile, 1.0);
    assert(rt_projectile2d_has_landed(projectile) == 0);
    rt_projectile2d_advance(projectile, 1.1);
    assert(rt_projectile2d_has_landed(projectile) == 1);
    assert(rt_projectile2d_total_time(projectile) > 2.0);
    rt_projectile2d_reset(projectile);
    rt_projectile2d_set_drag(projectile, 0.5);
    assert(rt_projectile2d_x_at(projectile, 1.0) < 10.0);

    int64_t music_id = rt_audio_register_group(rt_const_cstr("music"));
    int64_t sfx_id = rt_audio_register_group(rt_const_cstr("sfx"));
    assert(music_id == RT_MIXGROUP_MUSIC);
    assert(sfx_id == RT_MIXGROUP_SFX);
    int64_t ui_id = rt_audio_register_group(rt_const_cstr("ui"));
    assert(ui_id >= RT_MIXGROUP_NAMED_BASE);
    assert(rt_audio_find_group(rt_const_cstr("ui")) == ui_id);
    rt_audio_set_group_volume(ui_id, 42);
    assert(rt_audio_get_group_volume(ui_id) == 42);
    rt_audio_set_group_volume_named(rt_const_cstr("ambience"), 130);
    int64_t ambience_id = rt_audio_find_group(rt_const_cstr("ambience"));
    assert(ambience_id >= RT_MIXGROUP_NAMED_BASE);
    assert(rt_audio_get_group_volume_named(rt_const_cstr("ambience")) == 100);
    assert(str_eq(rt_audio_group_name(ambience_id), "ambience"));
    assert(str_eq(rt_audio_group_name(-1), ""));

    release_obj(projectile);
    PASS();
}

static void test_camera_smooth_follow_deadzone_and_system_clipboard() {
    TEST("Camera smooth/deadzone methods and System.Clipboard wrappers");
    void *camera = rt_camera_new(800, 600);
    assert(camera != nullptr);

    rt_camera_smooth_follow(camera, 1000, 500, 1000);
    assert(rt_camera_get_x(camera) == 600);
    assert(rt_camera_get_y(camera) == 200);

    rt_camera_set_deadzone(camera, 100, 100);
    rt_camera_smooth_follow(camera, 1020, 520, 500);
    assert(rt_camera_get_x(camera) == 600);
    assert(rt_camera_get_y(camera) == 200);

    rt_camera_smooth_follow(camera, 1300, 500, 500);
    assert(rt_camera_get_x(camera) > 600);
    rt_camera_set_deadzone(camera, -1, -1);
    rt_camera_smooth_follow(camera, 1000, 500, 0);
    assert(rt_camera_get_x(camera) > 600);

    rt_string previous = rt_system_clipboard_get();
    int64_t had_previous = rt_system_clipboard_has_text();
    const char *sentinel = "viper-runtime-clipboard-\303\251";
    rt_string sentinel_text = rt_const_cstr(sentinel);
    rt_system_clipboard_set(sentinel_text);
    rt_string roundtrip = rt_system_clipboard_get();

    if (str_eq(roundtrip, sentinel))
        assert(rt_system_clipboard_has_text() == 1);
    else
        assert(rt_str_len(roundtrip) >= 0); // Headless desktop backends may have no clipboard owner.

    if (had_previous && !rt_str_is_empty(previous))
        rt_system_clipboard_set(previous);
    else
        rt_clipboard_clear();

    rt_str_release_maybe(sentinel_text);
    rt_str_release_maybe(roundtrip);
    rt_str_release_maybe(previous);
    release_obj(camera);
    PASS();
}

int main() {
    printf("Running runtime additions tests...\n");
    test_textinput_unicode_selection_and_limits();
    test_table_sorting_selection_and_header_clicks();
    test_slider_dropdown_modal_and_tooltip();
    test_renderer2d_rotated_texture_paths();
    test_animstate_events_and_timeline();
    test_projectile2d_and_named_audio_groups();
    test_camera_smooth_follow_deadzone_and_system_clipboard();
    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
