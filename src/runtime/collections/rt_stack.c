//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_stack.c
// Purpose: Implements Viper.Collections.Stack, a LIFO (last-in-first-out)
//   dynamic collection backed by a contiguous dynamic array. Push and pop
//   operate on the top (highest index), providing O(1) amortized push and O(1)
//   pop with cache-friendly sequential memory layout.
//
// Key invariants:
//   - Initial capacity is STACK_DEFAULT_CAP (16); grows by STACK_GROWTH_FACTOR (2).
//   - The "top" of the stack is items[len-1]; push writes to items[len] and
//     increments len; pop reads items[len-1] and decrements len.
//   - Pop on an empty stack traps with a descriptive error message.
//   - Peek returns items[len-1] without removing it; returns NULL if empty.
//   - The Stack does NOT retain element references; element lifetime is the
//     caller's responsibility.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - Stack objects are GC-managed (rt_obj_new_i64). The items array is
//     malloc-managed and freed by the GC finalizer (stack_finalizer).
//
// Links: src/runtime/collections/rt_stack.h (public API),
//        src/runtime/collections/rt_deque.h (double-ended queue, superset)
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

#define STACK_DEFAULT_CAP 16
#define STACK_GROWTH_FACTOR 2

/// @brief Internal stack implementation structure.
///
/// The Stack is implemented as a dynamic array that grows as needed.
/// Elements are stored contiguously, with the "top" of the stack being
/// the element at index (len - 1). This provides O(1) push/pop operations
/// and cache-friendly memory access patterns.
///
/// Memory layout:
/// ```
/// Stack object (GC-managed):
///   +-----+-----+-------+
///   | len | cap | items |
///   |  3  | 16  | ----->|
///   +-----+-----+---|---+
///                   |
///                   v
/// items array (malloc'd):
///   +---+---+---+---+---+---+...+----+
///   | A | B | C | ? | ? | ? |   | ?  |
///   +---+---+---+---+---+---+...+----+
///   [0]  [1] [2]              [cap-1]
///         ^
///         | top = items[len-1] = C
/// ```
typedef struct rt_stack_impl
{
    int64_t len;  ///< Number of elements currently on the stack
    int64_t cap;  ///< Current capacity (allocated slots)
    void **items; ///< Array of element pointers
} rt_stack_impl;

/// @brief Finalizer callback invoked when a Stack is garbage collected.
///
/// This function is automatically called by Viper's garbage collector when a
/// Stack object becomes unreachable. It frees the internal items array to
/// prevent memory leaks.
///
/// @param obj Pointer to the Stack object being finalized. May be NULL (no-op).
///
/// @note The Stack does NOT own the elements it contains. Elements are not
///       freed during finalization - they must be managed separately by the
///       caller. This allows the same object to be in multiple collections.
/// @note This function is idempotent - safe to call on already-finalized stacks.
///
/// @see rt_stack_clear For removing elements without finalization
static void rt_stack_finalize(void *obj)
{
    if (!obj)
        return;
    rt_stack_impl *stack = (rt_stack_impl *)obj;
    free(stack->items);
    stack->items = NULL;
    stack->len = 0;
    stack->cap = 0;
}

/// @brief Ensures the stack has capacity for at least `needed` elements.
///
/// If the current capacity is insufficient, the items array is reallocated
/// to a larger size. Growth is exponential (doubling) to amortize allocation
/// costs over many push operations, giving O(1) amortized push complexity.
///
/// **Growth strategy:**
/// - Capacity doubles each time growth is needed
/// - Starting capacity is 16 (STACK_DEFAULT_CAP)
/// - Growth sequence: 16 → 32 → 64 → 128 → 256 → ...
///
/// @param stack Pointer to the stack implementation. Must not be NULL.
/// @param needed Minimum required capacity after this call.
///
/// @note Traps on memory allocation failure with "Stack: memory allocation failed".
/// @note Never shrinks the capacity - only grows when needed.
///
/// @see rt_stack_push For the primary user of this function
static void stack_ensure_capacity(rt_stack_impl *stack, int64_t needed)
{
    if (needed <= stack->cap)
        return;

    int64_t new_cap = stack->cap;
    while (new_cap < needed)
    {
        new_cap *= STACK_GROWTH_FACTOR;
    }

    void **new_items = realloc(stack->items, (size_t)new_cap * sizeof(void *));
    if (!new_items)
    {
        rt_trap("Stack: memory allocation failed");
    }

    stack->items = new_items;
    stack->cap = new_cap;
}

/// @brief Creates a new empty Stack with default capacity.
///
/// Allocates and initializes a Stack data structure for LIFO (Last-In-First-Out)
/// operations. The Stack starts with a default capacity of 16 slots and grows
/// automatically as elements are pushed.
///
/// The Stack is allocated through Viper's garbage-collected object system,
/// meaning it will be automatically freed when no longer referenced. A finalizer
/// is registered to clean up the internal items array.
///
/// **Usage example:**
/// ```
/// Dim stack = Stack.New()
/// stack.Push("first")
/// stack.Push("second")
/// stack.Push("third")
/// Print stack.Pop()   ' Outputs: third
/// Print stack.Pop()   ' Outputs: second
/// Print stack.Pop()   ' Outputs: first
/// ```
///
/// @return A pointer to the newly created Stack object. Traps and does not
///         return if memory allocation fails.
///
/// @note Initial capacity is 16 elements (STACK_DEFAULT_CAP).
/// @note The Stack does not own the elements stored in it - they must be
///       managed separately by the caller.
/// @note Thread safety: Not thread-safe. External synchronization required
///       for concurrent access.
///
/// @see rt_stack_push For adding elements
/// @see rt_stack_pop For removing elements
/// @see rt_stack_finalize For cleanup behavior
void *rt_stack_new(void)
{
    rt_stack_impl *stack = (rt_stack_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_stack_impl));
    if (!stack)
    {
        rt_trap("Stack: memory allocation failed");
    }

    stack->len = 0;
    stack->cap = STACK_DEFAULT_CAP;
    stack->items = malloc((size_t)STACK_DEFAULT_CAP * sizeof(void *));
    rt_obj_set_finalizer(stack, rt_stack_finalize);

    if (!stack->items)
    {
        if (rt_obj_release_check0(stack))
            rt_obj_free(stack);
        rt_trap("Stack: memory allocation failed");
    }

    return stack;
}

/// @brief Returns the number of elements currently on the Stack.
///
/// This function returns how many elements have been pushed and not yet popped.
/// The count is maintained internally and returned in O(1) time.
///
/// @param obj Pointer to a Stack object. If NULL, returns 0.
///
/// @return The number of elements on the Stack (>= 0). Returns 0 if obj is NULL.
///
/// @note O(1) time complexity.
///
/// @see rt_stack_is_empty For a boolean check
/// @see rt_stack_push For operations that increase the count
/// @see rt_stack_pop For operations that decrease the count
int64_t rt_stack_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_stack_impl *)obj)->len;
}

/// @brief Checks whether the Stack contains no elements.
///
/// A Stack is considered empty when its length is 0, which occurs:
/// - Immediately after creation
/// - After all elements have been popped
/// - After calling rt_stack_clear
///
/// Calling Pop or Peek on an empty Stack will trap with an error.
///
/// @param obj Pointer to a Stack object. If NULL, returns true (1).
///
/// @return 1 (true) if the Stack is empty or obj is NULL, 0 (false) otherwise.
///
/// @note O(1) time complexity.
///
/// @see rt_stack_len For the exact count
/// @see rt_stack_pop For removing elements (traps if empty)
/// @see rt_stack_peek For viewing top element (traps if empty)
int8_t rt_stack_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_stack_impl *)obj)->len == 0 ? 1 : 0;
}

/// @brief Pushes an element onto the top of the Stack.
///
/// Adds a new element to the top of the Stack. This is the primary insertion
/// operation for LIFO behavior - the most recently pushed element will be
/// the first one returned by Pop.
///
/// If the Stack's capacity is exceeded, it automatically grows to accommodate
/// the new element. Growth is exponential (doubling) for O(1) amortized time.
///
/// **Visual example:**
/// ```
/// Before Push(D):  [A, B, C]  (top = C)
/// After Push(D):   [A, B, C, D]  (top = D)
/// ```
///
/// @param obj Pointer to a Stack object. Must not be NULL.
/// @param val The element to push. May be NULL (NULL is a valid element).
///
/// @note O(1) amortized time complexity. Occasional O(n) when resizing occurs.
/// @note The Stack does not take ownership of val - the caller manages its lifetime.
/// @note Traps with "Stack.Push: null stack" if obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_stack_pop For the inverse operation
/// @see rt_stack_peek For viewing without removing
void rt_stack_push(void *obj, void *val)
{
    if (!obj)
        rt_trap("Stack.Push: null stack");

    rt_stack_impl *stack = (rt_stack_impl *)obj;

    stack_ensure_capacity(stack, stack->len + 1);
    stack->items[stack->len] = val;
    stack->len++;
}

/// @brief Removes and returns the top element from the Stack.
///
/// Removes the most recently pushed element (the "top" of the Stack) and
/// returns it. This is the primary retrieval operation for LIFO behavior.
///
/// **Visual example:**
/// ```
/// Before Pop():    [A, B, C, D]  (top = D)
/// After Pop():     [A, B, C]     (top = C)
/// Returns: D
/// ```
///
/// **Error handling:**
/// Calling Pop on an empty Stack is a programming error and traps with
/// "Stack.Pop: stack is empty". Always check rt_stack_is_empty before
/// popping, or use a try-catch pattern if available.
///
/// @param obj Pointer to a Stack object. Must not be NULL.
///
/// @return The element that was on top of the Stack.
///
/// @note O(1) time complexity.
/// @note The Stack releases its reference to the element - the caller now
///       owns it and is responsible for its lifetime.
/// @note Traps if the Stack is empty or obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_stack_push For the inverse operation
/// @see rt_stack_peek For viewing without removing
/// @see rt_stack_is_empty For checking before pop
void *rt_stack_pop(void *obj)
{
    if (!obj)
        rt_trap("Stack.Pop: null stack");

    rt_stack_impl *stack = (rt_stack_impl *)obj;

    if (stack->len == 0)
    {
        rt_trap("Stack.Pop: stack is empty");
    }

    stack->len--;
    return stack->items[stack->len];
}

/// @brief Returns the top element without removing it from the Stack.
///
/// Peeks at the most recently pushed element without modifying the Stack.
/// This is useful for:
/// - Inspecting the next element to be popped
/// - Implementing conditional pop logic
/// - Debugging or logging
///
/// **Example:**
/// ```
/// stack.Push("A")
/// stack.Push("B")
/// Print stack.Peek()  ' Outputs: B
/// Print stack.Peek()  ' Outputs: B (still there)
/// Print stack.Pop()   ' Outputs: B (now removed)
/// Print stack.Peek()  ' Outputs: A
/// ```
///
/// @param obj Pointer to a Stack object. Must not be NULL.
///
/// @return The element on top of the Stack (not removed).
///
/// @note O(1) time complexity.
/// @note The Stack retains ownership - the returned pointer is only valid
///       as long as the element remains on the Stack.
/// @note Traps if the Stack is empty or obj is NULL.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_stack_pop For removing while retrieving
/// @see rt_stack_is_empty For checking before peek
void *rt_stack_peek(void *obj)
{
    if (!obj)
        rt_trap("Stack.Peek: null stack");

    rt_stack_impl *stack = (rt_stack_impl *)obj;

    if (stack->len == 0)
    {
        rt_trap("Stack.Peek: stack is empty");
    }

    return stack->items[stack->len - 1];
}

/// @brief Removes all elements from the Stack.
///
/// Clears the Stack by resetting its length to 0. The capacity remains
/// unchanged (no memory is freed), allowing the Stack to be efficiently
/// reused for new elements.
///
/// **After clear:**
/// - Length becomes 0
/// - is_empty returns true
/// - Capacity unchanged (no reallocation)
/// - All element references are forgotten (not freed)
///
/// @param obj Pointer to a Stack object. If NULL, this is a no-op.
///
/// @note O(1) time complexity - just resets the length counter.
/// @note The Stack does NOT free the elements - they must be managed
///       separately by the caller if needed.
/// @note The internal array is not zeroed - old pointers remain but are
///       inaccessible through the public API.
/// @note Thread safety: Not thread-safe.
///
/// @see rt_stack_finalize For complete cleanup (including the array)
/// @see rt_stack_is_empty For checking if empty
void rt_stack_clear(void *obj)
{
    if (!obj)
        return;
    ((rt_stack_impl *)obj)->len = 0;
}
