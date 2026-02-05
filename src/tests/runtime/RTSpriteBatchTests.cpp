//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTSpriteBatchTests.cpp
// Purpose: Tests for Viper.Graphics.SpriteBatch.
//
//===----------------------------------------------------------------------===//

#include "rt_spritebatch.h"
#include "tests/common/PosixCompat.h"

#include <cassert>
#include <cstdio>

// Stub for rt_abort that's used by runtime
extern "C" void rt_abort(const char *msg);

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// SpriteBatch Creation Tests
// ============================================================================

static void test_spritebatch_new_default()
{
    void *batch = rt_spritebatch_new(0);
    assert(batch != nullptr);

    assert(rt_spritebatch_count(batch) == 0);
    assert(rt_spritebatch_capacity(batch) > 0);
    assert(rt_spritebatch_is_active(batch) == 0);

    printf("test_spritebatch_new_default: PASSED\n");
}

static void test_spritebatch_new_capacity()
{
    void *batch = rt_spritebatch_new(512);
    assert(batch != nullptr);
    assert(rt_spritebatch_capacity(batch) >= 512);

    printf("test_spritebatch_new_capacity: PASSED\n");
}

// ============================================================================
// SpriteBatch Begin/End Tests
// ============================================================================

static void test_spritebatch_begin()
{
    void *batch = rt_spritebatch_new(0);

    assert(rt_spritebatch_is_active(batch) == 0);

    rt_spritebatch_begin(batch);
    assert(rt_spritebatch_is_active(batch) == 1);

    printf("test_spritebatch_begin: PASSED\n");
}

static void test_spritebatch_begin_clears_count()
{
    void *batch = rt_spritebatch_new(0);

    // First batch
    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_pixels(batch, (void *)1, 0, 0); // Dummy pixels
    rt_spritebatch_draw_pixels(batch, (void *)2, 10, 10);
    assert(rt_spritebatch_count(batch) == 2);

    // Second begin should clear
    rt_spritebatch_begin(batch);
    assert(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_begin_clears_count: PASSED\n");
}

// ============================================================================
// SpriteBatch Draw Tests
// ============================================================================

static void test_spritebatch_draw_increments_count()
{
    void *batch = rt_spritebatch_new(0);

    rt_spritebatch_begin(batch);
    assert(rt_spritebatch_count(batch) == 0);

    rt_spritebatch_draw_pixels(batch, (void *)1, 0, 0);
    assert(rt_spritebatch_count(batch) == 1);

    rt_spritebatch_draw_pixels(batch, (void *)2, 10, 10);
    assert(rt_spritebatch_count(batch) == 2);

    rt_spritebatch_draw_pixels(batch, (void *)3, 20, 20);
    assert(rt_spritebatch_count(batch) == 3);

    printf("test_spritebatch_draw_increments_count: PASSED\n");
}

static void test_spritebatch_draw_not_active()
{
    void *batch = rt_spritebatch_new(0);

    // Without begin, draw should not add
    rt_spritebatch_draw_pixels(batch, (void *)1, 0, 0);
    assert(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_draw_not_active: PASSED\n");
}

static void test_spritebatch_draw_null()
{
    void *batch = rt_spritebatch_new(0);

    rt_spritebatch_begin(batch);

    // Drawing null should not add
    rt_spritebatch_draw_pixels(batch, nullptr, 0, 0);
    assert(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_draw_null: PASSED\n");
}

// ============================================================================
// SpriteBatch Settings Tests
// ============================================================================

static void test_spritebatch_settings()
{
    void *batch = rt_spritebatch_new(0);

    // Test sort by depth
    rt_spritebatch_set_sort_by_depth(batch, 1);
    // No getter, but should not crash

    // Test tint
    rt_spritebatch_set_tint(batch, 0xFF0000FF); // Red

    // Test alpha
    rt_spritebatch_set_alpha(batch, 128);

    // Test reset
    rt_spritebatch_reset_settings(batch);

    printf("test_spritebatch_settings: PASSED\n");
}

static void test_spritebatch_alpha_clamp()
{
    void *batch = rt_spritebatch_new(0);

    // Test alpha clamping (no direct getter, but should not crash)
    rt_spritebatch_set_alpha(batch, -100);
    rt_spritebatch_set_alpha(batch, 500);
    rt_spritebatch_set_alpha(batch, 0);
    rt_spritebatch_set_alpha(batch, 255);

    printf("test_spritebatch_alpha_clamp: PASSED\n");
}

// ============================================================================
// SpriteBatch Capacity Tests
// ============================================================================

static void test_spritebatch_grow()
{
    void *batch = rt_spritebatch_new(4);

    rt_spritebatch_begin(batch);

    // Add more than initial capacity
    for (int i = 0; i < 20; i++)
    {
        rt_spritebatch_draw_pixels(batch, (void *)(intptr_t)(i + 1), i * 10, i * 10);
    }

    assert(rt_spritebatch_count(batch) == 20);
    assert(rt_spritebatch_capacity(batch) >= 20);

    printf("test_spritebatch_grow: PASSED\n");
}

// ============================================================================
// SpriteBatch Region Draw Tests
// ============================================================================

static void test_spritebatch_draw_region()
{
    void *batch = rt_spritebatch_new(0);

    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_region(batch, (void *)1, 0, 0, 10, 10, 32, 32);
    assert(rt_spritebatch_count(batch) == 1);

    printf("test_spritebatch_draw_region: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("Running SpriteBatch tests...\n\n");

    // Creation tests
    test_spritebatch_new_default();
    test_spritebatch_new_capacity();

    // Begin/End tests
    test_spritebatch_begin();
    test_spritebatch_begin_clears_count();

    // Draw tests
    test_spritebatch_draw_increments_count();
    test_spritebatch_draw_not_active();
    test_spritebatch_draw_null();

    // Settings tests
    test_spritebatch_settings();
    test_spritebatch_alpha_clamp();

    // Capacity tests
    test_spritebatch_grow();

    // Region draw tests
    test_spritebatch_draw_region();

    printf("\nAll SpriteBatch tests passed!\n");
    return 0;
}
