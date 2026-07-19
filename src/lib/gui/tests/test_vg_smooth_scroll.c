//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_smooth_scroll.c
// Purpose: Behavior tests for the shared smooth-scroll easing step — monotone
//          convergence, terminal snapping, direction handling, large-delta
//          clamping, and the process-wide toggle.
// Links: lib/gui/src/core/vg_widget.c (vg_smooth_scroll_step)
//
//===----------------------------------------------------------------------===//

#include "vg_widget.h"
#include <stdio.h>

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, label)                                                                         \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passed;                                                                            \
        } else {                                                                                   \
            ++g_failed;                                                                            \
            printf("FAIL %s (line %d)\n", (label), __LINE__);                                      \
        }                                                                                          \
    } while (0)

int main(void) {
    // Monotone convergence downward onto the target, then snap + stop.
    float position = 0.0f;
    int active_frames = 0;
    while (vg_smooth_scroll_step(&position, 400.0f, 16.0f)) {
        ++active_frames;
        CHECK(position > 0.0f && position <= 400.0f, "position stays inside [start, target]");
        if (active_frames > 1000)
            break;
    }
    CHECK(position == 400.0f, "position snaps exactly onto the target");
    CHECK(active_frames > 3, "easing spans multiple frames");
    CHECK(active_frames < 200, "easing terminates promptly");

    // Upward (negative direction) works symmetrically.
    position = 400.0f;
    int reverse_frames = 0;
    while (vg_smooth_scroll_step(&position, 120.0f, 16.0f) && reverse_frames < 1000)
        ++reverse_frames;
    CHECK(position == 120.0f, "reverse direction snaps onto the target");

    // A near-target position snaps immediately (sub-half-pixel band).
    position = 100.3f;
    CHECK(vg_smooth_scroll_step(&position, 100.0f, 16.0f) == false, "sub-half-pixel snaps");
    CHECK(position == 100.0f, "snap lands exactly");

    // Huge frame deltas clamp to a full step (never overshoot).
    position = 0.0f;
    vg_smooth_scroll_step(&position, 300.0f, 100000.0f);
    CHECK(position == 300.0f, "large delta lands on the target without overshoot");

    // Identical input sequences produce identical trajectories.
    float a = 0.0f, b = 0.0f;
    for (int i = 0; i < 32; ++i) {
        vg_smooth_scroll_step(&a, 640.0f, 16.0f);
        vg_smooth_scroll_step(&b, 640.0f, 16.0f);
        CHECK(a == b, "trajectories identical for identical inputs");
    }

    // Process-wide toggle round-trips.
    vg_set_smooth_scroll_enabled(false);
    CHECK(vg_smooth_scroll_enabled() == false, "toggle off observable");
    vg_set_smooth_scroll_enabled(true);
    CHECK(vg_smooth_scroll_enabled() == true, "toggle on observable");

    // NULL position is a safe no-op.
    CHECK(vg_smooth_scroll_step(NULL, 10.0f, 16.0f) == false, "NULL position no-op");

    printf("test_vg_smooth_scroll: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
