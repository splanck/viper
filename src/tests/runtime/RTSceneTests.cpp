//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTSceneTests.cpp
// Purpose: Tests for Viper.Graphics.Scene and SceneNode.
//
//===----------------------------------------------------------------------===//

#include "rt_scene.h"
#include "rt_string.h"
#include "tests/common/PosixCompat.h"

#include <cassert>
#include <cstdio>

// Stub for rt_abort that's used by runtime
extern "C" void rt_abort(const char *msg);

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// SceneNode Creation Tests
// ============================================================================

static void test_scene_node_new()
{
    void *node = rt_scene_node_new();
    assert(node != nullptr);

    // Check default values
    assert(rt_scene_node_get_x(node) == 0);
    assert(rt_scene_node_get_y(node) == 0);
    assert(rt_scene_node_get_scale_x(node) == 100);
    assert(rt_scene_node_get_scale_y(node) == 100);
    assert(rt_scene_node_get_rotation(node) == 0);
    assert(rt_scene_node_get_visible(node) == 1);
    assert(rt_scene_node_get_depth(node) == 0);

    printf("test_scene_node_new: PASSED\n");
}

// ============================================================================
// SceneNode Transform Tests
// ============================================================================

static void test_scene_node_position()
{
    void *node = rt_scene_node_new();

    rt_scene_node_set_x(node, 100);
    rt_scene_node_set_y(node, 200);

    assert(rt_scene_node_get_x(node) == 100);
    assert(rt_scene_node_get_y(node) == 200);

    // World position should be same for root node
    assert(rt_scene_node_get_world_x(node) == 100);
    assert(rt_scene_node_get_world_y(node) == 200);

    printf("test_scene_node_position: PASSED\n");
}

static void test_scene_node_scale()
{
    void *node = rt_scene_node_new();

    rt_scene_node_set_scale_x(node, 200);
    rt_scene_node_set_scale_y(node, 50);

    assert(rt_scene_node_get_scale_x(node) == 200);
    assert(rt_scene_node_get_scale_y(node) == 50);
    assert(rt_scene_node_get_world_scale_x(node) == 200);
    assert(rt_scene_node_get_world_scale_y(node) == 50);

    printf("test_scene_node_scale: PASSED\n");
}

static void test_scene_node_rotation()
{
    void *node = rt_scene_node_new();

    rt_scene_node_set_rotation(node, 45);
    assert(rt_scene_node_get_rotation(node) == 45);
    assert(rt_scene_node_get_world_rotation(node) == 45);

    printf("test_scene_node_rotation: PASSED\n");
}

// ============================================================================
// SceneNode Hierarchy Tests
// ============================================================================

static void test_scene_node_hierarchy()
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();

    rt_scene_node_add_child(parent, child);

    assert(rt_scene_node_child_count(parent) == 1);
    assert(rt_scene_node_get_child(parent, 0) == child);
    assert(rt_scene_node_get_parent(child) == parent);

    rt_scene_node_remove_child(parent, child);
    assert(rt_scene_node_child_count(parent) == 0);
    assert(rt_scene_node_get_parent(child) == nullptr);

    printf("test_scene_node_hierarchy: PASSED\n");
}

static void test_scene_node_transform_inheritance()
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();

    // Position parent at (100, 100)
    rt_scene_node_set_x(parent, 100);
    rt_scene_node_set_y(parent, 100);

    // Position child at (50, 50) relative to parent
    rt_scene_node_set_x(child, 50);
    rt_scene_node_set_y(child, 50);

    rt_scene_node_add_child(parent, child);

    // Child's world position should be parent + local
    assert(rt_scene_node_get_world_x(child) == 150);
    assert(rt_scene_node_get_world_y(child) == 150);

    printf("test_scene_node_transform_inheritance: PASSED\n");
}

static void test_scene_node_scale_inheritance()
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();

    // Scale parent to 200%
    rt_scene_node_set_scale_x(parent, 200);
    rt_scene_node_set_scale_y(parent, 200);

    // Child has 50% local scale
    rt_scene_node_set_scale_x(child, 50);
    rt_scene_node_set_scale_y(child, 50);

    rt_scene_node_add_child(parent, child);

    // Child world scale should be combined: 200% * 50% = 100%
    assert(rt_scene_node_get_world_scale_x(child) == 100);
    assert(rt_scene_node_get_world_scale_y(child) == 100);

    printf("test_scene_node_scale_inheritance: PASSED\n");
}

static void test_scene_node_rotation_inheritance()
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();

    rt_scene_node_set_rotation(parent, 30);
    rt_scene_node_set_rotation(child, 15);

    rt_scene_node_add_child(parent, child);

    // Child world rotation should be sum: 30 + 15 = 45
    assert(rt_scene_node_get_world_rotation(child) == 45);

    printf("test_scene_node_rotation_inheritance: PASSED\n");
}

// ============================================================================
// SceneNode Name/Find Tests
// ============================================================================

static void test_scene_node_name()
{
    void *node = rt_scene_node_new();
    rt_string name = rt_const_cstr("player");
    rt_scene_node_set_name(node, name);

    rt_string result = rt_scene_node_get_name(node);
    assert(rt_string_cstr(result) != nullptr);

    printf("test_scene_node_name: PASSED\n");
}

static void test_scene_node_find()
{
    void *root = rt_scene_node_new();
    void *child1 = rt_scene_node_new();
    void *child2 = rt_scene_node_new();

    rt_scene_node_set_name(child1, rt_const_cstr("enemy"));
    rt_scene_node_set_name(child2, rt_const_cstr("player"));

    rt_scene_node_add_child(root, child1);
    rt_scene_node_add_child(root, child2);

    void *found = rt_scene_node_find(root, rt_const_cstr("player"));
    assert(found == child2);

    found = rt_scene_node_find(root, rt_const_cstr("notfound"));
    assert(found == nullptr);

    printf("test_scene_node_find: PASSED\n");
}

// ============================================================================
// SceneNode Methods Tests
// ============================================================================

static void test_scene_node_move()
{
    void *node = rt_scene_node_new();
    rt_scene_node_set_x(node, 10);
    rt_scene_node_set_y(node, 20);

    rt_scene_node_move(node, 5, -10);

    assert(rt_scene_node_get_x(node) == 15);
    assert(rt_scene_node_get_y(node) == 10);

    printf("test_scene_node_move: PASSED\n");
}

static void test_scene_node_set_position()
{
    void *node = rt_scene_node_new();

    rt_scene_node_set_position(node, 100, 200);

    assert(rt_scene_node_get_x(node) == 100);
    assert(rt_scene_node_get_y(node) == 200);

    printf("test_scene_node_set_position: PASSED\n");
}

static void test_scene_node_set_scale()
{
    void *node = rt_scene_node_new();

    rt_scene_node_set_scale(node, 150);

    assert(rt_scene_node_get_scale_x(node) == 150);
    assert(rt_scene_node_get_scale_y(node) == 150);

    printf("test_scene_node_set_scale: PASSED\n");
}

static void test_scene_node_detach()
{
    void *parent = rt_scene_node_new();
    void *child = rt_scene_node_new();

    rt_scene_node_add_child(parent, child);
    assert(rt_scene_node_get_parent(child) == parent);

    rt_scene_node_detach(child);
    assert(rt_scene_node_get_parent(child) == nullptr);
    assert(rt_scene_node_child_count(parent) == 0);

    printf("test_scene_node_detach: PASSED\n");
}

static void test_scene_node_visibility()
{
    void *node = rt_scene_node_new();

    assert(rt_scene_node_get_visible(node) == 1);

    rt_scene_node_set_visible(node, 0);
    assert(rt_scene_node_get_visible(node) == 0);

    rt_scene_node_set_visible(node, 1);
    assert(rt_scene_node_get_visible(node) == 1);

    printf("test_scene_node_visibility: PASSED\n");
}

static void test_scene_node_depth()
{
    void *node = rt_scene_node_new();

    rt_scene_node_set_depth(node, 5);
    assert(rt_scene_node_get_depth(node) == 5);

    rt_scene_node_set_depth(node, -10);
    assert(rt_scene_node_get_depth(node) == -10);

    printf("test_scene_node_depth: PASSED\n");
}

// ============================================================================
// Scene Tests
// ============================================================================

static void test_scene_new()
{
    void *scene = rt_scene_new();
    assert(scene != nullptr);

    void *root = rt_scene_get_root(scene);
    assert(root != nullptr);

    printf("test_scene_new: PASSED\n");
}

static void test_scene_add_remove()
{
    void *scene = rt_scene_new();
    void *node = rt_scene_node_new();

    rt_scene_add(scene, node);

    void *root = rt_scene_get_root(scene);
    assert(rt_scene_node_child_count(root) == 1);
    assert(rt_scene_node_get_parent(node) == root);

    rt_scene_remove(scene, node);
    assert(rt_scene_node_child_count(root) == 0);

    printf("test_scene_add_remove: PASSED\n");
}

static void test_scene_find()
{
    void *scene = rt_scene_new();
    void *node = rt_scene_node_new();
    rt_scene_node_set_name(node, rt_const_cstr("hero"));

    rt_scene_add(scene, node);

    void *found = rt_scene_find(scene, rt_const_cstr("hero"));
    assert(found == node);

    found = rt_scene_find(scene, rt_const_cstr("villain"));
    assert(found == nullptr);

    printf("test_scene_find: PASSED\n");
}

static void test_scene_clear()
{
    void *scene = rt_scene_new();

    rt_scene_add(scene, rt_scene_node_new());
    rt_scene_add(scene, rt_scene_node_new());
    rt_scene_add(scene, rt_scene_node_new());

    void *root = rt_scene_get_root(scene);
    assert(rt_scene_node_child_count(root) == 3);

    rt_scene_clear(scene);
    assert(rt_scene_node_child_count(root) == 0);

    printf("test_scene_clear: PASSED\n");
}

static void test_scene_update()
{
    void *scene = rt_scene_new();
    void *node = rt_scene_node_new();
    rt_scene_add(scene, node);

    // Update should not crash
    rt_scene_update(scene);

    printf("test_scene_update: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("Running Scene graph tests...\n\n");

    // SceneNode creation
    test_scene_node_new();

    // SceneNode transforms
    test_scene_node_position();
    test_scene_node_scale();
    test_scene_node_rotation();

    // SceneNode hierarchy
    test_scene_node_hierarchy();
    test_scene_node_transform_inheritance();
    test_scene_node_scale_inheritance();
    test_scene_node_rotation_inheritance();

    // SceneNode name/find
    test_scene_node_name();
    test_scene_node_find();

    // SceneNode methods
    test_scene_node_move();
    test_scene_node_set_position();
    test_scene_node_set_scale();
    test_scene_node_detach();
    test_scene_node_visibility();
    test_scene_node_depth();

    // Scene tests
    test_scene_new();
    test_scene_add_remove();
    test_scene_find();
    test_scene_clear();
    test_scene_update();

    printf("\nAll Scene graph tests passed!\n");
    return 0;
}
