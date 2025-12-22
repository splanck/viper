//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares internal runtime structures and utilities shared across
// multiple runtime implementation files. These definitions provide the
// scaffolding for memory management, input buffering, and allocation hooks
// used by the higher-level runtime APIs exposed to IL programs.
//
// The runtime system must coordinate memory allocation across different
// subsystems (strings, arrays, file buffers). This file centralizes internal
// helpers that manage buffer growth, allocation hooks for testing, and shared
// data structures that don't belong in the public runtime interface.
//
// Key Components:
// - Input buffer management: rt_input_try_grow handles dynamic buffer expansion
//   for file I/O operations, detecting allocation failures and overflow conditions
// - Allocation hooks: rt_set_alloc_hook provides test infrastructure for
//   simulating allocation failures and tracking memory usage patterns
// - Internal type definitions: Structures used by implementation files but
//   not exposed to IL programs or external C code
//
// This file is part of the runtime's implementation layer and should only be
// included by runtime .c/.cpp files, never by IL-generated code or user programs.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt.hpp"
#include "rt_heap.h"

#include <stddef.h>
#include <stdint.h>

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

    /// What: Attempt to grow an input buffer in-place.
    /// Why:  Expand buffers during I/O without excessive reallocations.
    /// How:  Computes a safe new capacity, reallocates, and updates pointers.
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
    uint64_t magic;
    char *data;
    rt_heap_hdr_t *heap;
    size_t literal_len;
    size_t literal_refs;
};

#define RT_STRING_MAGIC 0x5354524D41474943ULL /* "STRMAGIC" */
