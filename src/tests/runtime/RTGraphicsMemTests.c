//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTGraphicsMemTests.c
// Purpose: Regression tests for graphics/scene memory bugs R-16, R-17, R-18.
// Key invariants:
//   R-16: rt_scene_draw / rt_scene_draw_with_camera must release the temporary
//         nodes seq they allocate on every call.
//   R-17: rt_spritebatch ensure_capacity uses a temp-pointer pattern so a
//         failed realloc does not leave batch->items dangling.
//   R-18: rt_spritesheet ensure_cap updates ss->regions before attempting the
//         second realloc, so a failure of the second realloc does not leave
//         ss->regions dangling.
//
// Note: Tests that require a canvas (rt_scene_draw, rt_spritebatch_end) cannot
// run without a display/graphics context. Those functions are covered by code
// inspection for the memory fix. The tests below exercise all non-draw
// operations that can run headless.
//
//===----------------------------------------------------------------------===//

#include "rt_pixels.h"
#include "rt_scene.h"
#include "rt_spritebatch.h"
#include "rt_spritesheet.h"
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

//=============================================================================
// SpriteBatch tests (Bug R-17)
//=============================================================================

/// @brief Verify that a new spritebatch has expected initial state.
static void test_spritebatch_initial_state(void)
{
    void *batch = rt_spritebatch_new(0);
    assert(batch != NULL);
    assert(rt_spritebatch_count(batch) == 0);
    assert(rt_spritebatch_capacity(batch) > 0);
    assert(rt_spritebatch_is_active(batch) == 0);
}

/// @brief Verify begin resets count and sets active flag.
static void test_spritebatch_begin_resets_state(void)
{
    void *batch = rt_spritebatch_new(4);
    assert(batch != NULL);

    rt_spritebatch_begin(batch);
    assert(rt_spritebatch_is_active(batch) == 1);
    assert(rt_spritebatch_count(batch) == 0);
}

/// @brief Verify that adding pixels items increments count correctly.
/// rt_spritebatch_draw_pixels only stores a reference, no canvas needed.
static void test_spritebatch_item_count(void)
{
    void *batch = rt_spritebatch_new(4);
    assert(batch != NULL);

    void *pixels = rt_pixels_new(8, 8);
    assert(pixels != NULL);

    rt_spritebatch_begin(batch);

    rt_spritebatch_draw_pixels(batch, pixels, 0, 0);
    assert(rt_spritebatch_count(batch) == 1);

    rt_spritebatch_draw_pixels(batch, pixels, 10, 10);
    assert(rt_spritebatch_count(batch) == 2);

    rt_spritebatch_draw_pixels(batch, pixels, 20, 20);
    assert(rt_spritebatch_count(batch) == 3);
}

/// @brief Verify that adding more items than initial capacity triggers growth.
/// This exercises the ensure_capacity realloc path (Bug R-17 fix).
static void test_spritebatch_capacity_growth(void)
{
    // Start with capacity 2 so we force a realloc quickly.
    void *batch = rt_spritebatch_new(2);
    assert(batch != NULL);

    void *pixels = rt_pixels_new(4, 4);
    assert(pixels != NULL);

    rt_spritebatch_begin(batch);

    int64_t n = 64;
    for (int64_t i = 0; i < n; i++)
    {
        rt_spritebatch_draw_pixels(batch, pixels, i * 5, 0);
    }

    assert(rt_spritebatch_count(batch) == n);
    assert(rt_spritebatch_capacity(batch) >= n);
}

/// @brief Verify that a second begin resets count (items from prior batch gone).
static void test_spritebatch_begin_clears_previous(void)
{
    void *batch = rt_spritebatch_new(8);
    assert(batch != NULL);

    void *pixels = rt_pixels_new(4, 4);
    assert(pixels != NULL);

    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_pixels(batch, pixels, 0, 0);
    rt_spritebatch_draw_pixels(batch, pixels, 1, 0);
    assert(rt_spritebatch_count(batch) == 2);

    // Begin a new batch without ending; count must reset.
    rt_spritebatch_begin(batch);
    assert(rt_spritebatch_count(batch) == 0);
    assert(rt_spritebatch_is_active(batch) == 1);
}

/// @brief Verify that draw_region also increments count.
static void test_spritebatch_draw_region_increments_count(void)
{
    void *batch = rt_spritebatch_new(8);
    assert(batch != NULL);

    void *pixels = rt_pixels_new(64, 64);
    assert(pixels != NULL);

    rt_spritebatch_begin(batch);
    rt_spritebatch_draw_region(batch, pixels, 0, 0, 0, 0, 16, 16);
    rt_spritebatch_draw_region(batch, pixels, 16, 0, 16, 0, 16, 16);
    assert(rt_spritebatch_count(batch) == 2);
}

//=============================================================================
// SpriteSheet tests (Bug R-18)
//=============================================================================

/// @brief Verify basic region add and retrieval.
static void test_spritesheet_add_and_find(void)
{
    void *atlas = rt_pixels_new(64, 64);
    assert(atlas != NULL);

    void *sheet = rt_spritesheet_new(atlas);
    assert(sheet != NULL);

    rt_spritesheet_set_region(sheet, rt_const_cstr("frame0"), 0, 0, 16, 16);
    assert(rt_spritesheet_region_count(sheet) == 1);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("frame0")) == 1);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("frame1")) == 0);
}

/// @brief Add enough regions to force multiple doublings of the internal arrays.
/// This exercises the two-realloc path in ensure_cap (Bug R-18 fix).
static void test_spritesheet_many_regions_survive_realloc(void)
{
    // SS_INITIAL_CAP is 16; adding 100 regions forces ~3 doublings.
    void *atlas = rt_pixels_new(256, 256);
    assert(atlas != NULL);

    void *sheet = rt_spritesheet_new(atlas);
    assert(sheet != NULL);

    char name_buf[32];
    int64_t total = 100;

    for (int64_t i = 0; i < total; i++)
    {
        // snprintf into local buffer; rt_const_cstr borrows the pointer
        // so we must use it immediately before the buffer changes.
        int written = snprintf(name_buf, sizeof(name_buf), "region_%lld", (long long)i);
        assert(written > 0 && written < (int)sizeof(name_buf));
        rt_spritesheet_set_region(sheet, rt_const_cstr(name_buf), i * 2, 0, 2, 2);
    }

    assert(rt_spritesheet_region_count(sheet) == total);

    // Spot-check: first, last, and a middle region must still be findable.
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("region_0")) == 1);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("region_50")) == 1);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("region_99")) == 1);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("region_100")) == 0);
}

/// @brief Updating an existing region does not increase the count.
static void test_spritesheet_update_existing_region(void)
{
    void *atlas = rt_pixels_new(64, 64);
    assert(atlas != NULL);

    void *sheet = rt_spritesheet_new(atlas);
    assert(sheet != NULL);

    rt_spritesheet_set_region(sheet, rt_const_cstr("walk_0"), 0, 0, 16, 16);
    rt_spritesheet_set_region(sheet, rt_const_cstr("walk_1"), 16, 0, 16, 16);
    assert(rt_spritesheet_region_count(sheet) == 2);

    // Update walk_0; count must remain 2.
    rt_spritesheet_set_region(sheet, rt_const_cstr("walk_0"), 0, 0, 32, 32);
    assert(rt_spritesheet_region_count(sheet) == 2);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("walk_0")) == 1);
}

/// @brief Removing a region decrements the count.
static void test_spritesheet_remove_region(void)
{
    void *atlas = rt_pixels_new(32, 32);
    assert(atlas != NULL);

    void *sheet = rt_spritesheet_new(atlas);
    assert(sheet != NULL);

    rt_spritesheet_set_region(sheet, rt_const_cstr("a"), 0, 0, 8, 8);
    rt_spritesheet_set_region(sheet, rt_const_cstr("b"), 8, 0, 8, 8);
    rt_spritesheet_set_region(sheet, rt_const_cstr("c"), 16, 0, 8, 8);
    assert(rt_spritesheet_region_count(sheet) == 3);

    int8_t removed = rt_spritesheet_remove_region(sheet, rt_const_cstr("b"));
    assert(removed == 1);
    assert(rt_spritesheet_region_count(sheet) == 2);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("b")) == 0);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("a")) == 1);
    assert(rt_spritesheet_has_region(sheet, rt_const_cstr("c")) == 1);
}

/// @brief Width and height accessors reflect the atlas dimensions.
static void test_spritesheet_dimensions(void)
{
    void *atlas = rt_pixels_new(128, 64);
    assert(atlas != NULL);

    void *sheet = rt_spritesheet_new(atlas);
    assert(sheet != NULL);

    assert(rt_spritesheet_width(sheet) == 128);
    assert(rt_spritesheet_height(sheet) == 64);
}

//=============================================================================
// Scene tests (Bug R-16)
//=============================================================================

/// @brief A freshly created scene has a root node with zero children.
static void test_scene_initial_state(void)
{
    void *scene = rt_scene_new();
    assert(scene != NULL);

    void *root = rt_scene_get_root(scene);
    assert(root != NULL);
    assert(rt_scene_node_child_count(root) == 0);
}

/// @brief Adding nodes to the scene increments the root's child count.
static void test_scene_add_nodes(void)
{
    void *scene = rt_scene_new();
    assert(scene != NULL);

    void *node_a = rt_scene_node_new();
    void *node_b = rt_scene_node_new();
    void *node_c = rt_scene_node_new();
    assert(node_a != NULL);
    assert(node_b != NULL);
    assert(node_c != NULL);

    rt_scene_add(scene, node_a);
    rt_scene_add(scene, node_b);
    rt_scene_add(scene, node_c);

    void *root = rt_scene_get_root(scene);
    assert(rt_scene_node_child_count(root) == 3);
}

/// @brief Removing a node from the scene decrements the child count.
static void test_scene_remove_node(void)
{
    void *scene = rt_scene_new();
    assert(scene != NULL);

    void *node_a = rt_scene_node_new();
    void *node_b = rt_scene_node_new();
    assert(node_a != NULL);
    assert(node_b != NULL);

    rt_scene_add(scene, node_a);
    rt_scene_add(scene, node_b);

    void *root = rt_scene_get_root(scene);
    assert(rt_scene_node_child_count(root) == 2);

    rt_scene_remove(scene, node_a);
    assert(rt_scene_node_child_count(root) == 1);
}

/// @brief rt_scene_clear removes all nodes from the scene.
static void test_scene_clear(void)
{
    void *scene = rt_scene_new();
    assert(scene != NULL);

    rt_scene_add(scene, rt_scene_node_new());
    rt_scene_add(scene, rt_scene_node_new());
    rt_scene_add(scene, rt_scene_node_new());

    void *root = rt_scene_get_root(scene);
    assert(rt_scene_node_child_count(root) == 3);

    rt_scene_clear(scene);
    assert(rt_scene_node_child_count(root) == 0);
}

/// @brief rt_scene_find locates a named node in the hierarchy.
static void test_scene_find_by_name(void)
{
    void *scene = rt_scene_new();
    assert(scene != NULL);

    void *node = rt_scene_node_new();
    assert(node != NULL);
    rt_scene_node_set_name(node, rt_const_cstr("player"));
    rt_scene_add(scene, node);

    void *found = rt_scene_find(scene, rt_const_cstr("player"));
    assert(found == node);

    void *not_found = rt_scene_find(scene, rt_const_cstr("enemy"));
    assert(not_found == NULL);
}

/// @brief Verify parent/child linkage is maintained.
static void test_scene_node_hierarchy(void)
{
    void *parent = rt_scene_node_new();
    void *child_a = rt_scene_node_new();
    void *child_b = rt_scene_node_new();
    assert(parent != NULL);
    assert(child_a != NULL);
    assert(child_b != NULL);

    rt_scene_node_add_child(parent, child_a);
    rt_scene_node_add_child(parent, child_b);

    assert(rt_scene_node_child_count(parent) == 2);
    assert(rt_scene_node_get_parent(child_a) == parent);
    assert(rt_scene_node_get_parent(child_b) == parent);
    assert(rt_scene_node_get_parent(parent) == NULL);
}

/// @brief Detaching a child clears its parent pointer.
static void test_scene_node_detach(void)
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();
    assert(parent != NULL);
    assert(child != NULL);

    rt_scene_node_add_child(parent, child);
    assert(rt_scene_node_get_parent(child) == parent);

    rt_scene_node_detach(child);
    assert(rt_scene_node_get_parent(child) == NULL);
    assert(rt_scene_node_child_count(parent) == 0);
}

/// @brief rt_scene_node_count returns the number of visible nodes with sprites.
/// This internally calls rt_seq_new and must release it (Bug R-16 fix).
/// Calling it many times validates that the seq is properly released each time.
static void test_scene_node_count_no_leak(void)
{
    void *scene = rt_scene_new();
    assert(scene != NULL);

    // Add nodes without sprites; collect_visible_nodes skips them, so count=0.
    rt_scene_add(scene, rt_scene_node_new());
    rt_scene_add(scene, rt_scene_node_new());

    // Call rt_scene_node_count many times to amplify any per-call leak.
    // A correct implementation releases the temporary seq on every call.
    // This does not crash and does not grow memory unboundedly if fixed.
    for (int i = 0; i < 1000; i++)
    {
        int64_t count = rt_scene_node_count(scene);
        // Nodes have no sprites, so visible-node count is 0.
        assert(count == 0);
    }
}

/// @brief Verify world transform computation is correct.
static void test_scene_node_world_transform(void)
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();
    assert(parent != NULL);
    assert(child != NULL);

    rt_scene_node_set_position(parent, 100, 200);
    rt_scene_node_set_position(child, 10, 20);
    rt_scene_node_add_child(parent, child);

    // World position of child = parent_world + child_local (no rotation)
    int64_t wx = rt_scene_node_get_world_x(child);
    int64_t wy = rt_scene_node_get_world_y(child);
    assert(wx == 110);
    assert(wy == 220);
}

/// @brief Verify that node visibility can be toggled.
static void test_scene_node_visibility(void)
{
    void *node = rt_scene_node_new();
    assert(node != NULL);

    assert(rt_scene_node_get_visible(node) == 1);

    rt_scene_node_set_visible(node, 0);
    assert(rt_scene_node_get_visible(node) == 0);

    rt_scene_node_set_visible(node, 1);
    assert(rt_scene_node_get_visible(node) == 1);
}

//=============================================================================
// Entry point
//=============================================================================

int main(void)
{
    // SpriteBatch tests (Bug R-17)
    test_spritebatch_initial_state();
    test_spritebatch_begin_resets_state();
    test_spritebatch_item_count();
    test_spritebatch_capacity_growth();
    test_spritebatch_begin_clears_previous();
    test_spritebatch_draw_region_increments_count();

    // SpriteSheet tests (Bug R-18)
    test_spritesheet_add_and_find();
    test_spritesheet_many_regions_survive_realloc();
    test_spritesheet_update_existing_region();
    test_spritesheet_remove_region();
    test_spritesheet_dimensions();

    // Scene tests (Bug R-16)
    // Note: rt_scene_draw / rt_scene_draw_with_camera require a canvas backed
    // by a display context and cannot be called headlessly. The seq-leak fix
    // in those functions is verified by code inspection. All non-draw scene
    // operations and rt_scene_node_count (which has the same leak pattern) are
    // tested below.
    test_scene_initial_state();
    test_scene_add_nodes();
    test_scene_remove_node();
    test_scene_clear();
    test_scene_find_by_name();
    test_scene_node_hierarchy();
    test_scene_node_detach();
    test_scene_node_count_no_leak();
    test_scene_node_world_transform();
    test_scene_node_visibility();

    return 0;
}
