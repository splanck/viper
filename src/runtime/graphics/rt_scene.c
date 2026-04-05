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
//   - Draw order follows insertion order within each parent. There is no
//     automatic Z-sort; callers must manage insertion order or use SpriteBatch
//     with depth sorting for Z-ordered rendering.
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

#include <math.h>
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

// Forward declarations
static void mark_transform_dirty(scene_node_impl *node);
static void update_world_transform(scene_node_impl *node);
static void collect_visible_nodes(scene_node_impl *node, void *list);
static int compare_depth(const void *a, const void *b);
static void release_owned_ref(void **slot);
static void scene_node_finalize(void *obj);
static void scene_finalize(void *obj);

static void release_owned_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void scene_node_finalize(void *obj) {
    scene_node_impl *node = (scene_node_impl *)obj;
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

static void scene_finalize(void *obj) {
    scene_impl *scene = (scene_impl *)obj;
    if (!scene)
        return;
    if (scene->root)
        scene->root->parent = NULL;
    release_owned_ref((void **)&scene->root);
}

//=============================================================================
// Scene Node Creation
//=============================================================================

void *rt_scene_node_new(void) {
    scene_node_impl *node = (scene_node_impl *)rt_obj_new_i64(0, (int64_t)sizeof(scene_node_impl));
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

void *rt_scene_node_from_sprite(void *sprite) {
    scene_node_impl *node = (scene_node_impl *)rt_scene_node_new();
    if (node && sprite)
        rt_scene_node_set_sprite(node, sprite);
    return node;
}

//=============================================================================
// Transform Management
//=============================================================================

static void mark_transform_dirty(scene_node_impl *node) {
    if (!node || node->transform_dirty)
        return;

    node->transform_dirty = 1;

    // Recursively mark children dirty
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        scene_node_impl *child = (scene_node_impl *)rt_seq_get(node->children, i);
        mark_transform_dirty(child);
    }
}

// Compute the world transform for a single node, assuming its parent is already clean.
static void apply_node_transform(scene_node_impl *node) {
    if (node->parent) {
        node->world_scale_x = (node->parent->world_scale_x * node->scale_x) / 100;
        node->world_scale_y = (node->parent->world_scale_y * node->scale_y) / 100;
        node->world_rotation = node->parent->world_rotation + node->rotation;

        int64_t scaled_x = (node->x * node->parent->world_scale_x) / 100;
        int64_t scaled_y = (node->y * node->parent->world_scale_y) / 100;

        if (node->parent->world_rotation == 0) {
            node->world_x = node->parent->world_x + scaled_x;
            node->world_y = node->parent->world_y + scaled_y;
        } else {
            double rad = node->parent->world_rotation * 3.14159265359 / 180.0;
            double cos_r = cos(rad);
            double sin_r = sin(rad);

            node->world_x = node->parent->world_x + (int64_t)(scaled_x * cos_r - scaled_y * sin_r);
            node->world_y = node->parent->world_y + (int64_t)(scaled_x * sin_r + scaled_y * cos_r);
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

// Iterative equivalent of the former recursive update_world_transform.
// Walks UP the ancestor chain to find the highest dirty ancestor, then
// applies transforms top-down so each node's parent is always clean first.
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
            if (!grown)
                break; // OOM — process what we have (partial update, better than overflow)
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

/// @brief Get the x of the node.
int64_t rt_scene_node_get_x(void *node_ptr) {
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->x;
}

/// @brief Set the x of the node.
void rt_scene_node_set_x(void *node_ptr, int64_t x) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->x = x;
    mark_transform_dirty(node);
}

/// @brief Get the y of the node.
int64_t rt_scene_node_get_y(void *node_ptr) {
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->y;
}

/// @brief Set the y of the node.
void rt_scene_node_set_y(void *node_ptr, int64_t y) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->y = y;
    mark_transform_dirty(node);
}

/// @brief Get the world x of the node.
int64_t rt_scene_node_get_world_x(void *node_ptr) {
    if (!node_ptr)
        return 0;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_x;
}

/// @brief Get the world y of the node.
int64_t rt_scene_node_get_world_y(void *node_ptr) {
    if (!node_ptr)
        return 0;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_y;
}

//=============================================================================
// Scene Node Properties - Scale
//=============================================================================

/// @brief Get the scale x of the node.
int64_t rt_scene_node_get_scale_x(void *node_ptr) {
    if (!node_ptr)
        return 100;
    return ((scene_node_impl *)node_ptr)->scale_x;
}

/// @brief Set the scale x of the node.
void rt_scene_node_set_scale_x(void *node_ptr, int64_t scale) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->scale_x = scale;
    mark_transform_dirty(node);
}

/// @brief Get the scale y of the node.
int64_t rt_scene_node_get_scale_y(void *node_ptr) {
    if (!node_ptr)
        return 100;
    return ((scene_node_impl *)node_ptr)->scale_y;
}

/// @brief Set the scale y of the node.
void rt_scene_node_set_scale_y(void *node_ptr, int64_t scale) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->scale_y = scale;
    mark_transform_dirty(node);
}

/// @brief Get the world scale x of the node.
int64_t rt_scene_node_get_world_scale_x(void *node_ptr) {
    if (!node_ptr)
        return 100;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_scale_x;
}

/// @brief Get the world scale y of the node.
int64_t rt_scene_node_get_world_scale_y(void *node_ptr) {
    if (!node_ptr)
        return 100;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_scale_y;
}

//=============================================================================
// Scene Node Properties - Rotation
//=============================================================================

/// @brief Get the rotation of the node.
int64_t rt_scene_node_get_rotation(void *node_ptr) {
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->rotation;
}

/// @brief Set the rotation of the node.
void rt_scene_node_set_rotation(void *node_ptr, int64_t degrees) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->rotation = degrees;
    mark_transform_dirty(node);
}

/// @brief Get the world rotation of the node.
int64_t rt_scene_node_get_world_rotation(void *node_ptr) {
    if (!node_ptr)
        return 0;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_rotation;
}

//=============================================================================
// Scene Node Properties - Visibility & Depth
//=============================================================================

/// @brief Get the visible of the node.
int8_t rt_scene_node_get_visible(void *node_ptr) {
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->visible;
}

/// @brief Set the visible of the node.
void rt_scene_node_set_visible(void *node_ptr, int8_t visible) {
    if (!node_ptr)
        return;
    ((scene_node_impl *)node_ptr)->visible = visible ? 1 : 0;
}

/// @brief Get the depth of the node.
int64_t rt_scene_node_get_depth(void *node_ptr) {
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->depth;
}

/// @brief Set the depth of the node.
void rt_scene_node_set_depth(void *node_ptr, int64_t depth) {
    if (!node_ptr)
        return;
    ((scene_node_impl *)node_ptr)->depth = depth;
}

//=============================================================================
// Scene Node Properties - Name & Sprite
//=============================================================================

/// @brief Get the name of the node.
rt_string rt_scene_node_get_name(void *node_ptr) {
    if (!node_ptr)
        return rt_const_cstr("");
    return ((scene_node_impl *)node_ptr)->name;
}

/// @brief Set the name of the node.
void rt_scene_node_set_name(void *node_ptr, rt_string name) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    if (!name)
        name = rt_const_cstr("");
    if (node->name == name)
        return;
    rt_obj_retain_maybe(name);
    release_owned_ref((void **)&node->name);
    node->name = name;
}

void *rt_scene_node_get_sprite(void *node_ptr) {
    if (!node_ptr)
        return NULL;
    return ((scene_node_impl *)node_ptr)->sprite;
}

/// @brief Set the sprite of the node.
void rt_scene_node_set_sprite(void *node_ptr, void *sprite) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    if (node->sprite == sprite)
        return;
    rt_obj_retain_maybe(sprite);
    release_owned_ref(&node->sprite);
    node->sprite = sprite;
}

//=============================================================================
// Scene Node Hierarchy
//=============================================================================

/// @brief Add the child of the node.
void rt_scene_node_add_child(void *node_ptr, void *child_ptr) {
    if (!node_ptr || !child_ptr)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;
    scene_node_impl *child = (scene_node_impl *)child_ptr;

    // Guard against cycles: walk node's ancestor chain and reject if child is found
    for (scene_node_impl *anc = node; anc; anc = anc->parent) {
        if (anc == child)
            return; // Would create a cycle — silently reject
    }

    // Detach from previous parent if any
    if (child->parent) {
        rt_scene_node_remove_child(child->parent, child);
    }

    child->parent = node;
    rt_seq_push(node->children, child);
    mark_transform_dirty(child);
}

/// @brief Remove the child of the node.
void rt_scene_node_remove_child(void *node_ptr, void *child_ptr) {
    if (!node_ptr || !child_ptr)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;
    scene_node_impl *child = (scene_node_impl *)child_ptr;

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

/// @brief Return the count of elements in the node.
int64_t rt_scene_node_child_count(void *node_ptr) {
    if (!node_ptr)
        return 0;
    return rt_seq_len(((scene_node_impl *)node_ptr)->children);
}

void *rt_scene_node_get_child(void *node_ptr, int64_t index) {
    if (!node_ptr)
        return NULL;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    if (index < 0 || index >= rt_seq_len(node->children))
        return NULL;
    return rt_seq_get(node->children, index);
}

void *rt_scene_node_get_parent(void *node_ptr) {
    if (!node_ptr)
        return NULL;
    return ((scene_node_impl *)node_ptr)->parent;
}

void *rt_scene_node_find(void *node_ptr, rt_string name) {
    if (!node_ptr)
        return NULL;

    scene_node_impl *node = (scene_node_impl *)node_ptr;
    const char *search = rt_string_cstr(name);
    const char *node_name = rt_string_cstr(node->name);

    // Check this node
    if (node_name && search && strcmp(node_name, search) == 0)
        return node;

    // Search children recursively
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        void *found = rt_scene_node_find(rt_seq_get(node->children, i), name);
        if (found)
            return found;
    }

    return NULL;
}

/// @brief Detach the node.
void rt_scene_node_detach(void *node_ptr) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    if (node->parent) {
        rt_scene_node_remove_child(node->parent, node);
    }
}

//=============================================================================
// Scene Node Methods
//=============================================================================

/// @brief Draw the node.
void rt_scene_node_draw(void *node_ptr, void *canvas) {
    if (!node_ptr || !canvas)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;

    if (!node->visible)
        return;

    update_world_transform(node);

    if (node->sprite)
        rt_sprite_draw_transformed(node->sprite,
                                   canvas,
                                   node->world_x,
                                   node->world_y,
                                   node->world_scale_x,
                                   node->world_scale_y,
                                   node->world_rotation,
                                   0,
                                   255);

    // Draw children in insertion order
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        rt_scene_node_draw(rt_seq_get(node->children, i), canvas);
    }
}

/// @brief Draw the with camera of the node.
void rt_scene_node_draw_with_camera(void *node_ptr, void *canvas, void *camera) {
    if (!node_ptr || !canvas)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;

    if (!node->visible)
        return;

    update_world_transform(node);

    if (node->sprite) {
        int64_t screen_x = node->world_x;
        int64_t screen_y = node->world_y;
        int64_t scale_x = node->world_scale_x;
        int64_t scale_y = node->world_scale_y;
        int64_t rotation = node->world_rotation;

        if (camera) {
            rt_camera_world_to_screen(camera, node->world_x, node->world_y, &screen_x, &screen_y);
            int64_t zoom = rt_camera_get_zoom(camera);
            scale_x = (node->world_scale_x * zoom) / 100;
            scale_y = (node->world_scale_y * zoom) / 100;
            rotation -= rt_camera_get_rotation(camera);
        }

        rt_sprite_draw_transformed(
            node->sprite, canvas, screen_x, screen_y, scale_x, scale_y, rotation, 0, 255);
    }

    // Draw children
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        rt_scene_node_draw_with_camera(rt_seq_get(node->children, i), canvas, camera);
    }
}

/// @brief Update the node state (called per frame/tick).
void rt_scene_node_update(void *node_ptr) {
    if (!node_ptr)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;

    // Update sprite animation if any
    if (node->sprite) {
        rt_sprite_update(node->sprite);
    }

    // Update children
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        rt_scene_node_update(rt_seq_get(node->children, i));
    }
}

/// @brief Move the node.
void rt_scene_node_move(void *node_ptr, int64_t dx, int64_t dy) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->x += dx;
    node->y += dy;
    mark_transform_dirty(node);
}

/// @brief Set the position of the node.
void rt_scene_node_set_position(void *node_ptr, int64_t x, int64_t y) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->x = x;
    node->y = y;
    mark_transform_dirty(node);
}

/// @brief Set the scale of the node.
void rt_scene_node_set_scale(void *node_ptr, int64_t scale) {
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->scale_x = scale;
    node->scale_y = scale;
    mark_transform_dirty(node);
}

//=============================================================================
// Scene (Root Container)
//=============================================================================

void *rt_scene_new(void) {
    scene_impl *scene = (scene_impl *)rt_obj_new_i64(0, (int64_t)sizeof(scene_impl));
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

void *rt_scene_get_root(void *scene_ptr) {
    if (!scene_ptr)
        return NULL;
    return ((scene_impl *)scene_ptr)->root;
}

/// @brief Add add.
/// @param scene_ptr
/// @param node_ptr
void rt_scene_add(void *scene_ptr, void *node_ptr) {
    if (!scene_ptr || !node_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;
    rt_scene_node_add_child(scene->root, node_ptr);
}

/// @brief Remove remove.
/// @param scene_ptr
/// @param node_ptr
void rt_scene_remove(void *scene_ptr, void *node_ptr) {
    if (!scene_ptr || !node_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;
    rt_scene_node_remove_child(scene->root, node_ptr);
}

void *rt_scene_find(void *scene_ptr, rt_string name) {
    if (!scene_ptr)
        return NULL;
    scene_impl *scene = (scene_impl *)scene_ptr;
    return rt_scene_node_find(scene->root, name);
}

//=============================================================================
// Depth-sorted rendering helpers
//=============================================================================

typedef struct {
    scene_node_impl *node;
    int64_t effective_depth;
} node_sort_entry;

static void collect_visible_nodes(scene_node_impl *node, void *list) {
    if (!node || !node->visible)
        return;

    // Add this node if it has a sprite
    if (node->sprite) {
        rt_seq_push(list, node);
    }

    // Recursively collect from children
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++) {
        collect_visible_nodes((scene_node_impl *)rt_seq_get(node->children, i), list);
    }
}

static int compare_depth(const void *a, const void *b) {
    scene_node_impl *na = *(scene_node_impl *const *)a;
    scene_node_impl *nb = *(scene_node_impl *const *)b;

    if (na->depth < nb->depth)
        return -1;
    if (na->depth > nb->depth)
        return 1;
    return 0;
}

/// @brief Draw draw to the canvas.
/// @param scene_ptr
/// @param canvas
void rt_scene_draw(void *scene_ptr, void *canvas) {
    if (!scene_ptr || !canvas)
        return;

    scene_impl *scene = (scene_impl *)scene_ptr;

    // Collect all visible nodes
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);

    int64_t count = rt_seq_len(nodes);
    if (count == 0) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    // Sort by depth
    scene_node_impl **arr = (scene_node_impl **)malloc(count * sizeof(scene_node_impl *));
    if (!arr) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    for (int64_t i = 0; i < count; i++) {
        arr[i] = (scene_node_impl *)rt_seq_get(nodes, i);
    }

    qsort(arr, (size_t)count, sizeof(scene_node_impl *), compare_depth);

    // Draw in depth order
    for (int64_t i = 0; i < count; i++) {
        scene_node_impl *node = arr[i];
        update_world_transform(node);

        if (node->sprite)
            rt_sprite_draw_transformed(node->sprite,
                                       canvas,
                                       node->world_x,
                                       node->world_y,
                                       node->world_scale_x,
                                       node->world_scale_y,
                                       node->world_rotation,
                                       0,
                                       255);
    }

    free(arr);
    if (rt_obj_release_check0(nodes))
        rt_obj_free(nodes);
}

/// @brief Draw with camera to the canvas.
/// @param scene_ptr
/// @param canvas
/// @param camera
void rt_scene_draw_with_camera(void *scene_ptr, void *canvas, void *camera) {
    if (!scene_ptr || !canvas)
        return;

    scene_impl *scene = (scene_impl *)scene_ptr;

    // Collect all visible nodes
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);

    int64_t count = rt_seq_len(nodes);
    if (count == 0) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    // Sort by depth
    scene_node_impl **arr = (scene_node_impl **)malloc(count * sizeof(scene_node_impl *));
    if (!arr) {
        if (rt_obj_release_check0(nodes))
            rt_obj_free(nodes);
        return;
    }

    for (int64_t i = 0; i < count; i++) {
        arr[i] = (scene_node_impl *)rt_seq_get(nodes, i);
    }

    qsort(arr, (size_t)count, sizeof(scene_node_impl *), compare_depth);

    // Draw in depth order
    for (int64_t i = 0; i < count; i++) {
        scene_node_impl *node = arr[i];
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
                final_sx = (node->world_scale_x * zoom) / 100;
                final_sy = (node->world_scale_y * zoom) / 100;
                rotation -= rt_camera_get_rotation(camera);
            }

            rt_sprite_draw_transformed(
                node->sprite, canvas, screen_x, screen_y, final_sx, final_sy, rotation, 0, 255);
        }
    }

    free(arr);
    if (rt_obj_release_check0(nodes))
        rt_obj_free(nodes);
}

/// @brief Update update state for current frame.
/// @param scene_ptr
void rt_scene_update(void *scene_ptr) {
    if (!scene_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;
    rt_scene_node_update(scene->root);
}

/// @brief Return the count of elements in the node.
int64_t rt_scene_node_count(void *scene_ptr) {
    if (!scene_ptr)
        return 0;
    scene_impl *scene = (scene_impl *)scene_ptr;
    return scene && scene->root ? rt_seq_len(scene->root->children) : 0;
}

/// @brief Clear all clear.
/// @param scene_ptr
void rt_scene_clear(void *scene_ptr) {
    if (!scene_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;

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
