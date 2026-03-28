//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCanvasFrameTests.cpp
// Purpose: Tests for Canvas frame management helpers — SetDTMax() and
//   BeginFrame(). Validates null safety, delta-time clamping logic, and
//   the combined poll+close-check of BeginFrame.
// Key invariants:
//   - SetDTMax(0) disables clamping; SetDTMax(N>0) clamps DeltaTime to [1,N].
//   - BeginFrame() returns 0 on NULL canvas or when ShouldClose is set.
//   - All functions are null-safe (no crash on NULL canvas_ptr).
// Ownership/Lifetime:
//   - Tests that create a real canvas skip gracefully when no display server
//     is available (CI environments).
// Links: rt_canvas.c, rt_graphics.h, rt_graphics_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_graphics_internal.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    (void)msg;
    /* Swallow traps during testing — prevents abort on expected trap paths. */
}

// ============================================================================
// Null Safety Tests (always run, no display needed)
// ============================================================================

static void test_set_dt_max_null_canvas() {
    // Should not crash with NULL canvas
    rt_canvas_set_dt_max(nullptr, 50);
    rt_canvas_set_dt_max(nullptr, 0);
    rt_canvas_set_dt_max(nullptr, -10);
    printf("  test_set_dt_max_null_canvas: PASSED\n");
}

static void test_begin_frame_null_canvas() {
    // Should return 0 (stop) for NULL canvas
    int64_t result = rt_canvas_begin_frame(nullptr);
    assert(result == 0);
    printf("  test_begin_frame_null_canvas: PASSED\n");
}

static void test_get_delta_time_null_canvas() {
    // Should return 0 for NULL canvas
    int64_t dt = rt_canvas_get_delta_time(nullptr);
    assert(dt == 0);
    printf("  test_get_delta_time_null_canvas: PASSED\n");
}

// ============================================================================
// Struct-level clamping tests (requires VIPER_ENABLE_GRAPHICS for struct def)
// ============================================================================

#ifdef VIPER_ENABLE_GRAPHICS

/// Allocate a minimal rt_canvas on the heap with zeroed fields.
/// This is NOT a real canvas — no GC, no window — just enough to test
/// the DeltaTime clamping and SetDTMax logic through the C API.
static rt_canvas *make_fake_canvas() {
    rt_canvas *c = (rt_canvas *)calloc(1, sizeof(rt_canvas));
    assert(c != nullptr);
    c->vptr = nullptr;
    c->gfx_win = nullptr;
    c->should_close = 0;
    c->last_flip_us = 0;
    c->delta_time_ms = 0;
    c->dt_max_ms = 0;
    c->title = nullptr;
    return c;
}

static void test_dt_clamping_disabled_by_default() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 200;

    // dt_max_ms == 0 means no clamping; raw value returned
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 200);

    free(c);
    printf("  test_dt_clamping_disabled_by_default: PASSED\n");
}

static void test_dt_clamping_upper_bound() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 200;

    rt_canvas_set_dt_max(c, 50);
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 50); // clamped to max

    free(c);
    printf("  test_dt_clamping_upper_bound: PASSED\n");
}

static void test_dt_clamping_lower_bound() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 0;

    rt_canvas_set_dt_max(c, 50);
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 1); // clamped to minimum of 1

    free(c);
    printf("  test_dt_clamping_lower_bound: PASSED\n");
}

static void test_dt_clamping_negative_dt() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = -5;

    rt_canvas_set_dt_max(c, 50);
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 1); // negative clamped to 1

    free(c);
    printf("  test_dt_clamping_negative_dt: PASSED\n");
}

static void test_dt_clamping_within_range() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 16;

    rt_canvas_set_dt_max(c, 50);
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 16); // within [1, 50] — returned as-is

    free(c);
    printf("  test_dt_clamping_within_range: PASSED\n");
}

static void test_dt_clamping_exact_max() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 50;

    rt_canvas_set_dt_max(c, 50);
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 50); // exactly at max — not clamped

    free(c);
    printf("  test_dt_clamping_exact_max: PASSED\n");
}

static void test_dt_clamping_exact_one() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 1;

    rt_canvas_set_dt_max(c, 50);
    int64_t dt = rt_canvas_get_delta_time(c);
    assert(dt == 1); // exactly at min — not clamped

    free(c);
    printf("  test_dt_clamping_exact_one: PASSED\n");
}

static void test_set_dt_max_disables_clamping() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 200;

    // Enable clamping
    rt_canvas_set_dt_max(c, 50);
    assert(rt_canvas_get_delta_time(c) == 50);

    // Disable clamping with 0
    rt_canvas_set_dt_max(c, 0);
    assert(rt_canvas_get_delta_time(c) == 200);

    free(c);
    printf("  test_set_dt_max_disables_clamping: PASSED\n");
}

static void test_set_dt_max_negative_treated_as_zero() {
    rt_canvas *c = make_fake_canvas();
    c->delta_time_ms = 200;

    // Negative max should be treated as 0 (disabled)
    rt_canvas_set_dt_max(c, -10);
    assert(c->dt_max_ms == 0);
    assert(rt_canvas_get_delta_time(c) == 200);

    free(c);
    printf("  test_set_dt_max_negative_treated_as_zero: PASSED\n");
}

static void test_begin_frame_returns_1_when_open() {
    rt_canvas *c = make_fake_canvas();
    c->should_close = 0;

    // BeginFrame calls Poll (which is a no-op on NULL gfx_win) then checks
    // should_close.  With should_close == 0, it should return 1.
    int64_t result = rt_canvas_begin_frame(c);
    assert(result == 1);

    free(c);
    printf("  test_begin_frame_returns_1_when_open: PASSED\n");
}

static void test_begin_frame_returns_0_when_closing() {
    rt_canvas *c = make_fake_canvas();
    c->should_close = 1;

    int64_t result = rt_canvas_begin_frame(c);
    assert(result == 0);

    free(c);
    printf("  test_begin_frame_returns_0_when_closing: PASSED\n");
}

#endif /* VIPER_ENABLE_GRAPHICS */

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== RTCanvasFrameTests (SetDTMax + BeginFrame) ===\n\n");

    printf("--- Null Safety ---\n");
    test_set_dt_max_null_canvas();
    test_begin_frame_null_canvas();
    test_get_delta_time_null_canvas();

#ifdef VIPER_ENABLE_GRAPHICS
    printf("\n--- DeltaTime Clamping ---\n");
    test_dt_clamping_disabled_by_default();
    test_dt_clamping_upper_bound();
    test_dt_clamping_lower_bound();
    test_dt_clamping_negative_dt();
    test_dt_clamping_within_range();
    test_dt_clamping_exact_max();
    test_dt_clamping_exact_one();
    test_set_dt_max_disables_clamping();
    test_set_dt_max_negative_treated_as_zero();

    printf("\n--- BeginFrame Logic ---\n");
    test_begin_frame_returns_1_when_open();
    test_begin_frame_returns_0_when_closing();
#else
    printf("\n  (struct-level tests skipped — VIPER_ENABLE_GRAPHICS not defined)\n");
#endif

    printf("\n=== All RTCanvasFrameTests passed! ===\n");
    return 0;
}
