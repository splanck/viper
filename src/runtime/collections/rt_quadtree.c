//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_quadtree.c
// Purpose: Spatial quadtree for Viper games. Accelerates nearest-neighbour and
//   rectangular-region queries from O(n) linear scans to approximately
//   O(n log n) by recursively subdividing a 2D world region into four equal
//   quadrants (NW, NE, SW, SE). Typical use cases: enemy radar, player
//   proximity triggers, broad-phase collision detection, and line-of-sight
//   culling over large open worlds.
//
// Key invariants:
//   - Items are identified by unique int64 IDs and stored as (center x, center y,
//     width, height) AABBs. All coordinates are in the same integer unit space
//     as the world bounds given to rt_quadtree_new().
//   - The tree subdivides a node when it would overflow RT_QUADTREE_MAX_ITEMS
//     (8) items, up to a maximum depth of RT_QUADTREE_MAX_DEPTH (8) levels.
//     Items that span a subdivision midpoint are kept in the parent node.
//   - Nodes are heap-allocated individually via malloc(). The GC finalizer
//     (quadtree_finalizer) recursively destroys the entire node tree when the
//     root object is collected. This means the tree is NOT fully GC-transparent:
//     node memory is invisible to the GC and is freed via the finalizer path
//     rather than by GC tracing.
//   - The flat item array (items[MAX_TOTAL_ITEMS]) lives inside the root struct
//     which IS GC-managed. item_count is the high-water mark (never decrements);
//     inactive items are marked with active == 0.
//   - Duplicate-ID insert guard: rt_quadtree_insert() performs a linear scan of
//     the flat array before insertion. Inserting the same ID twice is rejected
//     (returns 0) to prevent ghost items after a single Remove().
//   - Query results are stored in the root's results[] array (capacity
//     RT_QUADTREE_MAX_RESULTS = 256). If more items match, query_truncated is
//     set to 1. Callers that need all results must check
//     rt_quadtree_query_was_truncated() after every query.
//   - Pair collection (rt_quadtree_get_pairs) generates up to MAX_PAIRS (1024)
//     candidate collision pairs by walking the tree recursively and testing
//     items in each node against each other AND against all ancestor items.
//
// Ownership/Lifetime:
//   - The root struct is GC-managed (rt_obj_new_i64). Node memory is freed by
//     quadtree_finalizer via recursive destroy_node(). Callers may also call
//     rt_quadtree_destroy() explicitly, but it is a documented no-op — rely on
//     the finalizer for cleanup.
//
// Links: src/runtime/collections/rt_quadtree.h (public API),
//        docs/viperlib/game.md (Quadtree section, truncation notes)
//
//===----------------------------------------------------------------------===//

#include "rt_quadtree.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// Maximum total items across all nodes.
#define MAX_TOTAL_ITEMS 4096

/// Maximum collision pairs.
#define MAX_PAIRS 1024

/// Item entry.
struct qt_item
{
    int64_t id;
    int64_t x, y;          ///< Center position.
    int64_t width, height; ///< Dimensions.
    int8_t active;
};

/// Quadtree node.
struct qt_node
{
    int64_t x, y;                         ///< Bounds top-left.
    int64_t width, height;                ///< Bounds dimensions.
    int64_t items[RT_QUADTREE_MAX_ITEMS]; ///< Item indices in this node.
    int64_t item_count;
    int64_t depth;
    struct qt_node *children[4]; ///< NW, NE, SW, SE.
    int8_t is_split;
};

/// Collision pair.
struct qt_pair
{
    int64_t first;
    int64_t second;
};

/// Internal quadtree structure.
struct rt_quadtree_impl
{
    struct qt_node *root;
    struct qt_item items[MAX_TOTAL_ITEMS];
    int64_t item_count;
    int64_t results[RT_QUADTREE_MAX_RESULTS];
    int64_t result_count;
    int8_t  query_truncated; ///< 1 if last query hit RT_QUADTREE_MAX_RESULTS cap.
    struct qt_pair pairs[MAX_PAIRS];
    int64_t pair_count;
};

/// Creates a new node.
static struct qt_node *create_node(
    int64_t x, int64_t y, int64_t width, int64_t height, int64_t depth)
{
    struct qt_node *node = malloc(sizeof(struct qt_node));
    if (!node)
        return NULL;

    node->x = x;
    node->y = y;
    node->width = width;
    node->height = height;
    node->item_count = 0;
    node->depth = depth;
    node->is_split = 0;
    node->children[0] = NULL;
    node->children[1] = NULL;
    node->children[2] = NULL;
    node->children[3] = NULL;

    return node;
}

/// Destroys a node and its children.
static void destroy_node(struct qt_node *node)
{
    if (!node)
        return;

    for (int i = 0; i < 4; i++)
    {
        if (node->children[i])
            destroy_node(node->children[i]);
    }
    free(node);
}

/// Clears a node (keeps structure but removes items).
static void clear_node(struct qt_node *node)
{
    if (!node)
        return;

    node->item_count = 0;

    for (int i = 0; i < 4; i++)
    {
        if (node->children[i])
        {
            destroy_node(node->children[i]);
            node->children[i] = NULL;
        }
    }
    node->is_split = 0;
}

/// Check if a rectangle intersects a node's bounds.
static int8_t intersects(struct qt_node *node, int64_t x, int64_t y, int64_t w, int64_t h)
{
    return !(x >= node->x + node->width || x + w <= node->x || y >= node->y + node->height ||
             y + h <= node->y);
}

/// Check if a rectangle is fully contained in a node.
static int8_t contains(struct qt_node *node, int64_t x, int64_t y, int64_t w, int64_t h)
{
    return x >= node->x && x + w <= node->x + node->width && y >= node->y &&
           y + h <= node->y + node->height;
}

/// Split a node into 4 children.
static void split_node(struct qt_node *node)
{
    if (node->is_split || node->depth >= RT_QUADTREE_MAX_DEPTH)
        return;

    int64_t half_w = node->width / 2;
    int64_t half_h = node->height / 2;
    int64_t next_depth = node->depth + 1;

    // NW, NE, SW, SE
    node->children[0] = create_node(node->x, node->y, half_w, half_h, next_depth);
    node->children[1] = create_node(node->x + half_w, node->y, half_w, half_h, next_depth);
    node->children[2] = create_node(node->x, node->y + half_h, half_w, half_h, next_depth);
    node->children[3] = create_node(node->x + half_w, node->y + half_h, half_w, half_h, next_depth);

    node->is_split = 1;
}

/// Get which child quadrant(s) an item belongs to.
/// Returns -1 if item spans multiple quadrants.
static int get_quadrant(struct qt_node *node, int64_t x, int64_t y, int64_t w, int64_t h)
{
    int64_t mid_x = node->x + node->width / 2;
    int64_t mid_y = node->y + node->height / 2;

    int8_t in_top = y < mid_y;
    int8_t in_bottom = y + h > mid_y;
    int8_t in_left = x < mid_x;
    int8_t in_right = x + w > mid_x;

    // If spanning multiple quadrants, stay in parent
    if ((in_top && in_bottom) || (in_left && in_right))
        return -1;

    if (in_top && in_left)
        return 0; // NW
    if (in_top && in_right)
        return 1; // NE
    if (in_bottom && in_left)
        return 2; // SW
    if (in_bottom && in_right)
        return 3; // SE

    return -1;
}

/// Insert item into node.
static int8_t insert_into_node(struct rt_quadtree_impl *tree,
                               struct qt_node *node,
                               int64_t item_idx)
{
    if (!node)
        return 0;

    struct qt_item *item = &tree->items[item_idx];
    int64_t x = item->x - item->width / 2; // Convert center to top-left
    int64_t y = item->y - item->height / 2;

    // If node is split, try to insert into child
    if (node->is_split)
    {
        int quad = get_quadrant(node, x, y, item->width, item->height);
        if (quad >= 0 && node->children[quad])
        {
            return insert_into_node(tree, node->children[quad], item_idx);
        }
    }

    // Insert into this node
    if (node->item_count < RT_QUADTREE_MAX_ITEMS)
    {
        node->items[node->item_count++] = item_idx;
        return 1;
    }

    // Node is full, try to split
    if (!node->is_split && node->depth < RT_QUADTREE_MAX_DEPTH)
    {
        split_node(node);

        // Re-distribute existing items
        for (int64_t i = 0; i < node->item_count; i++)
        {
            struct qt_item *existing = &tree->items[node->items[i]];
            int64_t ex = existing->x - existing->width / 2;
            int64_t ey = existing->y - existing->height / 2;
            int quad = get_quadrant(node, ex, ey, existing->width, existing->height);
            if (quad >= 0 && node->children[quad])
            {
                insert_into_node(tree, node->children[quad], node->items[i]);
                // Move last item to this slot
                node->items[i] = node->items[node->item_count - 1];
                node->item_count--;
                i--; // Re-check this slot
            }
        }

        // Try again with the new item
        int quad = get_quadrant(node, x, y, item->width, item->height);
        if (quad >= 0 && node->children[quad])
        {
            return insert_into_node(tree, node->children[quad], item_idx);
        }
    }

    // Still can't fit, add anyway (overflow)
    if (node->item_count < RT_QUADTREE_MAX_ITEMS)
    {
        node->items[node->item_count++] = item_idx;
        return 1;
    }

    return 0; // Node is truly full
}

/// Query items in a region.
static void query_node(
    struct rt_quadtree_impl *tree, struct qt_node *node, int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (!node)
        return;
    // When result_count is already at the cap, more matching items may exist in
    // this subtree (the caller only invokes us when the subtree intersects the
    // query rect).  Mark truncation so the caller can detect partial results.
    if (tree->result_count >= RT_QUADTREE_MAX_RESULTS)
    {
        tree->query_truncated = 1;
        return;
    }

    // Check items in this node
    for (int64_t i = 0; i < node->item_count; i++)
    {
        if (tree->result_count >= RT_QUADTREE_MAX_RESULTS)
        {
            tree->query_truncated = 1; // Mark truncation for caller detection
            break;
        }

        struct qt_item *item = &tree->items[node->items[i]];
        if (!item->active)
            continue;

        // Check if item intersects query region
        int64_t ix = item->x - item->width / 2;
        int64_t iy = item->y - item->height / 2;

        if (!(ix >= x + w || ix + item->width <= x || iy >= y + h || iy + item->height <= y))
        {
            tree->results[tree->result_count++] = item->id;
        }
    }

    // Query children
    if (node->is_split)
    {
        for (int i = 0; i < 4; i++)
        {
            if (node->children[i] && intersects(node->children[i], x, y, w, h))
            {
                query_node(tree, node->children[i], x, y, w, h);
            }
        }
    }
}

/// Remove item from node.
static int8_t remove_from_node(struct rt_quadtree_impl *tree, struct qt_node *node, int64_t id)
{
    if (!node)
        return 0;

    // Check items in this node
    for (int64_t i = 0; i < node->item_count; i++)
    {
        if (tree->items[node->items[i]].id == id)
        {
            // Remove by swapping with last
            node->items[i] = node->items[node->item_count - 1];
            node->item_count--;
            return 1;
        }
    }

    // Check children
    if (node->is_split)
    {
        for (int i = 0; i < 4; i++)
        {
            if (node->children[i] && remove_from_node(tree, node->children[i], id))
                return 1;
        }
    }

    return 0;
}

/// Collect all item pairs that could potentially collide.
static void collect_pairs_node(struct rt_quadtree_impl *tree,
                               struct qt_node *node,
                               int64_t *ancestors,
                               int64_t ancestor_count)
{
    if (!node || tree->pair_count >= MAX_PAIRS)
        return;

    // Check items in this node against each other
    for (int64_t i = 0; i < node->item_count && tree->pair_count < MAX_PAIRS; i++)
    {
        struct qt_item *item_i = &tree->items[node->items[i]];
        if (!item_i->active)
            continue;

        // Against other items in same node
        for (int64_t j = i + 1; j < node->item_count && tree->pair_count < MAX_PAIRS; j++)
        {
            struct qt_item *item_j = &tree->items[node->items[j]];
            if (!item_j->active)
                continue;

            tree->pairs[tree->pair_count].first = item_i->id;
            tree->pairs[tree->pair_count].second = item_j->id;
            tree->pair_count++;
        }

        // Against ancestor items
        for (int64_t a = 0; a < ancestor_count && tree->pair_count < MAX_PAIRS; a++)
        {
            struct qt_item *item_a = &tree->items[ancestors[a]];
            if (!item_a->active)
                continue;

            tree->pairs[tree->pair_count].first = item_i->id;
            tree->pairs[tree->pair_count].second = item_a->id;
            tree->pair_count++;
        }
    }

    // Recurse into children with updated ancestor list
    if (node->is_split)
    {
        // Expand ancestors to include this node's items
        int64_t new_ancestors[256];
        int64_t new_count = ancestor_count;
        for (int64_t i = 0; i < node->item_count && new_count < 256; i++)
        {
            new_ancestors[new_count++] = node->items[i];
        }
        for (int64_t i = 0; i < ancestor_count; i++)
        {
            new_ancestors[i] = ancestors[i];
        }

        for (int i = 0; i < 4; i++)
        {
            if (node->children[i])
            {
                collect_pairs_node(tree, node->children[i], new_ancestors, new_count);
            }
        }
    }
}

static void quadtree_finalizer(void *obj)
{
    struct rt_quadtree_impl *tree = (struct rt_quadtree_impl *)obj;
    destroy_node(tree->root);
    tree->root = NULL;
}

rt_quadtree rt_quadtree_new(int64_t x, int64_t y, int64_t width, int64_t height)
{
    struct rt_quadtree_impl *tree = rt_obj_new_i64(0, sizeof(struct rt_quadtree_impl));
    if (!tree)
        return NULL;

    memset(tree, 0, sizeof(struct rt_quadtree_impl));

    tree->root = create_node(x, y, width, height, 0);
    if (!tree->root)
    {
        return NULL;
    }

    rt_obj_set_finalizer(tree, quadtree_finalizer);
    return tree;
}

void rt_quadtree_destroy(rt_quadtree tree)
{
    // Object is GC-managed; finalizer frees internal nodes.
    (void)tree;
}

void rt_quadtree_clear(rt_quadtree tree)
{
    if (!tree)
        return;

    // Clear all items
    for (int64_t i = 0; i < tree->item_count; i++)
    {
        tree->items[i].active = 0;
    }
    tree->item_count = 0;
    tree->result_count = 0;
    tree->pair_count = 0;

    // Clear tree structure
    if (tree->root)
        clear_node(tree->root);
}

int8_t rt_quadtree_insert(
    rt_quadtree tree, int64_t id, int64_t x, int64_t y, int64_t width, int64_t height)
{
    if (!tree || tree->item_count >= MAX_TOTAL_ITEMS)
        return 0;

    // Guard against duplicate IDs — inserting the same ID twice leaves a ghost
    // after the first Remove(), producing phantom collision responses.
    for (int64_t i = 0; i < tree->item_count; i++)
    {
        if (tree->items[i].active && tree->items[i].id == id)
            return 0; // Already present; caller should use rt_quadtree_update()
    }

    // Check bounds
    int64_t left = x - width / 2;
    int64_t top = y - height / 2;
    if (!intersects(tree->root, left, top, width, height))
        return 0;

    // Add to item list
    int64_t idx = tree->item_count;
    tree->items[idx].id = id;
    tree->items[idx].x = x;
    tree->items[idx].y = y;
    tree->items[idx].width = width;
    tree->items[idx].height = height;
    tree->items[idx].active = 1;
    tree->item_count++;

    // Insert into tree
    return insert_into_node(tree, tree->root, idx);
}

int8_t rt_quadtree_remove(rt_quadtree tree, int64_t id)
{
    if (!tree)
        return 0;

    // Mark item as inactive
    for (int64_t i = 0; i < tree->item_count; i++)
    {
        if (tree->items[i].id == id && tree->items[i].active)
        {
            tree->items[i].active = 0;
            remove_from_node(tree, tree->root, id);
            return 1;
        }
    }

    return 0;
}

int8_t rt_quadtree_update(
    rt_quadtree tree, int64_t id, int64_t x, int64_t y, int64_t width, int64_t height)
{
    if (!tree)
        return 0;

    // Find and update item
    for (int64_t i = 0; i < tree->item_count; i++)
    {
        if (tree->items[i].id == id && tree->items[i].active)
        {
            // Remove from tree
            remove_from_node(tree, tree->root, id);

            // Update position
            tree->items[i].x = x;
            tree->items[i].y = y;
            tree->items[i].width = width;
            tree->items[i].height = height;

            // Re-insert
            return insert_into_node(tree, tree->root, i);
        }
    }

    return 0;
}

int64_t rt_quadtree_query_rect(
    rt_quadtree tree, int64_t x, int64_t y, int64_t width, int64_t height)
{
    if (!tree)
        return 0;

    tree->result_count = 0;
    tree->query_truncated = 0;
    query_node(tree, tree->root, x, y, width, height);
    return tree->result_count;
}

int8_t rt_quadtree_query_was_truncated(rt_quadtree tree)
{
    return tree ? tree->query_truncated : 0;
}

int64_t rt_quadtree_query_point(rt_quadtree tree, int64_t x, int64_t y, int64_t radius)
{
    if (!tree)
        return 0;

    // Query as rectangle for simplicity
    return rt_quadtree_query_rect(tree, x - radius, y - radius, radius * 2, radius * 2);
}

int64_t rt_quadtree_get_result(rt_quadtree tree, int64_t index)
{
    if (!tree || index < 0 || index >= tree->result_count)
        return -1;
    return tree->results[index];
}

int64_t rt_quadtree_result_count(rt_quadtree tree)
{
    return tree ? tree->result_count : 0;
}

int64_t rt_quadtree_item_count(rt_quadtree tree)
{
    if (!tree)
        return 0;

    int64_t count = 0;
    for (int64_t i = 0; i < tree->item_count; i++)
    {
        if (tree->items[i].active)
            count++;
    }
    return count;
}

int64_t rt_quadtree_get_pairs(rt_quadtree tree)
{
    if (!tree)
        return 0;

    tree->pair_count = 0;
    int64_t ancestors[1] = {0};
    collect_pairs_node(tree, tree->root, ancestors, 0);
    return tree->pair_count;
}

int64_t rt_quadtree_pair_first(rt_quadtree tree, int64_t pair_index)
{
    if (!tree || pair_index < 0 || pair_index >= tree->pair_count)
        return -1;
    return tree->pairs[pair_index].first;
}

int64_t rt_quadtree_pair_second(rt_quadtree tree, int64_t pair_index)
{
    if (!tree || pair_index < 0 || pair_index >= tree->pair_count)
        return -1;
    return tree->pairs[pair_index].second;
}
