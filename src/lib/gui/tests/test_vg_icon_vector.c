//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_icon_vector.c
// Purpose: Unit + determinism tests for the scalable vector icon library.
//          Renders into the mock backend's software framebuffer and asserts
//          registry integrity, anti-aliased coverage, bit-identical repeat
//          rendering (cache on and off), tinting, and brand-mark multi-role
//          output.
// Key invariants:
//   - Every registered icon renders at least one covered pixel at 16px and
//     never writes outside its icon box.
//   - The same draw hashes identically with a cold and a warm cache
//     (cross-platform determinism guard).
// Links: lib/gui/src/core/vg_icon_vector.c
//
//===----------------------------------------------------------------------===//

#include "vg_icon_vector.h"
#include "vgfx.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/// @brief FNV-1a hash of the full framebuffer for determinism comparisons.
static uint64_t framebuffer_hash(vgfx_window_t win) {
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(win, &fb))
        return 0;
    const uint8_t *bytes = (const uint8_t *)fb.pixels;
    size_t byte_count = (size_t)fb.width * (size_t)fb.height * 4u;
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < byte_count; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/// @brief Count pixels differing from the clear color inside/outside a box.
static void count_coverage(vgfx_window_t win,
                           int32_t x,
                           int32_t y,
                           int32_t size,
                           uint32_t clear_rgb,
                           int32_t *inside,
                           int32_t *outside) {
    vgfx_framebuffer_t fb;
    *inside = 0;
    *outside = 0;
    if (!vgfx_get_framebuffer(win, &fb))
        return;
    const uint32_t *pixels = (const uint32_t *)fb.pixels;
    for (int32_t py = 0; py < fb.height; ++py) {
        for (int32_t px = 0; px < fb.width; ++px) {
            uint32_t rgb = pixels[py * fb.width + px] & 0x00FFFFFF;
            if (rgb == (clear_rgb & 0x00FFFFFF))
                continue;
            if (px >= x && px < x + size && py >= y && py < y + size)
                ++(*inside);
            else
                ++(*outside);
        }
    }
}

int main(void) {
    vgfx_window_params_t params = {
        .width = 64, .height = 64, .title = "icon_test", .fps = 0, .resizable = 0};
    vgfx_window_t win = vgfx_create_window(&params);
    if (!win) {
        printf("FAIL could not create mock window\n");
        return 1;
    }

    // Registry integrity: names resolve round-trip and are unique.
    int32_t count = vg_icon_vector_count();
    CHECK(count >= 45, "at least 45 icons registered");
    for (int32_t i = 0; i < count; ++i) {
        const char *name = vg_icon_vector_name(i);
        CHECK(name != NULL && name[0] != '\0', "icon has a name");
        CHECK(vg_icon_vector_find(name) == i, "name resolves to its id");
    }
    CHECK(vg_icon_vector_find("no-such-icon") == VG_ICON_VECTOR_INVALID, "unknown name invalid");
    CHECK(vg_icon_vector_find(NULL) == VG_ICON_VECTOR_INVALID, "NULL name invalid");

    // Every icon renders inside its box (and only there) at a small size.
    for (int32_t i = 0; i < count; ++i) {
        vgfx_cls(win, 0x101010);
        vg_icon_vector_draw(win, i, 24, 24, 16, 0xE0E0E0);
        int32_t inside = 0, outside = 0;
        count_coverage(win, 24, 24, 16, 0x101010, &inside, &outside);
        CHECK(inside > 0, vg_icon_vector_name(i));
        CHECK(outside == 0, "no pixels outside the icon box");
    }

    // Determinism: cold-cache and warm-cache renders hash identically.
    int32_t run_id = vg_icon_vector_find("run");
    CHECK(run_id != VG_ICON_VECTOR_INVALID, "run icon exists");
    vg_icon_vector_cache_clear();
    vgfx_cls(win, 0x101010);
    vg_icon_vector_draw(win, run_id, 8, 8, 32, 0x8CC63F);
    uint64_t cold_hash = framebuffer_hash(win);
    vgfx_cls(win, 0x101010);
    vg_icon_vector_draw(win, run_id, 8, 8, 32, 0x8CC63F);
    uint64_t warm_hash = framebuffer_hash(win);
    vg_icon_vector_cache_clear();
    vgfx_cls(win, 0x101010);
    vg_icon_vector_draw(win, run_id, 8, 8, 32, 0x8CC63F);
    uint64_t cold_again = framebuffer_hash(win);
    CHECK(cold_hash == warm_hash, "warm cache matches cold cache");
    CHECK(cold_hash == cold_again, "repeat cold render is bit-identical");
    CHECK(cold_hash != 0, "render produced output");

    // Anti-aliasing: the play triangle's diagonal must emit partial coverage
    // (pixels that are neither the clear color nor the full tint). The
    // framebuffer stores RGBA bytes.
    {
        vgfx_framebuffer_t fb;
        CHECK(vgfx_get_framebuffer(win, &fb) == 1, "framebuffer accessible");
        const uint8_t *bytes = fb.pixels;
        int32_t partial = 0, full = 0;
        for (int32_t i = 0; i < fb.width * fb.height; ++i) {
            uint8_t r = bytes[i * 4 + 0], g = bytes[i * 4 + 1], b = bytes[i * 4 + 2];
            if (r == 0x10 && g == 0x10 && b == 0x10)
                continue;
            if (r == 0x8C && g == 0xC6 && b == 0x3F)
                ++full;
            else
                ++partial;
        }
        CHECK(partial > 0, "AA intermediate coverage present");
        CHECK(full > 0, "fully covered interior present");
    }

    // Tinting: a different tint changes the output.
    vgfx_cls(win, 0x101010);
    vg_icon_vector_draw(win, run_id, 8, 8, 32, 0x2BC8C4);
    CHECK(framebuffer_hash(win) != cold_hash, "tint changes rendered output");

    // Brand mark renders all three roles regardless of tint. The framebuffer
    // stores RGBA bytes, so exact-color checks compare channel bytes.
    int32_t mark_id = vg_icon_vector_find("zanna-mark");
    CHECK(mark_id != VG_ICON_VECTOR_INVALID, "zanna-mark exists");
    vgfx_cls(win, 0x101010);
    vg_icon_vector_draw(win, mark_id, 8, 8, 48, 0xFFFFFF);
    {
        vgfx_framebuffer_t fb;
        vgfx_get_framebuffer(win, &fb);
        const uint8_t *bytes = fb.pixels;
        int32_t green = 0, steel = 0, teal = 0;
        for (int32_t i = 0; i < fb.width * fb.height; ++i) {
            uint8_t r = bytes[i * 4 + 0], g = bytes[i * 4 + 1], b = bytes[i * 4 + 2];
            if (r == 0x8C && g == 0xC6 && b == 0x3F)
                ++green;
            else if (r == 0xB9 && g == 0xC2 && b == 0xC6)
                ++steel;
            else if (r == 0x2B && g == 0xC8 && b == 0xC4)
                ++teal;
        }
        CHECK(green > 0, "brand mark green stroke present");
        CHECK(steel > 0, "brand mark steel stroke present");
        CHECK(teal > 0, "brand mark teal stroke present");
    }

    // Invalid parameters are safe no-ops.
    vg_icon_vector_draw(win, -1, 0, 0, 16, 0xFFFFFF);
    vg_icon_vector_draw(win, count, 0, 0, 16, 0xFFFFFF);
    vg_icon_vector_draw(win, run_id, 0, 0, 0, 0xFFFFFF);
    vg_icon_vector_draw(NULL, run_id, 0, 0, 16, 0xFFFFFF);
    ++g_passed;

    vgfx_destroy_window(win);
    printf("test_vg_icon_vector: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
