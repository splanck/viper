//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCanvasExtTests.cpp
// Purpose: Tests for extended Canvas drawing primitives (Phase 3).
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_internal.h"

#include <cassert>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Color Function Tests
// ============================================================================

static void test_color_rgb_basic()
{
    // Red: 255, 0, 0 -> 0x00FF0000
    int64_t red = rt_color_rgb(255, 0, 0);
    assert(red == 0x00FF0000);

    // Green: 0, 255, 0 -> 0x0000FF00
    int64_t green = rt_color_rgb(0, 255, 0);
    assert(green == 0x0000FF00);

    // Blue: 0, 0, 255 -> 0x000000FF
    int64_t blue = rt_color_rgb(0, 0, 255);
    assert(blue == 0x000000FF);

    // White: 255, 255, 255 -> 0x00FFFFFF
    int64_t white = rt_color_rgb(255, 255, 255);
    assert(white == 0x00FFFFFF);

    // Black: 0, 0, 0 -> 0x00000000
    int64_t black = rt_color_rgb(0, 0, 0);
    assert(black == 0x00000000);

    printf("test_color_rgb_basic: PASSED\n");
}

static void test_color_rgb_clamping()
{
    // Test clamping of out-of-range values
    int64_t clamped_high = rt_color_rgb(300, 400, 500);
    assert(clamped_high == 0x00FFFFFF); // All clamped to 255

    int64_t clamped_low = rt_color_rgb(-10, -20, -30);
    assert(clamped_low == 0x00000000); // All clamped to 0

    // Mixed clamping
    int64_t mixed = rt_color_rgb(-10, 128, 300);
    assert(mixed == 0x000080FF); // 0, 128, 255

    printf("test_color_rgb_clamping: PASSED\n");
}

static void test_color_rgba_basic()
{
    // Red with full alpha
    int64_t red_opaque = rt_color_rgba(255, 0, 0, 255);
    assert(red_opaque == (int64_t)0xFFFF0000);

    // Green with half alpha
    int64_t green_half = rt_color_rgba(0, 255, 0, 128);
    assert(green_half == (int64_t)0x8000FF00);

    // Blue with no alpha (transparent)
    int64_t blue_transparent = rt_color_rgba(0, 0, 255, 0);
    assert(blue_transparent == (int64_t)0x000000FF);

    printf("test_color_rgba_basic: PASSED\n");
}

static void test_color_rgba_clamping()
{
    // Test clamping of out-of-range values including alpha
    int64_t clamped = rt_color_rgba(300, -10, 128, 400);
    // r=255, g=0, b=128, a=255 -> 0xFF00FF80 (but stored as 0xAARRGGBB)
    // Actually: 0xFFFF0080
    assert(clamped == (int64_t)0xFFFF0080);

    printf("test_color_rgba_clamping: PASSED\n");
}

// ============================================================================
// Null Canvas Safety Tests
// These test that functions don't crash when passed NULL canvas pointers.
// ============================================================================

static void test_thick_line_null_safety()
{
    // Should not crash with NULL canvas
    rt_canvas_thick_line(nullptr, 0, 0, 100, 100, 5, 0x00FFFFFF);
    printf("test_thick_line_null_safety: PASSED\n");
}

static void test_round_box_null_safety()
{
    rt_canvas_round_box(nullptr, 10, 10, 100, 50, 10, 0x00FF0000);
    printf("test_round_box_null_safety: PASSED\n");
}

static void test_round_frame_null_safety()
{
    rt_canvas_round_frame(nullptr, 10, 10, 100, 50, 10, 0x0000FF00);
    printf("test_round_frame_null_safety: PASSED\n");
}

static void test_flood_fill_null_safety()
{
    rt_canvas_flood_fill(nullptr, 50, 50, 0x000000FF);
    printf("test_flood_fill_null_safety: PASSED\n");
}

static void test_triangle_null_safety()
{
    rt_canvas_triangle(nullptr, 10, 10, 50, 100, 90, 10, 0x00FFFF00);
    printf("test_triangle_null_safety: PASSED\n");
}

static void test_triangle_frame_null_safety()
{
    rt_canvas_triangle_frame(nullptr, 10, 10, 50, 100, 90, 10, 0x00FF00FF);
    printf("test_triangle_frame_null_safety: PASSED\n");
}

static void test_ellipse_null_safety()
{
    rt_canvas_ellipse(nullptr, 100, 100, 50, 30, 0x0000FFFF);
    printf("test_ellipse_null_safety: PASSED\n");
}

static void test_ellipse_frame_null_safety()
{
    rt_canvas_ellipse_frame(nullptr, 100, 100, 50, 30, 0x00808080);
    printf("test_ellipse_frame_null_safety: PASSED\n");
}

// ============================================================================
// Phase 4: Advanced Curves & Shapes - Null Safety Tests
// ============================================================================

static void test_arc_null_safety()
{
    rt_canvas_arc(nullptr, 100, 100, 50, 0, 90, 0x00FF0000);
    printf("test_arc_null_safety: PASSED\n");
}

static void test_arc_frame_null_safety()
{
    rt_canvas_arc_frame(nullptr, 100, 100, 50, 0, 90, 0x0000FF00);
    printf("test_arc_frame_null_safety: PASSED\n");
}

static void test_bezier_null_safety()
{
    rt_canvas_bezier(nullptr, 10, 10, 50, 100, 100, 10, 0x000000FF);
    printf("test_bezier_null_safety: PASSED\n");
}

static void test_polyline_null_safety()
{
    int64_t points[] = {10, 10, 50, 50, 100, 10};
    rt_canvas_polyline(nullptr, points, 3, 0x00FFFF00);
    printf("test_polyline_null_safety: PASSED\n");
}

static void test_polygon_null_safety()
{
    int64_t points[] = {50, 10, 10, 90, 90, 90};
    rt_canvas_polygon(nullptr, points, 3, 0x00FF00FF);
    printf("test_polygon_null_safety: PASSED\n");
}

static void test_polygon_frame_null_safety()
{
    int64_t points[] = {50, 10, 10, 90, 90, 90};
    rt_canvas_polygon_frame(nullptr, points, 3, 0x0000FFFF);
    printf("test_polygon_frame_null_safety: PASSED\n");
}

// ============================================================================
// Phase 5: Canvas Utilities - Null Safety Tests
// ============================================================================

static void test_get_pixel_null_safety()
{
    int64_t result = rt_canvas_get_pixel(nullptr, 50, 50);
    assert(result == 0); // Should return 0 for null canvas
    printf("test_get_pixel_null_safety: PASSED\n");
}

static void test_copy_rect_null_safety()
{
    void *result = rt_canvas_copy_rect(nullptr, 0, 0, 100, 100);
    assert(result == NULL); // Should return NULL for null canvas
    printf("test_copy_rect_null_safety: PASSED\n");
}

static void test_save_bmp_null_safety()
{
    int64_t result = rt_canvas_save_bmp(nullptr, (rt_string) "test.bmp");
    assert(result == 0); // Should return 0 for null canvas
    printf("test_save_bmp_null_safety: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== RTCanvasExtTests (Phase 3-5 Extended Drawing) ===\n\n");

    // Color function tests
    printf("--- Color Functions ---\n");
    test_color_rgb_basic();
    test_color_rgb_clamping();
    test_color_rgba_basic();
    test_color_rgba_clamping();

    // Phase 3: Null safety tests for extended primitives
    printf("\n--- Phase 3: Extended Primitives ---\n");
    test_thick_line_null_safety();
    test_round_box_null_safety();
    test_round_frame_null_safety();
    test_flood_fill_null_safety();
    test_triangle_null_safety();
    test_triangle_frame_null_safety();
    test_ellipse_null_safety();
    test_ellipse_frame_null_safety();

    // Phase 4: Advanced curves and shapes
    printf("\n--- Phase 4: Advanced Curves & Shapes ---\n");
    test_arc_null_safety();
    test_arc_frame_null_safety();
    test_bezier_null_safety();
    test_polyline_null_safety();
    test_polygon_null_safety();
    test_polygon_frame_null_safety();

    // Phase 5: Canvas utilities
    printf("\n--- Phase 5: Canvas Utilities ---\n");
    test_get_pixel_null_safety();
    test_copy_rect_null_safety();
    test_save_bmp_null_safety();

    printf("\n=== All RTCanvasExtTests passed! ===\n");
    return 0;
}
