//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_modvar.c
// Purpose: Provides runtime-managed storage for module-level BASIC variables.
//          Each VM instance holds an independent indexed table keyed by variable
//          name and kind tag, enabling multiple VMs to maintain isolated global
//          variable namespaces without shared state.
//
// Key invariants:
//   - Variables are keyed by (name, kind) pairs; the same name with different
//     kind tags (I64, F64, I1, PTR, STR) is stored as a separate entry.
//   - The backing entry table and hash index both grow geometrically.
//   - Storage for each variable is zero-initialized at creation time.
//   - Table entries are never removed; once created, a variable persists for
//     the lifetime of the RtContext.
//   - Repeated lookups for the same (name, kind) pair return the same address,
//     and mismatched storage sizes trap instead of silently reusing the wrong
//     slot.
//   - All operations require an active RtContext; passing NULL traps.
//
// Ownership/Lifetime:
//   - Storage blocks and the entry table are allocated via rt_alloc and are
//     owned by the RtContext; they are freed when the context is destroyed.
//   - Variable names are compared by value (strcmp); no ownership of the
//     name pointer is taken beyond the duration of the lookup call.
//
// Links: src/runtime/core/rt_modvar.h (public API),
//        src/runtime/core/rt_context.h (RtContext definition),
//        src/runtime/core/rt_memory.c (rt_alloc)
//

#include "rt_modvar.h"
#include "rt_context.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MV_I64 = RT_MODVAR_KIND_I64,
    MV_F64 = RT_MODVAR_KIND_F64,
    MV_I1 = RT_MODVAR_KIND_I1,
    MV_PTR = RT_MODVAR_KIND_PTR,
    MV_STR = RT_MODVAR_KIND_STR,
    MV_BLOCK = RT_MODVAR_KIND_BLOCK
} mv_kind_t;

static uint64_t mv_hash_key(const char *key, mv_kind_t kind) {
    uint64_t hash = 1469598103934665603ULL ^ (uint64_t)kind;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p) {
        hash ^= (uint64_t)*p;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1;
}

static void mv_ensure_index_capacity(RtContext *ctx, size_t entry_count) {
    if (ctx->modvar_index_capacity != 0 && (entry_count + 1) * 10 < ctx->modvar_index_capacity * 7)
        return;

    size_t new_cap = ctx->modvar_index_capacity ? ctx->modvar_index_capacity * 2 : 32;
    while ((entry_count + 1) * 10 >= new_cap * 7) {
        if (new_cap > SIZE_MAX / 2)
            rt_trap("rt_modvar: index capacity overflow");
        new_cap *= 2;
    }

    size_t *new_slots = (size_t *)calloc(new_cap, sizeof(size_t));
    if (!new_slots)
        rt_trap("rt_modvar: index alloc failed");

    size_t mask = new_cap - 1;
    for (size_t i = 0; i < ctx->modvar_count; ++i) {
        RtModvarEntry *entry = &ctx->modvar_entries[i];
        size_t pos = (size_t)(entry->hash & mask);
        while (new_slots[pos] != 0)
            pos = (pos + 1) & mask;
        new_slots[pos] = i + 1;
    }

    free(ctx->modvar_index_slots);
    ctx->modvar_index_slots = new_slots;
    ctx->modvar_index_capacity = new_cap;
}

static RtModvarEntry *mv_lookup(RtContext *ctx,
                                const char *key,
                                uint64_t hash,
                                mv_kind_t kind,
                                size_t size) {
    if (!ctx->modvar_index_slots || ctx->modvar_index_capacity == 0)
        return NULL;

    size_t mask = ctx->modvar_index_capacity - 1;
    size_t pos = (size_t)(hash & mask);
    while (ctx->modvar_index_slots[pos] != 0) {
        RtModvarEntry *entry = &ctx->modvar_entries[ctx->modvar_index_slots[pos] - 1];
        if (entry->hash == hash && entry->kind == (int)kind && strcmp(entry->name, key) == 0) {
            if (entry->size != size)
                rt_trap("rt_modvar: storage size mismatch");
            return entry;
        }
        pos = (pos + 1) & mask;
    }
    return NULL;
}

static void mv_insert_index(RtContext *ctx, size_t entry_index) {
    RtModvarEntry *entry = &ctx->modvar_entries[entry_index];
    size_t mask = ctx->modvar_index_capacity - 1;
    size_t pos = (size_t)(entry->hash & mask);
    while (ctx->modvar_index_slots[pos] != 0)
        pos = (pos + 1) & mask;
    ctx->modvar_index_slots[pos] = entry_index + 1;
}

/// @brief Allocate zero-initialized storage for a module variable.
///
/// @details Uses the runtime allocator, trapping on failure, and initializes
///          the entire region to zero. The pointer is suitable for use as the
///          backing storage of a module-level variable of @p size bytes.
/// @param size Size in bytes of the requested storage.
/// @return Pointer to zeroed storage; never NULL (traps on failure).
static void *mv_alloc(size_t size) {
    void *p = rt_alloc((int64_t)size);
    if (!p)
        rt_trap("rt_modvar: alloc failed");
    memset(p, 0, size); // Defensive: ensure all fields zeroed regardless of rt_alloc behavior
    return p;
}

/// @brief Find or create a module variable entry in the current VM context.
///
/// @details Performs an indexed lookup over the per-VM modvar table using the
///          exact @p key and @p kind. When not found, grows the entry table and
///          hash index, then appends a new entry with a freshly allocated,
///          zeroed storage block sized for the requested type.
/// @param ctx  Active runtime context (thread-local binding).
/// @param key  Canonical variable name.
/// @param kind Kind tag used to distinguish same-named variables of different types.
/// @param size Size in bytes for the associated storage.
/// @return Pointer to the entry describing the variable.
static RtModvarEntry *mv_find_or_create(RtContext *ctx,
                                        const char *key,
                                        mv_kind_t kind,
                                        size_t size) {
    assert(ctx && "mv_find_or_create called without active RtContext");
    uint64_t hash = mv_hash_key(key, kind);
    RtModvarEntry *existing = mv_lookup(ctx, key, hash, kind, size);
    if (existing)
        return existing;

    if (ctx->modvar_count == ctx->modvar_capacity) {
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
        if (newCap > oldCap) {
            memset(np + oldCap, 0, (newCap - oldCap) * sizeof(RtModvarEntry));
        }
        ctx->modvar_entries = np;
        ctx->modvar_capacity = newCap;
    }

    mv_ensure_index_capacity(ctx, ctx->modvar_count + 1);

    RtModvarEntry *e = &ctx->modvar_entries[ctx->modvar_count++];
    size_t nlen = strlen(key);
    e->name = (char *)rt_alloc((int64_t)(nlen + 1));
    if (!e->name)
        rt_trap("rt_modvar: name alloc failed");
    memcpy(e->name, key, nlen + 1);
    e->kind = kind;
    e->size = size;
    e->hash = hash;
    e->addr = mv_alloc(size);
    mv_insert_index(ctx, ctx->modvar_count - 1);
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
static void *mv_addr(rt_string name, mv_kind_t kind, size_t size) {
    RtContext *ctx = rt_get_current_context();
    if (!ctx)
        ctx = rt_legacy_context();

    const char *c = rt_string_cstr(name);
    if (!c)
        rt_trap("rt_modvar: null name");
    if (size == 0)
        rt_trap("rt_modvar: zero-sized storage");
    RtModvarEntry *e = mv_find_or_create(ctx, c, kind, size);
    return e->addr;
}

/// @brief Address of a 64-bit integer module variable.
void *rt_modvar_addr_i64(rt_string name) {
    return mv_addr(name, MV_I64, 8);
}

/// @brief Address of a 64-bit floating module variable.
void *rt_modvar_addr_f64(rt_string name) {
    return mv_addr(name, MV_F64, 8);
}

/// @brief Address of a boolean (i1) module variable.
void *rt_modvar_addr_i1(rt_string name) {
    return mv_addr(name, MV_I1, 1);
}

/// @brief Address of a pointer module variable.
void *rt_modvar_addr_ptr(rt_string name) {
    return mv_addr(name, MV_PTR, 8);
}

/// @brief Address of a string module variable (stores rt_string handle).
void *rt_modvar_addr_str(rt_string name) {
    return mv_addr(name, MV_STR, sizeof(void *));
}

/// @brief Address of a module variable block with arbitrary size.
/// @details Used for arrays and records that need more than 8 bytes.
void *rt_modvar_addr_block(rt_string name, int64_t size) {
    if (size < 0)
        rt_trap("rt_modvar: negative block size");
    return mv_addr(name, MV_BLOCK, (size_t)size);
}
