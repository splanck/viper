//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTDebugOverlayTests.cpp
// Purpose: Tests for DebugOverlay — FPS rolling average, watch variable CRUD,
//   enable/disable/toggle, and null safety. Drawing is not tested (requires
//   canvas), but all non-rendering logic is exercised.
// Key invariants:
//   - FPS = 1000 * frame_count / sum_of_dt_ms (rolling average over 16 frames).
//   - Max 16 watch variables; duplicates update existing entry.
//   - Disabled by default; toggle flips state.
//   - All functions are null-safe.
// Ownership/Lifetime:
//   - DebugOverlay objects are GC-managed; tests rely on the runtime allocator.
// Links: rt_debugoverlay.c, rt_debugoverlay.h
//
//===----------------------------------------------------------------------===//

#include "rt_debugoverlay.h"

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    (void)msg;
}

/// @brief Helper: create rt_string from C literal.
static rt_string S(const char *s) {
    return rt_const_cstr(s);
}

// ============================================================================
// Null Safety
// ============================================================================

static void test_null_safety() {
    // All operations on NULL should not crash
    rt_debugoverlay_enable(NULL);
    rt_debugoverlay_disable(NULL);
    rt_debugoverlay_toggle(NULL);
    assert(rt_debugoverlay_is_enabled(NULL) == 0);
    rt_debugoverlay_update(NULL, 16);
    rt_debugoverlay_watch(NULL, S("x"), 42);
    assert(rt_debugoverlay_unwatch(NULL, S("x")) == 0);
    rt_debugoverlay_clear(NULL);
    assert(rt_debugoverlay_get_fps(NULL) == 0);
    rt_debugoverlay_draw(NULL, NULL);

    printf("  test_null_safety: PASSED\n");
}

// ============================================================================
// Enable / Disable / Toggle
// ============================================================================

static void test_disabled_by_default() {
    rt_debugoverlay dbg = rt_debugoverlay_new();
    assert(dbg != NULL);
    assert(rt_debugoverlay_is_enabled(dbg) == 0);
    printf("  test_disabled_by_default: PASSED\n");
}

static void test_enable_disable() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    rt_debugoverlay_enable(dbg);
    assert(rt_debugoverlay_is_enabled(dbg) == 1);

    rt_debugoverlay_disable(dbg);
    assert(rt_debugoverlay_is_enabled(dbg) == 0);

    printf("  test_enable_disable: PASSED\n");
}

static void test_toggle() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    assert(rt_debugoverlay_is_enabled(dbg) == 0);
    rt_debugoverlay_toggle(dbg);
    assert(rt_debugoverlay_is_enabled(dbg) == 1);
    rt_debugoverlay_toggle(dbg);
    assert(rt_debugoverlay_is_enabled(dbg) == 0);

    printf("  test_toggle: PASSED\n");
}

// ============================================================================
// FPS Calculation
// ============================================================================

static void test_fps_zero_frames() {
    rt_debugoverlay dbg = rt_debugoverlay_new();
    // No frames recorded → FPS = 0
    assert(rt_debugoverlay_get_fps(dbg) == 0);
    printf("  test_fps_zero_frames: PASSED\n");
}

static void test_fps_steady_60() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // Feed 16 frames at 16ms each → ~62 FPS (1000*16/256 = 62)
    for (int i = 0; i < 16; i++)
        rt_debugoverlay_update(dbg, 16);

    int64_t fps = rt_debugoverlay_get_fps(dbg);
    assert(fps >= 60 && fps <= 63);

    printf("  test_fps_steady_60: PASSED\n");
}

static void test_fps_steady_30() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // Feed 16 frames at 33ms each → ~30 FPS (1000*16/528 = 30)
    for (int i = 0; i < 16; i++)
        rt_debugoverlay_update(dbg, 33);

    int64_t fps = rt_debugoverlay_get_fps(dbg);
    assert(fps >= 29 && fps <= 31);

    printf("  test_fps_steady_30: PASSED\n");
}

static void test_fps_rolling_average() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // Fill with 16ms frames
    for (int i = 0; i < 16; i++)
        rt_debugoverlay_update(dbg, 16);

    // Now replace with 33ms frames — should gradually shift
    for (int i = 0; i < 16; i++)
        rt_debugoverlay_update(dbg, 33);

    // After 16 new frames, the ring buffer is fully 33ms
    int64_t fps = rt_debugoverlay_get_fps(dbg);
    assert(fps >= 29 && fps <= 31);

    printf("  test_fps_rolling_average: PASSED\n");
}

static void test_fps_partial_fill() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // Only 4 frames at 10ms each → 1000*4/40 = 100 FPS
    for (int i = 0; i < 4; i++)
        rt_debugoverlay_update(dbg, 10);

    assert(rt_debugoverlay_get_fps(dbg) == 100);

    printf("  test_fps_partial_fill: PASSED\n");
}

static void test_fps_zero_dt() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // dt=0 for all frames → sum=0 → FPS=0 (avoid division by zero)
    for (int i = 0; i < 16; i++)
        rt_debugoverlay_update(dbg, 0);

    assert(rt_debugoverlay_get_fps(dbg) == 0);

    printf("  test_fps_zero_dt: PASSED\n");
}

// ============================================================================
// Watch Variables
// ============================================================================

static void test_watch_add() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    rt_debugoverlay_watch(dbg, S("Score"), 42000);
    rt_debugoverlay_watch(dbg, S("Lives"), 3);

    // Verify via unwatch (returns 1 if found)
    assert(rt_debugoverlay_unwatch(dbg, S("Score")) == 1);
    assert(rt_debugoverlay_unwatch(dbg, S("Lives")) == 1);
    assert(rt_debugoverlay_unwatch(dbg, S("Missing")) == 0);

    printf("  test_watch_add: PASSED\n");
}

static void test_watch_update() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    rt_debugoverlay_watch(dbg, S("Score"), 100);
    rt_debugoverlay_watch(dbg, S("Score"), 200); // Update, not duplicate

    // Only one entry — unwatch removes it, second unwatch fails
    assert(rt_debugoverlay_unwatch(dbg, S("Score")) == 1);
    assert(rt_debugoverlay_unwatch(dbg, S("Score")) == 0);

    printf("  test_watch_update: PASSED\n");
}

static void test_watch_clear() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    rt_debugoverlay_watch(dbg, S("A"), 1);
    rt_debugoverlay_watch(dbg, S("B"), 2);
    rt_debugoverlay_watch(dbg, S("C"), 3);

    rt_debugoverlay_clear(dbg);

    assert(rt_debugoverlay_unwatch(dbg, S("A")) == 0);
    assert(rt_debugoverlay_unwatch(dbg, S("B")) == 0);
    assert(rt_debugoverlay_unwatch(dbg, S("C")) == 0);

    printf("  test_watch_clear: PASSED\n");
}

static void test_watch_max_capacity() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // Fill all 16 slots
    char name[8];
    for (int i = 0; i < RT_DEBUG_MAX_WATCHES; i++) {
        name[0] = 'A' + (char)(i % 26);
        name[1] = '0' + (char)(i / 26);
        name[2] = '\0';
        rt_debugoverlay_watch(dbg, S(name), i);
    }

    // 17th should be silently ignored (no crash)
    rt_debugoverlay_watch(dbg, S("Overflow"), 999);

    // Original entries still exist
    assert(rt_debugoverlay_unwatch(dbg, S("A0")) == 1);

    // The overflow entry should not exist
    assert(rt_debugoverlay_unwatch(dbg, S("Overflow")) == 0);

    printf("  test_watch_max_capacity: PASSED\n");
}

static void test_watch_null_name() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    // NULL name should not crash
    rt_debugoverlay_watch(dbg, NULL, 42);
    assert(rt_debugoverlay_unwatch(dbg, NULL) == 0);

    printf("  test_watch_null_name: PASSED\n");
}

static void test_watch_reuse_slot() {
    rt_debugoverlay dbg = rt_debugoverlay_new();

    rt_debugoverlay_watch(dbg, S("X"), 1);
    assert(rt_debugoverlay_unwatch(dbg, S("X")) == 1);

    // Slot should be reusable
    rt_debugoverlay_watch(dbg, S("Y"), 2);
    assert(rt_debugoverlay_unwatch(dbg, S("Y")) == 1);

    printf("  test_watch_reuse_slot: PASSED\n");
}

// ============================================================================
// Draw null safety
// ============================================================================

static void test_draw_null_canvas() {
    rt_debugoverlay dbg = rt_debugoverlay_new();
    rt_debugoverlay_enable(dbg);

    // Draw with NULL canvas should not crash
    rt_debugoverlay_draw(dbg, NULL);

    printf("  test_draw_null_canvas: PASSED\n");
}

static void test_draw_disabled_noop() {
    rt_debugoverlay dbg = rt_debugoverlay_new();
    // Disabled by default — draw with non-null canvas should be a no-op
    // (We can't provide a real canvas, but the function checks enabled first)
    rt_debugoverlay_draw(dbg, NULL);

    printf("  test_draw_disabled_noop: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== RTDebugOverlayTests ===\n\n");

    printf("--- Null Safety ---\n");
    test_null_safety();

    printf("\n--- Enable / Disable / Toggle ---\n");
    test_disabled_by_default();
    test_enable_disable();
    test_toggle();

    printf("\n--- FPS Calculation ---\n");
    test_fps_zero_frames();
    test_fps_steady_60();
    test_fps_steady_30();
    test_fps_rolling_average();
    test_fps_partial_fill();
    test_fps_zero_dt();

    printf("\n--- Watch Variables ---\n");
    test_watch_add();
    test_watch_update();
    test_watch_clear();
    test_watch_max_capacity();
    test_watch_null_name();
    test_watch_reuse_slot();

    printf("\n--- Draw ---\n");
    test_draw_null_canvas();
    test_draw_disabled_noop();

    printf("\n=== All RTDebugOverlayTests passed! ===\n");
    return 0;
}
