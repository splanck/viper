//===----------------------------------------------------------------------===//
// RTSpriteSheetTests.cpp - Tests for rt_spritesheet (sprite atlas)
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_seq.h"
#include "rt_spritesheet.h"
#include "rt_string.h"

    void vm_trap(const char *msg)
    {
        fprintf(stderr, "TRAP: %s\n", msg);
    }
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

// Helper: create a test atlas with known pixel values
static void *make_test_atlas(int64_t w, int64_t h)
{
    void *px = rt_pixels_new(w, h);
    int64_t x, y;
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            // Encode position into color: ARGB with R=x, G=y
            int64_t color = (int64_t)0xFF000000 | ((x & 0xFF) << 16) | ((y & 0xFF) << 8);
            rt_pixels_set(px, x, y, color);
        }
    }
    return px;
}

static void test_new_basic()
{
    void *atlas = make_test_atlas(64, 64);
    void *sheet = rt_spritesheet_new(atlas);
    ASSERT(sheet != NULL, "spritesheet_new should return non-null");
    ASSERT(rt_spritesheet_region_count(sheet) == 0, "new sheet has 0 regions");
    ASSERT(rt_spritesheet_width(sheet) == 64, "width matches atlas");
    ASSERT(rt_spritesheet_height(sheet) == 64, "height matches atlas");
    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

static void test_new_null_atlas()
{
    void *sheet = rt_spritesheet_new(NULL);
    ASSERT(sheet == NULL, "null atlas returns null sheet");
}

static void test_set_and_get_region()
{
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

static void test_region_offset()
{
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

static void test_has_region_false()
{
    void *atlas = make_test_atlas(32, 32);
    void *sheet = rt_spritesheet_new(atlas);

    rt_string name = rt_const_cstr("nonexistent");
    ASSERT(rt_spritesheet_has_region(sheet, name) == 0, "has_region returns 0 for missing");
    ASSERT(rt_spritesheet_get_region(sheet, name) == NULL, "get_region returns null for missing");

    rt_obj_release_check0(sheet);
    rt_obj_release_check0(atlas);
}

static void test_update_existing_region()
{
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

static void test_remove_region()
{
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

static void test_multiple_regions()
{
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

static void test_from_grid()
{
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

static void test_from_grid_invalid()
{
    void *atlas = make_test_atlas(32, 32);
    ASSERT(rt_spritesheet_from_grid(NULL, 16, 16) == NULL, "null atlas returns null");
    ASSERT(rt_spritesheet_from_grid(atlas, 0, 16) == NULL, "zero frame_w returns null");
    ASSERT(rt_spritesheet_from_grid(atlas, 16, 0) == NULL, "zero frame_h returns null");
    rt_obj_release_check0(atlas);
}

static void test_region_names()
{
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

static void test_null_safety()
{
    rt_string name = rt_const_cstr("test");
    // All functions should handle NULL gracefully
    ASSERT(rt_spritesheet_region_count(NULL) == 0, "null count = 0");
    ASSERT(rt_spritesheet_width(NULL) == 0, "null width = 0");
    ASSERT(rt_spritesheet_height(NULL) == 0, "null height = 0");
    ASSERT(rt_spritesheet_has_region(NULL, name) == 0, "null has = 0");
    ASSERT(rt_spritesheet_get_region(NULL, name) == NULL, "null get = null");
    ASSERT(rt_spritesheet_remove_region(NULL, name) == 0, "null remove = 0");
}

int main()
{
    test_new_basic();
    test_new_null_atlas();
    test_set_and_get_region();
    test_region_offset();
    test_has_region_false();
    test_update_existing_region();
    test_remove_region();
    test_multiple_regions();
    test_from_grid();
    test_from_grid_invalid();
    test_region_names();
    test_null_safety();

    printf("SpriteSheet tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
