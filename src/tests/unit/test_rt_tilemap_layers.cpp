//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_tilemap_layers.cpp
// Purpose: Unit tests for multi-layer tilemap support. Tests layer creation,
//   naming, per-layer tile access, visibility toggling, collision layer
//   designation, layer removal with index shifting, and backwards compatibility
//   of single-layer API operating on layer 0.
//
// Key invariants:
//   - Layer 0 ("base") is created automatically and cannot be removed.
//   - Per-layer tile grids are independent (writing to layer 0 does not
//     affect layer 1 and vice versa).
//   - Collision queries (IsSolidAt, CollideBody) read from the designated
//     collision layer.
//   - Existing single-layer API (SetTile, GetTile, Fill, Clear) operates
//     on layer 0 for backwards compatibility.
//   - Maximum 16 layers; AddLayer returns -1 when full.
//
// Ownership/Lifetime:
//   - Uses runtime library. Tilemap objects are GC-managed.
//
// Links: src/runtime/graphics/rt_tilemap.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_physics2d.h"
#include "rt_string.h"
#include "rt_tilemap.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Trap handler for runtime
extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

//=============================================================================
// Helpers
//=============================================================================

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

/// Helper to create an rt_string from a C literal.
static rt_string make_str(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

//=============================================================================
// Tests
//=============================================================================

static void test_default_single_layer(void) {
    TEST("Default tilemap has 1 layer");
    void *tm = rt_tilemap_new(10, 10, 16, 16);
    assert(tm != NULL);
    assert(rt_tilemap_get_layer_count(tm) == 1);

    // Existing SetTile/GetTile works on layer 0
    rt_tilemap_set_tile(tm, 3, 3, 42);
    assert(rt_tilemap_get_tile(tm, 3, 3) == 42);

    // Layer 0 tile access matches
    assert(rt_tilemap_get_tile_layer(tm, 0, 3, 3) == 42);
    PASS();
}

static void test_add_and_name_layers(void) {
    TEST("Add and name layers");
    void *tm = rt_tilemap_new(8, 8, 16, 16);
    assert(tm != NULL);

    rt_string fg_name = make_str("foreground");
    int64_t fg_id = rt_tilemap_add_layer(tm, fg_name);
    assert(fg_id == 1);
    assert(rt_tilemap_get_layer_count(tm) == 2);

    // Lookup by name
    assert(rt_tilemap_get_layer_by_name(tm, fg_name) == 1);

    // Base layer lookup
    rt_string base_name = make_str("base");
    assert(rt_tilemap_get_layer_by_name(tm, base_name) == 0);

    // Unknown name returns -1
    rt_string unknown = make_str("nope");
    assert(rt_tilemap_get_layer_by_name(tm, unknown) == -1);
    PASS();
}

static void test_per_layer_tile_independence(void) {
    TEST("Per-layer tile independence");
    void *tm = rt_tilemap_new(10, 10, 16, 16);
    rt_string fg = make_str("fg");
    int64_t layer1 = rt_tilemap_add_layer(tm, fg);
    assert(layer1 == 1);

    // Set different tiles on each layer at same position
    rt_tilemap_set_tile_layer(tm, 0, 5, 5, 1);
    rt_tilemap_set_tile_layer(tm, 1, 5, 5, 2);

    // Verify independence
    assert(rt_tilemap_get_tile_layer(tm, 0, 5, 5) == 1);
    assert(rt_tilemap_get_tile_layer(tm, 1, 5, 5) == 2);

    // Writing to layer 0 doesn't affect layer 1
    rt_tilemap_set_tile_layer(tm, 0, 5, 5, 99);
    assert(rt_tilemap_get_tile_layer(tm, 1, 5, 5) == 2);
    PASS();
}

static void test_fill_and_clear_layer(void) {
    TEST("Fill and clear layer");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string l1 = make_str("l1");
    rt_tilemap_add_layer(tm, l1);

    rt_tilemap_fill_layer(tm, 1, 7);
    assert(rt_tilemap_get_tile_layer(tm, 1, 0, 0) == 7);
    assert(rt_tilemap_get_tile_layer(tm, 1, 3, 3) == 7);

    // Layer 0 unaffected
    assert(rt_tilemap_get_tile_layer(tm, 0, 0, 0) == 0);

    rt_tilemap_clear_layer(tm, 1);
    assert(rt_tilemap_get_tile_layer(tm, 1, 0, 0) == 0);
    assert(rt_tilemap_get_tile_layer(tm, 1, 3, 3) == 0);
    PASS();
}

static void test_layer_visibility(void) {
    TEST("Layer visibility toggle");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string l1 = make_str("vis");
    rt_tilemap_add_layer(tm, l1);

    // Default: visible
    assert(rt_tilemap_get_layer_visible(tm, 0) == 1);
    assert(rt_tilemap_get_layer_visible(tm, 1) == 1);

    // Hide layer 1
    rt_tilemap_set_layer_visible(tm, 1, 0);
    assert(rt_tilemap_get_layer_visible(tm, 1) == 0);

    // Re-show
    rt_tilemap_set_layer_visible(tm, 1, 1);
    assert(rt_tilemap_get_layer_visible(tm, 1) == 1);
    PASS();
}

static void test_collision_layer_designation(void) {
    TEST("Collision layer designation");
    void *tm = rt_tilemap_new(10, 10, 16, 16);
    rt_string coll = make_str("collision");
    int64_t coll_id = rt_tilemap_add_layer(tm, coll);
    assert(coll_id == 1);

    // Default collision layer is 0
    assert(rt_tilemap_get_collision_layer(tm) == 0);

    // Set collision layer to 1
    rt_tilemap_set_collision_layer(tm, 1);
    assert(rt_tilemap_get_collision_layer(tm) == 1);

    // Place solid tile on collision layer (layer 1)
    rt_tilemap_set_tile_layer(tm, 1, 5, 9, 1);
    rt_tilemap_set_collision(tm, 1, 1); // tile ID 1 = solid

    // IsSolidAt should read from collision layer (1), not layer 0
    int8_t solid = rt_tilemap_is_solid_at(tm, 5 * 16 + 1, 9 * 16 + 1);
    assert(solid == 1);

    // Layer 0 at same position has no tile — if collision layer were 0, it would not be solid
    assert(rt_tilemap_get_tile_layer(tm, 0, 5, 9) == 0);
    PASS();
}

static void test_negative_pixel_to_tile_rounds_down(void) {
    TEST("Negative pixel-to-tile conversion rounds down");
    void *tm = rt_tilemap_new(10, 10, 16, 16);
    assert(tm != NULL);

    int64_t tx = 0;
    int64_t ty = 0;
    rt_tilemap_pixel_to_tile(tm, -1, -1, &tx, &ty);
    assert(tx == -1);
    assert(ty == -1);

    rt_tilemap_pixel_to_tile(tm, -17, -33, &tx, &ty);
    assert(tx == -2);
    assert(ty == -3);

    assert(rt_tilemap_to_tile_x(tm, -1) == -1);
    assert(rt_tilemap_to_tile_x(tm, -17) == -2);
    assert(rt_tilemap_to_tile_y(tm, -1) == -1);
    assert(rt_tilemap_to_tile_y(tm, -33) == -3);
    PASS();
}

static void test_negative_pixels_do_not_hit_tile_zero(void) {
    TEST("Negative pixel samples stay outside tile 0");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    assert(tm != NULL);

    rt_tilemap_set_tile(tm, 0, 0, 1);
    rt_tilemap_set_collision(tm, 1, 1);

    assert(rt_tilemap_is_solid_at(tm, 1, 1) == 1);
    assert(rt_tilemap_is_solid_at(tm, -1, 1) == 0);
    assert(rt_tilemap_is_solid_at(tm, 1, -1) == 0);
    assert(rt_tilemap_is_solid_at(tm, -1, -1) == 0);
    PASS();
}

static void test_negative_body_does_not_false_collide(void) {
    TEST("Body outside left edge does not false-collide");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    assert(tm != NULL);

    rt_tilemap_set_tile(tm, 0, 0, 1);
    rt_tilemap_set_collision(tm, 1, 1);

    void *body = rt_physics2d_body_new(-15.5, 1.0, 15.0, 8.0, 1.0);
    assert(body != NULL);

    assert(rt_tilemap_collide_body(tm, body) == 0);
    assert(rt_physics2d_body_x(body) == -15.5);
    assert(rt_physics2d_body_y(body) == 1.0);
    PASS();
}

static void test_backwards_compatibility(void) {
    TEST("Backwards compatibility (single-layer API on layer 0)");
    void *tm = rt_tilemap_new(10, 10, 16, 16);

    // Add a second layer
    rt_string l1 = make_str("extra");
    rt_tilemap_add_layer(tm, l1);

    // Use old API
    rt_tilemap_set_tile(tm, 2, 3, 55);
    assert(rt_tilemap_get_tile(tm, 2, 3) == 55);

    // Should match layer 0
    assert(rt_tilemap_get_tile_layer(tm, 0, 2, 3) == 55);

    // Layer 1 unaffected
    assert(rt_tilemap_get_tile_layer(tm, 1, 2, 3) == 0);

    // Fill via old API
    rt_tilemap_fill(tm, 10);
    assert(rt_tilemap_get_tile(tm, 0, 0) == 10);
    assert(rt_tilemap_get_tile_layer(tm, 0, 0, 0) == 10);
    assert(rt_tilemap_get_tile_layer(tm, 1, 0, 0) == 0); // layer 1 untouched

    // Clear via old API
    rt_tilemap_clear(tm);
    assert(rt_tilemap_get_tile(tm, 0, 0) == 0);
    PASS();
}

static void test_remove_layer(void) {
    TEST("Remove layer shifts indices");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string l1 = make_str("mid");
    rt_string l2 = make_str("top");
    rt_tilemap_add_layer(tm, l1); // layer 1
    rt_tilemap_add_layer(tm, l2); // layer 2
    assert(rt_tilemap_get_layer_count(tm) == 3);

    // Set tiles on each layer
    rt_tilemap_set_tile_layer(tm, 1, 0, 0, 11);
    rt_tilemap_set_tile_layer(tm, 2, 0, 0, 22);

    // Remove layer 1 ("mid")
    rt_tilemap_remove_layer(tm, 1);
    assert(rt_tilemap_get_layer_count(tm) == 2);

    // Former layer 2 ("top") is now layer 1
    assert(rt_tilemap_get_tile_layer(tm, 1, 0, 0) == 22);

    // Lookup by name: "top" should now be at index 1
    assert(rt_tilemap_get_layer_by_name(tm, l2) == 1);
    PASS();
}

static void test_cannot_remove_base_layer(void) {
    TEST("Cannot remove base layer (layer 0)");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_tilemap_set_tile(tm, 0, 0, 99);

    rt_tilemap_remove_layer(tm, 0); // should be a no-op
    assert(rt_tilemap_get_layer_count(tm) == 1);
    assert(rt_tilemap_get_tile(tm, 0, 0) == 99);
    PASS();
}

static void test_max_layers(void) {
    TEST("Maximum 16 layers");
    void *tm = rt_tilemap_new(2, 2, 16, 16);
    // Layer 0 already exists, add 15 more
    for (int i = 1; i < 16; i++) {
        rt_string name = make_str("layer");
        int64_t id = rt_tilemap_add_layer(tm, name);
        assert(id == i);
    }
    assert(rt_tilemap_get_layer_count(tm) == 16);

    // 17th should fail
    rt_string extra = make_str("overflow");
    assert(rt_tilemap_add_layer(tm, extra) == -1);
    assert(rt_tilemap_get_layer_count(tm) == 16);
    PASS();
}

static void test_invalid_layer_id(void) {
    TEST("Invalid layer ID returns defaults");
    void *tm = rt_tilemap_new(4, 4, 16, 16);

    // Get tile from non-existent layer returns 0
    assert(rt_tilemap_get_tile_layer(tm, 5, 0, 0) == 0);
    assert(rt_tilemap_get_tile_layer(tm, -1, 0, 0) == 0);

    // Set tile on non-existent layer is a no-op
    rt_tilemap_set_tile_layer(tm, 99, 0, 0, 42);

    // Visibility of non-existent layer returns 0
    assert(rt_tilemap_get_layer_visible(tm, 10) == 0);
    PASS();
}

static void test_null_safety(void) {
    TEST("NULL tilemap safety");
    assert(rt_tilemap_get_layer_count(NULL) == 0);
    assert(rt_tilemap_get_collision_layer(NULL) == 0);
    assert(rt_tilemap_get_tile_layer(NULL, 0, 0, 0) == 0);
    assert(rt_tilemap_get_layer_visible(NULL, 0) == 0);
    assert(rt_tilemap_add_layer(NULL, NULL) == -1);

    // These should not crash
    rt_tilemap_set_tile_layer(NULL, 0, 0, 0, 1);
    rt_tilemap_fill_layer(NULL, 0, 1);
    rt_tilemap_clear_layer(NULL, 0);
    rt_tilemap_remove_layer(NULL, 0);
    rt_tilemap_set_layer_visible(NULL, 0, 1);
    rt_tilemap_set_collision_layer(NULL, 0);
    rt_tilemap_draw_layer(NULL, NULL, 0, 0, 0);
    PASS();
}

static void test_collision_layer_adjusts_on_remove(void) {
    TEST("Collision layer adjusts on layer removal");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string l1 = make_str("a");
    rt_string l2 = make_str("b");
    rt_tilemap_add_layer(tm, l1); // layer 1
    rt_tilemap_add_layer(tm, l2); // layer 2

    // Set collision layer to 2
    rt_tilemap_set_collision_layer(tm, 2);
    assert(rt_tilemap_get_collision_layer(tm) == 2);

    // Remove layer 1 — collision layer should shift down to 1
    rt_tilemap_remove_layer(tm, 1);
    assert(rt_tilemap_get_collision_layer(tm) == 1);
    PASS();
}

static void test_collision_layer_resets_on_remove_self(void) {
    TEST("Collision layer resets to 0 when its layer is removed");
    void *tm = rt_tilemap_new(4, 4, 16, 16);
    rt_string l1 = make_str("c");
    rt_tilemap_add_layer(tm, l1); // layer 1

    rt_tilemap_set_collision_layer(tm, 1);
    assert(rt_tilemap_get_collision_layer(tm) == 1);

    // Remove the collision layer itself
    rt_tilemap_remove_layer(tm, 1);
    assert(rt_tilemap_get_collision_layer(tm) == 0);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("test_rt_tilemap_layers:\n");

    test_default_single_layer();
    test_add_and_name_layers();
    test_per_layer_tile_independence();
    test_fill_and_clear_layer();
    test_layer_visibility();
    test_collision_layer_designation();
    test_negative_pixel_to_tile_rounds_down();
    test_negative_pixels_do_not_hit_tile_zero();
    test_negative_body_does_not_false_collide();
    test_backwards_compatibility();
    test_remove_layer();
    test_cannot_remove_base_layer();
    test_max_layers();
    test_invalid_layer_id();
    test_null_safety();
    test_collision_layer_adjusts_on_remove();
    test_collision_layer_resets_on_remove_self();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
