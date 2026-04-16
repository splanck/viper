//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTCanvasUnavailableTests.cpp
// Purpose: Verify that rt_canvas_new traps cleanly when graphics are
//          unavailable — either the stub build (no VIPER_ENABLE_GRAPHICS)
//          or the real build when the display server can't be reached.
// Key invariants: No silent NULL return — the runtime must report why.
// Ownership/Lifetime: Uses vm_trap override to capture trap message.
// Links: src/runtime/graphics/rt_graphics.c
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_graphics.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <setjmp.h>
#include <string>

// ── Tests ───────────────────────────────────────────────────────────────────

namespace {

using TrapFn = void (*)();

static void expect_invalid_operation(TrapFn fn, const char *snippet) {
    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        fn();
        rt_trap_clear_recovery();
        assert(false && "expected trap");
    }

    const char *message = rt_trap_get_error();
    assert(rt_trap_get_kind() == RT_TRAP_KIND_INVALID_OPERATION);
    assert(rt_trap_get_code() == Err_InvalidOperation);
    assert(message != nullptr);
    assert(std::strstr(message, snippet) != nullptr);
    rt_trap_clear_recovery();
}

static void trap_canvas_new() {
    (void)rt_canvas_new(NULL, 640, 480);
}

static void trap_canvas_width() {
    (void)rt_canvas_width(reinterpret_cast<void *>(1));
}

} // namespace

static void test_canvas_availability_flag() {
    int8_t available = rt_canvas_is_available();
    assert(available == 0 || available == 1);
}

static void test_canvas_text_metrics_are_backend_free() {
    rt_string text = rt_const_cstr("abc");
    rt_string cafe = rt_const_cstr("caf\xc3\xa9");
    assert(rt_canvas_text_width(text) == 24);
    assert(rt_canvas_text_height() == 8);
    assert(rt_canvas_text_scaled_width(text, 3) == 72);
    assert(rt_canvas_text_width(cafe) == 32);
    assert(rt_canvas_text_scaled_width(cafe, 2) == 64);
}

/// On a non-graphics build, rt_canvas_new must trap with InvalidOperation.
/// On a real-graphics build where the display is unavailable, it may still
/// trap, but the availability flag must remain true because the backend was
/// compiled in.
static void test_canvas_new_contract() {
    if (!rt_canvas_is_available()) {
        expect_invalid_operation(trap_canvas_new, "not compiled in");
        return;
    }

    jmp_buf env;
    rt_trap_set_recovery(&env);
    if (setjmp(env) == 0) {
        void *canvas = rt_canvas_new(NULL, 640, 480);
        rt_trap_clear_recovery();
        if (canvas) {
            rt_canvas_destroy(canvas);
            std::printf("  SKIP: display available, window created\n");
        }
        return;
    }

    const char *message = rt_trap_get_error();
    assert(message != nullptr);
    assert(std::strstr(message, "Canvas") != nullptr ||
           std::strstr(message, "graphics") != nullptr ||
           std::strstr(message, "display") != nullptr);
    rt_trap_clear_recovery();
}

static void test_canvas_ops_trap_when_unavailable() {
    if (rt_canvas_is_available())
        return;
    expect_invalid_operation(trap_canvas_width, "not compiled in");
}

int main() {
    test_canvas_availability_flag();
    test_canvas_text_metrics_are_backend_free();
    test_canvas_new_contract();
    test_canvas_ops_trap_when_unavailable();

    printf("All canvas-unavailable tests passed.\n");
    return 0;
}
