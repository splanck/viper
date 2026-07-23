//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_wayland_backend.c
// Purpose: Exercise native Wayland through the public ZannaGFX window API.
// Key invariants:
//   - A live compositor produces typed Wayland display/surface handles.
//   - A complete software framebuffer can be presented and destroyed safely.
// Ownership/Lifetime: The test owns its one window and retains no native handles after destroy.
// Links: src/lib/graphics/src/vgfx_platform_wayland.c
//
//===----------------------------------------------------------------------===//

#include "test_harness.h"
#include "vgfx.h"

#include <stdlib.h>
#include <string.h>

static int32_t framebuffer_extent(int32_t logical, float scale) {
    double exact = (double)logical * (double)scale;
    int32_t result = (int32_t)exact;
    return (double)result < exact ? result + 1 : result;
}

static void test_public_wayland_window_lifecycle(void) {
    TEST_BEGIN("Wayland backend: public window and framebuffer lifecycle");

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = 96;
    params.height = 64;
    params.title = "Zanna Wayland Backend Test";
    params.fps = -1;
    vgfx_window_t window = vgfx_create_window(&params);

    if (!getenv("WAYLAND_DISPLAY")) {
        ASSERT_NULL(window);
        const char *error = vgfx_get_last_error();
        ASSERT_NOT_NULL(error);
        ASSERT_TRUE(strstr(error, "Wayland backend unavailable:") != NULL);
        TEST_END();
        return;
    }

    ASSERT_NOT_NULL(window);
    vgfx_native_handles_t handles;
    ASSERT_EQ(vgfx_get_native_handles(window, &handles), 1);
    ASSERT_EQ(handles.backend, VGFX_NATIVE_BACKEND_WAYLAND);
    ASSERT_NOT_NULL(handles.display);
    ASSERT_NOT_NULL(handles.surface);
    ASSERT_EQ(handles.window, 0);
    vgfx_window_capabilities_t capabilities = vgfx_get_window_capabilities(window);
    ASSERT_EQ(capabilities & VGFX_CAP_WINDOW_POSITION, 0);
    ASSERT_EQ(capabilities & VGFX_CAP_CURSOR_WARP, 0);
    ASSERT_TRUE((capabilities & VGFX_CAP_CLIPBOARD_TEXT) != 0);

    ASSERT_EQ(vgfx_pump_events(window), 1);
    float scale = vgfx_window_get_scale(window);
    ASSERT_TRUE(scale >= 1.0f);

    vgfx_set_cursor(window, VGFX_CURSOR_TEXT);
    vgfx_set_cursor_visible(window, 0);
    vgfx_set_cursor_visible(window, 1);
    vgfx_set_cursor(window, VGFX_CURSOR_DEFAULT);

    int32_t relative_native = vgfx_set_relative_mouse(window, 1);
    if (relative_native)
        ASSERT_EQ(vgfx_relative_mouse_native(window), 1);
    ASSERT_EQ(vgfx_set_relative_mouse(window, 0), 0);
    ASSERT_EQ(vgfx_relative_mouse_native(window), 0);
    vgfx_focus(window);
    vgfx_request_foreground(window);
    vgfx_maximize(window);
    ASSERT_EQ(vgfx_pump_events(window), 1);
    vgfx_restore(window);
    ASSERT_EQ(vgfx_pump_events(window), 1);

    vgfx_framebuffer_t framebuffer;
    ASSERT_EQ(vgfx_get_framebuffer(window, &framebuffer), 1);
    ASSERT_EQ(framebuffer.width, framebuffer_extent(96, scale));
    ASSERT_EQ(framebuffer.height, framebuffer_extent(64, scale));
    ASSERT_NOT_NULL(framebuffer.pixels);
    for (int32_t y = 0; y < framebuffer.height; ++y) {
        for (int32_t x = 0; x < framebuffer.width; ++x) {
            size_t offset = (size_t)y * (size_t)framebuffer.stride + (size_t)x * 4u;
            framebuffer.pixels[offset] = (uint8_t)(x * 255 / framebuffer.width);
            framebuffer.pixels[offset + 1] = (uint8_t)(y * 255 / framebuffer.height);
            framebuffer.pixels[offset + 2] = 0xA0;
            framebuffer.pixels[offset + 3] = 0xFF;
        }
    }
    ASSERT_EQ(vgfx_update(window), 1);
    ASSERT_EQ(vgfx_pump_events(window), 1);

    const char *clipboard_fixture = "Zanna native Wayland clipboard ✓";
    vgfx_clipboard_set_text(clipboard_fixture);
    ASSERT_EQ(vgfx_clipboard_has_format(VGFX_CLIPBOARD_TEXT), 1);
    char *clipboard_text = vgfx_clipboard_get_text();
    ASSERT_NOT_NULL(clipboard_text);
    ASSERT_TRUE(strcmp(clipboard_text, clipboard_fixture) == 0);
    free(clipboard_text);
    vgfx_clipboard_clear();

    vgfx_destroy_window(window);
    TEST_END();
}

int main(void) {
    test_public_wayland_window_lifecycle();
    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}
