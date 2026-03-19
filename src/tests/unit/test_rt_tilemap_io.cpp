//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_tilemap_io.cpp
// Purpose: Unit tests for tilemap file I/O, CSV import, auto-tiling, and
//   tile properties.
//
// Key invariants:
//   - JSON round-trip preserves tile data and layer structure.
//   - CSV import produces correct tilemap dimensions and tile values.
//   - Auto-tiling computes correct 4-bit neighbor bitmasks.
//   - Tile properties are stored/retrieved by key.
//
// Ownership/Lifetime:
//   - Uses runtime library. Tilemap objects are GC-managed.
//
// Links: src/runtime/graphics/rt_tilemap.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_string.h"
#include "rt_tilemap.h"
#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)
#define PASS()                                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

static void test_tile_properties(void)
{
    TEST("Tile properties set/get/has");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string key = make_str("damage");
    rt_tilemap_set_tile_property(tm, 5, key, 10);
    assert(rt_tilemap_get_tile_property(tm, 5, key, 0) == 10);
    assert(rt_tilemap_has_tile_property(tm, 5, key) == 1);

    // Non-existent property returns default
    rt_string other = make_str("speed");
    assert(rt_tilemap_get_tile_property(tm, 5, other, -1) == -1);
    assert(rt_tilemap_has_tile_property(tm, 5, other) == 0);
    PASS();
}

static void test_tile_property_update(void)
{
    TEST("Tile property update overwrites");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string key = make_str("hp");
    rt_tilemap_set_tile_property(tm, 1, key, 100);
    assert(rt_tilemap_get_tile_property(tm, 1, key, 0) == 100);
    rt_tilemap_set_tile_property(tm, 1, key, 200);
    assert(rt_tilemap_get_tile_property(tm, 1, key, 0) == 200);
    PASS();
}

static void test_autotile_basic(void)
{
    TEST("Auto-tiling basic neighbor computation");
    void *tm = rt_tilemap_new(5, 1, 16, 16);
    // Place base tile (ID=1) at x=1,2,3 (horizontal strip)
    rt_tilemap_set_tile(tm, 1, 0, 1);
    rt_tilemap_set_tile(tm, 2, 0, 1);
    rt_tilemap_set_tile(tm, 3, 0, 1);

    // Set autotile rules: variant[mask] = 100 + mask
    // So we can check which mask was computed
    rt_tilemap_set_autotile_lo(tm, 1, 100, 101, 102, 103, 104, 105, 106, 107);
    rt_tilemap_set_autotile_hi(tm, 1, 108, 109, 110, 111, 112, 113, 114, 115);

    rt_tilemap_apply_autotile(tm);

    // x=1: right neighbor(2) → mask = 2
    assert(rt_tilemap_get_tile(tm, 1, 0) == 102);
    // x=2: left(8) + right(2) → mask = 10
    assert(rt_tilemap_get_tile(tm, 2, 0) == 110);
    // x=3: left(8) → mask = 8
    assert(rt_tilemap_get_tile(tm, 3, 0) == 108);
    PASS();
}

static void test_autotile_isolated(void)
{
    TEST("Auto-tiling isolated cell");
    void *tm = rt_tilemap_new(3, 3, 16, 16);
    rt_tilemap_set_tile(tm, 1, 1, 1);

    rt_tilemap_set_autotile_lo(tm, 1, 50, 51, 52, 53, 54, 55, 56, 57);
    rt_tilemap_set_autotile_hi(tm, 1, 58, 59, 60, 61, 62, 63, 64, 65);

    rt_tilemap_apply_autotile(tm);

    // No neighbors → mask = 0
    assert(rt_tilemap_get_tile(tm, 1, 1) == 50);
    PASS();
}

static void test_json_save_load(void)
{
    TEST("JSON save/load round-trip");
    void *tm = rt_tilemap_new(3, 3, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 5);
    rt_tilemap_set_tile(tm, 1, 1, 10);
    rt_tilemap_set_tile(tm, 2, 2, 15);

    rt_string path = make_str("/tmp/test_tilemap_roundtrip.json");
    int8_t saved = rt_tilemap_save_to_file(tm, path);
    assert(saved == 1);

    void *loaded = rt_tilemap_load_from_file(path);
    assert(loaded != NULL);
    assert(rt_tilemap_get_width(loaded) == 3);
    assert(rt_tilemap_get_height(loaded) == 3);
    assert(rt_tilemap_get_tile(loaded, 0, 0) == 5);
    assert(rt_tilemap_get_tile(loaded, 1, 1) == 10);
    assert(rt_tilemap_get_tile(loaded, 2, 2) == 15);
    PASS();
}

static void test_csv_import(void)
{
    TEST("CSV import");
    // Write a test CSV
    const char *csv_path = "/tmp/test_tilemap.csv";
    FILE *f = fopen(csv_path, "w");
    assert(f != NULL);
    fprintf(f, "0,0,1\n0,2,1\n1,1,0\n");
    fclose(f);

    rt_string path = make_str(csv_path);
    void *tm = rt_tilemap_load_csv(path, 16, 16);
    assert(tm != NULL);
    assert(rt_tilemap_get_width(tm) == 3);
    assert(rt_tilemap_get_height(tm) == 3);
    assert(rt_tilemap_get_tile(tm, 2, 0) == 1);
    assert(rt_tilemap_get_tile(tm, 1, 1) == 2);
    assert(rt_tilemap_get_tile(tm, 0, 2) == 1);
    PASS();
}

static void test_load_nonexistent(void)
{
    TEST("Load nonexistent file returns NULL");
    assert(rt_tilemap_load_from_file(make_str("/tmp/nonexistent_tilemap.json")) == NULL);
    assert(rt_tilemap_load_csv(make_str("/tmp/nonexistent.csv"), 16, 16) == NULL);
    PASS();
}

static void test_clear_autotile(void)
{
    TEST("Clear autotile rule");
    void *tm = rt_tilemap_new(3, 1, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 1);
    rt_tilemap_set_tile(tm, 1, 0, 1);

    rt_tilemap_set_autotile_lo(tm, 1, 50, 51, 52, 53, 54, 55, 56, 57);
    rt_tilemap_set_autotile_hi(tm, 1, 58, 59, 60, 61, 62, 63, 64, 65);

    // Clear the rule
    rt_tilemap_clear_autotile(tm, 1);

    // Apply — should have no effect since rule is cleared
    rt_tilemap_apply_autotile(tm);
    assert(rt_tilemap_get_tile(tm, 0, 0) == 1); // unchanged
    PASS();
}

int main()
{
    printf("test_rt_tilemap_io:\n");
    test_tile_properties();
    test_tile_property_update();
    test_autotile_basic();
    test_autotile_isolated();
    test_json_save_load();
    test_csv_import();
    test_load_nonexistent();
    test_clear_autotile();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
