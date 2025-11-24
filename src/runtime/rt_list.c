//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_list.c
// Purpose: Implement a simple object-backed list using rt_arr_obj as storage.
// Structure: [vptr | arr]
// - vptr: points to class vtable (not required for these helpers)
// - arr:  dynamic array of void* managed by rt_arr_obj_*
//
//===----------------------------------------------------------------------===//

#include "rt_list.h"

#include "rt_array_obj.h"
#include "rt_heap.h"
#include "rt_object.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct rt_list_impl
{
    void **vptr; // vtable pointer placeholder
    void **arr;  // dynamic array of elements
} rt_list_impl;

void *rt_ns_list_new(void)
{
    // Allocate object payload with header via object allocator to match object lifetime rules
    rt_list_impl *list = (rt_list_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_list_impl));
    if (!list)
        return NULL;
    list->vptr = NULL;
    list->arr = NULL;
    return list;
}

static inline rt_list_impl *as_list(void *p)
{
    return (rt_list_impl *)p;
}

int64_t rt_list_get_count(void *list)
{
    if (!list)
        return 0;
    rt_list_impl *L = as_list(list);
    return (int64_t)rt_arr_obj_len(L->arr);
}

void rt_list_clear(void *list)
{
    if (!list)
        return;
    rt_list_impl *L = as_list(list);
    if (L->arr)
    {
        rt_arr_obj_release(L->arr);
        L->arr = NULL;
    }
}

void rt_list_add(void *list, void *elem)
{
    if (!list)
        return;
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    void **arr2 = rt_arr_obj_resize(L->arr, len + 1);
    if (!arr2)
        return;
    L->arr = arr2;
    rt_arr_obj_put(L->arr, len, elem);
}

void *rt_list_get_item(void *list, int64_t index)
{
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index >= (uint64_t)len)
        return NULL;
    return rt_arr_obj_get(L->arr, (size_t)index);
}

void rt_list_set_item(void *list, int64_t index, void *elem)
{
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index >= (uint64_t)len)
        return;
    rt_arr_obj_put(L->arr, (size_t)index, elem);
}

void rt_list_remove_at(void *list, int64_t index)
{
    rt_list_impl *L = as_list(list);
    size_t len = rt_arr_obj_len(L->arr);
    if ((uint64_t)index >= (uint64_t)len)
        return;
    // Shift elements left from index
    if (len > 0)
    {
        // Release element at index by overwriting it via put with next (or NULL for last)
        for (size_t i = (size_t)index; i + 1 < len; ++i)
        {
            void *next = L->arr[i + 1];
            rt_arr_obj_put(L->arr, i, next);
        }
        // Clear last slot
        rt_arr_obj_put(L->arr, len - 1, NULL);
        // Shrink storage
        L->arr = rt_arr_obj_resize(L->arr, len - 1);
    }
}

