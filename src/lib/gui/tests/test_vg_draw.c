//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_draw.c
// Purpose: Unit + determinism tests for the shared anti-aliased drawing core
//          (vg_draw). Renders into the mock backend's software framebuffer and
//          asserts exact and hashed pixel output.
// Key invariants:
//   - AA shapes emit intermediate coverage values (not just bg/fg); a hard
//     renderer would fail the "AA present" assertions.
//   - Opaque interiors have no gaps/seams.
//   - The same scene hashes bit-identically (cross-platform determinism guard).
// Links: lib/gui/src/core/vg_draw.c, lib/graphics/src/vgfx_platform_mock.c
//
//===----------------------------------------------------------------------===//

#include "vg_draw.h"
#include "vgfx.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

//=============================================================================
// Tiny test harness
//=============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name)                                                                                  \
    do {                                                                                           \
        int before = g_failed;                                                                     \
        printf("  %-50s", #name "...");                                                            \
        fflush(stdout);                                                                            \
        test_##name();                                                                             \
        if (g_failed == before)                                                                    \
            printf("OK\n");                                                                        \
        g_passed++;                                                                                \
    } while (0)

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL\n    (%s:%d: %s)\n", __FILE__, __LINE__, #cond);                          \
            g_failed++;                                                                            \
            return;                                                                                \
        }                                                                                          \
    } while (0)

//=============================================================================
// Helpers
//=============================================================================

static vgfx_window_t make_win(int w, int h) {
    vgfx_window_params_t p = {.width = w, .height = h, .title = "vg_draw_test", .fps = 0,
                              .resizable = 0};
    vgfx_window_t win = vgfx_create_window(&p);
    if (win)
        vgfx_cls(win, 0x000000);
    return win;
}

static uint32_t px(vgfx_window_t win, int x, int y) {
    vgfx_color_t c = 0;
    vgfx_point(win, x, y, &c);
    return c;
}

/// @brief True when the colour is a grey strictly between black and white
///        (i.e. an anti-aliased coverage value, not a fully on/off pixel).
static int is_mid_grey(uint32_t c) {
    int r = (int)((c >> 16) & 0xFF), g = (int)((c >> 8) & 0xFF), b = (int)(c & 0xFF);
    return r == g && g == b && r > 0 && r < 255;
}

static uint64_t hash_region(vgfx_window_t win, int w, int h) {
    uint64_t hsh = 1469598103934665603ULL; // FNV-1a offset basis
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t c = px(win, x, y);
            for (int s = 0; s < 24; s += 8) {
                hsh ^= (uint64_t)((c >> s) & 0xFF);
                hsh *= 1099511628211ULL; // FNV prime
            }
        }
    }
    return hsh;
}

//=============================================================================
// Tests
//=============================================================================

TEST(rrect_interior_is_solid) {
    vgfx_window_t win = make_win(100, 100);
    CHECK(win != NULL);
    vg_draw_round_rect_fill(win, 20, 20, 60, 40, 8, 0xFFFFFF);
    // Centre and the opaque bars are fully white, no gaps.
    CHECK(px(win, 50, 40) == 0xFFFFFF);
    CHECK(px(win, 27, 27) == 0xFFFFFF); // inner corner (covered)
    CHECK(px(win, 22, 40) == 0xFFFFFF); // left bar
    for (int x = 28; x < 72; ++x)
        CHECK(px(win, x, 20) == 0xFFFFFF); // top edge of centre column
    vgfx_destroy_window(win);
}

TEST(rrect_corners_are_rounded) {
    vgfx_window_t win = make_win(100, 100);
    CHECK(win != NULL);
    vg_draw_round_rect_fill(win, 20, 20, 60, 40, 8, 0xFFFFFF);
    // The extreme corner of the bounding box is rounded away (background).
    CHECK(px(win, 20, 20) == 0x000000);
    CHECK(px(win, 5, 5) == 0x000000); // far outside untouched
    vgfx_destroy_window(win);
}

TEST(rrect_radius_zero_is_hard_rect) {
    vgfx_window_t win = make_win(60, 40);
    CHECK(win != NULL);
    vg_draw_round_rect_fill(win, 10, 10, 30, 20, 0, 0xFFFFFF);
    CHECK(px(win, 10, 10) == 0xFFFFFF); // square corner present
    CHECK(px(win, 39, 29) == 0xFFFFFF);
    // No intermediate coverage anywhere: hard edges only.
    int mids = 0;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 60; ++x)
            if (is_mid_grey(px(win, x, y)))
                mids++;
    CHECK(mids == 0);
    vgfx_destroy_window(win);
}

TEST(disc_is_antialiased) {
    vgfx_window_t win = make_win(100, 100);
    CHECK(win != NULL);
    vg_draw_disc_fill(win, 50, 50, 20, 0xFFFFFF);
    CHECK(px(win, 50, 50) == 0xFFFFFF); // centre solid
    CHECK(px(win, 50, 5) == 0x000000);  // far outside untouched
    // The rim must contain anti-aliased (partial-coverage) pixels.
    int mids = 0;
    for (int y = 25; y <= 75; ++y)
        for (int x = 25; x <= 75; ++x)
            if (is_mid_grey(px(win, x, y)))
                mids++;
    CHECK(mids > 0);
    vgfx_destroy_window(win);
}

TEST(rrect_corner_is_antialiased) {
    vgfx_window_t win = make_win(100, 100);
    CHECK(win != NULL);
    vg_draw_round_rect_fill(win, 20, 20, 60, 40, 10, 0xFFFFFF);
    // Each rounded corner contributes partial-coverage pixels.
    int mids = 0;
    for (int y = 20; y < 32; ++y)
        for (int x = 20; x < 32; ++x)
            if (is_mid_grey(px(win, x, y)))
                mids++;
    CHECK(mids > 0);
    vgfx_destroy_window(win);
}

TEST(line_is_antialiased) {
    vgfx_window_t win = make_win(80, 80);
    CHECK(win != NULL);
    vg_draw_line_aa(win, 10, 10, 70, 50, 2.0f, 0xFFFFFF);
    int mids = 0, lit = 0;
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 80; ++x) {
            uint32_t c = px(win, x, y);
            if (c != 0x000000)
                lit++;
            if (is_mid_grey(c))
                mids++;
        }
    CHECK(lit > 0);  // line drew something
    CHECK(mids > 0); // with anti-aliased edges
    vgfx_destroy_window(win);
}

TEST(circle_stroke_hollow_centre) {
    vgfx_window_t win = make_win(100, 100);
    CHECK(win != NULL);
    vg_draw_circle_stroke(win, 50, 50, 25, 3.0f, 0xFFFFFF);
    CHECK(px(win, 50, 50) == 0x000000); // hollow centre
    int lit = 0;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            if (px(win, x, y) != 0x000000)
                lit++;
    CHECK(lit > 0);
    vgfx_destroy_window(win);
}

TEST(rendering_is_deterministic) {
    vgfx_window_t a = make_win(120, 120);
    vgfx_window_t b = make_win(120, 120);
    CHECK(a != NULL && b != NULL);
    for (int i = 0; i < 2; ++i) {
        vgfx_window_t w = (i == 0) ? a : b;
        vg_draw_round_rect_fill(w, 12, 14, 80, 50, 9, 0x49A6FF);
        vg_draw_round_rect_stroke(w, 12, 14, 80, 50, 9, 2.0f, 0xE7EEF7);
        vg_draw_disc_fill(w, 80, 90, 18, 0x71C784);
        vg_draw_line_aa(w, 5, 5, 110, 100, 1.5f, 0xFF6B6B);
    }
    uint64_t ha = hash_region(a, 120, 120);
    uint64_t hb = hash_region(b, 120, 120);
    CHECK(ha == hb);
    // And it actually drew (not an all-black no-op hash).
    CHECK(ha != hash_region(make_win(120, 120), 120, 120));
    vgfx_destroy_window(a);
    vgfx_destroy_window(b);
}

TEST(gradient_runs_top_to_bottom) {
    vgfx_window_t win = make_win(80, 60);
    CHECK(win != NULL);
    // White at top, black at bottom, square corners for an exact check.
    vg_draw_round_rect_gradient_v(win, 10, 10, 60, 40, 0, 0xFFFFFF, 0x000000);
    CHECK(px(win, 40, 10) == 0xFFFFFF); // top row == top colour
    CHECK(px(win, 40, 49) == 0x000000); // bottom row == bottom colour
    CHECK(is_mid_grey(px(win, 40, 30))); // middle blends
    vgfx_destroy_window(win);
}

TEST(shadow_spreads_beyond_silhouette) {
    vgfx_window_t win = make_win(120, 120);
    CHECK(win != NULL);
    // White shadow on black so it is visible; centred (no offset).
    vg_draw_round_rect_shadow(win, 40, 40, 40, 30, 8, 10.0f, 0, 0, 200, 0xFFFFFF);
    CHECK(px(win, 60, 55) != 0x000000); // lit under the silhouette
    CHECK(px(win, 2, 2) == 0x000000);   // far corner untouched
    // The blur must spread outside the 40..80 x 40..70 silhouette box.
    int outside_lit = 0;
    for (int yy = 30; yy < 90; ++yy)
        for (int xx = 30; xx < 90; ++xx)
            if ((xx < 40 || xx >= 80 || yy < 40 || yy >= 70) && px(win, xx, yy) != 0x000000)
                outside_lit++;
    CHECK(outside_lit > 0);
    vgfx_destroy_window(win);
}

//=============================================================================
// Entry point
//=============================================================================

int main(void) {
    printf("vg_draw anti-aliased core tests\n");
    RUN(rrect_interior_is_solid);
    RUN(rrect_corners_are_rounded);
    RUN(rrect_radius_zero_is_hard_rect);
    RUN(disc_is_antialiased);
    RUN(rrect_corner_is_antialiased);
    RUN(line_is_antialiased);
    RUN(circle_stroke_hollow_centre);
    RUN(gradient_runs_top_to_bottom);
    RUN(shadow_spreads_beyond_silhouette);
    RUN(rendering_is_deterministic);
    printf("\n%d passed, %d failed\n", g_passed - g_failed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
