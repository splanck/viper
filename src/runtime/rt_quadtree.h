//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_quadtree.h
/// @brief Quadtree for spatial partitioning and efficient collision queries.
///
/// Provides a quadtree data structure for:
/// - Fast spatial queries (find objects in a region)
/// - Collision detection optimization
/// - Reducing O(n^2) collision checks to ~O(n log n)
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_QUADTREE_H
#define VIPER_RT_QUADTREE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum items per node before splitting.
#define RT_QUADTREE_MAX_ITEMS 8

/// Maximum tree depth.
#define RT_QUADTREE_MAX_DEPTH 8

/// Maximum items in a query result.
#define RT_QUADTREE_MAX_RESULTS 256

    /// Opaque handle to a Quadtree instance.
    typedef struct rt_quadtree_impl *rt_quadtree;

    /// Creates a new Quadtree.
    /// @param x Bounds X (top-left, fixed-point: 1000 = 1 unit).
    /// @param y Bounds Y (top-left).
    /// @param width Bounds width.
    /// @param height Bounds height.
    /// @return A new Quadtree instance.
    rt_quadtree rt_quadtree_new(int64_t x, int64_t y, int64_t width, int64_t height);

    /// Destroys a Quadtree.
    /// @param tree The quadtree to destroy.
    void rt_quadtree_destroy(rt_quadtree tree);

    /// Clears all items from the quadtree.
    /// @param tree The quadtree.
    void rt_quadtree_clear(rt_quadtree tree);

    /// Inserts an item into the quadtree.
    /// @param tree The quadtree.
    /// @param id Unique item ID.
    /// @param x Item X position (center).
    /// @param y Item Y position (center).
    /// @param width Item width.
    /// @param height Item height.
    /// @return 1 on success, 0 if out of bounds or tree is full.
    int8_t rt_quadtree_insert(
        rt_quadtree tree, int64_t id, int64_t x, int64_t y, int64_t width, int64_t height);

    /// Removes an item from the quadtree.
    /// @param tree The quadtree.
    /// @param id Item ID to remove.
    /// @return 1 if found and removed, 0 if not found.
    int8_t rt_quadtree_remove(rt_quadtree tree, int64_t id);

    /// Updates an item's position (remove + insert).
    /// @param tree The quadtree.
    /// @param id Item ID.
    /// @param x New X position.
    /// @param y New Y position.
    /// @param width Item width.
    /// @param height Item height.
    /// @return 1 on success, 0 on failure.
    int8_t rt_quadtree_update(
        rt_quadtree tree, int64_t id, int64_t x, int64_t y, int64_t width, int64_t height);

    /// Queries items in a rectangular region.
    /// @param tree The quadtree.
    /// @param x Query region X.
    /// @param y Query region Y.
    /// @param width Query region width.
    /// @param height Query region height.
    /// @return Number of items found (results stored internally).
    int64_t rt_quadtree_query_rect(
        rt_quadtree tree, int64_t x, int64_t y, int64_t width, int64_t height);

    /// Queries items near a point.
    /// @param tree The quadtree.
    /// @param x Center X.
    /// @param y Center Y.
    /// @param radius Search radius.
    /// @return Number of items found (results stored internally).
    int64_t rt_quadtree_query_point(rt_quadtree tree, int64_t x, int64_t y, int64_t radius);

    /// Gets an item ID from the last query result.
    /// @param tree The quadtree.
    /// @param index Result index (0 to query_count-1).
    /// @return Item ID, or -1 if invalid index.
    int64_t rt_quadtree_get_result(rt_quadtree tree, int64_t index);

    /// Gets the number of results from the last query.
    /// @param tree The quadtree.
    /// @return Number of results.
    int64_t rt_quadtree_result_count(rt_quadtree tree);

    /// Gets the total number of items in the tree.
    /// @param tree The quadtree.
    /// @return Total item count.
    int64_t rt_quadtree_item_count(rt_quadtree tree);

    /// Gets potential collision pairs (broad phase).
    /// Use this for efficient collision detection.
    /// @param tree The quadtree.
    /// @return Number of potential collision pairs found.
    int64_t rt_quadtree_get_pairs(rt_quadtree tree);

    /// Gets the first item ID of a collision pair.
    /// @param tree The quadtree.
    /// @param pair_index Pair index (0 to pair_count-1).
    /// @return First item ID, or -1 if invalid.
    int64_t rt_quadtree_pair_first(rt_quadtree tree, int64_t pair_index);

    /// Gets the second item ID of a collision pair.
    /// @param tree The quadtree.
    /// @param pair_index Pair index (0 to pair_count-1).
    /// @return Second item ID, or -1 if invalid.
    int64_t rt_quadtree_pair_second(rt_quadtree tree, int64_t pair_index);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_QUADTREE_H
