//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_achievement.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct FakeCanvas {
    int64_t width;
};

int g_box_calls = 0;
int g_text_calls = 0;
const char *g_last_text = nullptr;

void reset_draw_counters() {
    g_box_calls = 0;
    g_text_calls = 0;
    g_last_text = nullptr;
}

} // namespace

extern "C" void *rt_obj_new_i64(int64_t, int64_t byte_size) {
    return std::calloc(1, static_cast<size_t>(byte_size));
}

extern "C" int rt_obj_release_check0(void *) {
    return 1;
}

extern "C" void rt_obj_free(void *obj) {
    std::free(obj);
}

extern "C" void rt_canvas_box_alpha(void *, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t) {
    g_box_calls++;
}

extern "C" void rt_canvas_text_scaled(void *, int64_t, int64_t, const char *text, int64_t, int64_t) {
    g_text_calls++;
    g_last_text = text;
}

extern "C" int64_t rt_canvas_width(void *canvas) {
    return canvas ? static_cast<FakeCanvas *>(canvas)->width : 0;
}

static void test_capacity_is_honored() {
    rt_achievement ach = rt_achievement_new(2);
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, "First", "A");
    rt_achievement_add(ach, 1, "Second", "B");
    rt_achievement_add(ach, 2, "Ignored", "C");

    assert(rt_achievement_total_count(ach) == 2);
    assert(rt_achievement_unlock(ach, 2) == 0);
    assert(rt_achievement_is_unlocked(ach, 2) == 0);

    rt_achievement_destroy(ach);
}

static void test_unlock_requires_defined_entry() {
    rt_achievement ach = rt_achievement_new(4);
    assert(ach != nullptr);

    assert(rt_achievement_unlock(ach, 0) == 0);
    assert(rt_achievement_has_notification(ach) == 0);

    rt_achievement_add(ach, 0, "Alpha", "First");
    assert(rt_achievement_unlock(ach, 0) == 1);
    assert(rt_achievement_unlock(ach, 0) == 0);
    assert(rt_achievement_is_unlocked(ach, 0) == 1);
    assert(rt_achievement_unlocked_count(ach) == 1);
    assert(rt_achievement_has_notification(ach) == 1);

    rt_achievement_destroy(ach);
}

static void test_mask_round_trip_clamps_to_capacity() {
    rt_achievement ach = rt_achievement_new(3);
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, "A", "A");
    rt_achievement_add(ach, 1, "B", "B");
    rt_achievement_add(ach, 2, "C", "C");
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

static void test_notification_lifetime_and_draw() {
    rt_achievement ach = rt_achievement_new(2);
    FakeCanvas canvas{640};
    assert(ach != nullptr);

    rt_achievement_add(ach, 0, "Unlocked", "Done");
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

int main() {
    test_capacity_is_honored();
    test_unlock_requires_defined_entry();
    test_mask_round_trip_clamps_to_capacity();
    test_stat_tracking();
    test_notification_lifetime_and_draw();
    std::printf("RTAchievementTests passed.\n");
    return 0;
}
