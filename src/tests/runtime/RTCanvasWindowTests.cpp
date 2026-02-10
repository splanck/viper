//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTCanvasWindowTests.cpp
// Purpose: Validate canvas window management (title, fullscreen, windowed).
// Key invariants: Functions are null-safe, bridge to vgfx correctly.
//
//===----------------------------------------------------------------------===//

#include "rt_graphics.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Null safety tests (no canvas created — just verify no crash)
//=============================================================================

static void test_set_title_null_canvas()
{
    printf("\nTesting set_title with NULL canvas:\n");

    // Should not crash with NULL canvas
    rt_canvas_set_title(nullptr, rt_const_cstr("Test Title"));
    test_result("set_title(NULL, title) does not crash", true);

    // Should not crash with NULL title
    rt_canvas_set_title(nullptr, nullptr);
    test_result("set_title(NULL, NULL) does not crash", true);
}

static void test_fullscreen_null_canvas()
{
    printf("\nTesting fullscreen/windowed with NULL canvas:\n");

    rt_canvas_fullscreen(nullptr);
    test_result("fullscreen(NULL) does not crash", true);

    rt_canvas_windowed(nullptr);
    test_result("windowed(NULL) does not crash", true);
}

//=============================================================================
// Functional tests (requires mock graphics backend)
//=============================================================================

static void test_canvas_title()
{
    printf("\nTesting canvas set_title:\n");

    rt_string title = rt_const_cstr("Test Window");
    void *canvas = rt_canvas_new(title, 320, 240);
    if (!canvas)
    {
        printf("  (skipped - canvas creation not available)\n");
        return;
    }

    // Set title should not crash
    rt_canvas_set_title(canvas, rt_const_cstr("New Title"));
    test_result("set_title succeeds on valid canvas", true);

    // Set title with empty string
    rt_canvas_set_title(canvas, rt_const_cstr(""));
    test_result("set_title with empty string succeeds", true);
}

static void test_canvas_fullscreen_windowed()
{
    printf("\nTesting canvas fullscreen/windowed:\n");

    rt_string title = rt_const_cstr("FS Test");
    void *canvas = rt_canvas_new(title, 320, 240);
    if (!canvas)
    {
        printf("  (skipped - canvas creation not available)\n");
        return;
    }

    // Fullscreen should not crash
    rt_canvas_fullscreen(canvas);
    test_result("fullscreen succeeds on valid canvas", true);

    // Windowed should not crash
    rt_canvas_windowed(canvas);
    test_result("windowed succeeds on valid canvas", true);

    // Multiple toggles
    rt_canvas_fullscreen(canvas);
    rt_canvas_windowed(canvas);
    rt_canvas_fullscreen(canvas);
    rt_canvas_windowed(canvas);
    test_result("multiple fullscreen/windowed toggles succeed", true);
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    printf("=== Canvas Window Management Tests ===\n");

    // Null safety tests (always run)
    test_set_title_null_canvas();
    test_fullscreen_null_canvas();

    // Functional tests (may skip if no graphics backend)
    test_canvas_title();
    test_canvas_fullscreen_windowed();

    printf("\nAll canvas window tests passed.\n");
    return 0;
}
