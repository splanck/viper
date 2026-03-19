//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_screenfx_transitions.cpp
// Purpose: Unit tests for new ScreenFX transition effects (Wipe, CircleIn/Out,
//   Dissolve, Pixelate). Tests lifecycle, progress tracking, completion
//   detection, and NULL safety. Does not test Canvas rendering (requires
//   VIPER_ENABLE_GRAPHICS).
//
// Key invariants:
//   - Transitions advance with Update(dt) and complete when elapsed >= duration.
//   - IsFinished returns true when no effects are active.
//   - TransitionProgress returns 0-1000 fixed-point.
//   - All functions are safe with NULL inputs.
//
// Ownership/Lifetime:
//   - Uses runtime library. ScreenFX objects are GC-managed.
//
// Links: src/runtime/collections/rt_screenfx.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_screenfx.h"
#include <cassert>
#include <cstdio>

// Trap handler
extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

//=============================================================================
// Wipe tests
//=============================================================================

static void test_wipe_starts_active(void)
{
    TEST("Wipe starts as active");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 500);
    assert(rt_screenfx_is_active(fx) == 1);
    assert(rt_screenfx_is_finished(fx) == 0);
    PASS();
}

static void test_wipe_completes(void)
{
    TEST("Wipe completes after duration");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 500);
    rt_screenfx_update(fx, 500);
    assert(rt_screenfx_is_finished(fx) == 1);
    assert(rt_screenfx_is_active(fx) == 0);
    PASS();
}

static void test_wipe_progress_midway(void)
{
    TEST("Wipe progress at midpoint");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 1000);
    rt_screenfx_update(fx, 500);
    int64_t prog = rt_screenfx_get_transition_progress(fx);
    assert(prog == 500); // 50% = 500/1000
    PASS();
}

static void test_wipe_all_directions(void)
{
    TEST("Wipe works in all 4 directions");
    for (int dir = 0; dir <= 3; dir++)
    {
        rt_screenfx fx = rt_screenfx_new();
        rt_screenfx_wipe(fx, dir, 0xFF0000, 100);
        assert(rt_screenfx_is_active(fx) == 1);
        rt_screenfx_update(fx, 100);
        assert(rt_screenfx_is_finished(fx) == 1);
    }
    PASS();
}

static void test_wipe_invalid_direction(void)
{
    TEST("Wipe invalid direction defaults to LEFT");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 99, 0x000000, 100); // Invalid direction
    assert(rt_screenfx_is_active(fx) == 1);  // Still works
    rt_screenfx_update(fx, 100);
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

//=============================================================================
// Circle tests
//=============================================================================

static void test_circle_in_lifecycle(void)
{
    TEST("CircleIn starts active and completes");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_circle_in(fx, 160, 120, 0x000000, 1000);
    assert(rt_screenfx_is_active(fx) == 1);
    assert(rt_screenfx_is_finished(fx) == 0);

    rt_screenfx_update(fx, 1000);
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

static void test_circle_out_lifecycle(void)
{
    TEST("CircleOut starts active and completes");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_circle_out(fx, 160, 120, 0x000000, 500);
    assert(rt_screenfx_is_active(fx) == 1);

    rt_screenfx_update(fx, 500);
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

static void test_circle_type_active(void)
{
    TEST("CircleIn reports correct type");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_circle_in(fx, 100, 100, 0x000000, 500);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_CIRCLE_IN) == 1);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_WIPE) == 0);
    PASS();
}

//=============================================================================
// Dissolve tests
//=============================================================================

static void test_dissolve_lifecycle(void)
{
    TEST("Dissolve starts active and completes");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_dissolve(fx, 0x000000, 1000);
    assert(rt_screenfx_is_active(fx) == 1);

    rt_screenfx_update(fx, 500);
    assert(rt_screenfx_is_active(fx) == 1);
    assert(rt_screenfx_is_finished(fx) == 0);

    rt_screenfx_update(fx, 500);
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

static void test_dissolve_type(void)
{
    TEST("Dissolve reports correct type");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_dissolve(fx, 0xFFFFFF, 200);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_DISSOLVE) == 1);
    PASS();
}

//=============================================================================
// Pixelate tests
//=============================================================================

static void test_pixelate_lifecycle(void)
{
    TEST("Pixelate starts active and completes");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_pixelate(fx, 16, 500);
    assert(rt_screenfx_is_active(fx) == 1);

    rt_screenfx_update(fx, 500);
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

static void test_pixelate_min_block(void)
{
    TEST("Pixelate clamps block size >= 2");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_pixelate(fx, 0, 100); // 0 → clamped to 2
    assert(rt_screenfx_is_active(fx) == 1);
    PASS();
}

//=============================================================================
// Multiple effects / interaction tests
//=============================================================================

static void test_multiple_transitions(void)
{
    TEST("Multiple transitions can run concurrently");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0xFF0000, 500);
    rt_screenfx_dissolve(fx, 0x00FF00, 1000);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_WIPE) == 1);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_DISSOLVE) == 1);

    // After 500ms, wipe done but dissolve still running
    rt_screenfx_update(fx, 500);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_WIPE) == 0);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_DISSOLVE) == 1);
    assert(rt_screenfx_is_finished(fx) == 0);
    PASS();
}

static void test_transition_with_existing_effects(void)
{
    TEST("Transitions coexist with shake/fade");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_shake(fx, 1000, 500, 1000);
    rt_screenfx_wipe(fx, 0, 0x000000, 500);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_SHAKE) == 1);
    assert(rt_screenfx_is_type_active(fx, RT_SCREENFX_WIPE) == 1);
    PASS();
}

static void test_cancel_all_clears_transitions(void)
{
    TEST("CancelAll clears transitions too");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 1000);
    rt_screenfx_circle_in(fx, 100, 100, 0x000000, 1000);
    rt_screenfx_cancel_all(fx);
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

//=============================================================================
// NULL safety / edge cases
//=============================================================================

static void test_null_safety(void)
{
    TEST("NULL safety for all transition functions");
    rt_screenfx_wipe(NULL, 0, 0, 100);
    rt_screenfx_circle_in(NULL, 0, 0, 0, 100);
    rt_screenfx_circle_out(NULL, 0, 0, 0, 100);
    rt_screenfx_dissolve(NULL, 0, 100);
    rt_screenfx_pixelate(NULL, 16, 100);
    assert(rt_screenfx_is_finished(NULL) == 1);
    assert(rt_screenfx_get_transition_progress(NULL) == 0);
    rt_screenfx_draw(NULL, NULL, 320, 240);
    PASS();
}

static void test_zero_duration(void)
{
    TEST("Zero duration is rejected (no-op)");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 0);
    assert(rt_screenfx_is_finished(fx) == 1); // Nothing was added
    PASS();
}

static void test_draw_null_canvas(void)
{
    TEST("Draw with NULL canvas is no-op");
    rt_screenfx fx = rt_screenfx_new();
    rt_screenfx_wipe(fx, 0, 0x000000, 100);
    rt_screenfx_draw(fx, NULL, 320, 240); // No crash
    PASS();
}

static void test_is_finished_initially(void)
{
    TEST("New ScreenFX is finished (no effects)");
    rt_screenfx fx = rt_screenfx_new();
    assert(rt_screenfx_is_finished(fx) == 1);
    PASS();
}

static void test_transition_progress_no_transition(void)
{
    TEST("TransitionProgress returns 0 when no transition active");
    rt_screenfx fx = rt_screenfx_new();
    assert(rt_screenfx_get_transition_progress(fx) == 0);

    // Old-style effects don't count as transitions
    rt_screenfx_shake(fx, 1000, 500, 1000);
    assert(rt_screenfx_get_transition_progress(fx) == 0);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("test_rt_screenfx_transitions:\n");

    // Wipe
    test_wipe_starts_active();
    test_wipe_completes();
    test_wipe_progress_midway();
    test_wipe_all_directions();
    test_wipe_invalid_direction();

    // Circle
    test_circle_in_lifecycle();
    test_circle_out_lifecycle();
    test_circle_type_active();

    // Dissolve
    test_dissolve_lifecycle();
    test_dissolve_type();

    // Pixelate
    test_pixelate_lifecycle();
    test_pixelate_min_block();

    // Interaction
    test_multiple_transitions();
    test_transition_with_existing_effects();
    test_cancel_all_clears_transitions();

    // Edge cases
    test_null_safety();
    test_zero_duration();
    test_draw_null_canvas();
    test_is_finished_initially();
    test_transition_progress_no_transition();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
