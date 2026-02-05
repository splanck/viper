//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_convert_coll.h
/// @brief Collection conversion utilities.
///
/// Provides functions to convert between different collection types:
/// Seq, List, Set, Map, Stack, Queue, Deque, Bag.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_CONVERT_COLL_H
#define VIPER_RT_CONVERT_COLL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Seq Conversions
//=============================================================================

/// @brief Convert a Seq to a List.
/// @param seq Source Seq.
/// @return New List with same elements.
void *rt_seq_to_list(void *seq);

/// @brief Convert a Seq to a Set (removes duplicates by pointer equality).
/// @param seq Source Seq.
/// @return New Set with unique elements.
void *rt_seq_to_set(void *seq);

/// @brief Convert a Seq to a Stack (top = last element).
/// @param seq Source Seq.
/// @return New Stack with same elements.
void *rt_seq_to_stack(void *seq);

/// @brief Convert a Seq to a Queue (front = first element).
/// @param seq Source Seq.
/// @return New Queue with same elements.
void *rt_seq_to_queue(void *seq);

/// @brief Convert a Seq to a Deque.
/// @param seq Source Seq.
/// @return New Deque with same elements.
void *rt_seq_to_deque(void *seq);

/// @brief Convert a Seq to a Bag (string set).
/// @param seq Source Seq (must contain strings).
/// @return New Bag with unique strings.
void *rt_seq_to_bag(void *seq);

//=============================================================================
// List Conversions
//=============================================================================

/// @brief Convert a List to a Seq.
/// @param list Source List.
/// @return New Seq with same elements.
void *rt_list_to_seq(void *list);

/// @brief Convert a List to a Set.
/// @param list Source List.
/// @return New Set with unique elements.
void *rt_list_to_set(void *list);

/// @brief Convert a List to a Stack.
/// @param list Source List.
/// @return New Stack with same elements.
void *rt_list_to_stack(void *list);

/// @brief Convert a List to a Queue.
/// @param list Source List.
/// @return New Queue with same elements.
void *rt_list_to_queue(void *list);

//=============================================================================
// Set Conversions
//=============================================================================

/// @brief Convert a Set to a Seq.
/// @param set Source Set.
/// @return New Seq with all elements.
void *rt_set_to_seq(void *set);

/// @brief Convert a Set to a List.
/// @param set Source Set.
/// @return New List with all elements.
void *rt_set_to_list(void *set);

//=============================================================================
// Stack Conversions
//=============================================================================

/// @brief Convert a Stack to a Seq (order: bottom to top).
/// @param stack Source Stack.
/// @return New Seq with all elements.
void *rt_stack_to_seq(void *stack);

/// @brief Convert a Stack to a List.
/// @param stack Source Stack.
/// @return New List with all elements.
void *rt_stack_to_list(void *stack);

//=============================================================================
// Queue Conversions
//=============================================================================

/// @brief Convert a Queue to a Seq (order: front to back).
/// @param queue Source Queue.
/// @return New Seq with all elements.
void *rt_queue_to_seq(void *queue);

/// @brief Convert a Queue to a List.
/// @param queue Source Queue.
/// @return New List with all elements.
void *rt_queue_to_list(void *queue);

//=============================================================================
// Deque Conversions
//=============================================================================

/// @brief Convert a Deque to a Seq (order: front to back).
/// @param deque Source Deque.
/// @return New Seq with all elements.
void *rt_deque_to_seq(void *deque);

/// @brief Convert a Deque to a List.
/// @param deque Source Deque.
/// @return New List with all elements.
void *rt_deque_to_list(void *deque);

//=============================================================================
// Map Conversions
//=============================================================================

/// @brief Get all keys from a Map as a Seq.
/// @param map Source Map.
/// @return New Seq with all keys.
void *rt_map_keys_to_seq(void *map);

/// @brief Get all values from a Map as a Seq.
/// @param map Source Map.
/// @return New Seq with all values.
void *rt_map_values_to_seq(void *map);

//=============================================================================
// Bag Conversions
//=============================================================================

/// @brief Convert a Bag to a Seq (each element appears count times).
/// @param bag Source Bag.
/// @return New Seq with repeated elements.
void *rt_bag_to_seq(void *bag);

/// @brief Convert a Bag to a Set (unique elements only, counts discarded).
/// @param bag Source Bag.
/// @return New Set with unique elements.
void *rt_bag_to_set(void *bag);

//=============================================================================
// Ring Conversions
//=============================================================================

/// @brief Convert a Ring to a Seq.
/// @param ring Source Ring.
/// @return New Seq with all elements.
void *rt_ring_to_seq(void *ring);

//=============================================================================
// Utility Functions
//=============================================================================

/// @brief Create a Seq from a variable number of elements.
/// @param count Number of elements.
/// @param ... Elements to add.
/// @return New Seq with given elements.
void *rt_seq_of(int64_t count, ...);

/// @brief Create a List from a variable number of elements.
/// @param count Number of elements.
/// @param ... Elements to add.
/// @return New List with given elements.
void *rt_list_of(int64_t count, ...);

/// @brief Create a Set from a variable number of elements.
/// @param count Number of elements.
/// @param ... Elements to add.
/// @return New Set with given elements.
void *rt_set_of(int64_t count, ...);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_CONVERT_COLL_H
