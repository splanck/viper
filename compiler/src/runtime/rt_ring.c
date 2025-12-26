//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_ring.c
// Purpose: Implement a fixed-size circular buffer (ring buffer).
// Structure: [vptr | items | capacity | head | count]
// - vptr: points to class vtable (placeholder for OOP compatibility)
// - items: array of element pointers
// - capacity: fixed maximum size
// - head: index of oldest element
// - count: number of elements currently stored
//
// Behavior:
// - Push adds to tail; if full, overwrites oldest (head advances)
// - Pop removes from head (FIFO order)
// - Get(0) returns oldest, Get(len-1) returns newest
//
//===----------------------------------------------------------------------===//

#include "rt_ring.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// @brief Ring buffer implementation structure.
typedef struct rt_ring_impl
{
    void **vptr;     ///< Vtable pointer placeholder.
    void **items;    ///< Array of element pointers.
    size_t capacity; ///< Maximum number of elements.
    size_t head;     ///< Index of oldest element.
    size_t count;    ///< Number of elements currently stored.
} rt_ring_impl;

/// @brief Finalizer callback invoked by the garbage collector when a Ring is collected.
///
/// This function is called automatically by the Viper runtime's garbage collector
/// when a Ring object becomes unreachable and is about to be freed. The finalizer
/// releases the internal items array that was allocated to store element pointers.
///
/// @note The Ring container does NOT take ownership of the elements it stores.
///       Elements are not released during finalization - they must be managed
///       separately by the caller. This is consistent with other Viper containers
///       like Stack and Queue, which store references without owning them.
///
/// @param obj Pointer to the Ring object being finalized. May be NULL (no-op).
static void rt_ring_finalize(void *obj)
{
    if (!obj)
        return;
    rt_ring_impl *ring = (rt_ring_impl *)obj;
    // Note: We don't release contained items - container doesn't own them
    // (same behavior as Stack and Queue)
    free(ring->items);
    ring->items = NULL;
    ring->capacity = 0;
    ring->head = 0;
    ring->count = 0;
}

/// @brief Creates a new Ring buffer with the specified fixed capacity.
///
/// Allocates and initializes a circular buffer that can hold up to `capacity` elements.
/// The Ring is a fixed-size FIFO container - once created, its capacity cannot change.
/// When the buffer is full and a new element is pushed, the oldest element is
/// automatically overwritten (no memory allocation occurs during push operations).
///
/// The Ring is allocated through Viper's garbage-collected object system via
/// `rt_obj_new_i64`, which means it will be automatically freed when no longer
/// referenced. A finalizer is registered to clean up the internal items array.
///
/// Memory layout after successful creation:
/// ```
/// Ring object (GC-managed):
///   +--------+--------+----------+------+-------+
///   | vptr   | items  | capacity | head | count |
///   | (NULL) | -----> | N        | 0    | 0     |
///   +--------+---|----+----------+------+-------+
///                |
///                v
///   items array (malloc'd):
///   +------+------+------+-----+------+
///   | NULL | NULL | NULL | ... | NULL |
///   +------+------+------+-----+------+
///   (capacity slots, all initially NULL)
/// ```
///
/// @param capacity The maximum number of elements the Ring can hold. If <= 0,
///                 a minimum capacity of 1 is used. Large values may fail if
///                 memory allocation fails.
///
/// @return A pointer to the newly created Ring, or NULL if the Ring object
///         allocation fails. Note: If the items array allocation fails, a
///         Ring with capacity=0 is returned (all operations become no-ops).
///
/// @note The returned Ring does not own the elements stored in it. When elements
///       are overwritten or the Ring is finalized, elements are NOT freed.
///       Callers must manage element lifetimes separately.
///
/// @see rt_ring_push For adding elements to the Ring
/// @see rt_ring_pop For removing elements from the Ring
/// @see rt_ring_finalize For cleanup behavior
void *rt_ring_new(int64_t capacity)
{
    if (capacity <= 0)
        capacity = 1; // Minimum capacity of 1

    rt_ring_impl *ring = (rt_ring_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_ring_impl));
    if (!ring)
        return NULL;

    ring->vptr = NULL;
    ring->items = (void **)calloc((size_t)capacity, sizeof(void *));
    if (!ring->items)
    {
        ring->capacity = 0;
        ring->head = 0;
        ring->count = 0;
        rt_obj_set_finalizer(ring, rt_ring_finalize);
        return ring;
    }
    ring->capacity = (size_t)capacity;
    ring->head = 0;
    ring->count = 0;
    rt_obj_set_finalizer(ring, rt_ring_finalize);
    return ring;
}

/// @brief Returns the current number of elements stored in the Ring.
///
/// This function returns how many elements are currently in the Ring, which is
/// always between 0 and the Ring's capacity (inclusive). The length increases
/// when elements are pushed (until capacity is reached), decreases when elements
/// are popped, and resets to 0 when the Ring is cleared.
///
/// Note that once the Ring is full, pushing new elements does NOT increase the
/// length - the oldest element is overwritten and the length stays at capacity.
///
/// @param obj Pointer to a Ring object. If NULL, returns 0.
///
/// @return The number of elements currently stored in the Ring (0 to capacity).
///         Returns 0 if obj is NULL.
///
/// @note O(1) time complexity - the count is stored directly in the structure.
///
/// @see rt_ring_cap For the maximum capacity
/// @see rt_ring_is_empty For checking if the Ring is empty
int64_t rt_ring_len(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_ring_impl *)obj)->count;
}

/// @brief Returns the maximum capacity of the Ring.
///
/// This function returns the fixed maximum number of elements the Ring can hold,
/// as specified when the Ring was created via `rt_ring_new`. The capacity never
/// changes after creation - Rings are fixed-size containers.
///
/// @param obj Pointer to a Ring object. If NULL, returns 0.
///
/// @return The maximum number of elements the Ring can hold. Returns 0 if obj
///         is NULL or if the Ring was created with a failed items allocation.
///
/// @note O(1) time complexity - the capacity is stored directly in the structure.
///
/// @see rt_ring_new For creating a Ring with a specific capacity
/// @see rt_ring_len For the current number of elements
/// @see rt_ring_is_full For checking if the Ring is at capacity
int64_t rt_ring_cap(void *obj)
{
    if (!obj)
        return 0;
    return (int64_t)((rt_ring_impl *)obj)->capacity;
}

/// @brief Checks whether the Ring contains no elements.
///
/// A Ring is considered empty when its length is 0, which occurs:
/// - Immediately after creation (before any push operations)
/// - After all elements have been popped
/// - After calling rt_ring_clear
///
/// An empty Ring will return NULL from rt_ring_pop, rt_ring_peek, and rt_ring_get.
///
/// @param obj Pointer to a Ring object. If NULL, returns true (1) since a
///            non-existent Ring conceptually has no elements.
///
/// @return 1 (true) if the Ring is empty or obj is NULL, 0 (false) otherwise.
///
/// @note O(1) time complexity.
///
/// @see rt_ring_is_full For the opposite check
/// @see rt_ring_len For the exact count of elements
int8_t rt_ring_is_empty(void *obj)
{
    return rt_ring_len(obj) == 0;
}

/// @brief Checks whether the Ring is at maximum capacity.
///
/// A Ring is considered full when its current length equals its capacity.
/// When a Ring is full:
/// - Pushing a new element will overwrite the oldest element (no error occurs)
/// - The head pointer advances to maintain FIFO ordering
/// - The length stays the same (still at capacity)
///
/// This is useful for callers who want to know if a push will discard data,
/// or who want to implement a non-overwriting policy by checking before push.
///
/// @param obj Pointer to a Ring object. If NULL, returns false (0).
///
/// @return 1 (true) if the Ring is full, 0 (false) if not full or obj is NULL.
///
/// @note O(1) time complexity.
/// @note A Ring with capacity=0 (failed allocation) is never considered full.
///
/// @see rt_ring_is_empty For the opposite check
/// @see rt_ring_push For the behavior when pushing to a full Ring
int8_t rt_ring_is_full(void *obj)
{
    if (!obj)
        return 0;
    rt_ring_impl *ring = (rt_ring_impl *)obj;
    return ring->count == ring->capacity;
}

/// @brief Adds an element to the Ring buffer.
///
/// Pushes a new element to the "tail" (newest end) of the Ring. The behavior
/// depends on whether the Ring is full:
///
/// **When the Ring has space (count < capacity):**
/// - The element is stored at the tail position
/// - The count increases by 1
/// - No existing elements are affected
///
/// **When the Ring is full (count == capacity):**
/// - The element overwrites the oldest element (at head position)
/// - The head advances to the next-oldest element
/// - The count stays the same (still full)
/// - The overwritten element's pointer is lost (caller must manage lifetime)
///
/// Visual example with capacity=3:
/// ```
/// Initial (empty):     [_, _, _]  head=0, count=0
/// Push(A):             [A, _, _]  head=0, count=1
/// Push(B):             [A, B, _]  head=0, count=2
/// Push(C):             [A, B, C]  head=0, count=3 (FULL)
/// Push(D):             [D, B, C]  head=1, count=3 (A overwritten, D at old head position)
/// Push(E):             [D, E, C]  head=2, count=3 (B overwritten)
/// ```
///
/// @param obj Pointer to a Ring object. If NULL, this function is a no-op.
/// @param item The element pointer to add. May be NULL (NULL is a valid element).
///
/// @note O(1) time complexity - no memory allocation or copying occurs.
/// @note The Ring does NOT take ownership of the item. When an item is overwritten,
///       it is simply replaced in the array - no destructor or free is called.
/// @note Thread safety: Not thread-safe. External synchronization required for
///       concurrent access.
///
/// @warning When pushing to a full Ring, the oldest element is silently discarded.
///          Use rt_ring_is_full to check before pushing if data loss is unacceptable.
///
/// @see rt_ring_pop For removing and returning the oldest element
/// @see rt_ring_is_full For checking if push will overwrite
void rt_ring_push(void *obj, void *item)
{
    if (!obj)
        return;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (ring->capacity == 0 || !ring->items)
        return;

    // Calculate tail position (where new element goes)
    size_t tail = (ring->head + ring->count) % ring->capacity;

    if (ring->count == ring->capacity)
    {
        // Ring is full - overwrite oldest element at head position
        ring->items[ring->head] = item;
        // Advance head to next oldest
        ring->head = (ring->head + 1) % ring->capacity;
        // count stays the same (still full)
    }
    else
    {
        // Ring has space - add to tail
        ring->items[tail] = item;
        ring->count++;
    }
}

/// @brief Removes and returns the oldest element from the Ring.
///
/// Pops the element at the "head" (oldest end) of the Ring in FIFO order.
/// This is the element that was pushed earliest and hasn't been overwritten
/// or previously popped.
///
/// After a successful pop:
/// - The head index advances to the next-oldest element
/// - The count decreases by 1
/// - The slot is cleared to NULL (for safety/debugging)
///
/// Visual example with capacity=3:
/// ```
/// State before:  [A, B, C]  head=0, count=3
/// Pop() -> A:    [_, B, C]  head=1, count=2
/// Pop() -> B:    [_, _, C]  head=2, count=1
/// Pop() -> C:    [_, _, _]  head=0, count=0
/// Pop() -> NULL: [_, _, _]  (empty, nothing to pop)
/// ```
///
/// @param obj Pointer to a Ring object. If NULL, returns NULL.
///
/// @return The oldest element in the Ring, or NULL if the Ring is empty or
///         obj is NULL. Note that NULL may also be a valid stored element,
///         so use rt_ring_is_empty to distinguish between "empty" and
///         "contains NULL".
///
/// @note O(1) time complexity.
/// @note Ownership transfer: The Ring releases its reference to the element.
///       The caller is now responsible for the element's lifetime. The Ring
///       never frees elements - it only stores and returns pointers.
/// @note Thread safety: Not thread-safe. External synchronization required.
///
/// @see rt_ring_peek For reading the oldest element without removing it
/// @see rt_ring_push For adding elements
/// @see rt_ring_is_empty For checking if the Ring has elements to pop
void *rt_ring_pop(void *obj)
{
    if (!obj)
        return NULL;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (ring->count == 0 || !ring->items)
        return NULL;

    // Get oldest element (at head)
    void *item = ring->items[ring->head];
    ring->items[ring->head] = NULL;

    // Advance head
    ring->head = (ring->head + 1) % ring->capacity;
    ring->count--;

    // Note: We don't release here - caller takes ownership
    return item;
}

/// @brief Returns the oldest element without removing it from the Ring.
///
/// Peeks at the element at the "head" (oldest end) of the Ring without
/// modifying the Ring's state. This is equivalent to what rt_ring_pop would
/// return, but the element remains in the Ring for future access.
///
/// This function is useful for:
/// - Inspecting the next element to be popped without committing
/// - Implementing conditional pop logic ("peek then pop if condition met")
/// - Observing elements in a producer-consumer pattern
///
/// @param obj Pointer to a Ring object. If NULL, returns NULL.
///
/// @return The oldest element in the Ring, or NULL if the Ring is empty or
///         obj is NULL. Note that NULL may also be a valid stored element,
///         so use rt_ring_is_empty to distinguish between "empty" and
///         "contains NULL".
///
/// @note O(1) time complexity.
/// @note The Ring retains ownership of the element. The returned pointer
///       is only valid as long as the element remains in the Ring (i.e.,
///       until it is popped, overwritten by push, or the Ring is cleared).
/// @note Thread safety: Not thread-safe. The returned pointer may become
///       invalid if another thread modifies the Ring.
///
/// @see rt_ring_pop For removing and returning the oldest element
/// @see rt_ring_get For accessing elements by logical index
void *rt_ring_peek(void *obj)
{
    if (!obj)
        return NULL;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (ring->count == 0 || !ring->items)
        return NULL;

    // Return oldest element without removing
    return ring->items[ring->head];
}

/// @brief Retrieves an element by its logical index within the Ring.
///
/// Provides random access to Ring elements using logical indexing where:
/// - Index 0 is the oldest element (the head, what rt_ring_peek returns)
/// - Index (len-1) is the newest element (the most recently pushed)
///
/// The logical index is translated to the physical array position using
/// circular arithmetic: `actual = (head + index) % capacity`
///
/// Visual example with capacity=5, after Push(A), Push(B), Push(C):
/// ```
/// Physical array: [A, B, C, _, _]  head=0, count=3
/// Logical indices: Get(0)=A, Get(1)=B, Get(2)=C, Get(3)=NULL (out of bounds)
///
/// After Pop() (removes A):
/// Physical array: [_, B, C, _, _]  head=1, count=2
/// Logical indices: Get(0)=B, Get(1)=C, Get(2)=NULL (out of bounds)
///
/// After Push(D), Push(E), Push(F) (wraps around, overwrites B):
/// Physical array: [F, _, C, D, E]  head=2, count=4
/// Logical indices: Get(0)=C, Get(1)=D, Get(2)=E, Get(3)=F
/// ```
///
/// @param obj Pointer to a Ring object. If NULL, returns NULL.
/// @param index Logical index into the Ring (0 = oldest, len-1 = newest).
///              Must be in range [0, len-1] or NULL is returned.
///
/// @return The element at the specified logical index, or NULL if:
///         - obj is NULL
///         - index is negative
///         - index >= current length (out of bounds)
///         - Ring has no items array (allocation failed)
///
/// @note O(1) time complexity - direct array access with modular arithmetic.
/// @note The Ring retains ownership of the element. The returned pointer
///       is only valid as long as the element remains in the Ring.
/// @note Thread safety: Not thread-safe. Index validity may change if another
///       thread modifies the Ring.
///
/// @see rt_ring_peek For accessing just the oldest element (index 0)
/// @see rt_ring_len For determining valid index range
void *rt_ring_get(void *obj, int64_t index)
{
    if (!obj)
        return NULL;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (index < 0 || (size_t)index >= ring->count || !ring->items)
        return NULL;

    // Calculate actual index: logical 0 = head (oldest)
    size_t actual = (ring->head + (size_t)index) % ring->capacity;
    return ring->items[actual];
}

/// @brief Removes all elements from the Ring without deallocating memory.
///
/// Clears the Ring by resetting it to an empty state. After this call:
/// - The count becomes 0
/// - The head resets to 0
/// - All element slots are set to NULL
/// - The Ring can be reused with push operations
/// - The capacity remains unchanged (no reallocation)
///
/// This function iterates through all stored elements and sets their slots
/// to NULL for safety, preventing dangling pointer access through get/peek
/// operations that might occur due to bugs. This is a defensive measure
/// rather than a strict requirement.
///
/// @param obj Pointer to a Ring object. If NULL, this function is a no-op.
///
/// @note O(n) time complexity where n is the current number of elements,
///       due to the NULL-clearing loop. This could be optimized to O(1) if
///       the NULL-clearing is not needed, but the defensive safety is preferred.
///
/// @note Memory behavior: No memory is freed. The items array remains allocated
///       at its original capacity. To fully free a Ring, let the garbage
///       collector reclaim it (which triggers rt_ring_finalize).
///
/// @note Ownership: The Ring does NOT free the elements it contained. Callers
///       must ensure elements are either:
///       - Freed before calling clear (if caller owns them)
///       - Still referenced elsewhere (if shared ownership)
///       - Garbage-collected objects (if using Viper's GC)
///
/// @note Thread safety: Not thread-safe. External synchronization required.
///
/// @see rt_ring_pop For removing elements one at a time with retrieval
/// @see rt_ring_finalize For the destructor behavior
void rt_ring_clear(void *obj)
{
    if (!obj)
        return;

    rt_ring_impl *ring = (rt_ring_impl *)obj;
    if (!ring->items)
        return;

    // Clear element pointers (container doesn't own them)
    for (size_t i = 0; i < ring->count; i++)
    {
        size_t idx = (ring->head + i) % ring->capacity;
        ring->items[idx] = NULL;
    }

    ring->head = 0;
    ring->count = 0;
}
