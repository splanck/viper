/*
 * ViperGFX - Drawing Tests (T7-T13)
 * Tests drawing primitives (lines, rectangles, circles)
 */

#include "test_harness.h"
#include "vgfx.h"
#include <math.h>

/* Helper: Count pixels of a given color in window */
static int count_pixels(vgfx_window_t win, int32_t w, int32_t h, vgfx_color_t target)
{
    int count = 0;
    vgfx_color_t color;
    for (int32_t y = 0; y < h; y++)
    {
        for (int32_t x = 0; x < w; x++)
        {
            if (vgfx_point(win, x, y, &color) && color == target)
            {
                count++;
            }
        }
    }
    return count;
}

/* T7: Line Drawing – Horizontal */
void test_line_horizontal(void)
{
    TEST_BEGIN("T7: Line Drawing - Horizontal");

    vgfx_window_params_t params = {
        .width = 200, .height = 200, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_line(win, 10, 10, 50, 10, 0xFFFFFF);

    /* Check pixels on line are white */
    vgfx_color_t color;
    for (int32_t x = 10; x <= 50; x++)
    {
        int ok = vgfx_point(win, x, 10, &color);
        ASSERT_EQ(ok, 1);
        ASSERT_EQ(color, 0xFFFFFF);
    }

    /* Check pixels outside line are black */
    int ok = vgfx_point(win, 9, 10, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    ok = vgfx_point(win, 51, 10, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T8: Line Drawing – Vertical */
void test_line_vertical(void)
{
    TEST_BEGIN("T8: Line Drawing - Vertical");

    vgfx_window_params_t params = {
        .width = 200, .height = 200, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_line(win, 20, 10, 20, 30, 0xFF0000);

    /* Check all pixels on line are red */
    vgfx_color_t color;
    for (int32_t y = 10; y <= 30; y++)
    {
        int ok = vgfx_point(win, 20, y, &color);
        ASSERT_EQ(ok, 1);
        ASSERT_EQ(color, 0xFF0000);
    }

    vgfx_destroy_window(win);
    TEST_END();
}

/* T9: Line Drawing – Diagonal */
void test_line_diagonal(void)
{
    TEST_BEGIN("T9: Line Drawing - Diagonal");

    vgfx_window_params_t params = {
        .width = 200, .height = 200, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_line(win, 0, 0, 10, 10, 0x00FF00);

    /* Check endpoints and midpoint */
    vgfx_color_t color;
    int ok;

    ok = vgfx_point(win, 0, 0, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    ok = vgfx_point(win, 5, 5, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    ok = vgfx_point(win, 10, 10, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    /* Count green pixels - should be at least 8 */
    int green_count = count_pixels(win, 200, 200, 0x00FF00);
    ASSERT_TRUE(green_count >= 8);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T10: Rectangle Outline */
void test_rectangle_outline(void)
{
    TEST_BEGIN("T10: Rectangle Outline");

    vgfx_window_params_t params = {
        .width = 100, .height = 100, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_rect(win, 10, 10, 20, 15, 0xFFFFFF);

    vgfx_color_t color;
    int ok;

    /* Check top edge: x in [10, 30), y = 10 */
    for (int32_t x = 10; x < 30; x++)
    {
        ok = vgfx_point(win, x, 10, &color);
        ASSERT_EQ(ok, 1);
        ASSERT_EQ(color, 0xFFFFFF);
    }

    /* Check bottom edge: x in [10, 30), y = 24 */
    for (int32_t x = 10; x < 30; x++)
    {
        ok = vgfx_point(win, x, 24, &color);
        ASSERT_EQ(ok, 1);
        ASSERT_EQ(color, 0xFFFFFF);
    }

    /* Check left edge: y in [10, 25), x = 10 */
    for (int32_t y = 10; y < 25; y++)
    {
        ok = vgfx_point(win, 10, y, &color);
        ASSERT_EQ(ok, 1);
        ASSERT_EQ(color, 0xFFFFFF);
    }

    /* Check right edge: y in [10, 25), x = 29 */
    for (int32_t y = 10; y < 25; y++)
    {
        ok = vgfx_point(win, 29, y, &color);
        ASSERT_EQ(ok, 1);
        ASSERT_EQ(color, 0xFFFFFF);
    }

    /* Check interior is not filled */
    ok = vgfx_point(win, 15, 15, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T11: Filled Rectangle */
void test_filled_rectangle(void)
{
    TEST_BEGIN("T11: Filled Rectangle");

    vgfx_window_params_t params = {
        .width = 100, .height = 100, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_fill_rect(win, 5, 5, 10, 10, 0xFF0000);

    vgfx_color_t color;
    int ok;

    /* Check all pixels in [5, 15) × [5, 15) are red */
    for (int32_t y = 5; y < 15; y++)
    {
        for (int32_t x = 5; x < 15; x++)
        {
            ok = vgfx_point(win, x, y, &color);
            ASSERT_EQ(ok, 1);
            ASSERT_EQ(color, 0xFF0000);
        }
    }

    /* Check pixels outside are black */
    ok = vgfx_point(win, 4, 5, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    ok = vgfx_point(win, 15, 5, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T12: Circle Outline – Sanity */
void test_circle_outline(void)
{
    TEST_BEGIN("T12: Circle Outline - Sanity");

    vgfx_window_params_t params = {
        .width = 200, .height = 200, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_circle(win, 100, 100, 50, 0xFF0000);

    vgfx_color_t color;
    int ok;

    /* Check cardinal points are red */
    ok = vgfx_point(win, 150, 100, &color); /* East */
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0xFF0000);

    ok = vgfx_point(win, 50, 100, &color); /* West */
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0xFF0000);

    ok = vgfx_point(win, 100, 150, &color); /* South */
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0xFF0000);

    ok = vgfx_point(win, 100, 50, &color); /* North */
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0xFF0000);

    /* Check center is black (outline only) */
    ok = vgfx_point(win, 100, 100, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    /* Count red pixels - should be in approximate perimeter range */
    int red_count = count_pixels(win, 200, 200, 0xFF0000);
    ASSERT_TRUE(red_count >= 200 && red_count <= 400);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T13: Filled Circle – Sanity */
void test_filled_circle(void)
{
    TEST_BEGIN("T13: Filled Circle - Sanity");

    vgfx_window_params_t params = {
        .width = 200, .height = 200, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    vgfx_cls(win, VGFX_BLACK);
    vgfx_fill_circle(win, 100, 100, 30, 0x00FF00);

    vgfx_color_t color;
    int ok;

    /* Check center is green */
    ok = vgfx_point(win, 100, 100, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    /* Check cardinal points at radius 30 are green */
    ok = vgfx_point(win, 130, 100, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    ok = vgfx_point(win, 70, 100, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    ok = vgfx_point(win, 100, 130, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    ok = vgfx_point(win, 100, 70, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    /* Count green pixels - should be approximately π × 30² ≈ 2827 (within ±10%) */
    int green_count = count_pixels(win, 200, 200, 0x00FF00);
    int expected = (int)(3.14159 * 30 * 30); /* ~2827 */
    int tolerance = expected / 10;           /* 10% */
    ASSERT_TRUE(green_count >= expected - tolerance && green_count <= expected + tolerance);

    /* Check pixel outside radius is black */
    ok = vgfx_point(win, 131, 100, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    vgfx_destroy_window(win);
    TEST_END();
}

/* Main test runner */
int main(void)
{
    printf("========================================\n");
    printf("ViperGFX Drawing Tests (T7-T13)\n");
    printf("========================================\n");

    test_line_horizontal();
    test_line_vertical();
    test_line_diagonal();
    test_rectangle_outline();
    test_filled_rectangle();
    test_circle_outline();
    test_filled_circle();

    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}
