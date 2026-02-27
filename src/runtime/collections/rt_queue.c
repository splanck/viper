//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_queue.c
// Purpose: Implements a FIFO (first-in-first-out) queue backed by a circular
//   buffer. Elements are added (enqueued) at the tail and removed (dequeued)
//   from the head. Both operations are O(1) amortized; the circular buffer
//   avoids element shifting on dequeue.
//
// Key invariants:
//   - Backed by a circular buffer with initial capacity QUEUE_DEFAULT_CAP (16).
//     Growth factor is QUEUE_GROWTH_FACTOR (2); elements are linearized into
//     the new array during resize.
//   - head is the index of the next element to dequeue (oldest element).
//   - tail is computed as (head + count) % capacity (next write position).
//   - When head == (head + count) % capacity the buffer is full and must grow.
//   - Dequeue on an empty queue traps with an error message.
//   - Peek returns the head element without removing it; returns NULL if empty.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - Queue objects are GC-managed (rt_obj_new_i64). The items array is
//     realloc-managed and freed by the GC finalizer (queue_finalizer).
//
// Links: src/runtime/collections/rt_queue.h (public API),
//        src/runtime/collections/rt_deque.h (double-ended queue variant)
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

#define QUEUE_DEFAULT_CAP 16
#define QUEUE_GROWTH_FACTOR 2

/// @brief Internal queue implementation structure (circular buffer).
///
/// The Queue is implemented as a circular buffer (ring buffer) for efficient
/// O(1) add and take operations. Elements are stored in a contiguous array,
/// with head and tail indices that wrap around when they reach the end.
///
/// **Circular buffer concept:**
/// Instead of shifting elements when removing from the front, we just move
/// the head pointer forward. When indices reach the end of the array, they
/// wrap around to the beginning (modulo arithmetic).
///
/// **Memory layout example (capacity=8, 4 elements):**
/// ```
/// Scenario 1 - Contiguous:
///   indices: [0] [1] [2] [3] [4] [5] [6] [7]
///   items:   [ ] [ ] [A] [B] [C] [D] [ ] [ ]
///                     ^           ^
///                   head=2     tail=6
///
/// Scenario 2 - Wrapped around:
///   indices: [0] [1] [2] [3] [4] [5] [6] [7]
///   items:   [C] [D] [ ] [ ] [ ] [ ] [A] [B]
///             ^                       ^
///           tail=2                  head=6
/// ```
///
/// The circular design means:
/// - Add (enqueue) at tail: O(1)
/// - Take (dequeue) from head: O(1)
/// - No element shifting needed
typedef struct rt_queue_impl
{
    int64_t len;  ///< Number of elements currently in the queue
    int64_t cap;  ///< Current capacity (allocated slots)
    int64_t head; ///< Index of first element (front of queue)
    int64_t tail; ///< Index where next element will be inserted (back of queue)
    void **items; ///< Circular buffer of element pointers
} rt_queue_impl;

/// @brief Finalizer callback invoked when a Queue is garbage collected.
///
/// This function is automatically called by Viper's garbage collector when a
/// Queue object becomes unreachable. It frees the internal items array to
/// prevent memory leaks.
///
/// @param obj Pointer to the Queue object being finalized. May be NULL (no-op).
///
/// @note The Queue does NOT own the elements it contains. Elements are not
///       freed during finalization - they must be managed separately.
/// @note This function is idempotent - safe to call on already-finalized queues.
///
/// @see rt_queue_clear For removing elements without finalization
static void rt_queue_finalize(void *obj)
{
    if (!obj)
        return;
    rt_queue_impl *q = (rt_queue_impl *)obj;
    free(q->items);
    q->items = NULL;
    q->len = 0;
    q->cap = 0;
    q->head = 0;
    q->tail = 0;
}

/// @brief Grows the queue capacity and linearizes the circular buffer.
///
/// When the queue is full and a new element needs to be added, this function:
/// 1. Allocates a new array with double the capacity
/// 2. Copies elements from the circular buffer to the new array in linear order
/// 3. Resets head to 0 and tail to len
///
/// **Linearization process:**
/// The circular buffer may have elements wrapped around. During growth, we
/// "unwrap" them into a contiguous linear array:
/// ```
/// Before (wrapped):     [C] [D] [ ] [ ] [A] [B]   head=4, tail=2
/// After (linearized):   [A] [B] [C] [D] [ ] [ ] [ ] [ ]  head=0, tail=4
/// ```
///
/// @param q Pointer to the queue implementation. Must not be NULL.
///
/// @note Capacity doubles each time (QUEUE_GROWTH_FACTOR = 2).
/// @note Traps on memory allocation failure.
/// @note O(n) time complexity where n is the number of elements.
static void queue_grow(rt_queue_impl *q)
{
    int64_t new_cap = q->cap * QUEUE_GROWTH_FACTOR;
    void **new_items = malloc((size_t)new_cap * sizeof(void *));

    if (!new_items)
    {
        rt_trap("Queue: memory allocation failed");
    }

    // Linearize the circular buffer into the new array
    if (q->len > 0)
    {
        if (q->head < q->tail)
        {
            // Contiguous region: head...tail
            memcpy(new_items, &q->items[q->head], (size_t)q->len * sizeof(void *));
        }
        else
        {
            // Wrapped around: head...end, then start...tail
            int64_t first_part = q->cap - q->head;
            memcpy(new_items, &q->items[q->head], (size_t)first_part * sizeof(void *));
            memcpy(&new_items[first_part], q->items, (size_t)q->tail * sizeof(void *));
        }
    }

    free(q->items);
    q->items = new_items;
    q->head = 0;
    q->tail = q->len;
    q->cap = new_cap;
}

/// @brief Creates a new empty Queue with default capacity.
///
/// Allocates and initializes a Queue data structure for FIFO (First-In-First-Out)
/// operations. The Queue starts with a default capacity of 16 slots and grows
/// automatically when needed.
///
/// The Queue is implemented as a circular buffer, providing O(1) add and take
/// operations without element shifting.
///
/// **Usage example:**
/// ```
/// Dim queue = Queue.New()
/// queue.Add("first")
/// queue.Add("second")
/// queue.Add("third")
/// Print queue.Take()   ' Outputs: first
/// Print queue.Take()   ' Outputs: second
/// Print queue.Take()   ' Outputs: third
/// ```
///
/// @return A pointer to the newly created Queue object. Traps and does not
///         return if memory allocation fails.
///
/// @note Initial capacity is 16 elements (QUEUE_DEFAULT_CAP).
/// @note The Queue does not own the elements stored in it.
/// @note Thread safety: Not thread-safe. External synchronization required.
///
/// @see rt_queue_push For adding elements
/// @see rt_queue_pop For removing elements
/// @see rt_queue_finalize For cleanup behavior
void *rt_queue_new(void)
{
    rt_queue_impl *q = (rt_queue_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_queue_impl));
    if (!q)
    {
        rt_trap("Queue: memory allocation failed");
    }

    q->len = 0;
    q->cap = QUEUE_DEFAULT_CAP;
    q->head = 0;
    q->tail = 0;
    q->items = malloc((size_t)QUEUE_DEFAULT_CAP * sizeof(void *));
    rt_obj_set_finalizer(q, rt_queue_finalize);

    if (!q->items)
    {
        if (rt_obj_release_check0(q))
            rt_obj_free(q);
        rt_trap("Queue: memory allocation failed");
    }

    return q;
}

/// @brief Returns the number of elements currently in the Queue.
///
/// This function returns how many elements have been added and not yet taken.
/// The count is maintained internally and returned in O(1) time.
///
/// @param obj Pointer to a Queue object. If NULL, returns 0.
///
/// @return The number of elements in the Queue (>= 0). Returns 0 if obj is NULL.
///
/// @note O(1) time complexity.
///
/// @see rt_queue_is_empty For a boolean check
/// @see rt_queue_push For operations that increase the count
/// @see rt_queue_pop For operations that decrease the count
int64_t rt_queue_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_queue_impl *)obj)->len;
}

/// @brief Checks whether the Queue contains no elements.
///
/// A Queue is considered empty when its length is 0, which occurs:
/// - Immediately after creation
/// - After all elements have been taken
/// - After calling rt_queue_clear
///
/// Calling Take or Peek on an empty Queue will trap with an error.
///
/// @param obj Pointer to a Queue object. If NULL, returns true (1).
///
/// @return 1 (true) if the Queue is empty or obj is NULL, 0 (false) otherwise.
///
/// @note O(1) time complexity.
///
/// @see rt_queue_len For the exact count
/// @see rt_queue_pop For removing elements (traps if empty)
/// @see rt_queue_peek For viewing front element (traps if empty)
int8_t rt_queue_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_queue_impl *)obj)->len == 0 ? 1 : 0;
}

/// @brief Adds an element to the back of the Queue.
///
/// Enqueues a new element at the tail of the Queue. This is the primary
/// insertion operation for FIFO behavior - elements are added at the back
/// and removed from the front.
///
/// If the Queue's capacity is exceeded, it automatically grows (doubles)
/// to accommodate the new element.
///
/// **Visual example:**
/// ```
/// Before Add(D):  front->[A, B, C]<-back
/// After Add(D):   front->[A, B, C, D]<-back
/// ```
///
/// @param obj Pointer to a Queue object. Must not be NULL.
/// @param val The element to add. May be NULL (NULL is a valid element).
///
/// @note O(1) amortized time complexity. Occasional O(n) when resizing occurs.
/// @note The Queue does not take ownership of val.
/// @note Traps with "Queue.Add: null queue" if obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_queue_pop For the removal operation
/// @see rt_queue_peek For viewing without removing
void rt_queue_push(void *obj, void *val)
{
    if (!obj)
        rt_trap("Queue.Add: null queue");

    rt_queue_impl *q = (rt_queue_impl *)obj;

    if (q->len >= q->cap)
    {
        queue_grow(q);
    }

    q->items[q->tail] = val;
    q->tail = (q->tail + 1) % q->cap;
    q->len++;
}

/// @brief Removes and returns the front element from the Queue.
///
/// Dequeues the element at the front of the Queue (the oldest element).
/// This is the primary retrieval operation for FIFO behavior.
///
/// **Visual example:**
/// ```
/// Before Take():  front->[A, B, C, D]<-back
/// After Take():   front->[B, C, D]<-back
/// Returns: A
/// ```
///
/// **Error handling:**
/// Calling Take on an empty Queue is a programming error and traps with
/// "Queue.Take: queue is empty". Always check rt_queue_is_empty before
/// taking, or use a try-catch pattern if available.
///
/// @param obj Pointer to a Queue object. Must not be NULL.
///
/// @return The element that was at the front of the Queue.
///
/// @note O(1) time complexity.
/// @note The Queue releases its reference - caller now owns the element.
/// @note Traps if the Queue is empty or obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_queue_push For the insertion operation
/// @see rt_queue_peek For viewing without removing
/// @see rt_queue_is_empty For checking before take
void *rt_queue_pop(void *obj)
{
    if (!obj)
        rt_trap("Queue.Take: null queue");

    rt_queue_impl *q = (rt_queue_impl *)obj;

    if (q->len == 0)
    {
        rt_trap("Queue.Take: queue is empty");
    }

    void *val = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->len--;

    return val;
}

/// @brief Returns the front element without removing it from the Queue.
///
/// Peeks at the element at the front of the Queue (the next one to be taken)
/// without modifying the Queue. Useful for:
/// - Inspecting the next element before deciding to take it
/// - Implementing conditional dequeue logic
/// - Debugging or logging
///
/// **Example:**
/// ```
/// queue.Add("A")
/// queue.Add("B")
/// Print queue.Peek()  ' Outputs: A
/// Print queue.Peek()  ' Outputs: A (still there)
/// Print queue.Take()  ' Outputs: A (now removed)
/// Print queue.Peek()  ' Outputs: B
/// ```
///
/// @param obj Pointer to a Queue object. Must not be NULL.
///
/// @return The element at the front of the Queue (not removed).
///
/// @note O(1) time complexity.
/// @note The Queue retains ownership - the returned pointer is only valid
///       as long as the element remains in the Queue.
/// @note Traps if the Queue is empty or obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_queue_pop For removing while retrieving
/// @see rt_queue_is_empty For checking before peek
void *rt_queue_peek(void *obj)
{
    if (!obj)
        rt_trap("Queue.Peek: null queue");

    rt_queue_impl *q = (rt_queue_impl *)obj;

    if (q->len == 0)
    {
        rt_trap("Queue.Peek: queue is empty");
    }

    return q->items[q->head];
}

/// @brief Removes all elements from the Queue.
///
/// Clears the Queue by resetting its length, head, and tail to 0.
/// The capacity remains unchanged (no memory is freed), allowing the
/// Queue to be efficiently reused.
///
/// **After clear:**
/// - Length becomes 0
/// - Head and tail reset to 0
/// - is_empty returns true
/// - Capacity unchanged (no reallocation)
/// - All element references are forgotten (not freed)
///
/// @param obj Pointer to a Queue object. If NULL, this is a no-op.
///
/// @note O(1) time complexity - just resets the indices.
/// @note The Queue does NOT free the elements.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_queue_finalize For complete cleanup
/// @see rt_queue_is_empty For checking if empty
void rt_queue_clear(void *obj)
{
    if (!obj)
        return;

    rt_queue_impl *q = (rt_queue_impl *)obj;
    q->len = 0;
    q->head = 0;
    q->tail = 0;
}

/// @brief Check if the queue contains a given element (pointer equality).
/// @param obj Opaque Queue object pointer.
/// @param elem Element to search for.
/// @return 1 if found, 0 otherwise.
int8_t rt_queue_has(void *obj, void *elem)
{
    if (!obj)
        return 0;

    rt_queue_impl *q = (rt_queue_impl *)obj;
    for (int64_t i = 0; i < q->len; i++)
    {
        if (q->items[(q->head + i) % q->cap] == elem)
            return 1;
    }
    return 0;
}
