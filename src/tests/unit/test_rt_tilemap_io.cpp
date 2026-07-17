//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)
#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void test_tile_properties(void) {
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

static void test_tile_property_update(void) {
    TEST("Tile property update overwrites");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string key = make_str("hp");
    rt_tilemap_set_tile_property(tm, 1, key, 100);
    assert(rt_tilemap_get_tile_property(tm, 1, key, 0) == 100);
    rt_tilemap_set_tile_property(tm, 1, key, 200);
    assert(rt_tilemap_get_tile_property(tm, 1, key, 0) == 200);
    PASS();
}

static void test_autotile_basic(void) {
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

static void test_autotile_isolated(void) {
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

static void test_autotile_partial_rule_falls_back_to_base(void) {
    TEST("Auto-tiling partial rule falls back to base tile");
    void *tm = rt_tilemap_new(2, 1, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 1);
    rt_tilemap_set_tile(tm, 1, 0, 1);

    rt_tilemap_set_autotile_lo(tm, 1, 100, 101, 102, 103, 104, 105, 106, 107);
    rt_tilemap_apply_autotile(tm);

    assert(rt_tilemap_get_tile(tm, 0, 0) == 102);
    assert(rt_tilemap_get_tile(tm, 1, 0) == 1);
    PASS();
}

static void test_json_save_load(void) {
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

static void test_json_save_load_preserves_extended_state(void) {
    TEST("JSON save/load preserves layers, props, autotile, collision, animations");
    void *tm = rt_tilemap_new(3, 3, 16, 16);
    int64_t fg = rt_tilemap_add_layer(tm, make_str("fg"));
    assert(fg == 1);
    rt_tilemap_set_layer_visible(tm, fg, 0);
    rt_tilemap_set_tile_layer(tm, fg, 1, 1, 7);
    rt_tilemap_set_collision_layer(tm, fg);
    rt_tilemap_set_collision(tm, 7, 2);
    rt_tilemap_set_tile_property(tm, 7, make_str("damage"), 42);

    rt_tilemap_set_tile(tm, 1, 1, 3);
    rt_tilemap_set_autotile_lo(tm, 3, 50, 51, 52, 53, 54, 55, 56, 57);
    rt_tilemap_set_autotile_hi(tm, 3, 58, 59, 60, 61, 62, 63, 64, 65);

    rt_tilemap_set_tile_anim(tm, 7, 2, 100);
    rt_tilemap_set_tile_anim_frame(tm, 7, 0, 7);
    rt_tilemap_set_tile_anim_frame(tm, 7, 1, 8);
    rt_tilemap_update_anims(tm, 100);
    assert(rt_tilemap_resolve_anim_tile(tm, 7) == 8);

    rt_string path = make_str("/tmp/test_tilemap_extended_roundtrip.json");
    assert(rt_tilemap_save_to_file(tm, path) == 1);

    void *loaded = rt_tilemap_load_from_file(path);
    assert(loaded != NULL);
    assert(rt_tilemap_get_layer_count(loaded) == 2);
    assert(rt_tilemap_get_layer_by_name(loaded, make_str("fg")) == 1);
    assert(rt_tilemap_get_layer_visible(loaded, fg) == 0);
    assert(rt_tilemap_get_tile_layer(loaded, fg, 1, 1) == 7);
    assert(rt_tilemap_get_collision_layer(loaded) == fg);
    assert(rt_tilemap_get_collision(loaded, 7) == 2);
    assert(rt_tilemap_get_tile_property(loaded, 7, make_str("damage"), -1) == 42);
    assert(rt_tilemap_resolve_anim_tile(loaded, 7) == 8);

    rt_tilemap_apply_autotile(loaded);
    assert(rt_tilemap_get_tile(loaded, 1, 1) == 50);
    PASS();
}

static void test_csv_import(void) {
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

static void test_csv_import_clamps_overflow_values(void) {
    TEST("CSV import clamps overflowing integer values");
    const char *csv_path = "/tmp/test_tilemap_overflow.csv";
    FILE *f = fopen(csv_path, "w");
    assert(f != NULL);
    fprintf(f, "999999999999999999999999999999,-999999999999999999999999999999\n");
    fclose(f);

    void *tm = rt_tilemap_load_csv(make_str(csv_path), 16, 16);
    assert(tm != NULL);
    assert(rt_tilemap_get_tile(tm, 0, 0) == INT64_MAX);
    assert(rt_tilemap_get_tile(tm, 1, 0) == INT64_MIN);
    PASS();
}

static void test_csv_import_rejects_overlong_line(void) {
    TEST("CSV import rejects overlong line");
    const char *csv_path = "/tmp/test_tilemap_overlong.csv";
    FILE *f = fopen(csv_path, "w");
    assert(f != NULL);
    for (int i = 0; i < 17000; i++)
        fputc('1', f);
    fclose(f);

    assert(rt_tilemap_load_csv(make_str(csv_path), 16, 16) == NULL);
    PASS();
}

static void test_json_negative_anim_frame_normalizes(void) {
    TEST("JSON load normalizes negative animation frame");
    const char *path = "/tmp/test_tilemap_negative_frame.json";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f,
            "{"
            "\"version\":1,\"width\":1,\"height\":1,\"tileWidth\":16,\"tileHeight\":16,"
            "\"layers\":[{\"tiles\":[7],\"visible\":1,\"name\":\"base\"}],"
            "\"collision\":{\"layer\":0,\"types\":[]},"
            "\"tileProperties\":[],\"autotiles\":[],"
            "\"animations\":[{\"baseTile\":7,\"frameCount\":2,\"msPerFrame\":100,"
            "\"timer\":0,\"currentFrame\":-1,\"frames\":[7,8]}]"
            "}");
    fclose(f);

    void *tm = rt_tilemap_load_from_file(make_str(path));
    assert(tm != NULL);
    assert(rt_tilemap_resolve_anim_tile(tm, 7) == 8);
    PASS();
}

static void test_json_duplicate_animation_state_applies_to_replaced_base(void) {
    TEST("JSON duplicate animation state applies to replaced base");
    const char *path = "/tmp/test_tilemap_duplicate_anim_state.json";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f,
            "{"
            "\"version\":1,\"width\":1,\"height\":1,\"tileWidth\":16,\"tileHeight\":16,"
            "\"layers\":[{\"tiles\":[5],\"visible\":1,\"name\":\"base\"}],"
            "\"collision\":{\"layer\":0,\"types\":[]},"
            "\"tileProperties\":[],\"autotiles\":[],"
            "\"animations\":["
            "{\"baseTile\":5,\"frameCount\":2,\"msPerFrame\":100,"
            "\"timer\":0,\"currentFrame\":0,\"frames\":[5,6]},"
            "{\"baseTile\":6,\"frameCount\":2,\"msPerFrame\":100,"
            "\"timer\":0,\"currentFrame\":0,\"frames\":[60,61]},"
            "{\"baseTile\":5,\"frameCount\":2,\"msPerFrame\":100,"
            "\"timer\":0,\"currentFrame\":1,\"frames\":[50,51]}"
            "]"
            "}");
    fclose(f);

    void *tm = rt_tilemap_load_from_file(make_str(path));
    assert(tm != NULL);
    assert(rt_tilemap_resolve_anim_tile(tm, 5) == 51);
    assert(rt_tilemap_resolve_anim_tile(tm, 6) == 60);
    PASS();
}

static void test_tile_anim_sequential_frames_saturate(void) {
    TEST("Tile animation sequential defaults saturate near INT64_MAX");
    void *tm = rt_tilemap_new(1, 1, 16, 16);
    rt_tilemap_set_tile_anim(tm, INT64_MAX - 1, 3, 100);
    assert(rt_tilemap_resolve_anim_tile(tm, INT64_MAX - 1) == INT64_MAX - 1);
    rt_tilemap_update_anims(tm, 100);
    assert(rt_tilemap_resolve_anim_tile(tm, INT64_MAX - 1) == INT64_MAX);
    rt_tilemap_update_anims(tm, 100);
    assert(rt_tilemap_resolve_anim_tile(tm, INT64_MAX - 1) == INT64_MAX);
    PASS();
}

static void test_tile_anim_duplicate_base_replaces(void) {
    TEST("Tile animation duplicate base replaces existing animation");
    void *tm = rt_tilemap_new(1, 1, 16, 16);
    rt_tilemap_set_tile_anim(tm, 5, 2, 100);
    rt_tilemap_set_tile_anim_frame(tm, 5, 1, 9);
    rt_tilemap_update_anims(tm, 100);
    assert(rt_tilemap_resolve_anim_tile(tm, 5) == 9);

    rt_tilemap_set_tile_anim(tm, 5, 2, 100);
    rt_tilemap_set_tile_anim_frame(tm, 5, 1, 12);
    rt_tilemap_update_anims(tm, 100);
    assert(rt_tilemap_resolve_anim_tile(tm, 5) == 12);
    PASS();
}

static void test_json_rejects_wrong_layer_tile_count(void) {
    TEST("JSON load rejects wrong layer tile count");
    const char *path = "/tmp/test_tilemap_wrong_tile_count.json";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f,
            "{"
            "\"version\":1,\"width\":2,\"height\":2,\"tileWidth\":16,\"tileHeight\":16,"
            "\"layers\":[{\"tiles\":[1,2,3],\"visible\":1,\"name\":\"base\"}],"
            "\"collision\":{\"layer\":0,\"types\":[]},"
            "\"tileProperties\":[],\"autotiles\":[],\"animations\":[]"
            "}");
    fclose(f);

    assert(rt_tilemap_load_from_file(make_str(path)) == NULL);
    PASS();
}

static void test_json_rejects_truncated_tileset_pixels(void) {
    TEST("JSON load rejects truncated tileset pixels");
    const char *path = "/tmp/test_tilemap_truncated_tileset.json";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f,
            "{"
            "\"version\":1,\"width\":1,\"height\":1,\"tileWidth\":16,\"tileHeight\":16,"
            "\"tileset\":{\"width\":2,\"height\":1,\"pixels\":[4278190335]},"
            "\"layers\":[{\"tiles\":[7],\"visible\":1,\"name\":\"base\"}],"
            "\"collision\":{\"layer\":0,\"types\":[]},"
            "\"tileProperties\":[],\"autotiles\":[],\"animations\":[]"
            "}");
    fclose(f);

    assert(rt_tilemap_load_from_file(make_str(path)) == NULL);
    PASS();
}

static void test_json_excess_layers_are_ignored(void) {
    TEST("JSON load ignores layers beyond maximum");
    const char *path = "/tmp/test_tilemap_excess_layers.json";
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fprintf(f, "{\"version\":1,\"width\":1,\"height\":1,\"tileWidth\":16,\"tileHeight\":16,");
    fprintf(f, "\"layers\":[");
    for (int i = 0; i < 20; i++) {
        if (i > 0)
            fprintf(f, ",");
        fprintf(f, "{\"tiles\":[%d],\"visible\":1,\"name\":\"l%d\"}", i, i);
    }
    fprintf(f,
            "],\"collision\":{\"layer\":0,\"types\":[]},"
            "\"tileProperties\":[],\"autotiles\":[],\"animations\":[]}");
    fclose(f);

    void *tm = rt_tilemap_load_from_file(make_str(path));
    assert(tm != NULL);
    assert(rt_tilemap_get_layer_count(tm) == 16);
    assert(rt_tilemap_get_tile_layer(tm, 15, 0, 0) == 15);
    PASS();
}

static void test_load_nonexistent(void) {
    TEST("Load nonexistent file returns NULL");
    assert(rt_tilemap_load_from_file(make_str("/tmp/nonexistent_tilemap.json")) == NULL);
    assert(rt_tilemap_load_csv(make_str("/tmp/nonexistent.csv"), 16, 16) == NULL);
    PASS();
}

static void test_clear_autotile(void) {
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

int main() {
    printf("test_rt_tilemap_io:\n");
    test_tile_properties();
    test_tile_property_update();
    test_autotile_basic();
    test_autotile_isolated();
    test_autotile_partial_rule_falls_back_to_base();
    test_json_save_load();
    test_json_save_load_preserves_extended_state();
    test_csv_import();
    test_csv_import_clamps_overflow_values();
    test_csv_import_rejects_overlong_line();
    test_json_negative_anim_frame_normalizes();
    test_json_duplicate_animation_state_applies_to_replaced_base();
    test_tile_anim_sequential_frames_saturate();
    test_tile_anim_duplicate_base_replaces();
    test_json_rejects_wrong_layer_tile_count();
    test_json_rejects_truncated_tileset_pixels();
    test_json_excess_layers_are_ignored();
    test_load_nonexistent();
    test_clear_autotile();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
