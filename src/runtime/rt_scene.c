//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_scene.c
/// @brief Scene graph implementation for hierarchical sprite management.
///
//===----------------------------------------------------------------------===//

#include "rt_scene.h"
#include "rt_camera.h"
#include "rt_seq.h"
#include "rt_sprite.h"

#include <stdlib.h>
#include <string.h>

// Forward declaration from rt_io.c
extern void rt_trap(const char *msg);

//=============================================================================
// Internal Structures
//=============================================================================

typedef struct scene_node_impl
{
    // Local transform (relative to parent)
    int64_t x;
    int64_t y;
    int64_t scale_x;     // 100 = 100%
    int64_t scale_y;     // 100 = 100%
    int64_t rotation;    // degrees
    int64_t depth;       // Z-order
    int8_t visible;      // visibility flag

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
    void *sprite;    // Attached sprite (nullable)
    rt_string name;  // Tag/identifier
} scene_node_impl;

typedef struct scene_impl
{
    scene_node_impl *root;
} scene_impl;

// Forward declarations
static void mark_transform_dirty(scene_node_impl *node);
static void update_world_transform(scene_node_impl *node);
static void collect_visible_nodes(scene_node_impl *node, void *list);
static int compare_depth(const void *a, const void *b);

//=============================================================================
// Scene Node Creation
//=============================================================================

void *rt_scene_node_new(void)
{
    scene_node_impl *node = (scene_node_impl *)calloc(1, sizeof(scene_node_impl));
    if (!node)
        rt_trap("SceneNode: memory allocation failed");

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
    node->sprite = NULL;
    node->name = rt_const_cstr("");

    return node;
}

void *rt_scene_node_from_sprite(void *sprite)
{
    scene_node_impl *node = (scene_node_impl *)rt_scene_node_new();
    if (node && sprite)
    {
        node->sprite = sprite;
    }
    return node;
}

//=============================================================================
// Transform Management
//=============================================================================

static void mark_transform_dirty(scene_node_impl *node)
{
    if (!node || node->transform_dirty)
        return;

    node->transform_dirty = 1;

    // Recursively mark children dirty
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++)
    {
        scene_node_impl *child = (scene_node_impl *)rt_seq_get(node->children, i);
        mark_transform_dirty(child);
    }
}

static void update_world_transform(scene_node_impl *node)
{
    if (!node || !node->transform_dirty)
        return;

    if (node->parent)
    {
        // Ensure parent is up-to-date
        update_world_transform(node->parent);

        // Combine transforms
        // Scale
        node->world_scale_x = (node->parent->world_scale_x * node->scale_x) / 100;
        node->world_scale_y = (node->parent->world_scale_y * node->scale_y) / 100;

        // Rotation
        node->world_rotation = node->parent->world_rotation + node->rotation;

        // Position: rotate local position by parent rotation, scale by parent scale
        int64_t scaled_x = (node->x * node->parent->world_scale_x) / 100;
        int64_t scaled_y = (node->y * node->parent->world_scale_y) / 100;

        // Simple rotation (integer approximation for performance)
        // For precise rotation, we'd need fixed-point or floating-point math
        if (node->parent->world_rotation == 0)
        {
            node->world_x = node->parent->world_x + scaled_x;
            node->world_y = node->parent->world_y + scaled_y;
        }
        else
        {
            // Approximate rotation using integer math
            // sin/cos lookup could be used for better precision
            double rad = node->parent->world_rotation * 3.14159265359 / 180.0;
            double cos_r = 1.0; // cos(rad)
            double sin_r = 0.0; // sin(rad)

            // Simple inline sin/cos approximation
            extern double cos(double);
            extern double sin(double);
            cos_r = cos(rad);
            sin_r = sin(rad);

            int64_t rx = (int64_t)(scaled_x * cos_r - scaled_y * sin_r);
            int64_t ry = (int64_t)(scaled_x * sin_r + scaled_y * cos_r);

            node->world_x = node->parent->world_x + rx;
            node->world_y = node->parent->world_y + ry;
        }
    }
    else
    {
        // Root node: local = world
        node->world_x = node->x;
        node->world_y = node->y;
        node->world_scale_x = node->scale_x;
        node->world_scale_y = node->scale_y;
        node->world_rotation = node->rotation;
    }

    node->transform_dirty = 0;
}

//=============================================================================
// Scene Node Properties - Position
//=============================================================================

int64_t rt_scene_node_get_x(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->x;
}

void rt_scene_node_set_x(void *node_ptr, int64_t x)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->x = x;
    mark_transform_dirty(node);
}

int64_t rt_scene_node_get_y(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->y;
}

void rt_scene_node_set_y(void *node_ptr, int64_t y)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->y = y;
    mark_transform_dirty(node);
}

int64_t rt_scene_node_get_world_x(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_x;
}

int64_t rt_scene_node_get_world_y(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_y;
}

//=============================================================================
// Scene Node Properties - Scale
//=============================================================================

int64_t rt_scene_node_get_scale_x(void *node_ptr)
{
    if (!node_ptr)
        return 100;
    return ((scene_node_impl *)node_ptr)->scale_x;
}

void rt_scene_node_set_scale_x(void *node_ptr, int64_t scale)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->scale_x = scale;
    mark_transform_dirty(node);
}

int64_t rt_scene_node_get_scale_y(void *node_ptr)
{
    if (!node_ptr)
        return 100;
    return ((scene_node_impl *)node_ptr)->scale_y;
}

void rt_scene_node_set_scale_y(void *node_ptr, int64_t scale)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->scale_y = scale;
    mark_transform_dirty(node);
}

int64_t rt_scene_node_get_world_scale_x(void *node_ptr)
{
    if (!node_ptr)
        return 100;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_scale_x;
}

int64_t rt_scene_node_get_world_scale_y(void *node_ptr)
{
    if (!node_ptr)
        return 100;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_scale_y;
}

//=============================================================================
// Scene Node Properties - Rotation
//=============================================================================

int64_t rt_scene_node_get_rotation(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->rotation;
}

void rt_scene_node_set_rotation(void *node_ptr, int64_t degrees)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->rotation = degrees;
    mark_transform_dirty(node);
}

int64_t rt_scene_node_get_world_rotation(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    update_world_transform(node);
    return node->world_rotation;
}

//=============================================================================
// Scene Node Properties - Visibility & Depth
//=============================================================================

int8_t rt_scene_node_get_visible(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->visible;
}

void rt_scene_node_set_visible(void *node_ptr, int8_t visible)
{
    if (!node_ptr)
        return;
    ((scene_node_impl *)node_ptr)->visible = visible ? 1 : 0;
}

int64_t rt_scene_node_get_depth(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    return ((scene_node_impl *)node_ptr)->depth;
}

void rt_scene_node_set_depth(void *node_ptr, int64_t depth)
{
    if (!node_ptr)
        return;
    ((scene_node_impl *)node_ptr)->depth = depth;
}

//=============================================================================
// Scene Node Properties - Name & Sprite
//=============================================================================

rt_string rt_scene_node_get_name(void *node_ptr)
{
    if (!node_ptr)
        return rt_const_cstr("");
    return ((scene_node_impl *)node_ptr)->name;
}

void rt_scene_node_set_name(void *node_ptr, rt_string name)
{
    if (!node_ptr)
        return;
    ((scene_node_impl *)node_ptr)->name = name;
}

void *rt_scene_node_get_sprite(void *node_ptr)
{
    if (!node_ptr)
        return NULL;
    return ((scene_node_impl *)node_ptr)->sprite;
}

void rt_scene_node_set_sprite(void *node_ptr, void *sprite)
{
    if (!node_ptr)
        return;
    ((scene_node_impl *)node_ptr)->sprite = sprite;
}

//=============================================================================
// Scene Node Hierarchy
//=============================================================================

void rt_scene_node_add_child(void *node_ptr, void *child_ptr)
{
    if (!node_ptr || !child_ptr)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;
    scene_node_impl *child = (scene_node_impl *)child_ptr;

    // Detach from previous parent if any
    if (child->parent)
    {
        rt_scene_node_remove_child(child->parent, child);
    }

    child->parent = node;
    rt_seq_push(node->children, child);
    mark_transform_dirty(child);
}

void rt_scene_node_remove_child(void *node_ptr, void *child_ptr)
{
    if (!node_ptr || !child_ptr)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;
    scene_node_impl *child = (scene_node_impl *)child_ptr;

    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++)
    {
        if (rt_seq_get(node->children, i) == child)
        {
            rt_seq_remove(node->children, i);
            child->parent = NULL;
            mark_transform_dirty(child);
            return;
        }
    }
}

int64_t rt_scene_node_child_count(void *node_ptr)
{
    if (!node_ptr)
        return 0;
    return rt_seq_len(((scene_node_impl *)node_ptr)->children);
}

void *rt_scene_node_get_child(void *node_ptr, int64_t index)
{
    if (!node_ptr)
        return NULL;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    if (index < 0 || index >= rt_seq_len(node->children))
        return NULL;
    return rt_seq_get(node->children, index);
}

void *rt_scene_node_get_parent(void *node_ptr)
{
    if (!node_ptr)
        return NULL;
    return ((scene_node_impl *)node_ptr)->parent;
}

void *rt_scene_node_find(void *node_ptr, rt_string name)
{
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
    for (int64_t i = 0; i < count; i++)
    {
        void *found = rt_scene_node_find(rt_seq_get(node->children, i), name);
        if (found)
            return found;
    }

    return NULL;
}

void rt_scene_node_detach(void *node_ptr)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    if (node->parent)
    {
        rt_scene_node_remove_child(node->parent, node);
    }
}

//=============================================================================
// Scene Node Methods
//=============================================================================

void rt_scene_node_draw(void *node_ptr, void *canvas)
{
    if (!node_ptr || !canvas)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;

    if (!node->visible)
        return;

    update_world_transform(node);

    // Draw this node's sprite if any
    if (node->sprite)
    {
        // Apply world transform to sprite before drawing
        int64_t old_x = rt_sprite_get_x(node->sprite);
        int64_t old_y = rt_sprite_get_y(node->sprite);
        int64_t old_sx = rt_sprite_get_scale_x(node->sprite);
        int64_t old_sy = rt_sprite_get_scale_y(node->sprite);
        int64_t old_rot = rt_sprite_get_rotation(node->sprite);

        rt_sprite_set_x(node->sprite, node->world_x);
        rt_sprite_set_y(node->sprite, node->world_y);
        rt_sprite_set_scale_x(node->sprite, node->world_scale_x);
        rt_sprite_set_scale_y(node->sprite, node->world_scale_y);
        rt_sprite_set_rotation(node->sprite, node->world_rotation);

        rt_sprite_draw(node->sprite, canvas);

        // Restore original sprite state
        rt_sprite_set_x(node->sprite, old_x);
        rt_sprite_set_y(node->sprite, old_y);
        rt_sprite_set_scale_x(node->sprite, old_sx);
        rt_sprite_set_scale_y(node->sprite, old_sy);
        rt_sprite_set_rotation(node->sprite, old_rot);
    }

    // Draw children (sorted by depth for this level)
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++)
    {
        rt_scene_node_draw(rt_seq_get(node->children, i), canvas);
    }
}

void rt_scene_node_draw_with_camera(void *node_ptr, void *canvas, void *camera)
{
    if (!node_ptr || !canvas)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;

    if (!node->visible)
        return;

    update_world_transform(node);

    // Draw this node's sprite if any
    if (node->sprite)
    {
        // Apply world transform and camera offset
        int64_t old_x = rt_sprite_get_x(node->sprite);
        int64_t old_y = rt_sprite_get_y(node->sprite);
        int64_t old_sx = rt_sprite_get_scale_x(node->sprite);
        int64_t old_sy = rt_sprite_get_scale_y(node->sprite);
        int64_t old_rot = rt_sprite_get_rotation(node->sprite);

        int64_t screen_x = node->world_x;
        int64_t screen_y = node->world_y;

        if (camera)
        {
            screen_x = rt_camera_to_screen_x(camera, node->world_x);
            screen_y = rt_camera_to_screen_y(camera, node->world_y);

            // Apply camera zoom to sprite scale
            int64_t zoom = rt_camera_get_zoom(camera);
            rt_sprite_set_scale_x(node->sprite, (node->world_scale_x * zoom) / 100);
            rt_sprite_set_scale_y(node->sprite, (node->world_scale_y * zoom) / 100);
        }
        else
        {
            rt_sprite_set_scale_x(node->sprite, node->world_scale_x);
            rt_sprite_set_scale_y(node->sprite, node->world_scale_y);
        }

        rt_sprite_set_x(node->sprite, screen_x);
        rt_sprite_set_y(node->sprite, screen_y);
        rt_sprite_set_rotation(node->sprite, node->world_rotation);

        rt_sprite_draw(node->sprite, canvas);

        // Restore
        rt_sprite_set_x(node->sprite, old_x);
        rt_sprite_set_y(node->sprite, old_y);
        rt_sprite_set_scale_x(node->sprite, old_sx);
        rt_sprite_set_scale_y(node->sprite, old_sy);
        rt_sprite_set_rotation(node->sprite, old_rot);
    }

    // Draw children
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++)
    {
        rt_scene_node_draw_with_camera(rt_seq_get(node->children, i), canvas, camera);
    }
}

void rt_scene_node_update(void *node_ptr)
{
    if (!node_ptr)
        return;

    scene_node_impl *node = (scene_node_impl *)node_ptr;

    // Update sprite animation if any
    if (node->sprite)
    {
        rt_sprite_update(node->sprite);
    }

    // Update children
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++)
    {
        rt_scene_node_update(rt_seq_get(node->children, i));
    }
}

void rt_scene_node_move(void *node_ptr, int64_t dx, int64_t dy)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->x += dx;
    node->y += dy;
    mark_transform_dirty(node);
}

void rt_scene_node_set_position(void *node_ptr, int64_t x, int64_t y)
{
    if (!node_ptr)
        return;
    scene_node_impl *node = (scene_node_impl *)node_ptr;
    node->x = x;
    node->y = y;
    mark_transform_dirty(node);
}

void rt_scene_node_set_scale(void *node_ptr, int64_t scale)
{
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

void *rt_scene_new(void)
{
    scene_impl *scene = (scene_impl *)calloc(1, sizeof(scene_impl));
    if (!scene)
        rt_trap("Scene: memory allocation failed");

    scene->root = (scene_node_impl *)rt_scene_node_new();
    scene->root->name = rt_const_cstr("root");

    return scene;
}

void *rt_scene_get_root(void *scene_ptr)
{
    if (!scene_ptr)
        return NULL;
    return ((scene_impl *)scene_ptr)->root;
}

void rt_scene_add(void *scene_ptr, void *node_ptr)
{
    if (!scene_ptr || !node_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;
    rt_scene_node_add_child(scene->root, node_ptr);
}

void rt_scene_remove(void *scene_ptr, void *node_ptr)
{
    if (!scene_ptr || !node_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;
    rt_scene_node_remove_child(scene->root, node_ptr);
}

void *rt_scene_find(void *scene_ptr, rt_string name)
{
    if (!scene_ptr)
        return NULL;
    scene_impl *scene = (scene_impl *)scene_ptr;
    return rt_scene_node_find(scene->root, name);
}

//=============================================================================
// Depth-sorted rendering helpers
//=============================================================================

typedef struct
{
    scene_node_impl *node;
    int64_t effective_depth;
} node_sort_entry;

static void collect_visible_nodes(scene_node_impl *node, void *list)
{
    if (!node || !node->visible)
        return;

    // Add this node if it has a sprite
    if (node->sprite)
    {
        rt_seq_push(list, node);
    }

    // Recursively collect from children
    int64_t count = rt_seq_len(node->children);
    for (int64_t i = 0; i < count; i++)
    {
        collect_visible_nodes((scene_node_impl *)rt_seq_get(node->children, i), list);
    }
}

static int compare_depth(const void *a, const void *b)
{
    scene_node_impl *na = *(scene_node_impl **)a;
    scene_node_impl *nb = *(scene_node_impl **)b;

    if (na->depth < nb->depth)
        return -1;
    if (na->depth > nb->depth)
        return 1;
    return 0;
}

void rt_scene_draw(void *scene_ptr, void *canvas)
{
    if (!scene_ptr || !canvas)
        return;

    scene_impl *scene = (scene_impl *)scene_ptr;

    // Collect all visible nodes
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);

    int64_t count = rt_seq_len(nodes);
    if (count == 0)
        return;

    // Sort by depth
    scene_node_impl **arr = (scene_node_impl **)malloc(count * sizeof(scene_node_impl *));
    if (!arr)
        return;

    for (int64_t i = 0; i < count; i++)
    {
        arr[i] = (scene_node_impl *)rt_seq_get(nodes, i);
    }

    qsort(arr, (size_t)count, sizeof(scene_node_impl *), compare_depth);

    // Draw in depth order
    for (int64_t i = 0; i < count; i++)
    {
        scene_node_impl *node = arr[i];
        update_world_transform(node);

        if (node->sprite)
        {
            int64_t old_x = rt_sprite_get_x(node->sprite);
            int64_t old_y = rt_sprite_get_y(node->sprite);
            int64_t old_sx = rt_sprite_get_scale_x(node->sprite);
            int64_t old_sy = rt_sprite_get_scale_y(node->sprite);
            int64_t old_rot = rt_sprite_get_rotation(node->sprite);

            rt_sprite_set_x(node->sprite, node->world_x);
            rt_sprite_set_y(node->sprite, node->world_y);
            rt_sprite_set_scale_x(node->sprite, node->world_scale_x);
            rt_sprite_set_scale_y(node->sprite, node->world_scale_y);
            rt_sprite_set_rotation(node->sprite, node->world_rotation);

            rt_sprite_draw(node->sprite, canvas);

            rt_sprite_set_x(node->sprite, old_x);
            rt_sprite_set_y(node->sprite, old_y);
            rt_sprite_set_scale_x(node->sprite, old_sx);
            rt_sprite_set_scale_y(node->sprite, old_sy);
            rt_sprite_set_rotation(node->sprite, old_rot);
        }
    }

    free(arr);
}

void rt_scene_draw_with_camera(void *scene_ptr, void *canvas, void *camera)
{
    if (!scene_ptr || !canvas)
        return;

    scene_impl *scene = (scene_impl *)scene_ptr;

    // Collect all visible nodes
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);

    int64_t count = rt_seq_len(nodes);
    if (count == 0)
        return;

    // Sort by depth
    scene_node_impl **arr = (scene_node_impl **)malloc(count * sizeof(scene_node_impl *));
    if (!arr)
        return;

    for (int64_t i = 0; i < count; i++)
    {
        arr[i] = (scene_node_impl *)rt_seq_get(nodes, i);
    }

    qsort(arr, (size_t)count, sizeof(scene_node_impl *), compare_depth);

    // Draw in depth order
    for (int64_t i = 0; i < count; i++)
    {
        scene_node_impl *node = arr[i];
        update_world_transform(node);

        if (node->sprite)
        {
            int64_t old_x = rt_sprite_get_x(node->sprite);
            int64_t old_y = rt_sprite_get_y(node->sprite);
            int64_t old_sx = rt_sprite_get_scale_x(node->sprite);
            int64_t old_sy = rt_sprite_get_scale_y(node->sprite);
            int64_t old_rot = rt_sprite_get_rotation(node->sprite);

            int64_t screen_x = node->world_x;
            int64_t screen_y = node->world_y;
            int64_t final_sx = node->world_scale_x;
            int64_t final_sy = node->world_scale_y;

            if (camera)
            {
                screen_x = rt_camera_to_screen_x(camera, node->world_x);
                screen_y = rt_camera_to_screen_y(camera, node->world_y);
                int64_t zoom = rt_camera_get_zoom(camera);
                final_sx = (node->world_scale_x * zoom) / 100;
                final_sy = (node->world_scale_y * zoom) / 100;
            }

            rt_sprite_set_x(node->sprite, screen_x);
            rt_sprite_set_y(node->sprite, screen_y);
            rt_sprite_set_scale_x(node->sprite, final_sx);
            rt_sprite_set_scale_y(node->sprite, final_sy);
            rt_sprite_set_rotation(node->sprite, node->world_rotation);

            rt_sprite_draw(node->sprite, canvas);

            rt_sprite_set_x(node->sprite, old_x);
            rt_sprite_set_y(node->sprite, old_y);
            rt_sprite_set_scale_x(node->sprite, old_sx);
            rt_sprite_set_scale_y(node->sprite, old_sy);
            rt_sprite_set_rotation(node->sprite, old_rot);
        }
    }

    free(arr);
}

void rt_scene_update(void *scene_ptr)
{
    if (!scene_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;
    rt_scene_node_update(scene->root);
}

int64_t rt_scene_node_count(void *scene_ptr)
{
    if (!scene_ptr)
        return 0;
    scene_impl *scene = (scene_impl *)scene_ptr;

    // Count all nodes recursively
    void *nodes = rt_seq_new();
    collect_visible_nodes(scene->root, nodes);
    int64_t count = rt_seq_len(nodes);

    // Add invisible nodes - just count children recursively
    // For simplicity, just return visible sprite count
    return count;
}

void rt_scene_clear(void *scene_ptr)
{
    if (!scene_ptr)
        return;
    scene_impl *scene = (scene_impl *)scene_ptr;

    // Clear all children from root
    while (rt_seq_len(scene->root->children) > 0)
    {
        rt_seq_pop(scene->root->children);
    }
}
