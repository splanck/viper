// File: src/runtime/rt_modvar.c
// Purpose: Provide runtime-managed addresses for module-level BASIC variables.
// Notes: Uses a simple linear table keyed by name+kind; zero-initialized.

#include "rt_modvar.h"
#include "rt_string.h"
#include "rt_internal.h"

#include <string.h>

typedef enum {
    MV_I64,
    MV_F64,
    MV_I1,
    MV_PTR,
    MV_STR
} mv_kind_t;

typedef struct {
    char *name;     // owned copy of key
    mv_kind_t kind; // storage kind
    void *addr;     // allocated block
    size_t size;    // bytes
} modvar_entry_t;

static modvar_entry_t *g_entries = NULL;
static size_t g_count = 0;
static size_t g_cap = 0;

static void *mv_alloc(size_t size)
{
    void *p = rt_alloc((int64_t)size);
    if (!p)
        rt_trap("rt_modvar: alloc failed");
    memset(p, 0, size);
    return p;
}

static modvar_entry_t *mv_find_or_create(const char *key, mv_kind_t kind, size_t size)
{
    // linear search
    for (size_t i = 0; i < g_count; ++i) {
        if (g_entries[i].kind == kind && strcmp(g_entries[i].name, key) == 0)
            return &g_entries[i];
    }
    // grow table
    if (g_count == g_cap) {
        size_t newCap = g_cap ? g_cap * 2 : 16;
        void *np = rt_alloc((int64_t)(newCap * sizeof(modvar_entry_t)));
        if (!np)
            rt_trap("rt_modvar: table alloc failed");
        // move old contents
        if (g_entries && g_count) {
            memcpy(np, g_entries, g_count * sizeof(modvar_entry_t));
        }
        g_entries = (modvar_entry_t *)np;
        g_cap = newCap;
    }
    // insert new
    modvar_entry_t *e = &g_entries[g_count++];
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
    const char *c = rt_string_cstr(name);
    if (!c)
        rt_trap("rt_modvar: null name");
    modvar_entry_t *e = mv_find_or_create(c, kind, size);
    return e->addr;
}

void *rt_modvar_addr_i64(rt_string name) { return mv_addr(name, MV_I64, 8); }
void *rt_modvar_addr_f64(rt_string name) { return mv_addr(name, MV_F64, 8); }
void *rt_modvar_addr_i1(rt_string name) { return mv_addr(name, MV_I1, 1); }
void *rt_modvar_addr_ptr(rt_string name) { return mv_addr(name, MV_PTR, 8); }
void *rt_modvar_addr_str(rt_string name) { return mv_addr(name, MV_STR, sizeof(void *)); }
