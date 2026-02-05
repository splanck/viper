//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_unionfind.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal structure
// ---------------------------------------------------------------------------

typedef struct
{
    void *vptr;
    int64_t *parent; // Parent array (path-compressed)
    int64_t *rank;   // Rank array (for union by rank)
    int64_t *size;   // Size of each set
    int64_t n;       // Total number of elements
    int64_t sets;    // Number of disjoint sets
} rt_unionfind_impl;

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void unionfind_finalizer(void *obj)
{
    rt_unionfind_impl *uf = (rt_unionfind_impl *)obj;
    free(uf->parent);
    free(uf->rank);
    free(uf->size);
    uf->parent = NULL;
    uf->rank = NULL;
    uf->size = NULL;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

void *rt_unionfind_new(int64_t n)
{
    if (n < 1) n = 1;

    rt_unionfind_impl *uf =
        (rt_unionfind_impl *)rt_obj_new_i64(0, sizeof(rt_unionfind_impl));
    uf->parent = (int64_t *)malloc((size_t)n * sizeof(int64_t));
    uf->rank = (int64_t *)calloc((size_t)n, sizeof(int64_t));
    uf->size = (int64_t *)malloc((size_t)n * sizeof(int64_t));
    uf->n = n;
    uf->sets = n;

    for (int64_t i = 0; i < n; i++)
    {
        uf->parent[i] = i;
        uf->size[i] = 1;
    }

    rt_obj_set_finalizer(uf, unionfind_finalizer);
    return uf;
}

// ---------------------------------------------------------------------------
// Find (with path compression)
// ---------------------------------------------------------------------------

int64_t rt_unionfind_find(void *uf_ptr, int64_t x)
{
    if (!uf_ptr) return -1;
    rt_unionfind_impl *uf = (rt_unionfind_impl *)uf_ptr;

    if (x < 0 || x >= uf->n) return -1;

    // Path compression (iterative)
    int64_t root = x;
    while (uf->parent[root] != root)
        root = uf->parent[root];

    // Compress path
    while (uf->parent[x] != root)
    {
        int64_t next = uf->parent[x];
        uf->parent[x] = root;
        x = next;
    }

    return root;
}

// ---------------------------------------------------------------------------
// Union (by rank)
// ---------------------------------------------------------------------------

int64_t rt_unionfind_union(void *uf_ptr, int64_t x, int64_t y)
{
    if (!uf_ptr) return 0;
    rt_unionfind_impl *uf = (rt_unionfind_impl *)uf_ptr;

    int64_t rx = rt_unionfind_find(uf, x);
    int64_t ry = rt_unionfind_find(uf, y);

    if (rx < 0 || ry < 0 || rx == ry) return 0;

    // Union by rank
    if (uf->rank[rx] < uf->rank[ry])
    {
        int64_t tmp = rx;
        rx = ry;
        ry = tmp;
    }

    uf->parent[ry] = rx;
    uf->size[rx] += uf->size[ry];

    if (uf->rank[rx] == uf->rank[ry])
        uf->rank[rx]++;

    uf->sets--;
    return 1;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

int64_t rt_unionfind_connected(void *uf_ptr, int64_t x, int64_t y)
{
    if (!uf_ptr) return 0;
    int64_t rx = rt_unionfind_find(uf_ptr, x);
    int64_t ry = rt_unionfind_find(uf_ptr, y);
    return (rx >= 0 && rx == ry) ? 1 : 0;
}

int64_t rt_unionfind_count(void *uf_ptr)
{
    if (!uf_ptr) return 0;
    return ((rt_unionfind_impl *)uf_ptr)->sets;
}

int64_t rt_unionfind_set_size(void *uf_ptr, int64_t x)
{
    if (!uf_ptr) return 0;
    int64_t root = rt_unionfind_find(uf_ptr, x);
    if (root < 0) return 0;
    return ((rt_unionfind_impl *)uf_ptr)->size[root];
}

void rt_unionfind_reset(void *uf_ptr)
{
    if (!uf_ptr) return;
    rt_unionfind_impl *uf = (rt_unionfind_impl *)uf_ptr;

    for (int64_t i = 0; i < uf->n; i++)
    {
        uf->parent[i] = i;
        uf->rank[i] = 0;
        uf->size[i] = 1;
    }
    uf->sets = uf->n;
}
