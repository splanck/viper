//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements a simple argument store for the runtime. The store retains pushed
// strings and releases them on clear. Getters return retained copies so callers
// follow the usual ownership rules used by other rt_* getters.
//
//===----------------------------------------------------------------------===//

#include "rt_args.h"
#include "rt_string_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct rt_args_store
{
    rt_string *items;
    size_t size;
    size_t cap;
} rt_args_store;

static rt_args_store g_args = {NULL, 0, 0};

static void rt_args_grow_if_needed(size_t new_size)
{
    if (new_size <= g_args.cap)
        return;
    size_t new_cap = g_args.cap ? g_args.cap * 2 : 8;
    while (new_cap < new_size)
        new_cap *= 2;
    rt_string *next = (rt_string *)realloc(g_args.items, new_cap * sizeof(rt_string));
    if (!next)
    {
        fprintf(stderr, "rt_args: allocation failed\n");
        abort();
    }
    g_args.items = next;
    g_args.cap = new_cap;
}

void rt_args_clear(void)
{
    for (size_t i = 0; i < g_args.size; ++i)
    {
        if (g_args.items[i])
            rt_string_unref(g_args.items[i]);
        g_args.items[i] = NULL;
    }
    g_args.size = 0;
}

void rt_args_push(rt_string s)
{
    rt_args_grow_if_needed(g_args.size + 1);
    // Retain; store NULL as empty string for predictability
    if (!s)
        s = rt_str_empty();
    else
        rt_string_ref(s);
    g_args.items[g_args.size++] = s;
}

int64_t rt_args_count(void)
{
    return (int64_t)g_args.size;
}

rt_string rt_args_get(int64_t index)
{
    if (index < 0 || (size_t)index >= g_args.size)
    {
        fprintf(stderr, "rt_args_get: index out of range\n");
        abort();
    }
    rt_string s = g_args.items[index];
    // Return retained reference to match common getter semantics
    return rt_string_ref(s);
}

rt_string rt_cmdline(void)
{
    if (g_args.size == 0)
        return rt_str_empty();
    rt_string_builder sb;
    rt_sb_init(&sb);
    for (size_t i = 0; i < g_args.size; ++i)
    {
        const char *cstr = rt_string_cstr(g_args.items[i]);
        if (i > 0)
            (void)rt_sb_append_cstr(&sb, " ");
        (void)rt_sb_append_cstr(&sb, cstr ? cstr : "");
    }
    rt_string out = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return out;
}
