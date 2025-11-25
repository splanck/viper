/*
 * ViperGFX - Pixel Tests (T4-T6, T14)
 * Tests pixel operations and framebuffer access
 */

#include "test_harness.h"
#include "vgfx.h"

/* T4: Pixel Set/Get */
void test_pixel_set_get(void)
{
    TEST_BEGIN("T4: Pixel Set/Get");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Set pixel to red */
    vgfx_pset(win, 100, 100, 0xFF0000);

    /* Read it back */
    vgfx_color_t color = 0;
    int ok = vgfx_point(win, 100, 100, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0xFF0000);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T5: Out-of-Bounds Write Ignored */
void test_out_of_bounds_write(void)
{
    TEST_BEGIN("T5: Out-of-Bounds Write Ignored");

    vgfx_window_params_t params = {
        .width = 640, .height = 480, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Clear to black */
    vgfx_cls(win, VGFX_BLACK);

    /* Try to write out of bounds */
    vgfx_pset(win, 1000, 1000, 0x00FF00);

    /* Check that in-bounds pixel is still black */
    vgfx_color_t color = 0xFFFFFF; /* Initialize to non-black */
    int ok = vgfx_point(win, 639, 479, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x000000);

    /* Check that OOB read returns 0 */
    ok = vgfx_point(win, 1000, 1000, &color);
    ASSERT_EQ(ok, 0);

    vgfx_destroy_window(win);
    TEST_END();
}

/* T6: Clear Screen */
void test_clear_screen(void)
{
    TEST_BEGIN("T6: Clear Screen");

    vgfx_window_params_t params = {
        .width = 100, .height = 100, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Clear to red */
    vgfx_cls(win, 0xFF0000);

    /* Check all pixels are red */
    vgfx_color_t color = 0;
    for (int32_t y = 0; y < 100; y++)
    {
        for (int32_t x = 0; x < 100; x++)
        {
            int ok = vgfx_point(win, x, y, &color);
            ASSERT_EQ(ok, 1);
            ASSERT_EQ(color, 0xFF0000);
        }
    }

    vgfx_destroy_window(win);
    TEST_END();
}

/* T14: Framebuffer Access */
void test_framebuffer_access(void)
{
    TEST_BEGIN("T14: Framebuffer Access");

    vgfx_window_params_t params = {
        .width = 320, .height = 240, .title = "Test", .fps = 0, .resizable = 0};

    vgfx_window_t win = vgfx_create_window(&params);
    ASSERT_NOT_NULL(win);

    /* Get framebuffer */
    vgfx_framebuffer_t fb = {0};
    int ok = vgfx_get_framebuffer(win, &fb);
    ASSERT_EQ(ok, 1);
    ASSERT_NOT_NULL(fb.pixels);
    ASSERT_EQ(fb.width, 320);
    ASSERT_EQ(fb.height, 240);
    ASSERT_EQ(fb.stride, 320 * 4);

    /* Write directly to framebuffer (set pixel at 50, 50 to green) */
    int32_t x = 50, y = 50;
    uint8_t *pixel = fb.pixels + (y * fb.stride) + (x * 4);
    pixel[0] = 0x00; /* R */
    pixel[1] = 0xFF; /* G */
    pixel[2] = 0x00; /* B */
    pixel[3] = 0xFF; /* A */

    /* Read back via vgfx_point */
    vgfx_color_t color = 0;
    ok = vgfx_point(win, x, y, &color);
    ASSERT_EQ(ok, 1);
    ASSERT_EQ(color, 0x00FF00);

    vgfx_destroy_window(win);
    TEST_END();
}

/* Main test runner */
int main(void)
{
    printf("========================================\n");
    printf("ViperGFX Pixel Tests (T4-T6, T14)\n");
    printf("========================================\n");

    test_pixel_set_get();
    test_out_of_bounds_write();
    test_clear_screen();
    test_framebuffer_access();

    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}
