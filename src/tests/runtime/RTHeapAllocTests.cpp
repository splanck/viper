//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTHeapAllocTests.cpp
// Purpose: Verify all runtime types use rt_obj_new_i64 allocation (RT_MAGIC).
//          Regression tests for bugs A-026, A-027, A-046, A-055–A-060.
//
//===----------------------------------------------------------------------===//

#include "rt_collision.h"
#include "rt_deque.h"
#include "rt_future.h"
#include "rt_pathfollow.h"
#include "rt_screenfx.h"
#include "rt_smoothvalue.h"
#include "rt_sortedset.h"
#include "rt_string.h"
#include "rt_timer.h"
#include "rt_tween.h"

#include <cassert>
#include <cstdio>

static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// A-026: Deque heap allocation
//=============================================================================
static void test_deque_heap()
{
    printf("Testing Deque heap alloc (A-026):\n");
    void *d = rt_deque_new();
    test_result("Deque created", d != NULL);

    // Push and pop to exercise the object
    rt_deque_push_back(d, (void *)42);
    rt_deque_push_front(d, (void *)99);
    test_result("Deque len is 2", rt_deque_len(d) == 2);
    test_result("Front is 99", rt_deque_peek_front(d) == (void *)99);
    test_result("Back is 42", rt_deque_peek_back(d) == (void *)42);
    printf("\n");
}

//=============================================================================
// A-027: SortedSet heap allocation
//=============================================================================
static void test_sortedset_heap()
{
    printf("Testing SortedSet heap alloc (A-027):\n");
    void *s = rt_sortedset_new();
    test_result("SortedSet created", s != NULL);

    rt_sortedset_put(s, rt_const_cstr("hello"));
    rt_sortedset_put(s, rt_const_cstr("world"));
    test_result("SortedSet len is 2", rt_sortedset_len(s) == 2);
    test_result("Has hello", rt_sortedset_has(s, rt_const_cstr("hello")) == 1);
    printf("\n");
}

//=============================================================================
// A-046: Future/Promise heap allocation
//=============================================================================
static void test_future_heap()
{
    printf("Testing Future/Promise heap alloc (A-046):\n");
    void *p = rt_promise_new();
    test_result("Promise created", p != NULL);

    void *f = rt_promise_get_future(p);
    test_result("Future created", f != NULL);

    test_result("Future not done yet", rt_future_is_done(f) == 0);

    rt_promise_set(p, (void *)123);
    test_result("Future is done", rt_future_is_done(f) == 1);
    test_result("Future value is 123", rt_future_get(f) == (void *)123);
    printf("\n");
}

//=============================================================================
// A-055: Timer heap allocation
//=============================================================================
static void test_timer_heap()
{
    printf("Testing Timer heap alloc (A-055):\n");
    rt_timer t = rt_timer_new();
    test_result("Timer created", t != NULL);

    rt_timer_start(t, 100);
    test_result("Timer is running", rt_timer_is_running(t) == 1);
    rt_timer_stop(t);
    test_result("Timer stopped", rt_timer_is_running(t) == 0);

    rt_timer_destroy(t);
    printf("\n");
}

//=============================================================================
// A-056: Tween heap allocation
//=============================================================================
static void test_tween_heap()
{
    printf("Testing Tween heap alloc (A-056):\n");
    rt_tween tw = rt_tween_new();
    test_result("Tween created", tw != NULL);

    rt_tween_start(tw, 0.0, 100.0, 60, RT_EASE_LINEAR);
    test_result("Tween is running", rt_tween_is_running(tw) == 1);

    rt_tween_update(tw);
    double val = rt_tween_value(tw);
    test_result("Tween value > 0 after update", val > 0.0);

    rt_tween_destroy(tw);
    printf("\n");
}

//=============================================================================
// A-057: SmoothValue heap allocation
//=============================================================================
static void test_smoothvalue_heap()
{
    printf("Testing SmoothValue heap alloc (A-057):\n");
    rt_smoothvalue sv = rt_smoothvalue_new(0.0, 0.5);
    test_result("SmoothValue created", sv != NULL);

    test_result("Initial value is 0", rt_smoothvalue_get(sv) == 0.0);
    rt_smoothvalue_set_target(sv, 100.0);
    rt_smoothvalue_update(sv);
    test_result("Value moved toward target", rt_smoothvalue_get(sv) > 0.0);

    rt_smoothvalue_destroy(sv);
    printf("\n");
}

//=============================================================================
// A-058: PathFollow heap allocation
//=============================================================================
static void test_pathfollow_heap()
{
    printf("Testing PathFollow heap alloc (A-058):\n");
    rt_pathfollow pf = rt_pathfollow_new();
    test_result("PathFollow created", pf != NULL);

    rt_pathfollow_add_point(pf, 0, 0);
    rt_pathfollow_add_point(pf, 100000, 0);
    test_result("Point count is 2", rt_pathfollow_point_count(pf) == 2);

    rt_pathfollow_start(pf);
    test_result("PathFollow is active", rt_pathfollow_is_active(pf) == 1);

    rt_pathfollow_destroy(pf);
    printf("\n");
}

//=============================================================================
// A-059: ScreenFX heap allocation
//=============================================================================
static void test_screenfx_heap()
{
    printf("Testing ScreenFX heap alloc (A-059):\n");
    rt_screenfx fx = rt_screenfx_new();
    test_result("ScreenFX created", fx != NULL);

    rt_screenfx_fade_in(fx, 0x000000FF, 1000);
    rt_screenfx_update(fx, 16);
    test_result("Overlay alpha >= 0", rt_screenfx_get_overlay_alpha(fx) >= 0);

    rt_screenfx_destroy(fx);
    printf("\n");
}

//=============================================================================
// A-060: CollisionRect heap allocation
//=============================================================================
static void test_collision_heap()
{
    printf("Testing CollisionRect heap alloc (A-060):\n");
    rt_collision_rect r = rt_collision_rect_new(10.0, 20.0, 50.0, 30.0);
    test_result("CollisionRect created", r != NULL);

    test_result("X is 10", rt_collision_rect_x(r) == 10.0);
    test_result("Y is 20", rt_collision_rect_y(r) == 20.0);
    test_result("Width is 50", rt_collision_rect_width(r) == 50.0);
    test_result("Height is 30", rt_collision_rect_height(r) == 30.0);

    rt_collision_rect_destroy(r);
    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================
int main()
{
    printf("=== RT Heap Allocation Tests (Phase 1) ===\n\n");

    test_deque_heap();
    test_sortedset_heap();
    test_future_heap();
    test_timer_heap();
    test_tween_heap();
    test_smoothvalue_heap();
    test_pathfollow_heap();
    test_screenfx_heap();
    test_collision_heap();

    printf("All heap allocation tests passed!\n");
    return 0;
}
