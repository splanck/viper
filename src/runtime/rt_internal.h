// File: src/runtime/rt_internal.h
// Purpose: Defines internal runtime structures shared across implementation files.
// Key invariants: Strings use reference counts; structure layout is stable.
// Ownership/Lifetime: Caller manages lifetime of rt_string instances.
// Links: docs/codemap.md

#pragma once

#include "rt.hpp"
#include "rt_heap.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum rt_input_grow_result
    {
        RT_INPUT_GROW_OK = 0,
        RT_INPUT_GROW_ALLOC_FAILED = 1,
        RT_INPUT_GROW_OVERFLOW = 2
    } rt_input_grow_result;

    rt_input_grow_result rt_input_try_grow(char **buf, size_t *cap);

    typedef void *(*rt_alloc_hook_fn)(int64_t bytes, void *(*next)(int64_t bytes));

    /// @brief Install or remove the allocation hook used for testing.
    /// @details When non-null, @p hook receives the requested byte count and a
    ///          pointer to the default allocator implementation.  Passing
    ///          @c NULL restores the default behaviour.
    /// @param hook Replacement hook or @c NULL to disable overrides.
    void rt_set_alloc_hook(rt_alloc_hook_fn hook);

#ifdef __cplusplus
}
#endif

struct rt_string_impl
{
    char *data;
    rt_heap_hdr_t *heap;
    size_t literal_len;
    size_t literal_refs;
};
