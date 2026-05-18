//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_scene.c
// Purpose: Hierarchical scene graph for Viper games. Manages a tree of named
//   nodes (entities), each with a 2D transform (position, scale, rotation)
//   that accumulates parent transforms. Provides name-based lookup, child
//   iteration, and batched Draw(canvas, camera) that traverses the tree in
//   order and renders each visible node's sprite or custom draw callback.
//
// Key invariants:
//   - Each node has a unique name (string, owned by the node). Node lookup
//     (FindByName) does a depth-first search — O(n) in the worst case.
//   - Transforms are accumulated: a child's world position = its local position
//     + parent's world position (similarly for rotation and scale). Rotations
//     are additive (degrees), scales are multiplicative.
//   - The root node has no parent. Adding a child increments the child's
//     reference count; removing or destroying the parent decrements it.
//   - Nodes are reference-counted. A node destroyed while still attached to
//     a parent is detached first (its parent pointer is cleared) before the
//     reference count reaches zero.
//   - Scene draws are depth-sorted globally. Nodes with equal depth preserve
//     traversal order so sibling/insertion order remains stable for ties.
//
// Ownership/Lifetime:
//   - Scene (root) objects are GC-managed (rt_obj_new_i64). Nodes are
//     reference-counted. The scene holds a retained reference to the root
//     node; destroying the scene triggers a cascade destroy of the tree.
//
// Links: src/runtime/graphics/rt_scene.h (public API),
//        src/runtime/graphics/rt_sprite.h (node sprite payload),
//        docs/viperlib/game.md (Scene section)
//
//===----------------------------------------------------------------------===//

#include "rt_scene.h"
#include "rt_camera.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_sprite.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration from rt_io.c
#include "rt_trap.h"

//=============================================================================
// Internal Structures
//=============================================================================

typedef struct scene_node_impl {
    // Local transform (relative to parent)
    int64_t x;
    int64_t y;
    int64_t scale_x;  // 100 = 100%
    int64_t scale_y;  // 100 = 100%
    int64_t rotation; // degrees
    int64_t depth;    // Z-order
    int8_t visible;   // visibility flag

    // Cached world transform
    int64_t world_x;
    int64_t world_y;
    int64_t world_scale_x;
    int64_t world_scale_y;
    int64_t world_rotation;
    int8_t transform_dirty; // needs recalculation

    // Hierarchy
    struct scene_node_impl *parent;
    void *children; // Seq of child nodes

    // Content
    void *sprite;   // Attached sprite (nullable)
    rt_string name; // Tag/identifier
} scene_node_impl;

typedef struct scene_impl {
    scene_node_impl *root;
} scene_impl;

/// @brief Validate-and-return a SceneNode pointer; NULL for NULL or wrong class.
/// @details Soft check used by every public SceneNode entry point.
static scene_node_impl *scene_node_checked_or_null(void *node_ptr) {
    if (!node_ptr || !rt_obj_is_instance(node_ptr, RT_SCENE_NODE_CLASS_ID, sizeof(scene_node_impl)))
        return NULL;
    return (scene_node_impl *)node_ptr;
}

/// @brief Validate-and-return a Scene pointer; NULL for NULL or wrong class.
/// @details Soft check used by every public Scene entry point.
static scene_impl *scene_checked_or_null(void *scene_ptr) {
    if (!scene_ptr || !rt_obj_is_instance(scene_ptr, RT_SCENE_CLASS_ID, sizeof(scene_impl)))
        return NULL;
    return (scene_impl *)scene_ptr;
}

/// @brief Add two int64 values, saturating at INT64_MIN/MAX instead of wrapping.
/// @details Used to compose accumulated transforms (parent + child position)
///          without UB on overflow. Negative @p b correctly saturates at MIN.
static int64_t scene_add_saturating(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Round a long double to int64 with banker-style half-away rounding, saturating on
/// overflow.
/// @details Used as the final step of every long-double world-transform
///          calculation so the result lands cleanly in int64 storage. Out-
///          of-range inputs clamp to INT64_MIN/MAX rather than producing UB.
static int64_t scene_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)(value >= 0.0L ? value + 0.5L : value - 0.5L);
}

/// @brief Compute (value * mul) / div in long double, saturating to int64.
/// @details Used by scale composition (child_world = parent_world * child_local / 100)
///          where the intermediate product can blow past int64. div == 0 returns 0
///          rather than dividing by zero.
static int64_t scene_mul_div_saturating(int64_t value, int64_t mul, int64_t div) {
    if (div == 0)
        return 0;
    return scene_ld_to_i64_sat(((long double)value * (long double)mul) / (long double)div);
}

/// @brief Keep local node scale positive so draw-time scale normalization is explicit.
static int64_t scene_normalize_scale(int64_t scale) {
    return scale < 1 ? 1 : scale;
}

/// @brief Subtract @p b from @p a in long double, saturating to int64 on overflow.
/// @details Used by world-to-local transform inversion where the difference
///          can exceed int64 range mid-calculation.
static int64_t scene_sub_saturating(int64_t a, int64_t b) {
    return scene_ld_to_i64_sat((long double)a - (long double)b);
}

// Forward declarations
static void mark_transform_dirty(scene_node_impl *node);
static void update_world_transform(scene_node_impl *node);
static void collect_visible_nodes(scene_node_impl *node, void *list);
static int compare_depth(const void *a, const void *b);
static void release_owned_ref(void **slot);
static void scene_node_finalize(void *obj);
static void scene_finalize(void *obj);

typedef struct scene_node_stack {
    scene_node_impl **items;
    scene_node_impl *inline_items[64];
    int64_t count;
    int64_t capacity;
} scene_node_stack;

static void scene_node_stack_init(scene_node_stack *stack) {
    stack->items = stack->inline_items;
    stack->count = 0;
    stack->capacity = (int64_t)(sizeof(stack->inline_items) / sizeof(stack->inline_items[0]));
}

static void scene_node_stack_destroy(scene_node_stack *stack) {
    if (stack->items != stack->inline_items)
        free(stack->items);
    stack->items = stack->inline_items;
    stack->count = 0;
    stack->capacity = (int64_t)(sizeof(stack->inline_items) / sizeof(stack->inline_items[0]));
}

static int8_t scene_node_stack_push(scene_node_stack *stack, scene_node_impl *node) {
    if (!node)
        return 1;
    if (stack->count >= stack->capacity) {
        if (stack->capacity > INT64_MAX / 2 ||
            (uint64_t)(stack->capacity * 2) > (uint64_t)SIZE_MAX / sizeof(*stack->items))
            return 0;
        int64_t new_capacity = stack->capacity * 2;
        scene_node_impl **grown = NULL;
        if (stack->items == stack->inline_items) {
            grown = (scene_node_impl **)malloc((size_t)new_capacity * sizeof(*grown));
            if (grown)
                memcpy(grown, stack->items, (size_t)stack->count * sizeof(*grown));
        } else {
            grown =
                (scene_node_impl **)realloc(stack->items, (size_t)new_capacity * sizeof(*grown));
        }
        if (!grown)
            return 0;
        stack->items = grown;
        stack->capacity = new_capacity;
    }
    stack->items[stack->count++] = node;
    return 1;
}

static scene_node_impl *scene_node_stack_pop(scene_node_stack *stack) {
    if (!stack || stack->count <= 0)
        return NULL;
    return stack->items[--stack->count];
}

static int8_t scene_node_stack_push_children_reverse(scene_node_stack *stack,
                                                     scene_node_impl *node) {
    if (!node || !node->children)
        return 1;
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = count; i > 0; i--) {
        if (!scene_node_stack_push(stack, (scene_node_impl *)rt_seq_get(node->children, i - 1)))
            return 0;
    }
    return 1;
}

/// @brief Release a GC reference stored in @p slot and NULL it.
/// @details If the reference count drops to zero after release, frees the object
///   immediately.  Nulling the slot prevents double-free if called again.
static void release_owned_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief GC finalizer for a scene node — release children, sprite, and name.
/// @details Before releasing the children Seq, clears every child's parent pointer
///   so that children being freed during Seq teardown don't attempt a dangling
///   remove_child back on this node.  Called automatically when the node's
///   reference count reaches zero.
static void scene_node_finalize(void *obj) {
    scene_node_impl *node = scene_node_checked_or_null(obj);
    if (!node)
        return;

    if (node->children) {
        int64_t count = rt_seq_len(node->children);
        for (int64_t i = 0; i < count; i++) {
            scene_node_impl *child = (scene_node_impl *)rt_seq_get(node->children, i);
            if (child)
                child->parent = NULL;
        }
        release_owned_ref(&node->children);
    }

    release_owned_ref(&node->sprite);
    release_owned_ref((void **)&node->name);
}

/// @brief GC finalizer for the scene container — release the root node.
/// @details Clears root->parent before releasing so the root's finalizer doesn't
///   try to call remove_child on a now-dead parent.  Releasing root triggers
///   a cascade release of the whole node tree.
static void scene_finalize(void *obj) {
    scene_impl *scene = scene_checked_or_null(obj);
    if (!scene)
        return;
    if (scene->root)
        scene->root->parent = NULL;
    release_owned_ref((void **)&scene->root);
}

//=============================================================================
// Scene Node Creation
//=============================================================================

/// @brief Create an empty 2D scene node positioned at the origin with identity transform.
/// Scale is stored as a percentage (100 = 1.0x). Children list owns its elements.
void *rt_scene_node_new(void) {
    scene_node_impl *node =
        (scene_node_impl *)rt_obj_new_i64(RT_SCENE_NODE_CLASS_ID, (int64_t)sizeof(scene_node_impl));
    if (!node) {
        rt_trap("SceneNode: allocation failed");
        return NULL;
    }
    memset(node, 0, sizeof(scene_node_impl));

    node->x = 0;
    node->y = 0;
    node->scale_x = 100;
    node->scale_y = 100;
    node->rotation = 0;
    node->depth = 0;
    node->visible = 1;

    node->world_x = 0;
    node->world_y = 0;
    node->world_scale_x = 100;
    node->world_scale_y = 100;
    node->world_rotation = 0;
    node->transform_dirty = 1;

    node->parent = NULL;
    node->children = rt_seq_new();
    rt_seq_set_owns_elements(node->children, 1);
    node->sprite = NULL;
    node->name = NULL;

    rt_obj_set_finalizer(node, scene_node_finalize);
    rt_scene_node_set_name(node, rt_const_cstr(""));

    return node;
}

/// @brief Convenience constructor: create a scene node and attach @p sprite to it.
void *rt_scene_node_from_sprite(void *sprite) {
    scene_node_impl *node = (scene_node_impl *)rt_scene_node_new();
    if (node && sprite)
        rt_scene_node_set_sprite(node, sprite);
    return node;
}

//=============================================================================
// Transform Management
//=============================================================================

/// @brief Mark @p node and all of its descendants as needing a world-transform recalculation.
/// @details Sets transform_dirty on every node in the subtree rooted at @p node.  The
///   actual recalculation is deferred until a world-transform getter (world_x, world_y, etc.)
///   is called, making repeated local-transform changes O(1) per change rather than
///   O(subtree size).  Already-dirty subtrees are short-circuited to avoid redundant work.
static void mark_transform_dirty(scene_node_impl *node) {
    if (!node)
        return;

    scene_node_stack stack;
    scene_node_stack_init(&stack);
    if (!scene_node_stack_push(&stack, node)) {
        rt_trap("SceneNode: transform stack allocation failed");
        scene_node_stack_destroy(&stack);
        return;
    }

    while (stack.count > 0) {
        scene_node_impl *cur = scene_node_stack_pop(&stack);
        if (!cur)
            continue;
        cur->transform_dirty = 1;
        if (!scene_node_stack_push_children_reverse(&stack, cur)) {
            rt_trap("SceneNode: transform stack allocation failed");
            break;
        }
    }

    scene_node_stack_destroy(&stack);
}

/// @brief Compute and store the world transform for @p node, assuming its parent's world
///        transform is already up-to-date.
/// @details For non-root nodes the world position is derived by scaling the local offset by
///          the parent's world scale, then rotating it by the parent's world rotation angle.
///          World scale and rotation accumulate multiplicatively from the root. For root nodes
///          the world transform equals the local transform directly. Clears `transform_dirty`.
static void apply_node_transform(scene_node_impl *node) {
    if (node->parent) {
        node->world_scale_x =
            scene_mul_div_saturating(node->parent->world_scale_x, node->scale_x, 100);
        node->world_scale_y =
            scene_mul_div_saturating(node->parent->world_scale_y, node->scale_y, 100);
        node->world_rotation = scene_add_saturating(node->parent->world_rotation, node->rotation);

        int64_t scaled_x = scene_mul_div_saturating(node->x, node->parent->world_scale_x, 100);
        int64_t scaled_y = scene_mul_div_saturating(node->y, node->parent->world_scale_y, 100);

        if (node->parent->world_rotation == 0) {
            node->world_x = scene_add_saturating(node->parent->world_x, scaled_x);
            node->world_y = scene_add_saturating(node->parent->world_y, scaled_y);
        } else {
            double rad = node->parent->world_rotation * 3.14159265359 / 180.0;
            double cos_r = cos(rad);
            double sin_r = sin(rad);

            int64_t rx = scene_ld_to_i64_sat((long double)scaled_x * (long double)cos_r -
                                             (long double)scaled_y * (long double)sin_r);
            int64_t ry = scene_ld_to_i64_sat((long double)scaled_x * (long double)sin_r +
                                             (long double)scaled_y * (long double)cos_r);
            node->world_x = scene_add_saturating(node->parent->world_x, rx);
            node->world_y = scene_add_saturating(node->parent->world_y, ry);
        }
    } else {
        node->world_x = node->x;
        node->world_y = node->y;
        node->world_scale_x = node->scale_x;
        node->world_scale_y = node->scale_y;
        node->world_rotation = node->rotation;
    }

    node->transform_dirty = 0;
}

/// @brief Propagate world transforms from the highest dirty ancestor down to @p node.
/// @details Iterative replacement for the former recursive implementation. Walks up the
///          ancestor chain collecting all dirty nodes into a small inline buffer (spills to
///          heap for hierarchies deeper than 64 nodes), then applies `apply_node_transform`
///          top-down so each parent is always clean before its child is processed.
///          No-op when @p node is NULL or its transform is already clean.
static void update_world_transform(scene_node_impl *node) {
    if (!node || !node->transform_dirty)
        return;

    // Collect the chain of dirty ancestors (including node itself).
    // Use a small fixed inline buffer; spill to heap for deep hierarchies.
    scene_node_impl *inline_buf[64];
    scene_node_impl **chain = inline_buf;
    int capacity = 64;
    int depth = 0;
    int heap_allocated = 0;

    scene_node_impl *cur = node;
    while (cur && cur->transform_dirty) {
        if (depth >= capacity) {
            int new_cap = capacity * 2;
            scene_node_impl **grown = malloc((size_t)new_cap * sizeof(*grown));
            if (!grown) {
                if (heap_allocated)
                    free(chain);
                return;
            }
            memcpy(grown, chain, (size_t)depth * sizeof(*grown));
            if (heap_allocated)
                free(chain);
            chain = grown;
            capacity = new_cap;
            heap_allocated = 1;
        }
        chain[depth++] = cur;
        cur = cur->parent;
    }

    // Process top-down (root-most dirty node first)
    for (int i = depth - 1; i >= 0; i--)
        apply_node_transform(chain[i]);

    if (heap_allocated)
        free(chain);
}

//=============================================================================
// Scene Node Properties - Position
//=============================================================================

/// @brief Return the node's local X position relative to its parent (or world origin if root).
int64_t rt_scene_node_get_x(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    return node->x;
}

/// @brief Set the node's local X position and mark the subtree's world transforms dirty.
void rt_scene_node_set_x(void *node_ptr, int64_t x) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->x = x;
    mark_transform_dirty(node);
}

/// @brief Return the node's local Y position relative to its parent (or world origin if root).
int64_t rt_scene_node_get_y(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    return node->y;
}

/// @brief Set the node's local Y position and mark the subtree's world transforms dirty.
void rt_scene_node_set_y(void *node_ptr, int64_t y) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->y = y;
    mark_transform_dirty(node);
}

/// @brief Return the node's computed world-space X position, updating dirty transforms first.
int64_t rt_scene_node_get_world_x(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    update_world_transform(node);
    return node->world_x;
}

/// @brief Return the node's computed world-space Y position, updating dirty transforms first.
int64_t rt_scene_node_get_world_y(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    update_world_transform(node);
    return node->world_y;
}

//=============================================================================
// Scene Node Properties - Scale
//=============================================================================

/// @brief Return the node's local X scale as a percentage (100 = 1.0×).
int64_t rt_scene_node_get_scale_x(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 100;
    return node->scale_x;
}

/// @brief Set the node's local X scale (percentage) and mark the subtree's transforms dirty.
void rt_scene_node_set_scale_x(void *node_ptr, int64_t scale) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->scale_x = scene_normalize_scale(scale);
    mark_transform_dirty(node);
}

/// @brief Return the node's local Y scale as a percentage (100 = 1.0×).
int64_t rt_scene_node_get_scale_y(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 100;
    return node->scale_y;
}

/// @brief Set the node's local Y scale (percentage) and mark the subtree's transforms dirty.
void rt_scene_node_set_scale_y(void *node_ptr, int64_t scale) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->scale_y = scene_normalize_scale(scale);
    mark_transform_dirty(node);
}

/// @brief Return the node's accumulated world-space X scale (parent scales multiplied in).
int64_t rt_scene_node_get_world_scale_x(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 100;
    update_world_transform(node);
    return node->world_scale_x;
}

/// @brief Return the node's accumulated world-space Y scale (parent scales multiplied in).
int64_t rt_scene_node_get_world_scale_y(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 100;
    update_world_transform(node);
    return node->world_scale_y;
}

//=============================================================================
// Scene Node Properties - Rotation
//=============================================================================

/// @brief Return the node's local rotation in whole degrees.
int64_t rt_scene_node_get_rotation(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    return node->rotation;
}

/// @brief Set the node's local rotation in whole degrees and mark the subtree's transforms dirty.
void rt_scene_node_set_rotation(void *node_ptr, int64_t degrees) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->rotation = degrees;
    mark_transform_dirty(node);
}

/// @brief Return the node's accumulated world-space rotation (sum of all ancestor rotations).
int64_t rt_scene_node_get_world_rotation(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    update_world_transform(node);
    return node->world_rotation;
}

//=============================================================================
// Scene Node Properties - Visibility & Depth
//=============================================================================

/// @brief Return whether the node (and its subtree) will be rendered.
/// @details A node whose visible flag is 0 is skipped entirely during draw traversal,
///   including all of its descendants.
int8_t rt_scene_node_get_visible(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    return node->visible;
}

/// @brief Show or hide the node and its entire subtree.
/// @details Setting visible to 0 prevents the node from being collected during
///   draw traversal — equivalent to removing it from the scene without detaching.
void rt_scene_node_set_visible(void *node_ptr, int8_t visible) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->visible = visible ? 1 : 0;
}

/// @brief Return the node's Z-order depth used for depth-sorted rendering.
/// @details Higher values render on top of lower values.  Siblings with equal depth
///   are drawn in traversal order.
int64_t rt_scene_node_get_depth(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    return node->depth;
}

/// @brief Set the node's Z-order depth for depth-sorted rendering.
void rt_scene_node_set_depth(void *node_ptr, int64_t depth) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->depth = depth;
}

//=============================================================================
// Scene Node Properties - Name & Sprite
//=============================================================================

/// @brief Return the node's name string (borrowed — do not release the returned value).
rt_string rt_scene_node_get_name(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return rt_const_cstr("");
    return node->name;
}

/// @brief Set the node's name string, retaining the new value and releasing the old.
/// @details Retains @p name before releasing the old name so the value is safe even
///   when the old and new strings happen to be the same object.  Empty string is
///   substituted when @p name is NULL.
void rt_scene_node_set_name(void *node_ptr, rt_string name) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    if (!name)
        name = rt_const_cstr("");
    if (node->name == name)
        return;
    rt_obj_retain_maybe(name);
    release_owned_ref((void **)&node->name);
    node->name = name;
}

/// @brief Return the sprite attached to the node (borrowed reference — do not release).
/// @details Returns NULL if no sprite has been set.  The sprite is retained by the
///   node; callers that need to hold a long-lived reference must retain it themselves.
void *rt_scene_node_get_sprite(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return NULL;
    return node->sprite;
}

/// @brief Attach a sprite to the node, retaining it and releasing the previous sprite.
/// @details The node takes ownership: the sprite is retained on assignment and
///   released when the node is finalized or a new sprite is set.
void rt_scene_node_set_sprite(void *node_ptr, void *sprite) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    if (sprite && !rt_obj_is_instance(sprite, RT_SPRITE_CLASS_ID, 0)) {
        rt_trap("SceneNode.SetSprite: invalid sprite");
        return;
    }
    if (node->sprite == sprite)
        return;
    rt_obj_retain_maybe(sprite);
    release_owned_ref(&node->sprite);
    node->sprite = sprite;
}

//=============================================================================
// Scene Node Hierarchy
//=============================================================================

/// @brief Attach @p child_ptr as a child of @p node_ptr in the scene hierarchy.
/// @details Guards against cycles by walking the ancestor chain before attaching.
///   If the child already has a parent, it is detached first.  Marks the child's
///   world transforms dirty since its inherited transform will change.
void rt_scene_node_add_child(void *node_ptr, void *child_ptr) {
    if (!node_ptr || !child_ptr)
        return;

    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    scene_node_impl *child = scene_node_checked_or_null(child_ptr);
    if (!node || !child)
        return;

    // Guard against cycles: walk node's ancestor chain and reject if child is found
    for (scene_node_impl *anc = node; anc; anc = anc->parent) {
        if (anc == child)
            return; // Would create a cycle — silently reject
    }

    rt_obj_retain_maybe(child);

    // Detach from previous parent if any. The temporary retain above keeps a
    // borrowed child pointer alive even if the previous parent owned the only
    // strong reference.
    if (child->parent) {
        rt_scene_node_remove_child(child->parent, child);
    }

    rt_seq_push(node->children, child);
    child->parent = node;
    mark_transform_dirty(child);

    if (rt_obj_release_check0(child))
        rt_obj_free(child);
}

/// @brief Detach @p child_ptr from @p node_ptr and release the node's reference to it.
/// @details Clears child->parent before removing from the Seq so the child's finalizer
///   cannot call back into this parent during teardown.  Frees the child if the release
///   drops its reference count to zero.  No-op if the child is not a direct child.
void rt_scene_node_remove_child(void *node_ptr, void *child_ptr) {
    if (!node_ptr || !child_ptr)
        return;

    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    scene_node_impl *child = scene_node_checked_or_null(child_ptr);
    if (!node || !child)
        return;

    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        if (rt_seq_get(node->children, i) == child) {
            child->parent = NULL;
            void *removed = rt_seq_remove(node->children, i);
            mark_transform_dirty(child);
            if (removed && rt_obj_release_check0(removed))
                rt_obj_free(removed);
            return;
        }
    }
}

/// @brief Return the number of direct children attached to @p node_ptr.
int64_t rt_scene_node_child_count(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return 0;
    return rt_seq_len(node->children);
}

/// @brief Get the child at @p index in the node's child list (NULL if out of range).
void *rt_scene_node_get_child(void *node_ptr, int64_t index) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return NULL;
    if (index < 0 || index >= rt_seq_len(node->children))
        return NULL;
    return rt_seq_get(node->children, index);
}

/// @brief Return the node's parent in the scene tree (NULL for unparented or root).
void *rt_scene_node_get_parent(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return NULL;
    return node->parent;
}

/// @brief Iterative depth-first search for a node with @p name beneath @p node_ptr.
/// Returns the first match (including the start node itself). NULL on no match.
void *rt_scene_node_find(void *node_ptr, rt_string name) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node || !name)
        return NULL;

    const char *search = rt_string_cstr(name);
    if (!search)
        return NULL;

    scene_node_stack stack;
    scene_node_stack_init(&stack);
    if (!scene_node_stack_push(&stack, node)) {
        rt_trap("SceneNode.Find: stack allocation failed");
        scene_node_stack_destroy(&stack);
        return NULL;
    }

    while (stack.count > 0) {
        scene_node_impl *cur = scene_node_stack_pop(&stack);
        if (!cur)
            continue;
        const char *node_name = rt_string_cstr(cur->name);
        if (node_name && strcmp(node_name, search) == 0) {
            scene_node_stack_destroy(&stack);
            return cur;
        }
        if (!scene_node_stack_push_children_reverse(&stack, cur)) {
            rt_trap("SceneNode.Find: stack allocation failed");
            break;
        }
    }

    scene_node_stack_destroy(&stack);
    return NULL;
}

/// @brief Detach the node from its parent, if any.
/// @details Convenience wrapper that calls remove_child on the node's current parent.
///   No-op for root or unparented nodes.
void rt_scene_node_detach(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    if (node->parent) {
        rt_scene_node_remove_child(node->parent, node);
    }
}

//=============================================================================
// Scene Node Methods
//=============================================================================

/// @brief Iteratively draw this node and all its descendants to @p canvas.
/// @details Skips invisible nodes (and their subtrees).  Each visible node with a sprite
///   is rendered using its computed world-space transform.  Children are drawn in
///   insertion order after their parent, so siblings stack naturally.
void rt_scene_node_draw(void *node_ptr, void *canvas) {
    if (!node_ptr || !canvas)
        return;

    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;

    scene_node_stack stack;
    scene_node_stack_init(&stack);
    if (!scene_node_stack_push(&stack, node)) {
        rt_trap("SceneNode.Draw: stack allocation failed");
        scene_node_stack_destroy(&stack);
        return;
    }

    while (stack.count > 0) {
        scene_node_impl *cur = scene_node_stack_pop(&stack);
        if (!cur || !cur->visible)
            continue;

        update_world_transform(cur);

        if (cur->sprite)
            rt_sprite_draw_transformed(cur->sprite,
                                       canvas,
                                       cur->world_x,
                                       cur->world_y,
                                       cur->world_scale_x,
                                       cur->world_scale_y,
                                       cur->world_rotation,
                                       -1,
                                       255);

        if (!scene_node_stack_push_children_reverse(&stack, cur)) {
            rt_trap("SceneNode.Draw: stack allocation failed");
            break;
        }
    }

    scene_node_stack_destroy(&stack);
}

/// @brief Recursively draw this node and all its descendants, applying @p camera's view transform.
/// @details Same traversal as rt_scene_node_draw, but each node's world position is
///   converted to screen space via the camera and the camera zoom/rotation are applied.
void rt_scene_node_draw_with_camera(void *node_ptr, void *canvas, void *camera) {
    if (!node_ptr || !canvas)
        return;

    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;

    scene_node_stack stack;
    scene_node_stack_init(&stack);
    if (!scene_node_stack_push(&stack, node)) {
        rt_trap("SceneNode.DrawWithCamera: stack allocation failed");
        scene_node_stack_destroy(&stack);
        return;
    }

    while (stack.count > 0) {
        scene_node_impl *cur = scene_node_stack_pop(&stack);
        if (!cur || !cur->visible)
            continue;

        update_world_transform(cur);

        if (cur->sprite) {
            int64_t screen_x = cur->world_x;
            int64_t screen_y = cur->world_y;
            int64_t scale_x = cur->world_scale_x;
            int64_t scale_y = cur->world_scale_y;
            int64_t rotation = cur->world_rotation;

            if (camera) {
                rt_camera_world_to_screen(camera, cur->world_x, cur->world_y, &screen_x, &screen_y);
                int64_t zoom = rt_camera_get_zoom(camera);
                scale_x = scene_mul_div_saturating(cur->world_scale_x, zoom, 100);
                scale_y = scene_mul_div_saturating(cur->world_scale_y, zoom, 100);
                rotation = scene_sub_saturating(rotation, rt_camera_get_rotation(camera));
            }

            rt_sprite_draw_transformed(
                cur->sprite, canvas, screen_x, screen_y, scale_x, scale_y, rotation, -1, 255);
        }

        if (!scene_node_stack_push_children_reverse(&stack, cur)) {
            rt_trap("SceneNode.DrawWithCamera: stack allocation failed");
            break;
        }
    }

    scene_node_stack_destroy(&stack);
}

/// @brief Advance this node's state by one tick and recursively update all children.
/// @details Calls rt_sprite_update on any attached sprite to advance its frame animation,
///   then propagates the update to all children.
void rt_scene_node_update(void *node_ptr) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;

    scene_node_stack stack;
    scene_node_stack_init(&stack);
    if (!scene_node_stack_push(&stack, node)) {
        rt_trap("SceneNode.Update: stack allocation failed");
        scene_node_stack_destroy(&stack);
        return;
    }

    while (stack.count > 0) {
        scene_node_impl *cur = scene_node_stack_pop(&stack);
        if (!cur)
            continue;
        if (cur->sprite)
            rt_sprite_update(cur->sprite);
        if (!scene_node_stack_push_children_reverse(&stack, cur)) {
            rt_trap("SceneNode.Update: stack allocation failed");
            break;
        }
    }

    scene_node_stack_destroy(&stack);
}

/// @brief Translate the node by (dx, dy) relative to its current local position.
void rt_scene_node_move(void *node_ptr, int64_t dx, int64_t dy) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->x = scene_add_saturating(node->x, dx);
    node->y = scene_add_saturating(node->y, dy);
    mark_transform_dirty(node);
}

/// @brief Set the position of the node.
void rt_scene_node_set_position(void *node_ptr, int64_t x, int64_t y) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    node->x = x;
    node->y = y;
    mark_transform_dirty(node);
}

/// @brief Set the scale of the node.
void rt_scene_node_set_scale(void *node_ptr, int64_t scale) {
    scene_node_impl *node = scene_node_checked_or_null(node_ptr);
    if (!node)
        return;
    scale = scene_normalize_scale(scale);
    node->scale_x = scale;
    node->scale_y = scale;
    mark_transform_dirty(node);
}

//=============================================================================
// Scene (Root Container)
//=============================================================================

/// @brief Create an empty 2D scene with a single root node named "root".
/// All user nodes attach beneath this root for global transform inheritance.
void *rt_scene_new(void) {
    scene_impl *scene =
        (scene_impl *)rt_obj_new_i64(RT_SCENE_CLASS_ID, (int64_t)sizeof(scene_impl));
    if (!scene) {
        rt_trap("Scene: allocation failed");
        return NULL;
    }
    memset(scene, 0, sizeof(scene_impl));

    scene->root = (scene_node_impl *)rt_scene_node_new();
    rt_scene_node_set_name(scene->root, rt_const_cstr("root"));
    rt_obj_set_finalizer(scene, scene_finalize);

    return scene;
}

/// @brief Return the implicit root node so callers can attach children directly.
void *rt_scene_get_root(void *scene_ptr) {
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return NULL;
    return scene->root;
}

/// @brief Add a top-level node to the scene by attaching it as a child of the root.
/// @details Equivalent to rt_scene_node_add_child(scene->root, node).  The node's
///   world transform will inherit the root's identity transform.
/// @param scene_ptr  Scene handle.
/// @param node_ptr   Scene node to attach; silently ignored if NULL.
void rt_scene_add(void *scene_ptr, void *node_ptr) {
    if (!scene_ptr || !node_ptr)
        return;
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return;
    rt_scene_node_add_child(scene->root, node_ptr);
}

/// @brief Remove a top-level node from the scene (detach from the root).
/// @details Only direct children of the scene root are removed.  Nodes that are
///   grandchildren or deeper are not affected.
/// @param scene_ptr  Scene handle.
/// @param node_ptr   Node to detach; silently ignored if NULL or not a direct child.
void rt_scene_remove(void *scene_ptr, void *node_ptr) {
    if (!scene_ptr || !node_ptr)
        return;
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return;
    rt_scene_node_remove_child(scene->root, node_ptr);
}

/// @brief Search the scene's node tree for the first node matching @p name.
/// Returns NULL if no matching node is found.
void *rt_scene_find(void *scene_ptr, rt_string name) {
    if (!scene_ptr)
        return NULL;
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return NULL;
    return rt_scene_node_find(scene->root, name);
}

//=============================================================================
// Depth-sorted rendering helpers
//=============================================================================

typedef struct {
    scene_node_impl *node;
    int64_t effective_depth;
    int64_t traversal_order;
} node_sort_entry;

/// @brief Depth-first traversal: collect all visible nodes that have a sprite into @p list.
/// @details Stops descending into a node subtree if the root node is not visible.
///   Only nodes with a non-NULL sprite field are appended; nodes used purely as
///   transform containers are skipped.  Used by rt_scene_draw to build the depth-sort array.
static void collect_visible_nodes(scene_node_impl *node, void *list) {
    if (!node || !list)
        return;

    scene_node_stack stack;
    scene_node_stack_init(&stack);
    if (!scene_node_stack_push(&stack, node)) {
        rt_trap("Scene.Draw: stack allocation failed");
        scene_node_stack_destroy(&stack);
        return;
    }

    while (stack.count > 0) {
        scene_node_impl *cur = scene_node_stack_pop(&stack);
        if (!cur || !cur->visible)
            continue;

        if (cur->sprite)
            rt_seq_push(list, cur);

        if (!scene_node_stack_push_children_reverse(&stack, cur)) {
            rt_trap("Scene.Draw: stack allocation failed");
            break;
        }
    }

    scene_node_stack_destroy(&stack);
}

/// @brief qsort comparator for node_sort_entry — sorts by depth ascending, ties by traversal order.
/// @details Preserving traversal order for equal-depth nodes guarantees that sibling
///   insertion order and tree-traversal order remain stable across frames even when
///   many nodes share the same depth value.
static int compare_depth(const void *a, const void *b) {
    const node_sort_entry *na = (const node_sort_entry *)a;
    const node_sort_entry *nb = (const node_sort_entry *)b;

    if (na->effective_depth < nb->effective_depth)
        return -1;
    if (na->effective_depth > nb->effective_depth)
        return 1;
    if (na->traversal_order < nb->traversal_order)
        return -1;
    if (na->traversal_order > nb->traversal_order)
        return 1;
    return 0;
}

/// @brief Draw all visible nodes to @p canvas, sorted by depth.
/// @details Collects every visible node that has a sprite into a temporary list,
///   stable-sorts them by depth (ascending), and renders each in order using
///   world-space transforms.  Nodes with equal depth are drawn in traversal (insertion) order.
/// @param scene_ptr  Scene handle.
/// @param canvas     Target 2D canvas to draw to.
void rt_scene_draw(void *scene_ptr, void *canvas) {
    if (!scene_ptr || !canvas)
        return;

    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return;

    // Collect all visible nodes
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);

    int64_t count = rt_seq_len(nodes);
    if (count == 0) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    // Sort by depth, preserving traversal order among equal-depth nodes.
    node_sort_entry *arr = (node_sort_entry *)malloc((size_t)count * sizeof(node_sort_entry));
    if (!arr) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    for (int64_t i = 0; i < count; i++) {
        arr[i].node = (scene_node_impl *)rt_seq_get(nodes, i);
        arr[i].effective_depth = arr[i].node ? arr[i].node->depth : 0;
        arr[i].traversal_order = i;
    }

    qsort(arr, (size_t)count, sizeof(node_sort_entry), compare_depth);

    // Draw in depth order
    for (int64_t i = 0; i < count; i++) {
        scene_node_impl *node = arr[i].node;
        update_world_transform(node);

        if (node->sprite)
            rt_sprite_draw_transformed(node->sprite,
                                       canvas,
                                       node->world_x,
                                       node->world_y,
                                       node->world_scale_x,
                                       node->world_scale_y,
                                       node->world_rotation,
                                       -1,
                                       255);
    }

    free(arr);
    if (rt_obj_release_check0(nodes))
        rt_obj_free(nodes);
}

/// @brief Draw all visible nodes to @p canvas with camera-space transform applied.
/// @details Same depth-sorted traversal as rt_scene_draw, but each node's world position
///   is converted to screen space via rt_camera_world_to_screen and the camera zoom is
///   multiplied into the scale before rendering.  Camera rotation is subtracted from each
///   node's world rotation so nodes counter-rotate relative to the viewport.
/// @param scene_ptr  Scene handle.
/// @param canvas     Target 2D canvas.
/// @param camera     Camera that provides the view transform; may be NULL (identity view).
void rt_scene_draw_with_camera(void *scene_ptr, void *canvas, void *camera) {
    if (!scene_ptr || !canvas)
        return;

    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return;

    // Collect all visible nodes
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);

    int64_t count = rt_seq_len(nodes);
    if (count == 0) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    // Sort by depth, preserving traversal order among equal-depth nodes.
    node_sort_entry *arr = (node_sort_entry *)malloc((size_t)count * sizeof(node_sort_entry));
    if (!arr) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    for (int64_t i = 0; i < count; i++) {
        arr[i].node = (scene_node_impl *)rt_seq_get(nodes, i);
        arr[i].effective_depth = arr[i].node ? arr[i].node->depth : 0;
        arr[i].traversal_order = i;
    }

    qsort(arr, (size_t)count, sizeof(node_sort_entry), compare_depth);

    // Draw in depth order
    for (int64_t i = 0; i < count; i++) {
        scene_node_impl *node = arr[i].node;
        update_world_transform(node);

        if (node->sprite) {
            int64_t screen_x = node->world_x;
            int64_t screen_y = node->world_y;
            int64_t final_sx = node->world_scale_x;
            int64_t final_sy = node->world_scale_y;
            int64_t rotation = node->world_rotation;

            if (camera) {
                rt_camera_world_to_screen(
                    camera, node->world_x, node->world_y, &screen_x, &screen_y);
                int64_t zoom = rt_camera_get_zoom(camera);
                final_sx = scene_mul_div_saturating(node->world_scale_x, zoom, 100);
                final_sy = scene_mul_div_saturating(node->world_scale_y, zoom, 100);
                rotation = scene_sub_saturating(rotation, rt_camera_get_rotation(camera));
            }

            rt_sprite_draw_transformed(
                node->sprite, canvas, screen_x, screen_y, final_sx, final_sy, rotation, -1, 255);
        }
    }

    free(arr);
    if (rt_obj_release_check0(nodes))
        rt_obj_free(nodes);
}

/// @brief Advance all nodes in the scene by one frame — call once per game tick.
/// @details Recursively calls rt_scene_node_update on the root, which propagates to
///   every child, advancing sprite frame animations and any per-node update logic.
/// @param scene_ptr  Scene handle.
void rt_scene_update(void *scene_ptr) {
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return;
    rt_scene_node_update(scene->root);
}

/// @brief Return the number of direct children attached to the scene root.
/// @details Only counts immediate children of the root node, not the entire tree.
///   Returns 0 for a NULL or invalid scene.
int64_t rt_scene_node_count(void *scene_ptr) {
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return 0;
    return scene && scene->root ? rt_seq_len(scene->root->children) : 0;
}

/// @brief Remove all direct children from the scene root, releasing their references.
/// @details Clears every child's parent pointer before releasing the Seq entries to
///   prevent remove_child callbacks from firing during teardown.  Deep children are
///   freed transitively as their parent nodes lose their last reference.
/// @param scene_ptr  Scene handle.
void rt_scene_clear(void *scene_ptr) {
    scene_impl *scene = scene_checked_or_null(scene_ptr);
    if (!scene)
        return;

    // Clear parent pointers before removing children to avoid stale references
    int64_t n = rt_seq_len(scene->root->children);
    for (int64_t i = 0; i < n; i++) {
        scene_node_impl *child = (scene_node_impl *)rt_seq_get(scene->root->children, i);
        if (child)
            child->parent = NULL;
    }
    while (rt_seq_len(scene->root->children) > 0) {
        void *child = rt_seq_pop(scene->root->children);
        if (rt_obj_release_check0(child))
            rt_obj_free(child);
    }
}
