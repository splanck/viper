//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/sched/heap.hpp
// Purpose: Intrusive min-heap for efficient task selection.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../console/serial.hpp"
#include "../include/types.hpp"
#include "task.hpp"

/**
 * @file heap.hpp
 * @brief Intrusive min-heap for O(log n) task scheduling.
 *
 * @details
 * This heap is used to efficiently select the task with the minimum key value.
 * For CFS scheduling, the key is vruntime.
 * For deadline scheduling, the key is dl_abs_deadline.
 *
 * The heap is "intrusive" meaning tasks store their own heap index,
 * avoiding separate allocations and enabling O(1) removal by pointer.
 */
namespace sched {

/**
 * @brief Maximum heap capacity (matches MAX_TASKS).
 */
constexpr u32 HEAP_MAX_SIZE = task::MAX_TASKS;

/**
 * @brief Function type for extracting comparison key from a task.
 */
using HeapKeyFunc = u64 (*)(const task::Task *t);

/**
 * @brief Intrusive min-heap for task scheduling.
 *
 * @details
 * Tasks in the heap have their heap_index field set to their position.
 * The root (index 0) always has the minimum key value.
 */
struct TaskHeap {
    task::Task *nodes[HEAP_MAX_SIZE]; ///< Array of task pointers
    u32 size;                         ///< Current number of tasks
    HeapKeyFunc key_func;             ///< Function to extract comparison key
};

/**
 * @brief Initialize a task heap.
 *
 * @param heap Heap to initialize.
 * @param key_func Function to extract the comparison key from tasks.
 */
inline void heap_init(TaskHeap *heap, HeapKeyFunc key_func) {
    heap->size = 0;
    heap->key_func = key_func;
}

/**
 * @brief Check if heap is empty.
 *
 * @param heap Heap to check.
 * @return true if empty.
 */
inline bool heap_empty(const TaskHeap *heap) {
    return heap->size == 0;
}

/**
 * @brief Get the task with minimum key (without removing).
 *
 * @param heap Heap to peek.
 * @return Task with minimum key, or nullptr if empty.
 */
inline task::Task *heap_peek(const TaskHeap *heap) {
    return heap->size > 0 ? heap->nodes[0] : nullptr;
}

/**
 * @brief Restore heap property by moving element up.
 * @note Internal function.
 */
inline void heap_sift_up(TaskHeap *heap, u32 idx) {
    while (idx > 0) {
        u32 parent = (idx - 1) / 2;
        u64 key_idx = heap->key_func(heap->nodes[idx]);
        u64 key_parent = heap->key_func(heap->nodes[parent]);

        if (key_idx >= key_parent) {
            break; // Heap property satisfied
        }

        // Swap with parent
        task::Task *tmp = heap->nodes[idx];
        heap->nodes[idx] = heap->nodes[parent];
        heap->nodes[parent] = tmp;

        // Update stored indices
        heap->nodes[idx]->heap_index = idx;
        heap->nodes[parent]->heap_index = parent;

        idx = parent;
    }
}

/**
 * @brief Restore heap property by moving element down.
 * @note Internal function.
 */
inline void heap_sift_down(TaskHeap *heap, u32 idx) {
    while (true) {
        u32 smallest = idx;
        u32 left = 2 * idx + 1;
        u32 right = 2 * idx + 2;

        if (left < heap->size) {
            u64 key_smallest = heap->key_func(heap->nodes[smallest]);
            u64 key_left = heap->key_func(heap->nodes[left]);
            if (key_left < key_smallest) {
                smallest = left;
            }
        }

        if (right < heap->size) {
            u64 key_smallest = heap->key_func(heap->nodes[smallest]);
            u64 key_right = heap->key_func(heap->nodes[right]);
            if (key_right < key_smallest) {
                smallest = right;
            }
        }

        if (smallest == idx) {
            break; // Heap property satisfied
        }

        // Swap with smallest child
        task::Task *tmp = heap->nodes[idx];
        heap->nodes[idx] = heap->nodes[smallest];
        heap->nodes[smallest] = tmp;

        // Update stored indices
        heap->nodes[idx]->heap_index = idx;
        heap->nodes[smallest]->heap_index = smallest;

        idx = smallest;
    }
}

/**
 * @brief Insert a task into the heap.
 *
 * @param heap Heap to insert into.
 * @param t Task to insert.
 * @return true on success, false if heap is full.
 */
inline bool heap_insert(TaskHeap *heap, task::Task *t) {
    if (!t) {
        return false;
    }
    if (heap->size >= HEAP_MAX_SIZE) {
        // DEBUG: heap full
        serial::puts("[heap] FULL! size=");
        serial::put_dec(heap->size);
        serial::puts(" task='");
        serial::puts(t->name);
        serial::puts("'\n");
        return false;
    }

    // Prevent double-insertion - task must not already be in a heap
    if (t->heap_index != static_cast<u32>(-1)) {
        // DEBUG: double insert attempt
        serial::puts("[heap] DOUBLE INSERT! task='");
        serial::puts(t->name);
        serial::puts("' idx=");
        serial::put_dec(t->heap_index);
        serial::puts("\n");
        return false; // Already in a heap
    }

    // Add at end
    u32 idx = heap->size++;
    heap->nodes[idx] = t;
    t->heap_index = idx;

    // Restore heap property
    heap_sift_up(heap, idx);
    return true;
}

/**
 * @brief Remove and return the task with minimum key.
 *
 * @param heap Heap to extract from.
 * @return Task with minimum key, or nullptr if empty.
 */
inline task::Task *heap_extract_min(TaskHeap *heap) {
    if (heap->size == 0) {
        return nullptr;
    }

    task::Task *min = heap->nodes[0];
    min->heap_index = static_cast<u32>(-1); // Mark as not in heap

    // Move last element to root
    heap->size--;
    if (heap->size > 0) {
        heap->nodes[0] = heap->nodes[heap->size];
        heap->nodes[0]->heap_index = 0;
        heap_sift_down(heap, 0);
    }

    return min;
}

/**
 * @brief Remove a specific task from the heap.
 *
 * @param heap Heap to remove from.
 * @param t Task to remove.
 * @return true if removed, false if not in heap.
 */
inline bool heap_remove(TaskHeap *heap, task::Task *t) {
    if (!t || t->heap_index >= heap->size) {
        return false;
    }

    u32 idx = t->heap_index;
    if (heap->nodes[idx] != t) {
        return false; // Sanity check
    }

    t->heap_index = static_cast<u32>(-1); // Mark as not in heap

    // Replace with last element
    heap->size--;
    if (idx < heap->size) {
        heap->nodes[idx] = heap->nodes[heap->size];
        heap->nodes[idx]->heap_index = idx;

        // Restore heap property (may need to go up or down)
        u64 old_key = heap->key_func(t);
        u64 new_key = heap->key_func(heap->nodes[idx]);

        if (new_key < old_key) {
            heap_sift_up(heap, idx);
        } else {
            heap_sift_down(heap, idx);
        }
    }

    return true;
}

/**
 * @brief Update a task's position after its key changed.
 *
 * @param heap Heap containing the task.
 * @param t Task whose key changed.
 * @param old_key Previous key value.
 */
inline void heap_update(TaskHeap *heap, task::Task *t, u64 old_key) {
    if (!t || t->heap_index >= heap->size) {
        return;
    }

    // Verify task is actually at the claimed position
    if (heap->nodes[t->heap_index] != t) {
        return; // Corruption detected - heap_index is stale
    }

    u64 new_key = heap->key_func(t);
    if (new_key < old_key) {
        heap_sift_up(heap, t->heap_index);
    } else if (new_key > old_key) {
        heap_sift_down(heap, t->heap_index);
    }
}

// ============================================================================
// Key extraction functions for different scheduling policies
// ============================================================================

/**
 * @brief Extract vruntime for CFS scheduling.
 */
inline u64 cfs_key(const task::Task *t) {
    return t ? t->vruntime : static_cast<u64>(-1);
}

/**
 * @brief Extract absolute deadline for EDF scheduling.
 */
inline u64 deadline_key(const task::Task *t) {
    return t ? t->dl_abs_deadline : static_cast<u64>(-1);
}

} // namespace sched
