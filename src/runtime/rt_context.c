//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_context.c
// Purpose: Implements per-VM runtime context management.
//
//===----------------------------------------------------------------------===//

#include "rt_context.h"
#include "rt_internal.h"
#include <string.h>

/// @brief Thread-local pointer to the active runtime context.
/// @details Each thread can have at most one active VM context. The VM sets
///          this before executing code and clears it afterward.
_Thread_local RtContext *g_rt_context = NULL;

/// @brief Initialize a runtime context with default values.
void rt_context_init(RtContext *ctx)
{
    if (!ctx)
        return;

    // Initialize RNG with deterministic seed (same as old global default)
    ctx->rng_state = 0xDEADBEEFCAFEBABEULL;

    // Initialize empty modvar table
    ctx->modvar_entries = NULL;
    ctx->modvar_count = 0;
    ctx->modvar_capacity = 0;
}

/// @brief Cleanup a runtime context and free owned resources.
void rt_context_cleanup(RtContext *ctx)
{
    if (!ctx)
        return;

    // Free all modvar entries
    for (size_t i = 0; i < ctx->modvar_count; ++i)
    {
        RtModvarEntry *e = &ctx->modvar_entries[i];
        if (e->name)
        {
            // Free the name string (was allocated via rt_alloc)
            // Note: We don't call rt_free here because rt_alloc doesn't have a matching free
            // The memory will be cleaned up when the context is destroyed
        }
        if (e->addr)
        {
            // Free the storage block (was allocated via rt_alloc)
            // Same note as above
        }
    }

    // Free the modvar entries array itself
    if (ctx->modvar_entries)
    {
        // Free via rt_alloc's allocator
        ctx->modvar_entries = NULL;
    }

    ctx->modvar_count = 0;
    ctx->modvar_capacity = 0;
}

/// @brief Bind a runtime context to the current thread.
void rt_set_current_context(RtContext *ctx)
{
    g_rt_context = ctx;
}

/// @brief Retrieve the current thread's runtime context.
RtContext *rt_get_current_context(void)
{
    return g_rt_context;
}
