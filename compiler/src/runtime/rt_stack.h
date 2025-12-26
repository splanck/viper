//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_stack.h
// Purpose: Runtime-backed LIFO stack for Viper.Collections.Stack.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty stack with default capacity.
    /// @return Opaque pointer to the new Stack object.
    void *rt_stack_new(void);

    /// @brief Get the number of elements on the stack.
    /// @param obj Opaque Stack object pointer.
    /// @return Number of elements currently on the stack.
    int64_t rt_stack_len(void *obj);

    /// @brief Check if the stack is empty.
    /// @param obj Opaque Stack object pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_stack_is_empty(void *obj);

    /// @brief Push an element onto the top of the stack.
    /// @param obj Opaque Stack object pointer.
    /// @param val Element to push.
    void rt_stack_push(void *obj, void *val);

    /// @brief Pop and return the top element from the stack.
    /// @param obj Opaque Stack object pointer.
    /// @return The removed element; traps if empty.
    void *rt_stack_pop(void *obj);

    /// @brief Return the top element without removing it.
    /// @param obj Opaque Stack object pointer.
    /// @return The top element; traps if empty.
    void *rt_stack_peek(void *obj);

    /// @brief Remove all elements from the stack.
    /// @param obj Opaque Stack object pointer.
    void rt_stack_clear(void *obj);

#ifdef __cplusplus
}
#endif
