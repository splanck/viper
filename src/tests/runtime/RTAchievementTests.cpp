//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_achievement.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct FakeCanvas {
    int64_t width;
};

struct FakeObjectHeader {
    int64_t class_id;
    void (*finalizer)(void *);
};

int g_box_calls = 0;
int g_text_calls = 0;
int64_t g_last_class_id = 0;
int g_string_unref_calls = 0;
const char *g_last_text = nullptr;

FakeObjectHeader *fake_header(void *obj) {
    return reinterpret_cast<FakeObjectHeader *>(obj) - 1;
}

void reset_draw_counters() {
    g_box_calls = 0;
    g_text_calls = 0;
    g_last_text = nullptr;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t class_id, int64_t byte_size) {
    g_last_class_id = class_id;
    auto *header = static_cast<FakeObjectHeader *>(
        std::calloc(1, sizeof(FakeObjectHeader) + static_cast<size_t>(byte_size)));
    if (!header)
        return nullptr;
    header->class_id = class_id;
    return header + 1;
}

extern "C" int64_t rt_obj_class_id(void *obj) {
    return obj ? fake_header(obj)->class_id : 0;
}

extern "C" int rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    if (!obj)
        return;
    FakeObjectHeader *header = fake_header(obj);
    if (header->finalizer) {
        void (*finalizer)(void *) = header->finalizer;
        header->finalizer = nullptr;
        finalizer(obj);
    }
    std::free(header);
}

extern "C" void rt_obj_set_finalizer(void *obj, void (*finalizer)(void *)) {
    if (obj)
        fake_header(obj)->finalizer = finalizer;
}

extern "C" void rt_trap(const char *message) {
    std::fprintf(stderr, "rt_trap: %s\n", message ? message : "");
    std::abort();
}

extern "C" rt_string rt_const_cstr(const char *str) {
    return reinterpret_cast<rt_string>(const_cast<char *>(str));
}

extern "C" const char *rt_string_cstr(rt_string s) {
    return reinterpret_cast<const char *>(s);
}

extern "C" rt_string rt_string_ref(rt_string s) {
    return s;
}

extern "C" void rt_string_unref(rt_string) {
    g_string_unref_calls++;
}

extern "C" void rt_canvas_box_alpha(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {
    g_box_calls++;
}

extern "C" void rt_canvas_text_scaled(
    void *, int64_t, int64_t, rt_string text, int64_t, int64_t) {
    g_text_calls++;
    g_last_text = rt_string_cstr(text);
}

extern "C" int64_t rt_canvas_width(void *canvas) {
    return canvas ? static_cast<FakeCanvas *>(canvas)->width : 0;
}

static void test_capacity_is_honored() {
    rt_achievement ach = rt_achievement_new(2);
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, rt_const_cstr("First"), rt_const_cstr("A"));
    rt_achievement_add(ach, 1, rt_const_cstr("Second"), rt_const_cstr("B"));
    rt_achievement_add(ach, 2, rt_const_cstr("Ignored"), rt_const_cstr("C"));

    assert(rt_achievement_total_count(ach) == 2);
    assert(rt_achievement_unlock(ach, 2) == 0);
    assert(rt_achievement_is_unlocked(ach, 2) == 0);

    rt_achievement_destroy(ach);
}

static void test_class_id_is_tagged() {
    g_last_class_id = 0;
    rt_achievement ach = rt_achievement_new(1);
    assert(ach != nullptr);
    assert(g_last_class_id == RT_ACHIEVEMENT_CLASS_ID);

    rt_achievement_destroy(ach);
}

static void test_unlock_requires_defined_entry() {
    rt_achievement ach = rt_achievement_new(4);
    assert(ach != nullptr);

    assert(rt_achievement_unlock(ach, 0) == 0);
    assert(rt_achievement_has_notification(ach) == 0);

    rt_achievement_add(ach, 0, rt_const_cstr("Alpha"), rt_const_cstr("First"));
    assert(rt_achievement_unlock(ach, 0) == 1);
    assert(rt_achievement_unlock(ach, 0) == 0);
    assert(rt_achievement_is_unlocked(ach, 0) == 1);
    assert(rt_achievement_unlocked_count(ach) == 1);
    assert(rt_achievement_has_notification(ach) == 1);

    rt_achievement_destroy(ach);
}

static void test_negative_notification_delta_is_noop() {
    rt_achievement ach = rt_achievement_new(1);
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, rt_const_cstr("Unlocked"), rt_const_cstr("Done"));
    rt_achievement_set_notify_duration(ach, 100);
    assert(rt_achievement_unlock(ach, 0) == 1);

    rt_achievement_update(ach, -1000);
    assert(rt_achievement_has_notification(ach) == 1);
    rt_achievement_update(ach, 100);
    assert(rt_achievement_has_notification(ach) == 0);

    rt_achievement_destroy(ach);
}

static void test_large_notification_delta_dismisses() {
    rt_achievement ach = rt_achievement_new(1);
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, rt_const_cstr("Unlocked"), rt_const_cstr("Done"));
    rt_achievement_set_notify_duration(ach, 100);
    assert(rt_achievement_unlock(ach, 0) == 1);

    rt_achievement_update(ach, INT64_MAX);
    assert(rt_achievement_has_notification(ach) == 0);

    rt_achievement_destroy(ach);
}

static void test_mask_round_trip_clamps_to_capacity() {
    rt_achievement ach = rt_achievement_new(3);
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, rt_const_cstr("A"), rt_const_cstr("A"));
    rt_achievement_add(ach, 1, rt_const_cstr("B"), rt_const_cstr("B"));
    rt_achievement_add(ach, 2, rt_const_cstr("C"), rt_const_cstr("C"));
    rt_achievement_set_mask(ach, 0xFF);

    assert(rt_achievement_get_mask(ach) == 0x7);
    assert(rt_achievement_unlocked_count(ach) == 3);

    rt_achievement_destroy(ach);
}

static void test_stat_tracking() {
    rt_achievement ach = rt_achievement_new(1);
    assert(ach != nullptr);

    rt_achievement_set_stat(ach, 0, 10);
    rt_achievement_inc_stat(ach, 0, 5);
    assert(rt_achievement_get_stat(ach, 0) == 15);
    assert(rt_achievement_get_stat(ach, 99) == 0);

    rt_achievement_destroy(ach);
}

static void test_stat_increment_saturates() {
    rt_achievement ach = rt_achievement_new(1);
    assert(ach != nullptr);

    rt_achievement_set_stat(ach, 0, INT64_MAX - 1);
    rt_achievement_inc_stat(ach, 0, 10);
    assert(rt_achievement_get_stat(ach, 0) == INT64_MAX);

    rt_achievement_set_stat(ach, 1, INT64_MIN + 1);
    rt_achievement_inc_stat(ach, 1, -10);
    assert(rt_achievement_get_stat(ach, 1) == INT64_MIN);

    rt_achievement_destroy(ach);
}

static void test_destroy_runs_finalizer_for_retained_strings() {
    rt_achievement ach = rt_achievement_new(2);
    assert(ach != nullptr);

    g_string_unref_calls = 0;
    rt_achievement_add(ach, 0, rt_const_cstr("First"), rt_const_cstr("One"));
    rt_achievement_add(ach, 1, rt_const_cstr("Second"), rt_const_cstr("Two"));
    rt_achievement_destroy(ach);

    assert(g_string_unref_calls == 4);
}

static void test_notification_lifetime_and_draw() {
    rt_achievement ach = rt_achievement_new(2);
    FakeCanvas canvas{640};
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, rt_const_cstr("Unlocked"), rt_const_cstr("Done"));
    rt_achievement_set_notify_duration(ach, 100);
    assert(rt_achievement_unlock(ach, 0) == 1);

    reset_draw_counters();
    rt_achievement_draw(ach, &canvas);
    assert(g_box_calls == 1);
    assert(g_text_calls >= 2);
    assert(g_last_text != nullptr && std::strcmp(g_last_text, "Unlocked") == 0);

    rt_achievement_update(ach, 50);
    assert(rt_achievement_has_notification(ach) == 1);
    rt_achievement_update(ach, 60);
    assert(rt_achievement_has_notification(ach) == 0);

    reset_draw_counters();
    rt_achievement_draw(ach, &canvas);
    assert(g_box_calls == 0);
    assert(g_text_calls == 0);

    rt_achievement_destroy(ach);
}

static void test_highest_bit_unlock_is_supported() {
    rt_achievement ach = rt_achievement_new(64);
    assert(ach != nullptr);

    rt_achievement_add(ach, 63, rt_const_cstr("TopBit"), rt_const_cstr("Highest"));
    assert(rt_achievement_unlock(ach, 63) == 1);
    assert(rt_achievement_is_unlocked(ach, 63) == 1);
    assert(rt_achievement_unlocked_count(ach) == 1);
    assert(static_cast<uint64_t>(rt_achievement_get_mask(ach)) == (UINT64_C(1) << 63));

    rt_achievement_destroy(ach);
}

int main() {
    test_class_id_is_tagged();
    test_capacity_is_honored();
    test_unlock_requires_defined_entry();
    test_mask_round_trip_clamps_to_capacity();
    test_stat_tracking();
    test_stat_increment_saturates();
    test_destroy_runs_finalizer_for_retained_strings();
    test_notification_lifetime_and_draw();
    test_negative_notification_delta_is_noop();
    test_large_notification_delta_dismisses();
    test_highest_bit_unlock_is_supported();
    std::printf("RTAchievementTests passed.\n");
    return 0;
}
