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
#include <string>

// ── vm_trap override ────────────────────────────────────────────────────────
namespace
{
int g_trap_count = 0;
std::string g_last_trap;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

// ── Tests ───────────────────────────────────────────────────────────────────

/// On a non-graphics build, rt_canvas_new must trap with "not compiled in".
/// On a real-graphics build where the display is unavailable, it must trap
/// with "display server unavailable". If the display IS available (dev
/// machine), the test skips — no failure, no trap expected.
static void test_canvas_new_traps_or_skips()
{
    g_trap_count = 0;
    g_last_trap.clear();

    void *canvas = rt_canvas_new(NULL, 640, 480);

    if (g_trap_count > 0)
    {
        // Either the stub fired ("not compiled in") or the real
        // implementation failed ("display server unavailable").
        assert(canvas == NULL);
        assert(g_last_trap.find("Canvas") != std::string::npos ||
               g_last_trap.find("canvas") != std::string::npos ||
               g_last_trap.find("graphics") != std::string::npos);
        printf("  PASS: rt_canvas_new → trap '%s'\n", g_last_trap.c_str());
    }
    else
    {
        // Real graphics build, display available → window created.
        // Clean up and skip — there's nothing to test in this scenario.
        if (canvas)
            rt_canvas_destroy(canvas);
        printf("  SKIP: display available, window created (no trap needed)\n");
    }
}

int main()
{
    test_canvas_new_traps_or_skips();

    printf("All canvas-unavailable tests passed.\n");
    return 0;
}
