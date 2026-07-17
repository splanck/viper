//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_quadtree.h
// Purpose: Quadtree spatial partition for efficient collision queries, reducing O(n^2) broad-phase
// checks to O(n log n) by subdividing 2D space into adaptive quad cells.
//
// Key invariants:
//   - Items are identified by unique int64 IDs.
//   - The tree subdivides up to RT_QUADTREE_MAX_DEPTH levels.
//   - Leaf nodes hold at most RT_QUADTREE_MAX_ITEMS before splitting.
//   - Query results remain valid until the next query or mutation.
//
// Ownership/Lifetime:
//   - Caller owns the quadtree handle; destroy with rt_quadtree_destroy.
//   - Internal query result buffers are owned by the quadtree.
//
// Links: src/runtime/game/rt_quadtree.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_QUADTREE_CLASS_ID INT64_C(-0x510209)
#define RT_GAME_QUERY_RESULT_CLASS_ID INT64_C(-0x51021D)
#define RT_QUADTREE_PAIR_RESULT_CLASS_ID INT64_C(-0x51021E)

/// Maximum items per node before splitting.
#define RT_QUADTREE_MAX_ITEMS 8

/// Maximum tree depth.
#define RT_QUADTREE_MAX_DEPTH 8

/// Default reservation for query results. The runtime grows beyond this when needed.
#define RT_QUADTREE_MAX_RESULTS 256

/// Opaque handle to a Quadtree instance.
typedef struct rt_quadtree_impl *rt_quadtree;

/// @brief Create a new Quadtree covering the specified bounds.
/// @param x Bounds X (top-left, fixed-point: 1000 = 1 unit).
/// @param y Bounds Y (top-left).
/// @param width Bounds width.
/// @param height Bounds height.
/// @return A new Quadtree instance, or NULL if width/height are non-positive.
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
/// @return 1 on success, 0 if dimensions are non-positive, out of bounds, duplicate,
///         or tree is full.
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
/// @return 1 on success, 0 if missing, dimensions are non-positive, or placement fails.
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

/// @brief Query items intersecting a rectangle and return a snapshot result.
/// @details Performs the same query as rt_quadtree_query_rect, then copies the
///          result IDs into an immutable Zanna.Game.QueryResult. The returned
///          result remains valid after later quadtree queries or mutations.
/// @param tree The quadtree.
/// @param x Query region X.
/// @param y Query region Y.
/// @param width Query region width.
/// @param height Query region height.
/// @return New Zanna.Game.QueryResult object, or NULL on allocation failure.
void *rt_quadtree_query_rect_result(
    rt_quadtree tree, int64_t x, int64_t y, int64_t width, int64_t height);

/// @brief Query items near a point within a given radius.
/// @param tree The quadtree.
/// @param x Center X of the search area.
/// @param y Center Y of the search area.
/// @param radius Search radius around the point.
/// @return Number of items found (results stored internally; retrieve
///         with rt_quadtree_get_result()).
int64_t rt_quadtree_query_point(rt_quadtree tree, int64_t x, int64_t y, int64_t radius);

/// @brief Query items near a point and return a snapshot result.
/// @details Performs the same query as rt_quadtree_query_point, then copies the
///          filtered result IDs into an immutable Zanna.Game.QueryResult.
/// @param tree The quadtree.
/// @param x Center X of the search area.
/// @param y Center Y of the search area.
/// @param radius Search radius around the point.
/// @return New Zanna.Game.QueryResult object, or NULL on allocation failure.
void *rt_quadtree_query_point_result(rt_quadtree tree, int64_t x, int64_t y, int64_t radius);

/// @brief Get an item ID from the last query result.
/// @param tree The quadtree.
/// @param index Result index (0 to query_count-1).
/// @return Item ID, or -1 if invalid index.
int64_t rt_quadtree_get_result(rt_quadtree tree, int64_t index);

/// @brief Get the number of results from the last query.
/// @param tree The quadtree.
/// @return Number of results from the most recent query.
int64_t rt_quadtree_result_count(rt_quadtree tree);

/// @brief Return the number of IDs in a QueryResult.
/// @param result Zanna.Game.QueryResult object.
/// @return Result count, or 0 for invalid input.
int64_t rt_game_query_result_count(void *result);

/// @brief Read an item ID from a QueryResult by index.
/// @param result Zanna.Game.QueryResult object.
/// @param index Zero-based item index.
/// @return Item ID, or -1 if the index is invalid.
int64_t rt_game_query_result_get_id(void *result, int64_t index);

/// @brief Test whether a QueryResult contains an item ID.
/// @param result Zanna.Game.QueryResult object.
/// @param id Item ID to search for.
/// @return 1 when the ID exists in the result, otherwise 0.
int8_t rt_game_query_result_contains(void *result, int64_t id);

/// @brief Check whether a QueryResult is partial because storage growth failed.
/// @param result Zanna.Game.QueryResult object.
/// @return 1 when the underlying quadtree query was truncated, otherwise 0.
int8_t rt_game_query_result_truncated(void *result);

/// @brief Copy QueryResult IDs into a new Zanna.Collections.Seq.
/// @details Each ID is boxed as an integer. The caller owns the returned Seq.
/// @param result Zanna.Game.QueryResult object.
/// @return New Seq of boxed item IDs, or an empty Seq for invalid input.
void *rt_game_query_result_ids(void *result);

/// @brief Check whether the last query returned partial results.
///
/// Returns 1 only if the most recent rt_quadtree_query_rect() or
/// rt_quadtree_query_point() could not grow its result storage and therefore
/// returned a partial result. Normal queries grow beyond RT_QUADTREE_MAX_RESULTS.
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

/// @brief Compute broad-phase collision pairs and return a snapshot result.
/// @details Performs the same operation as rt_quadtree_get_pairs, then copies
///          pair IDs into an immutable Zanna.Game.QuadtreePairResult.
/// @param tree The quadtree.
/// @return New Zanna.Game.QuadtreePairResult object, or NULL on allocation failure.
void *rt_quadtree_query_pairs(rt_quadtree tree);

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

/// @brief Check whether the last rt_quadtree_get_pairs() returned partial pairs.
///
/// Returns 1 only if the most recent rt_quadtree_get_pairs() could not grow its
/// pair storage and therefore returned partial pairs.
/// @param tree The quadtree.
/// @return 1 if the last pair collection was truncated, 0 otherwise.
int8_t rt_quadtree_pairs_was_truncated(rt_quadtree tree);

/// @brief Return the number of pairs in a QuadtreePairResult.
/// @param result Zanna.Game.QuadtreePairResult object.
/// @return Pair count, or 0 for invalid input.
int64_t rt_quadtree_pair_result_count(void *result);

/// @brief Read the first item ID from a pair result by index.
/// @param result Zanna.Game.QuadtreePairResult object.
/// @param index Zero-based pair index.
/// @return First item ID, or -1 if @p index is invalid.
int64_t rt_quadtree_pair_result_first(void *result, int64_t index);

/// @brief Read the second item ID from a pair result by index.
/// @param result Zanna.Game.QuadtreePairResult object.
/// @param index Zero-based pair index.
/// @return Second item ID, or -1 if @p index is invalid.
int64_t rt_quadtree_pair_result_second(void *result, int64_t index);

/// @brief Check whether a pair result is partial because storage growth failed.
/// @param result Zanna.Game.QuadtreePairResult object.
/// @return 1 when the underlying pair collection was truncated, otherwise 0.
int8_t rt_quadtree_pair_result_truncated(void *result);

#ifdef __cplusplus
}
#endif
