//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_modvar.c
// Purpose: Provide runtime-managed addresses for module-level BASIC variables.
// Notes: Uses a simple linear table keyed by name+kind; zero-initialized.
//        Uses per-VM RtContext instead of global state for isolation.

#include "rt_modvar.h"
#include "rt_context.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    MV_I64,
    MV_F64,
    MV_I1,
    MV_PTR,
    MV_STR
} mv_kind_t;

/// @brief Allocate zero-initialized storage for a module variable.
///
/// @details Uses the runtime allocator, trapping on failure, and initializes
///          the entire region to zero. The pointer is suitable for use as the
///          backing storage of a module-level variable of @p size bytes.
/// @param size Size in bytes of the requested storage.
/// @return Pointer to zeroed storage; never NULL (traps on failure).
static void *mv_alloc(size_t size)
{
    void *p = rt_alloc((int64_t)size);
    if (!p)
        rt_trap("rt_modvar: alloc failed");
    memset(p, 0, size);
    return p;
}

/// @brief Find or create a module variable entry in the current VM context.
///
/// @details Performs a linear probe over the per-VM modvar table using the
///          exact @p key and @p kind. When not found, grows the table (doubling
///          capacity) and appends a new entry with a freshly allocated, zeroed
///          storage block sized for the requested type.
/// @param ctx  Active runtime context (thread-local binding).
/// @param key  Canonical variable name.
/// @param kind Kind tag used to distinguish same-named variables of different types.
/// @param size Size in bytes for the associated storage.
/// @return Pointer to the entry describing the variable.
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
        size_t oldCap = ctx->modvar_capacity;
        if (oldCap > (SIZE_MAX / 2))
            rt_trap("rt_modvar: table capacity overflow");
        size_t newCap = oldCap ? oldCap * 2 : 16;
        if (newCap > (SIZE_MAX / sizeof(RtModvarEntry)))
            rt_trap("rt_modvar: table size overflow");
        RtModvarEntry *np =
            (RtModvarEntry *)realloc(ctx->modvar_entries, newCap * sizeof(RtModvarEntry));
        if (!np)
            rt_trap("rt_modvar: table alloc failed");
        if (newCap > oldCap)
        {
            memset(np + oldCap, 0, (newCap - oldCap) * sizeof(RtModvarEntry));
        }
        ctx->modvar_entries = np;
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

/// @brief Resolve the address of a module variable by (name, kind).
///
/// @details Converts the runtime string to a C string (trapping on NULL),
///          looks up or creates the corresponding entry in the active runtime
///          context, and returns the stable address of the storage.
/// @param name Runtime string name of the variable.
/// @param kind Module variable kind tag (I64/F64/I1/PTR/STR).
/// @param size Size of the storage to allocate for new entries.
/// @return Stable pointer to the variable’s storage.
static void *mv_addr(rt_string name, mv_kind_t kind, size_t size)
{
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();

    const char *c = rt_string_cstr(name);
    if (!c)
        rt_trap("rt_modvar: null name");
    RtModvarEntry *e = mv_find_or_create(ctx, c, kind, size);
    return e->addr;
}

/// @brief Address of a 64-bit integer module variable.
void *rt_modvar_addr_i64(rt_string name)
{
    return mv_addr(name, MV_I64, 8);
}

/// @brief Address of a 64-bit floating module variable.
void *rt_modvar_addr_f64(rt_string name)
{
    return mv_addr(name, MV_F64, 8);
}

/// @brief Address of a boolean (i1) module variable.
void *rt_modvar_addr_i1(rt_string name)
{
    return mv_addr(name, MV_I1, 1);
}

/// @brief Address of a pointer module variable.
void *rt_modvar_addr_ptr(rt_string name)
{
    return mv_addr(name, MV_PTR, 8);
}

/// @brief Address of a string module variable (stores rt_string handle).
void *rt_modvar_addr_str(rt_string name)
{
    return mv_addr(name, MV_STR, sizeof(void *));
}

/// @brief Address of a module variable block with arbitrary size.
/// @details Used for arrays and records that need more than 8 bytes.
void *rt_modvar_addr_block(rt_string name, int64_t size)
{
    // Use MV_PTR kind for block storage - the size is what matters
    return mv_addr(name, MV_PTR, (size_t)size);
}
