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
/// Key invariants: Items are identified by unique int64 IDs. The tree
///     subdivides up to RT_QUADTREE_MAX_DEPTH levels, with at most
///     RT_QUADTREE_MAX_ITEMS per leaf before splitting.
/// Ownership/Lifetime: Caller owns the quadtree handle; destroy with
///     rt_quadtree_destroy(). Query results are stored internally and
///     remain valid until the next query or mutation.
/// Links: Viper.Quadtree standard library module.
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

    /// @brief Create a new Quadtree covering the specified bounds.
    /// @param x Bounds X (top-left, fixed-point: 1000 = 1 unit).
    /// @param y Bounds Y (top-left).
    /// @param width Bounds width.
    /// @param height Bounds height.
    /// @return A new Quadtree instance.
    rt_quadtree rt_quadtree_new(int64_t x, int64_t y, int64_t width, int64_t height);

    /// @brief Destroy a Quadtree and free all associated memory.
    /// @param tree The quadtree to destroy.
    void rt_quadtree_destroy(rt_quadtree tree);

    /// @brief Clear all items from the quadtree.
    /// @param tree The quadtree.
    void rt_quadtree_clear(rt_quadtree tree);

    /// @brief Insert an item into the quadtree.
    /// @param tree The quadtree.
    /// @param id Unique item ID.
    /// @param x Item X position (center).
    /// @param y Item Y position (center).
    /// @param width Item width.
    /// @param height Item height.
    /// @return 1 on success, 0 if out of bounds or tree is full.
    int8_t rt_quadtree_insert(
        rt_quadtree tree, int64_t id, int64_t x, int64_t y, int64_t width, int64_t height);

    /// @brief Remove an item from the quadtree.
    /// @param tree The quadtree.
    /// @param id Item ID to remove.
    /// @return 1 if found and removed, 0 if not found.
    int8_t rt_quadtree_remove(rt_quadtree tree, int64_t id);

    /// @brief Update an item's position and size (remove + re-insert).
    /// @param tree The quadtree.
    /// @param id Item ID.
    /// @param x New X position.
    /// @param y New Y position.
    /// @param width Item width.
    /// @param height Item height.
    /// @return 1 on success, 0 on failure.
    int8_t rt_quadtree_update(
        rt_quadtree tree, int64_t id, int64_t x, int64_t y, int64_t width, int64_t height);

    /// @brief Query items intersecting a rectangular region.
    /// @param tree The quadtree.
    /// @param x Query region X.
    /// @param y Query region Y.
    /// @param width Query region width.
    /// @param height Query region height.
    /// @return Number of items found (results stored internally; retrieve
    ///         with rt_quadtree_get_result()).
    int64_t rt_quadtree_query_rect(
        rt_quadtree tree, int64_t x, int64_t y, int64_t width, int64_t height);

    /// @brief Query items near a point within a given radius.
    /// @param tree The quadtree.
    /// @param x Center X of the search area.
    /// @param y Center Y of the search area.
    /// @param radius Search radius around the point.
    /// @return Number of items found (results stored internally; retrieve
    ///         with rt_quadtree_get_result()).
    int64_t rt_quadtree_query_point(rt_quadtree tree, int64_t x, int64_t y, int64_t radius);

    /// @brief Get an item ID from the last query result.
    /// @param tree The quadtree.
    /// @param index Result index (0 to query_count-1).
    /// @return Item ID, or -1 if invalid index.
    int64_t rt_quadtree_get_result(rt_quadtree tree, int64_t index);

    /// @brief Get the number of results from the last query.
    /// @param tree The quadtree.
    /// @return Number of results from the most recent query.
    int64_t rt_quadtree_result_count(rt_quadtree tree);

    /// @brief Check whether the last query was silently truncated.
    ///
    /// Returns 1 if the most recent rt_quadtree_query_rect() or
    /// rt_quadtree_query_point() hit the RT_QUADTREE_MAX_RESULTS cap and
    /// dropped items. Callers that rely on "all overlapping entities" MUST
    /// check this flag and subdivide the query or increase the cap.
    /// @param tree The quadtree.
    /// @return 1 if the last query was truncated, 0 otherwise.
    int8_t rt_quadtree_query_was_truncated(rt_quadtree tree);

    /// @brief Get the total number of items in the tree.
    /// @param tree The quadtree.
    /// @return Total item count.
    int64_t rt_quadtree_item_count(rt_quadtree tree);

    /// @brief Compute potential collision pairs (broad phase).
    /// @details Use this for efficient collision detection. Identifies all
    ///          pairs of items whose bounding boxes overlap.
    /// @param tree The quadtree.
    /// @return Number of potential collision pairs found (retrieve individual
    ///         pairs with rt_quadtree_pair_first() and rt_quadtree_pair_second()).
    int64_t rt_quadtree_get_pairs(rt_quadtree tree);

    /// @brief Get the first item ID of a collision pair.
    /// @param tree The quadtree.
    /// @param pair_index Pair index (0 to pair_count-1).
    /// @return First item ID of the pair, or -1 if invalid index.
    int64_t rt_quadtree_pair_first(rt_quadtree tree, int64_t pair_index);

    /// @brief Get the second item ID of a collision pair.
    /// @param tree The quadtree.
    /// @param pair_index Pair index (0 to pair_count-1).
    /// @return Second item ID of the pair, or -1 if invalid index.
    int64_t rt_quadtree_pair_second(rt_quadtree tree, int64_t pair_index);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_QUADTREE_H
