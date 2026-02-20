//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTPixelsDrawTests.cpp
// Purpose: Tests for Pixels drawing primitives (SetRGB/GetRGB and Draw* methods).
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_pixels.h"

#include "tests/common/PosixCompat.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// SetRGB / GetRGB
// ============================================================================

static void test_setrgb_getrgb_roundtrip()
{
    void *p = rt_pixels_new(10, 10);
    rt_pixels_set_rgb(p, 5, 5, 0x112233);
    assert(rt_pixels_get_rgb(p, 5, 5) == 0x112233);
    printf("test_setrgb_getrgb_roundtrip: PASSED\n");
}

static void test_setrgb_stores_full_alpha()
{
    // SetRGB should store 0xRRGGBBFF (alpha = 255)
    void *p = rt_pixels_new(4, 4);
    rt_pixels_set_rgb(p, 2, 2, 0xFF0000); // red
    int64_t raw = rt_pixels_get(p, 2, 2); // reads 0xRRGGBBAA
    assert((raw & 0xFF) == 0xFF);         // alpha must be 0xFF
    printf("test_setrgb_stores_full_alpha: PASSED\n");
}

static void test_getrgb_discards_alpha()
{
    // GetRGB should return 0x00RRGGBB regardless of stored alpha
    void *p = rt_pixels_new(4, 4);
    rt_pixels_set(p, 1, 1, 0xABCDEF42); // raw RGBA with alpha 0x42
    int64_t rgb = rt_pixels_get_rgb(p, 1, 1);
    assert(rgb == (int64_t)0xABCDEF); // alpha stripped
    printf("test_getrgb_discards_alpha: PASSED\n");
}

// ============================================================================
// DrawLine
// ============================================================================

static void test_drawline_horizontal()
{
    void *p = rt_pixels_new(20, 20);
    rt_pixels_draw_line(p, 0, 10, 19, 10, 0xFF0000); // red horizontal line
    for (int64_t x = 0; x < 20; x++)
        assert(rt_pixels_get_rgb(p, x, 10) == 0xFF0000);
    // Row above and below should be empty
    assert(rt_pixels_get_rgb(p, 0, 9) == 0);
    assert(rt_pixels_get_rgb(p, 0, 11) == 0);
    printf("test_drawline_horizontal: PASSED\n");
}

static void test_drawline_vertical()
{
    void *p = rt_pixels_new(20, 20);
    rt_pixels_draw_line(p, 5, 0, 5, 19, 0x00FF00); // green vertical line
    for (int64_t y = 0; y < 20; y++)
        assert(rt_pixels_get_rgb(p, 5, y) == 0x00FF00);
    assert(rt_pixels_get_rgb(p, 4, 10) == 0);
    assert(rt_pixels_get_rgb(p, 6, 10) == 0);
    printf("test_drawline_vertical: PASSED\n");
}

static void test_drawline_endpoints_set()
{
    void *p = rt_pixels_new(30, 30);
    rt_pixels_draw_line(p, 2, 3, 27, 18, 0x0000FF);
    // Start and end pixels must be set
    assert(rt_pixels_get_rgb(p, 2, 3) == 0x0000FF);
    assert(rt_pixels_get_rgb(p, 27, 18) == 0x0000FF);
    printf("test_drawline_endpoints_set: PASSED\n");
}

// ============================================================================
// DrawBox
// ============================================================================

static void test_drawbox_fills_all_pixels()
{
    void *p = rt_pixels_new(20, 20);
    rt_pixels_draw_box(p, 2, 3, 5, 4, 0xAABBCC); // 5x4 box at (2,3)
    for (int64_t y = 3; y < 7; y++)
        for (int64_t x = 2; x < 7; x++)
            assert(rt_pixels_get_rgb(p, x, y) == 0xAABBCC);
    // Outside should be clear
    assert(rt_pixels_get_rgb(p, 1, 3) == 0);
    assert(rt_pixels_get_rgb(p, 7, 3) == 0);
    assert(rt_pixels_get_rgb(p, 2, 2) == 0);
    assert(rt_pixels_get_rgb(p, 2, 7) == 0);
    printf("test_drawbox_fills_all_pixels: PASSED\n");
}

static void test_drawbox_clipped()
{
    // Box extending beyond buffer — no crash, only in-bounds pixels set
    void *p = rt_pixels_new(10, 10);
    rt_pixels_draw_box(p, 8, 8, 100, 100, 0x123456);
    assert(rt_pixels_get_rgb(p, 9, 9) == 0x123456); // corner inside
    assert(rt_pixels_get_rgb(p, 7, 7) == 0);         // outside box
    printf("test_drawbox_clipped: PASSED\n");
}

// ============================================================================
// DrawFrame
// ============================================================================

static void test_drawframe_outline_only()
{
    void *p = rt_pixels_new(10, 10);
    rt_pixels_draw_frame(p, 1, 1, 7, 7, 0xFF8800);
    // Corners must be set
    assert(rt_pixels_get_rgb(p, 1, 1) == 0xFF8800);
    assert(rt_pixels_get_rgb(p, 7, 1) == 0xFF8800);
    assert(rt_pixels_get_rgb(p, 1, 7) == 0xFF8800);
    assert(rt_pixels_get_rgb(p, 7, 7) == 0xFF8800);
    // Interior must be clear
    assert(rt_pixels_get_rgb(p, 4, 4) == 0);
    assert(rt_pixels_get_rgb(p, 3, 3) == 0);
    printf("test_drawframe_outline_only: PASSED\n");
}

// ============================================================================
// DrawDisc
// ============================================================================

static void test_drawdisc_center_set()
{
    void *p = rt_pixels_new(30, 30);
    rt_pixels_draw_disc(p, 15, 15, 8, 0x00FF00);
    assert(rt_pixels_get_rgb(p, 15, 15) == 0x00FF00); // center
    assert(rt_pixels_get_rgb(p, 15, 16) == 0x00FF00); // just inside
    printf("test_drawdisc_center_set: PASSED\n");
}

static void test_drawdisc_outside_clear()
{
    void *p = rt_pixels_new(30, 30);
    rt_pixels_draw_disc(p, 15, 15, 5, 0xFF0000);
    // Point clearly outside the disc (radius 5 from center 15,15)
    assert(rt_pixels_get_rgb(p, 15, 21) == 0); // dy=6 > r=5
    assert(rt_pixels_get_rgb(p, 21, 15) == 0);
    printf("test_drawdisc_outside_clear: PASSED\n");
}

// ============================================================================
// DrawRing
// ============================================================================

static void test_drawring_outline_set_interior_clear()
{
    void *p = rt_pixels_new(40, 40);
    rt_pixels_draw_ring(p, 20, 20, 8, 0x8800FF);
    // A point on the ring (approximately at (20, 20-8) = (20, 12))
    assert(rt_pixels_get_rgb(p, 20, 12) == 0x8800FF);
    // Center should be clear
    assert(rt_pixels_get_rgb(p, 20, 20) == 0);
    // Interior (halfway) should be clear
    assert(rt_pixels_get_rgb(p, 20, 15) == 0);
    printf("test_drawring_outline_set_interior_clear: PASSED\n");
}

// ============================================================================
// DrawEllipse
// ============================================================================

static void test_drawellipse_interior_set()
{
    void *p = rt_pixels_new(40, 40);
    rt_pixels_draw_ellipse(p, 20, 20, 10, 5, 0x00AAFF);
    assert(rt_pixels_get_rgb(p, 20, 20) == 0x00AAFF); // center
    assert(rt_pixels_get_rgb(p, 20, 23) == 0x00AAFF); // inside ry=5
    // Outside
    assert(rt_pixels_get_rgb(p, 20, 26) == 0); // dy=6 > ry=5
    printf("test_drawellipse_interior_set: PASSED\n");
}

// ============================================================================
// DrawEllipseFrame
// ============================================================================

static void test_drawellipseframe_outline_set()
{
    void *p = rt_pixels_new(40, 40);
    rt_pixels_draw_ellipse_frame(p, 20, 20, 10, 5, 0xFF5500);
    // Top of ellipse (20, 20-5) = (20, 15)
    assert(rt_pixels_get_rgb(p, 20, 15) == 0xFF5500);
    // Interior should be clear
    assert(rt_pixels_get_rgb(p, 20, 20) == 0);
    printf("test_drawellipseframe_outline_set: PASSED\n");
}

// ============================================================================
// FloodFill
// ============================================================================

static void test_floodfill_fills_region()
{
    void *p = rt_pixels_new(10, 10);
    // Clear canvas is all black (0x000000FF internally, RGB = 0x000000)
    // Fill with red starting from center
    rt_pixels_flood_fill(p, 5, 5, 0xFF0000);
    // Entire canvas should now be red (all pixels were the same color)
    for (int64_t y = 0; y < 10; y++)
        for (int64_t x = 0; x < 10; x++)
            assert(rt_pixels_get_rgb(p, x, y) == 0xFF0000);
    printf("test_floodfill_fills_region: PASSED\n");
}

static void test_floodfill_respects_boundary()
{
    void *p = rt_pixels_new(20, 20);
    // Draw a white box border at (5,5) 10x10
    rt_pixels_draw_frame(p, 5, 5, 10, 10, 0xFFFFFF);
    // Fill inside the box with blue
    rt_pixels_flood_fill(p, 10, 10, 0x0000FF);
    // Interior pixel should be blue
    assert(rt_pixels_get_rgb(p, 10, 10) == 0x0000FF);
    // Border pixel should still be white
    assert(rt_pixels_get_rgb(p, 5, 5) == 0xFFFFFF);
    // Outside the box should still be black
    assert(rt_pixels_get_rgb(p, 1, 1) == 0);
    printf("test_floodfill_respects_boundary: PASSED\n");
}

static void test_floodfill_noop_when_same_color()
{
    void *p = rt_pixels_new(5, 5);
    rt_pixels_draw_box(p, 0, 0, 5, 5, 0xFF0000);
    // Fill with same color — should do nothing, no crash
    rt_pixels_flood_fill(p, 2, 2, 0xFF0000);
    assert(rt_pixels_get_rgb(p, 2, 2) == 0xFF0000);
    printf("test_floodfill_noop_when_same_color: PASSED\n");
}

// ============================================================================
// DrawThickLine
// ============================================================================

static void test_drawthickline_width()
{
    void *p = rt_pixels_new(40, 40);
    // Horizontal thick line with thickness 7 at y=20
    rt_pixels_draw_thick_line(p, 0, 20, 39, 20, 7, 0x804020);
    // Center row
    assert(rt_pixels_get_rgb(p, 20, 20) == 0x804020);
    // Rows within radius (3) of center should be set
    assert(rt_pixels_get_rgb(p, 20, 17) == 0x804020); // 3 rows above center
    // Row beyond radius should be clear
    assert(rt_pixels_get_rgb(p, 20, 24) == 0); // 4 rows below center
    printf("test_drawthickline_width: PASSED\n");
}

// ============================================================================
// DrawTriangle
// ============================================================================

static void test_drawtriangle_interior()
{
    void *p = rt_pixels_new(30, 30);
    // Right triangle with vertices at (5,5), (25,5), (5,25)
    rt_pixels_draw_triangle(p, 5, 5, 25, 5, 5, 25, 0x00CC00);
    // Point on the top edge
    assert(rt_pixels_get_rgb(p, 15, 5) == 0x00CC00);
    // Point inside
    assert(rt_pixels_get_rgb(p, 8, 8) == 0x00CC00);
    // Point outside (bottom-right, past hypotenuse)
    assert(rt_pixels_get_rgb(p, 25, 25) == 0);
    printf("test_drawtriangle_interior: PASSED\n");
}

// ============================================================================
// DrawBezier
// ============================================================================

static void test_drawbezier_endpoints()
{
    void *p = rt_pixels_new(40, 40);
    // Bezier from (2,2) to (37,2) with control at (20,37)
    rt_pixels_draw_bezier(p, 2, 2, 20, 37, 37, 2, 0xCC0000);
    assert(rt_pixels_get_rgb(p, 2, 2) == 0xCC0000);   // start
    assert(rt_pixels_get_rgb(p, 37, 2) == 0xCC0000);  // end
    printf("test_drawbezier_endpoints: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    // SetRGB / GetRGB
    test_setrgb_getrgb_roundtrip();
    test_setrgb_stores_full_alpha();
    test_getrgb_discards_alpha();

    // DrawLine
    test_drawline_horizontal();
    test_drawline_vertical();
    test_drawline_endpoints_set();

    // DrawBox
    test_drawbox_fills_all_pixels();
    test_drawbox_clipped();

    // DrawFrame
    test_drawframe_outline_only();

    // DrawDisc
    test_drawdisc_center_set();
    test_drawdisc_outside_clear();

    // DrawRing
    test_drawring_outline_set_interior_clear();

    // DrawEllipse
    test_drawellipse_interior_set();

    // DrawEllipseFrame
    test_drawellipseframe_outline_set();

    // FloodFill
    test_floodfill_fills_region();
    test_floodfill_respects_boundary();
    test_floodfill_noop_when_same_color();

    // DrawThickLine
    test_drawthickline_width();

    // DrawTriangle
    test_drawtriangle_interior();

    // DrawBezier
    test_drawbezier_endpoints();

    printf("\nAll tests passed!\n");
    return 0;
}
