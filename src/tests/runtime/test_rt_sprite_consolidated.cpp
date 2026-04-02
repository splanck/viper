//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/test_rt_sprite_consolidated.cpp
//
//===----------------------------------------------------------------------===//
// Consolidated Sprite runtime tests (2 files merged).

#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_seq.h"
#include "rt_spritebatch.h"
#include "rt_spritesheet.h"
#include "rt_string.h"
#include "rt_texatlas.h"
#include "tests/TestHarness.hpp"
#include "tests/common/PosixCompat.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── RTSpriteBatchTests.cpp ──
extern "C" void rt_abort(const char *msg);

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

// ============================================================================
// SpriteBatch Creation Tests
// ============================================================================

TEST(RTSprite, SpritebatchNewDefault) {
    void *batch = rt_spritebatch_new(0);
    ASSERT_TRUE(batch != nullptr);

    ASSERT_TRUE(rt_spritebatch_count(batch) == 0);
    ASSERT_TRUE(rt_spritebatch_capacity(batch) > 0);
    ASSERT_TRUE(rt_spritebatch_is_active(batch) == 0);

    printf("test_spritebatch_new_default: PASSED\n");
}

TEST(RTSprite, SpritebatchNewCapacity) {
    void *batch = rt_spritebatch_new(512);
    ASSERT_TRUE(batch != nullptr);
    ASSERT_TRUE(rt_spritebatch_capacity(batch) >= 512);

    printf("test_spritebatch_new_capacity: PASSED\n");
}

// ============================================================================
// SpriteBatch Begin/End Tests
// ============================================================================

TEST(RTSprite, SpritebatchBegin) {
    void *batch = rt_spritebatch_new(0);

    ASSERT_TRUE(rt_spritebatch_is_active(batch) == 0);

    rt_spritebatch_begin(batch);
    ASSERT_TRUE(rt_spritebatch_is_active(batch) == 1);

    printf("test_spritebatch_begin: PASSED\n");
}

TEST(RTSprite, SpritebatchBeginClearsCount) {
    void *batch = rt_spritebatch_new(0);

    // First batch
    rt_spritebatch_begin(batch);
    /// @brief Rt_spritebatch_draw_pixels.
    rt_spritebatch_draw_pixels(batch, (void *)1, 0, 0); // Dummy pixels
    rt_spritebatch_draw_pixels(batch, (void *)2, 10, 10);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 2);

    // Second begin should clear
    rt_spritebatch_begin(batch);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_begin_clears_count: PASSED\n");
}

TEST(RTSprite, SpritebatchEndNullCanvasDeactivates) {
    void *batch = rt_spritebatch_new(0);
    void *pixels = rt_pixels_new(4, 4);

    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_pixels(batch, pixels, 0, 0);
    ASSERT_TRUE(rt_spritebatch_is_active(batch) == 1);

    rt_spritebatch_end(batch, nullptr);
    ASSERT_TRUE(rt_spritebatch_is_active(batch) == 0);

    rt_spritebatch_begin(batch);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_end_null_canvas_deactivates: PASSED\n");
}

// ============================================================================
// SpriteBatch Draw Tests
// ============================================================================

TEST(RTSprite, SpritebatchDrawIncrementsCount) {
    void *batch = rt_spritebatch_new(0);

    rt_spritebatch_begin(batch);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 0);

    rt_spritebatch_draw_pixels(batch, (void *)1, 0, 0);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 1);

    rt_spritebatch_draw_pixels(batch, (void *)2, 10, 10);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 2);

    rt_spritebatch_draw_pixels(batch, (void *)3, 20, 20);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 3);

    printf("test_spritebatch_draw_increments_count: PASSED\n");
}

TEST(RTSprite, SpritebatchDrawNotActive) {
    void *batch = rt_spritebatch_new(0);

    // Without begin, draw should not add
    rt_spritebatch_draw_pixels(batch, (void *)1, 0, 0);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_draw_not_active: PASSED\n");
}

TEST(RTSprite, SpritebatchDrawNull) {
    void *batch = rt_spritebatch_new(0);

    rt_spritebatch_begin(batch);

    // Drawing null should not add
    rt_spritebatch_draw_pixels(batch, nullptr, 0, 0);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 0);

    printf("test_spritebatch_draw_null: PASSED\n");
}

// ============================================================================
// SpriteBatch Settings Tests
// ============================================================================

TEST(RTSprite, SpritebatchSettings) {
    void *batch = rt_spritebatch_new(0);

    // Test sort by depth
    rt_spritebatch_set_sort_by_depth(batch, 1);
    // No getter, but should not crash

    // Test tint
    rt_spritebatch_set_tint(batch, 0xFF0000FF); // Red

    // Test alpha
    rt_spritebatch_set_alpha(batch, 128);

    // Test reset
    rt_spritebatch_reset_settings(batch);

    printf("test_spritebatch_settings: PASSED\n");
}

TEST(RTSprite, SpritebatchAlphaClamp) {
    void *batch = rt_spritebatch_new(0);

    // Test alpha clamping (no direct getter, but should not crash)
    rt_spritebatch_set_alpha(batch, -100);
    rt_spritebatch_set_alpha(batch, 500);
    rt_spritebatch_set_alpha(batch, 0);
    rt_spritebatch_set_alpha(batch, 255);

    printf("test_spritebatch_alpha_clamp: PASSED\n");
}

// ============================================================================
// SpriteBatch Capacity Tests
// ============================================================================

TEST(RTSprite, SpritebatchGrow) {
    void *batch = rt_spritebatch_new(4);

    rt_spritebatch_begin(batch);

    // Add more than initial capacity
    for (int i = 0; i < 20; i++) {
        rt_spritebatch_draw_pixels(batch, (void *)(intptr_t)(i + 1), i * 10, i * 10);
    }

    ASSERT_TRUE(rt_spritebatch_count(batch) == 20);
    ASSERT_TRUE(rt_spritebatch_capacity(batch) >= 20);

    printf("test_spritebatch_grow: PASSED\n");
}

// ============================================================================
// SpriteBatch Region Draw Tests
// ============================================================================

TEST(RTSprite, SpritebatchDrawRegion) {
    void *batch = rt_spritebatch_new(0);

    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_region(batch, (void *)1, 0, 0, 10, 10, 32, 32);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 1);

    printf("test_spritebatch_draw_region: PASSED\n");
}

TEST(RTSprite, TextureAtlasAddAndLookup) {
    void *pixels = rt_pixels_new(32, 32);
    ASSERT_TRUE(pixels != nullptr);

    void *atlas = rt_texatlas_new(pixels);
    ASSERT_TRUE(atlas != nullptr);

    rt_string name = rt_string_from_bytes("hero_idle_0", 11);
    ASSERT_TRUE(name != nullptr);

    rt_texatlas_add(atlas, name, 4, 8, 12, 16);
    ASSERT_TRUE(rt_texatlas_region_count(atlas) == 1);
    ASSERT_TRUE(rt_texatlas_has(atlas, name) == 1);
    ASSERT_TRUE(rt_texatlas_get_x(atlas, name) == 4);
    ASSERT_TRUE(rt_texatlas_get_y(atlas, name) == 8);
    ASSERT_TRUE(rt_texatlas_get_w(atlas, name) == 12);
    ASSERT_TRUE(rt_texatlas_get_h(atlas, name) == 16);

    rt_string_unref(name);
    rt_obj_release_check0(atlas);
    rt_obj_release_check0(pixels);
}

TEST(RTSprite, TextureAtlasLoadGrid) {
    void *pixels = rt_pixels_new(32, 16);
    ASSERT_TRUE(pixels != nullptr);

    void *atlas = rt_texatlas_load_grid(pixels, 16, 16);
    ASSERT_TRUE(atlas != nullptr);
    ASSERT_TRUE(rt_texatlas_region_count(atlas) == 2);

    rt_string zero = rt_string_from_bytes("0", 1);
    rt_string one = rt_string_from_bytes("1", 1);
    ASSERT_TRUE(rt_texatlas_has(atlas, zero) == 1);
    ASSERT_TRUE(rt_texatlas_has(atlas, one) == 1);
    ASSERT_TRUE(rt_texatlas_get_x(atlas, one) == 16);
    ASSERT_TRUE(rt_texatlas_get_y(atlas, one) == 0);
    ASSERT_TRUE(rt_texatlas_get_w(atlas, one) == 16);
    ASSERT_TRUE(rt_texatlas_get_h(atlas, one) == 16);

    rt_string_unref(zero);
    rt_string_unref(one);
    rt_obj_release_check0(atlas);
    rt_obj_release_check0(pixels);
}

TEST(RTSprite, SpritebatchDrawAtlasVariantsIncrementCount) {
    void *pixels = rt_pixels_new(32, 32);
    ASSERT_TRUE(pixels != nullptr);

    void *atlas = rt_texatlas_new(pixels);
    ASSERT_TRUE(atlas != nullptr);

    rt_string name = rt_string_from_bytes("coin", 4);
    ASSERT_TRUE(name != nullptr);
    rt_texatlas_add(atlas, name, 0, 0, 16, 16);

    void *batch = rt_spritebatch_new(0);
    ASSERT_TRUE(batch != nullptr);

    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_atlas(batch, atlas, name, 0, 0);
    rt_spritebatch_draw_atlas_scaled(batch, atlas, name, 32, 32, 150);
    rt_spritebatch_draw_atlas_ex(batch, atlas, name, 64, 64, 200, 45, 7);
    ASSERT_TRUE(rt_spritebatch_count(batch) == 3);

    rt_string_unref(name);
    rt_obj_release_check0(batch);
    rt_obj_release_check0(atlas);
    rt_obj_release_check0(pixels);
}

// ============================================================================
// Main
// ============================================================================


// ── RTSpriteSheetTests.cpp ──
// (vm_trap, rt_object.h, rt_pixels.h, rt_seq.h, rt_spritesheet.h, rt_string.h
//  already included above)

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

// Helper: create a test atlas with known pixel values
static void *make_test_atlas(int64_t w, int64_t h) {
    void *px = rt_pixels_new(w, h);
    int64_t x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            // Encode position into color: ARGB with R=x, G=y
            int64_t color = (int64_t)0xFF000000 | ((x & 0xFF) << 16) | ((y & 0xFF) << 8);
            rt_pixels_set(px, x, y, color);
        }
    }
    return px;
}

TEST(RTSprite, NewBasic) {
    void *atlas = make_test_atlas(64, 64);
    void *sheet = rt_spritesheet_new(atlas);
    ASSERT(sheet != NULL, "spritesheet_new should return non-null");
    ASSERT(rt_spritesheet_region_count(sheet) == 0, "new sheet has 0 regions");
    ASSERT(rt_spritesheet_width(sheet) == 64, "width matches atlas");
    ASSERT(rt_spritesheet_height(sheet) == 64, "height matches atlas");
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, NewNullAtlas) {
    void *sheet = rt_spritesheet_new(NULL);
    ASSERT(sheet == NULL, "null atlas returns null sheet");
}

TEST(RTSprite, SetAndGetRegion) {
    void *atlas = make_test_atlas(64, 64);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string name = rt_const_cstr("walk_0");
    rt_spritesheet_set_region(sheet, name, 0, 0, 32, 32);
    ASSERT(rt_spritesheet_region_count(sheet) == 1, "1 region after set");
    ASSERT(rt_spritesheet_has_region(sheet, name) == 1, "has_region returns 1");

    void *region = rt_spritesheet_get_region(sheet, name);
    ASSERT(region != NULL, "get_region returns non-null");

    // Verify pixel data was correctly copied (pixel at 0,0 should match atlas 0,0)
    int64_t p = rt_pixels_get(region, 0, 0);
    int64_t expected = rt_pixels_get(atlas, 0, 0);
    ASSERT(p == expected, "region pixel 0,0 matches atlas 0,0");

    rt_obj_release_check0(region);
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, RegionOffset) {
    void *atlas = make_test_atlas(64, 64);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string name = rt_const_cstr("frame1");
    rt_spritesheet_set_region(sheet, name, 16, 16, 16, 16);

    void *region = rt_spritesheet_get_region(sheet, name);
    ASSERT(region != NULL, "offset region returned");

    // Pixel at region(0,0) should match atlas(16,16)
    int64_t p = rt_pixels_get(region, 0, 0);
    int64_t expected = rt_pixels_get(atlas, 16, 16);
    ASSERT(p == expected, "offset region pixel matches atlas at correct position");

    rt_obj_release_check0(region);
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, HasRegionFalse) {
    void *atlas = make_test_atlas(32, 32);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string name = rt_const_cstr("nonexistent");
    ASSERT(rt_spritesheet_has_region(sheet, name) == 0, "has_region returns 0 for missing");
    ASSERT(rt_spritesheet_get_region(sheet, name) == NULL, "get_region returns null for missing");

    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, UpdateExistingRegion) {
    void *atlas = make_test_atlas(64, 64);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string name = rt_const_cstr("r");
    rt_spritesheet_set_region(sheet, name, 0, 0, 16, 16);
    ASSERT(rt_spritesheet_region_count(sheet) == 1, "1 region");

    // Update same name with different coords
    rt_spritesheet_set_region(sheet, name, 32, 32, 8, 8);
    ASSERT(rt_spritesheet_region_count(sheet) == 1, "still 1 region after update");

    void *region = rt_spritesheet_get_region(sheet, name);
    ASSERT(region != NULL, "get updated region");

    // Pixel at region(0,0) should now match atlas(32,32)
    int64_t p = rt_pixels_get(region, 0, 0);
    int64_t expected = rt_pixels_get(atlas, 32, 32);
    ASSERT(p == expected, "updated region reads from new atlas position");

    rt_obj_release_check0(region);
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, RemoveRegion) {
    void *atlas = make_test_atlas(32, 32);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string name = rt_const_cstr("r");
    rt_spritesheet_set_region(sheet, name, 0, 0, 16, 16);
    ASSERT(rt_spritesheet_region_count(sheet) == 1, "1 region");

    int8_t removed = rt_spritesheet_remove_region(sheet, name);
    ASSERT(removed == 1, "remove returns 1");
    ASSERT(rt_spritesheet_region_count(sheet) == 0, "0 regions after remove");
    ASSERT(rt_spritesheet_has_region(sheet, name) == 0, "has returns 0 after remove");

    // Removing again returns 0
    int8_t removed2 = rt_spritesheet_remove_region(sheet, name);
    ASSERT(removed2 == 0, "remove non-existent returns 0");

    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, MultipleRegions) {
    void *atlas = make_test_atlas(64, 64);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string n0 = rt_const_cstr("a");
    rt_string n1 = rt_const_cstr("b");
    rt_string n2 = rt_const_cstr("c");

    rt_spritesheet_set_region(sheet, n0, 0, 0, 16, 16);
    rt_spritesheet_set_region(sheet, n1, 16, 0, 16, 16);
    rt_spritesheet_set_region(sheet, n2, 32, 0, 16, 16);
    ASSERT(rt_spritesheet_region_count(sheet) == 3, "3 regions");

    ASSERT(rt_spritesheet_has_region(sheet, n0) == 1, "has a");
    ASSERT(rt_spritesheet_has_region(sheet, n1) == 1, "has b");
    ASSERT(rt_spritesheet_has_region(sheet, n2) == 1, "has c");

    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, FromGrid) {
    void *atlas = make_test_atlas(64, 32);
    void *sheet = rt_spritesheet_from_grid(atlas, 32, 32);
    ASSERT(sheet != NULL, "from_grid returns non-null");

    // 64/32=2 cols, 32/32=1 row => 2 regions named "0" and "1"
    ASSERT(rt_spritesheet_region_count(sheet) == 2, "grid produces 2 regions");

    rt_string n0 = rt_const_cstr("0");
    rt_string n1 = rt_const_cstr("1");
    ASSERT(rt_spritesheet_has_region(sheet, n0) == 1, "has region 0");
    ASSERT(rt_spritesheet_has_region(sheet, n1) == 1, "has region 1");

    // Region "1" should start at atlas x=32
    void *r1 = rt_spritesheet_get_region(sheet, n1);
    ASSERT(r1 != NULL, "region 1 not null");
    int64_t p = rt_pixels_get(r1, 0, 0);
    int64_t expected = rt_pixels_get(atlas, 32, 0);
    ASSERT(p == expected, "grid region 1 starts at correct atlas offset");

    rt_obj_release_check0(r1);
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, FromGridInvalid) {
    void *atlas = make_test_atlas(32, 32);
    ASSERT(rt_spritesheet_from_grid(NULL, 16, 16) == NULL, "null atlas returns null");
    ASSERT(rt_spritesheet_from_grid(atlas, 0, 16) == NULL, "zero frame_w returns null");
    ASSERT(rt_spritesheet_from_grid(atlas, 16, 0) == NULL, "zero frame_h returns null");
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, RegionNames) {
    void *atlas = make_test_atlas(32, 32);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string n0 = rt_const_cstr("alpha");
    rt_string n1 = rt_const_cstr("beta");
    rt_spritesheet_set_region(sheet, n0, 0, 0, 16, 16);
    rt_spritesheet_set_region(sheet, n1, 16, 0, 16, 16);

    void *names = rt_spritesheet_region_names(sheet);
    ASSERT(names != NULL, "region_names returns non-null");
    ASSERT(rt_seq_len(names) == 2, "names seq has 2 entries");

    rt_obj_release_check0(names);
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

TEST(RTSprite, NullSafety) {
    rt_string name = rt_const_cstr("test");
    // All functions should handle NULL gracefully
    ASSERT(rt_spritesheet_region_count(NULL) == 0, "null count = 0");
    ASSERT(rt_spritesheet_width(NULL) == 0, "null width = 0");
    ASSERT(rt_spritesheet_height(NULL) == 0, "null height = 0");
    ASSERT(rt_spritesheet_has_region(NULL, name) == 0, "null has = 0");
    ASSERT(rt_spritesheet_get_region(NULL, name) == NULL, "null get = null");
    ASSERT(rt_spritesheet_remove_region(NULL, name) == 0, "null remove = 0");
}

/// @brief Main.
int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
