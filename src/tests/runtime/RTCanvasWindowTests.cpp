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
#include <cstring>

//=============================================================================
// Null safety tests
//=============================================================================

static void test_set_title_null_canvas() {
    rt_canvas_set_title(nullptr, rt_const_cstr("Test Title"));
    rt_canvas_set_title(nullptr, nullptr);

    rt_string title = rt_canvas_get_title(nullptr);
    assert(title != nullptr);
    assert(std::strcmp(rt_string_cstr(title), "") == 0);
}

static void test_fullscreen_null_canvas() {
    rt_canvas_fullscreen(nullptr);
    rt_canvas_windowed(nullptr);
}

//=============================================================================
// Functional tests (require a real display-backed canvas)
//=============================================================================

static void test_canvas_title() {
    rt_string title = rt_const_cstr("Test Window");
    void *canvas = rt_canvas_new(title, 320, 240);
    if (!canvas) {
        std::printf("SKIP: display-backed canvas unavailable\n");
        return;
    }

    rt_canvas_set_title(canvas, rt_const_cstr("New Title"));
    rt_string new_title = rt_canvas_get_title(canvas);
    assert(new_title != nullptr);
    assert(std::strcmp(rt_string_cstr(new_title), "New Title") == 0);

    rt_canvas_set_title(canvas, rt_const_cstr(""));
    rt_string empty_title = rt_canvas_get_title(canvas);
    assert(empty_title != nullptr);
    assert(std::strcmp(rt_string_cstr(empty_title), "") == 0);

    rt_canvas_destroy(canvas);
}

static void test_canvas_fullscreen_windowed() {
    rt_string title = rt_const_cstr("FS Test");
    void *canvas = rt_canvas_new(title, 320, 240);
    if (!canvas) {
        std::printf("SKIP: display-backed canvas unavailable\n");
        return;
    }

    rt_canvas_fullscreen(canvas);
    rt_canvas_windowed(canvas);
    rt_canvas_fullscreen(canvas);
    rt_canvas_windowed(canvas);
    rt_canvas_fullscreen(canvas);
    rt_canvas_windowed(canvas);

    rt_canvas_destroy(canvas);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    test_set_title_null_canvas();
    test_fullscreen_null_canvas();
    test_canvas_title();
    test_canvas_fullscreen_windowed();
    std::printf("RTCanvasWindowTests passed.\n");
    return 0;
}
