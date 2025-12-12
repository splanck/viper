//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_stack.c
// Purpose: Implement Viper.Collections.Stack - a LIFO (last-in-first-out) collection.
//
// Structure:
// - Internal representation uses a header structure with len, cap, and items[]
// - Items are stored as void* (generic object pointers)
// - Automatic growth when capacity is exceeded
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

#define STACK_DEFAULT_CAP 16
#define STACK_GROWTH_FACTOR 2

/// Internal stack structure.
typedef struct rt_stack_impl
{
    int64_t len;  ///< Number of elements currently on the stack
    int64_t cap;  ///< Current capacity (allocated slots)
    void **items; ///< Array of element pointers
} rt_stack_impl;

/// @brief Ensure the stack has capacity for at least `needed` elements.
/// @param stack Stack to potentially grow.
/// @param needed Minimum required capacity.
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

/// @brief Create a new empty stack with default capacity.
/// @return New stack object.
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

    if (!stack->items)
    {
        rt_obj_free(stack);
        rt_trap("Stack: memory allocation failed");
    }

    return stack;
}

/// @brief Get the number of elements on the stack.
/// @param obj Stack object.
/// @return Number of elements.
int64_t rt_stack_len(void *obj)
{
    if (!obj)
        return 0;
    return ((rt_stack_impl *)obj)->len;
}

/// @brief Check if the stack is empty.
/// @param obj Stack object.
/// @return 1 if empty, 0 otherwise.
int8_t rt_stack_is_empty(void *obj)
{
    if (!obj)
        return 1;
    return ((rt_stack_impl *)obj)->len == 0 ? 1 : 0;
}

/// @brief Push an element onto the top of the stack.
/// @param obj Stack object.
/// @param val Element to push.
void rt_stack_push(void *obj, void *val)
{
    if (!obj)
        rt_trap("Stack.Push: null stack");

    rt_stack_impl *stack = (rt_stack_impl *)obj;

    stack_ensure_capacity(stack, stack->len + 1);
    stack->items[stack->len] = val;
    stack->len++;
}

/// @brief Pop and return the top element from the stack.
/// @param obj Stack object.
/// @return The removed element.
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

/// @brief Return the top element without removing it.
/// @param obj Stack object.
/// @return The top element.
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

/// @brief Remove all elements from the stack.
/// @param obj Stack object.
void rt_stack_clear(void *obj)
{
    if (!obj)
        return;
    ((rt_stack_impl *)obj)->len = 0;
}
