//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_linux_auto_backend.c
// Purpose: Verify Linux AUTO runtime selection through public typed native handles.
// Key invariants: Wayland is preferred and a failed Wayland connection falls back to X11.
// Ownership/Lifetime: Owns and destroys one native window per process.
// Links: src/lib/graphics/src/vgfx_platform_linux_auto.c
//
//===----------------------------------------------------------------------===//

#include "test_harness.h"
#include "vgfx.h"

#include <stdlib.h>
#include <string.h>

static void test_expected_backend(const char *expected) {
    TEST_BEGIN("Linux AUTO selects expected native backend");
    vgfx_window_params_t params = vgfx_window_params_default();
    const int forced_csd = getenv("ZANNA_WAYLAND_FORCE_CSD") != NULL;
    params.width = forced_csd ? 4 : 80;
    params.height = forced_csd ? 2 : 48;
    params.title = "Zanna Linux AUTO Test";
    params.fps = -1;
    vgfx_window_t window = vgfx_create_window(&params);
    ASSERT_NOT_NULL(window);
    if (!window) {
        TEST_END();
        return;
    }

    vgfx_native_handles_t handles;
    ASSERT_EQ(vgfx_get_native_handles(window, &handles), 1);
    if (strcmp(expected, "wayland") == 0) {
        ASSERT_EQ(handles.backend, VGFX_NATIVE_BACKEND_WAYLAND);
        ASSERT_NOT_NULL(handles.surface);
        vgfx_window_capabilities_t capabilities = vgfx_get_window_capabilities(window);
        ASSERT_EQ(capabilities & VGFX_CAP_WINDOW_POSITION, 0);
        ASSERT_EQ(capabilities & VGFX_CAP_CURSOR_WARP, 0);
        ASSERT_TRUE((capabilities & VGFX_CAP_CLIPBOARD_TEXT) != 0);
    } else {
        ASSERT_EQ(handles.backend, VGFX_NATIVE_BACKEND_X11);
        ASSERT_TRUE(handles.window != 0);
        vgfx_window_capabilities_t capabilities = vgfx_get_window_capabilities(window);
        ASSERT_TRUE((capabilities & VGFX_CAP_WINDOW_POSITION) != 0);
        ASSERT_TRUE((capabilities & VGFX_CAP_CURSOR_WARP) != 0);
    }
    ASSERT_NOT_NULL(handles.display);
    ASSERT_EQ(vgfx_update(window), 1);
    ASSERT_EQ(vgfx_pump_events(window), 1);
    vgfx_destroy_window(window);
    TEST_END();
}

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "wayland") != 0 && strcmp(argv[1], "x11") != 0))
        return 2;
    test_expected_backend(argv[1]);
    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}
