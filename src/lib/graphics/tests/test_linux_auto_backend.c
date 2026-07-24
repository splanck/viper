//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/test_linux_auto_backend.c
// Purpose: Verify Linux AUTO runtime selection through public typed native handles.
// Key invariants:
//   - Wayland is preferred and a failed Wayland connection falls back to X11.
//   - Empty display environment variables produce a deterministic diagnostic.
//   - Lock-free platform queries remain safe while the first window publishes
//     the process-wide backend selection.
// Ownership/Lifetime:
//   - Owns and destroys one native window per process.
//   - The query thread is stopped and joined before the window is destroyed.
// Links: src/lib/graphics/src/vgfx_platform_linux_auto.c
//
//===----------------------------------------------------------------------===//

#include "test_harness.h"
#include "vgfx.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static atomic_int g_stop_query_thread;

/// @brief Exercise lock-free AUTO dispatch while the main thread initializes a backend.
/// @param context Unused pthread context.
/// @return Always NULL.
static void *query_display_size(void *context) {
    (void)context;
    while (!atomic_load_explicit(&g_stop_query_thread, memory_order_acquire)) {
        int32_t width = 0;
        int32_t height = 0;
        vgfx_get_display_size(&width, &height);
    }
    return NULL;
}

static void test_expected_backend(const char *expected) {
    TEST_BEGIN("Linux AUTO selects expected native backend");
    vgfx_window_params_t params = vgfx_window_params_default();
    const int forced_csd = getenv("ZANNA_WAYLAND_FORCE_CSD") != NULL;
    params.width = forced_csd ? 4 : 80;
    params.height = forced_csd ? 2 : 48;
    params.title = "Zanna Linux AUTO Test";
    params.fps = -1;
    atomic_store_explicit(&g_stop_query_thread, 0, memory_order_relaxed);
    pthread_t query_thread;
    const int query_started = pthread_create(&query_thread, NULL, query_display_size, NULL) == 0;
    ASSERT_TRUE(query_started);
    vgfx_window_t window = vgfx_create_window(&params);
    atomic_store_explicit(&g_stop_query_thread, 1, memory_order_release);
    if (query_started)
        ASSERT_EQ(pthread_join(query_thread, NULL), 0);
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
    if (strcmp(expected, "wayland") == 0) {
        vgfx_maximize(window);
        ASSERT_EQ(vgfx_pump_events(window), 1);
        vgfx_restore(window);
        ASSERT_EQ(vgfx_pump_events(window), 1);
    }
    vgfx_destroy_window(window);
    TEST_END();
}

static void test_empty_display_environment(void) {
    TEST_BEGIN("Linux AUTO rejects empty display environment variables");
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    const char *x11_display = getenv("DISPLAY");
    ASSERT_TRUE(wayland_display != NULL && wayland_display[0] == '\0');
    ASSERT_TRUE(x11_display != NULL && x11_display[0] == '\0');

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = 80;
    params.height = 48;
    params.title = "Zanna Linux AUTO No Display Test";
    vgfx_window_t window = vgfx_create_window(&params);
    ASSERT_NULL(window);
    ASSERT_EQ(vgfx_last_error_code(), VGFX_ERR_PLATFORM);
    const char *error = vgfx_get_last_error();
    ASSERT_NOT_NULL(error);
    ASSERT_TRUE(error && strstr(error, "WAYLAND_DISPLAY is unset") != NULL);
    TEST_END();
}

int main(int argc, char **argv) {
    if (argc != 2 ||
        (strcmp(argv[1], "wayland") != 0 && strcmp(argv[1], "x11") != 0 &&
         strcmp(argv[1], "none") != 0))
        return 2;
    if (strcmp(argv[1], "none") == 0)
        test_empty_display_environment();
    else
        test_expected_backend(argv[1]);
    TEST_SUMMARY();
    return TEST_RETURN_CODE();
}
