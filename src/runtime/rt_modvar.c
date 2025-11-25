// File: src/runtime/rt_modvar.c
// Purpose: Provide runtime-managed addresses for module-level BASIC variables.
// Notes: Uses a simple linear table keyed by name+kind; zero-initialized.
//        Now uses per-VM RtContext instead of global state.

#include "rt_modvar.h"
#include "rt_context.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <string.h>

typedef enum
{
    MV_I64,
    MV_F64,
    MV_I1,
    MV_PTR,
    MV_STR
} mv_kind_t;

static void *mv_alloc(size_t size)
{
    void *p = rt_alloc((int64_t)size);
    if (!p)
        rt_trap("rt_modvar: alloc failed");
    memset(p, 0, size);
    return p;
}

static RtModvarEntry *mv_find_or_create(RtContext *ctx,
                                        const char *key,
                                        mv_kind_t kind,
                                        size_t size)
{
    assert(ctx && "mv_find_or_create called without active RtContext");

    // linear search
    for (size_t i = 0; i < ctx->modvar_count; ++i)
    {
        RtModvarEntry *e = &ctx->modvar_entries[i];
        if (e->kind == kind && strcmp(e->name, key) == 0)
            return e;
    }
    // grow table
    if (ctx->modvar_count == ctx->modvar_capacity)
    {
        size_t newCap = ctx->modvar_capacity ? ctx->modvar_capacity * 2 : 16;
        void *np = rt_alloc((int64_t)(newCap * sizeof(RtModvarEntry)));
        if (!np)
            rt_trap("rt_modvar: table alloc failed");
        // move old contents
        if (ctx->modvar_entries && ctx->modvar_count)
        {
            memcpy(np, ctx->modvar_entries, ctx->modvar_count * sizeof(RtModvarEntry));
        }
        ctx->modvar_entries = (RtModvarEntry *)np;
        ctx->modvar_capacity = newCap;
    }
    // insert new
    RtModvarEntry *e = &ctx->modvar_entries[ctx->modvar_count++];
    size_t nlen = strlen(key);
    e->name = (char *)rt_alloc((int64_t)(nlen + 1));
    if (!e->name)
        rt_trap("rt_modvar: name alloc failed");
    memcpy(e->name, key, nlen + 1);
    e->kind = kind;
    e->size = size;
    e->addr = mv_alloc(size);
    return e;
}

static void *mv_addr(rt_string name, mv_kind_t kind, size_t size)
{
    RtContext *ctx = rt_get_current_context();
    assert(ctx && "mv_addr called without active RtContext");

    const char *c = rt_string_cstr(name);
    if (!c)
        rt_trap("rt_modvar: null name");
    RtModvarEntry *e = mv_find_or_create(ctx, c, kind, size);
    return e->addr;
}

void *rt_modvar_addr_i64(rt_string name)
{
    return mv_addr(name, MV_I64, 8);
}

void *rt_modvar_addr_f64(rt_string name)
{
    return mv_addr(name, MV_F64, 8);
}

void *rt_modvar_addr_i1(rt_string name)
{
    return mv_addr(name, MV_I1, 1);
}

void *rt_modvar_addr_ptr(rt_string name)
{
    return mv_addr(name, MV_PTR, 8);
}

void *rt_modvar_addr_str(rt_string name)
{
    return mv_addr(name, MV_STR, sizeof(void *));
}
