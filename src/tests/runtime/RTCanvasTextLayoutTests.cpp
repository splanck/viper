//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCanvasTextLayoutTests.cpp
// Purpose: Tests for Canvas text layout helpers — TextCentered, TextRight,
//   TextCenteredScaled, and TextScaledWidth. Validates null safety, text
//   width computation, and that layout functions execute without crashing
//   on canvases with no backing window.
// Key invariants:
//   - TextScaledWidth returns strlen(text) * 8 * scale (pure math).
//   - All layout functions are null-safe.
//   - Layout functions don't crash on canvases with NULL gfx_win.
// Ownership/Lifetime:
//   - Fake canvases are heap-allocated for struct-level tests.
// Links: rt_drawing.c, rt_graphics.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_graphics_internal.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    (void)msg;
}

/// @brief Helper: create rt_string from C literal.
static rt_string S(const char *s)
{
    return rt_const_cstr(s);
}

// ============================================================================
// Null Safety Tests (always run)
// ============================================================================

static void test_text_centered_null_canvas()
{
    rt_canvas_text_centered(nullptr, 100, S("hello"), 0x00FFFFFF);
    printf("  test_text_centered_null_canvas: PASSED\n");
}

static void test_text_centered_null_text()
{
    rt_canvas_text_centered(nullptr, 100, nullptr, 0x00FFFFFF);
    printf("  test_text_centered_null_text: PASSED\n");
}

static void test_text_right_null_canvas()
{
    rt_canvas_text_right(nullptr, 10, 100, S("hello"), 0x00FFFFFF);
    printf("  test_text_right_null_canvas: PASSED\n");
}

static void test_text_right_null_text()
{
    rt_canvas_text_right(nullptr, 10, 100, nullptr, 0x00FFFFFF);
    printf("  test_text_right_null_text: PASSED\n");
}

static void test_text_centered_scaled_null_canvas()
{
    rt_canvas_text_centered_scaled(nullptr, 100, S("hello"), 0x00FFFFFF, 2);
    printf("  test_text_centered_scaled_null_canvas: PASSED\n");
}

static void test_text_centered_scaled_null_text()
{
    rt_canvas_text_centered_scaled(nullptr, 100, nullptr, 0x00FFFFFF, 2);
    printf("  test_text_centered_scaled_null_text: PASSED\n");
}

static void test_text_centered_scaled_zero_scale()
{
    // scale < 1 should early-return without crash
    rt_canvas_text_centered_scaled(nullptr, 100, S("hello"), 0x00FFFFFF, 0);
    rt_canvas_text_centered_scaled(nullptr, 100, S("hello"), 0x00FFFFFF, -1);
    printf("  test_text_centered_scaled_zero_scale: PASSED\n");
}

// ============================================================================
// TextScaledWidth (pure math — no canvas needed)
// ============================================================================

#ifdef VIPER_ENABLE_GRAPHICS

static void test_text_scaled_width_basic()
{
    // "hello" = 5 chars, scale 1 → 5 * 8 * 1 = 40
    assert(rt_canvas_text_scaled_width(S("hello"), 1) == 40);

    // "GAME OVER" = 9 chars, scale 1 → 72
    assert(rt_canvas_text_scaled_width(S("GAME OVER"), 1) == 72);

    // "A" = 1 char, scale 1 → 8
    assert(rt_canvas_text_scaled_width(S("A"), 1) == 8);

    printf("  test_text_scaled_width_basic: PASSED\n");
}

static void test_text_scaled_width_scaled()
{
    // "hello" = 5 chars, scale 2 → 5 * 8 * 2 = 80
    assert(rt_canvas_text_scaled_width(S("hello"), 2) == 80);

    // "hi" = 2 chars, scale 3 → 2 * 8 * 3 = 48
    assert(rt_canvas_text_scaled_width(S("hi"), 3) == 48);

    // scale 4
    assert(rt_canvas_text_scaled_width(S("X"), 4) == 32);

    printf("  test_text_scaled_width_scaled: PASSED\n");
}

static void test_text_scaled_width_empty()
{
    // Empty string → 0
    assert(rt_canvas_text_scaled_width(S(""), 1) == 0);

    printf("  test_text_scaled_width_empty: PASSED\n");
}

static void test_text_scaled_width_null()
{
    // NULL text → 0
    assert(rt_canvas_text_scaled_width(nullptr, 1) == 0);

    // scale < 1 → 0
    assert(rt_canvas_text_scaled_width(S("hello"), 0) == 0);
    assert(rt_canvas_text_scaled_width(S("hello"), -5) == 0);

    printf("  test_text_scaled_width_null: PASSED\n");
}

// ============================================================================
// Fake Canvas Tests (exercises computation path, drawing is no-op)
// ============================================================================

static rt_canvas *make_fake_canvas()
{
    rt_canvas *c = (rt_canvas *)calloc(1, sizeof(rt_canvas));
    assert(c != nullptr);
    c->vptr = nullptr;
    c->gfx_win = nullptr; // Drawing ops are no-ops but math runs
    c->should_close = 0;
    c->last_flip_us = 0;
    c->delta_time_ms = 0;
    c->dt_max_ms = 0;
    c->title = nullptr;
    return c;
}

static void test_text_centered_runs_without_crash()
{
    rt_canvas *c = make_fake_canvas();
    // gfx_win is NULL → rt_canvas_width returns 0, drawing is no-op
    // This exercises the full computation path without requiring a display
    rt_canvas_text_centered(c, 100, S("GAME OVER"), 0x00FFFFFF);
    rt_canvas_text_centered(c, 200, S(""), 0x00FFFFFF);
    free(c);
    printf("  test_text_centered_runs_without_crash: PASSED\n");
}

static void test_text_right_runs_without_crash()
{
    rt_canvas *c = make_fake_canvas();
    rt_canvas_text_right(c, 10, 100, S("Score: 42000"), 0x00FFFFFF);
    rt_canvas_text_right(c, 0, 200, S(""), 0x00FFFFFF);
    free(c);
    printf("  test_text_right_runs_without_crash: PASSED\n");
}

static void test_text_centered_scaled_runs_without_crash()
{
    rt_canvas *c = make_fake_canvas();
    rt_canvas_text_centered_scaled(c, 100, S("TITLE"), 0x00FFFFFF, 2);
    rt_canvas_text_centered_scaled(c, 100, S("TITLE"), 0x00FFFFFF, 3);
    // Edge: scale 0 → early return
    rt_canvas_text_centered_scaled(c, 100, S("TITLE"), 0x00FFFFFF, 0);
    free(c);
    printf("  test_text_centered_scaled_runs_without_crash: PASSED\n");
}

#endif /* VIPER_ENABLE_GRAPHICS */

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== RTCanvasTextLayoutTests ===\n\n");

    printf("--- Null Safety ---\n");
    test_text_centered_null_canvas();
    test_text_centered_null_text();
    test_text_right_null_canvas();
    test_text_right_null_text();
    test_text_centered_scaled_null_canvas();
    test_text_centered_scaled_null_text();
    test_text_centered_scaled_zero_scale();

#ifdef VIPER_ENABLE_GRAPHICS
    printf("\n--- TextScaledWidth ---\n");
    test_text_scaled_width_basic();
    test_text_scaled_width_scaled();
    test_text_scaled_width_empty();
    test_text_scaled_width_null();

    printf("\n--- Layout Functions (fake canvas) ---\n");
    test_text_centered_runs_without_crash();
    test_text_right_runs_without_crash();
    test_text_centered_scaled_runs_without_crash();
#else
    printf("\n  (struct-level tests skipped — VIPER_ENABLE_GRAPHICS not defined)\n");
#endif

    printf("\n=== All RTCanvasTextLayoutTests passed! ===\n");
    return 0;
}
