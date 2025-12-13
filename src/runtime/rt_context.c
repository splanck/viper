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
#include "rt_string.h"
#include <stdlib.h>
#include <string.h>

void rt_file_state_cleanup(RtContext *ctx);
void rt_type_registry_cleanup(RtContext *ctx);
void rt_args_state_cleanup(RtContext *ctx);

/// @brief Thread-local pointer to the active runtime context.
/// @details Each thread can have at most one active VM context. The VM sets
///          this before executing code and clears it afterward.
_Thread_local RtContext *g_rt_context = NULL;
static RtContext g_legacy_ctx;
static int g_legacy_inited = 0;

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

    // Initialize file state
    ctx->file_state.entries = NULL;
    ctx->file_state.count = 0;
    ctx->file_state.capacity = 0;

    // Initialize argument store
    ctx->args_state.items = NULL;
    ctx->args_state.size = 0;
    ctx->args_state.cap = 0;

    // Initialize type registry state
    ctx->type_registry.classes = NULL;
    ctx->type_registry.classes_len = 0;
    ctx->type_registry.classes_cap = 0;
    ctx->type_registry.ifaces = NULL;
    ctx->type_registry.ifaces_len = 0;
    ctx->type_registry.ifaces_cap = 0;
    ctx->type_registry.bindings = NULL;
    ctx->type_registry.bindings_len = 0;
    ctx->type_registry.bindings_cap = 0;
}

/// @brief Cleanup a runtime context and free owned resources.
void rt_context_cleanup(RtContext *ctx)
{
    if (!ctx)
        return;

    rt_file_state_cleanup(ctx);
    rt_args_state_cleanup(ctx);

    for (size_t i = 0; i < ctx->modvar_count; ++i)
    {
        RtModvarEntry *e = &ctx->modvar_entries[i];
        if (e->kind == 4 && e->addr)
        {
            rt_string *slot = (rt_string *)e->addr;
            if (slot && *slot)
            {
                rt_string_unref(*slot);
                *slot = NULL;
            }
        }
        free(e->name);
        free(e->addr);
        e->name = NULL;
        e->addr = NULL;
        e->size = 0;
        e->kind = 0;
    }
    free(ctx->modvar_entries);
    ctx->modvar_entries = NULL;
	    ctx->modvar_count = 0;
	    ctx->modvar_capacity = 0;

    rt_type_registry_cleanup(ctx);
}

/// @brief Bind a runtime context to the current thread.
void rt_set_current_context(RtContext *ctx)
{
    RtContext *old = g_rt_context;
    g_rt_context = ctx;

    if (ctx)
    {
        // Adopt legacy state on first bind to preserve single-VM behaviour.
        if (!g_legacy_inited)
        {
            rt_context_init(&g_legacy_ctx);
            g_legacy_inited = 1;
        }

        // Move file_state if destination is empty and legacy has entries.
        if (ctx->file_state.entries == NULL && g_legacy_ctx.file_state.entries != NULL)
        {
            ctx->file_state = g_legacy_ctx.file_state;
            g_legacy_ctx.file_state.entries = NULL;
            g_legacy_ctx.file_state.count = 0;
            g_legacy_ctx.file_state.capacity = 0;
        }

        // Move args_state if destination is empty and legacy has entries.
        if (ctx->args_state.items == NULL && g_legacy_ctx.args_state.items != NULL)
        {
            ctx->args_state = g_legacy_ctx.args_state;
            g_legacy_ctx.args_state.items = NULL;
            g_legacy_ctx.args_state.size = 0;
            g_legacy_ctx.args_state.cap = 0;
        }

        // Move type registry if destination is empty and legacy has data.
        if (ctx->type_registry.classes == NULL && g_legacy_ctx.type_registry.classes != NULL)
        {
            ctx->type_registry = g_legacy_ctx.type_registry;
            g_legacy_ctx.type_registry.classes = NULL;
            g_legacy_ctx.type_registry.classes_len = 0;
            g_legacy_ctx.type_registry.classes_cap = 0;
            g_legacy_ctx.type_registry.ifaces = NULL;
            g_legacy_ctx.type_registry.ifaces_len = 0;
            g_legacy_ctx.type_registry.ifaces_cap = 0;
            g_legacy_ctx.type_registry.bindings = NULL;
            g_legacy_ctx.type_registry.bindings_len = 0;
            g_legacy_ctx.type_registry.bindings_cap = 0;
        }
    }
    else if (old)
    {
        // Unbinding: move state back to legacy so calls after VM exit keep working.
        if (!g_legacy_inited)
        {
            rt_context_init(&g_legacy_ctx);
            g_legacy_inited = 1;
        }

        if (g_legacy_ctx.file_state.entries == NULL && old->file_state.entries != NULL)
        {
            g_legacy_ctx.file_state = old->file_state;
            old->file_state.entries = NULL;
            old->file_state.count = 0;
            old->file_state.capacity = 0;
        }

        if (g_legacy_ctx.args_state.items == NULL && old->args_state.items != NULL)
        {
            g_legacy_ctx.args_state = old->args_state;
            old->args_state.items = NULL;
            old->args_state.size = 0;
            old->args_state.cap = 0;
        }

        if (g_legacy_ctx.type_registry.classes == NULL && old->type_registry.classes != NULL)
        {
            g_legacy_ctx.type_registry = old->type_registry;
            old->type_registry.classes = NULL;
            old->type_registry.classes_len = 0;
            old->type_registry.classes_cap = 0;
            old->type_registry.ifaces = NULL;
            old->type_registry.ifaces_len = 0;
            old->type_registry.ifaces_cap = 0;
            old->type_registry.bindings = NULL;
            old->type_registry.bindings_len = 0;
            old->type_registry.bindings_cap = 0;
        }
    }
}

/// @brief Retrieve the current thread's runtime context.
RtContext *rt_get_current_context(void)
{
    return g_rt_context;
}

RtContext *rt_legacy_context(void)
{
    if (!g_legacy_inited)
    {
        rt_context_init(&g_legacy_ctx);
        g_legacy_inited = 1;
    }
    return &g_legacy_ctx;
}
