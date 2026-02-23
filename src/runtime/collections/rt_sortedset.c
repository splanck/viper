//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_sortedset.c
// Purpose: Implements a sorted set of strings backed by a sorted dynamic array
//   with binary search. All elements are maintained in lexicographic order at
//   all times; insertion and removal use binary search to find the correct
//   position, followed by memmove to shift elements.
//
// Key invariants:
//   - Backed by a heap-allocated array of rt_string references, sorted in
//     ascending lexicographic order (strcmp order).
//   - Binary search provides O(log n) lookup (Contains, Floor, Ceiling).
//   - Insertion is O(n) due to memmove of the suffix after the insert point.
//   - Removal is O(n) due to memmove of the suffix before the remove point.
//   - Each element is a copied rt_string (via rt_string_from_bytes); the set
//     owns the string objects and frees them on removal or finalizer.
//   - Duplicate keys are rejected: inserting an existing string is a no-op.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - SortedSet objects are GC-managed (rt_obj_new_i64). The data array and
//     all contained rt_string copies are freed by the GC finalizer.
//
// Links: src/runtime/collections/rt_sortedset.h (public API),
//        src/runtime/collections/rt_set.h (unordered set counterpart)
//
//===----------------------------------------------------------------------===//

#include "rt_sortedset.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include <stdlib.h>
#include <string.h>

/// Internal structure for SortedSet.
struct rt_sortedset_impl
{
    rt_string *data; // Sorted array of strings
    int64_t len;     // Number of elements
    int64_t cap;     // Capacity
};

typedef struct rt_sortedset_impl *rt_sortedset;

//=============================================================================
// Internal helpers
//=============================================================================

/// Copy an rt_string by creating a new string from its bytes.
static rt_string copy_string(rt_string s)
{
    if (!s)
        return rt_const_cstr("");
    const char *cstr = rt_string_cstr(s);
    return rt_string_from_bytes(cstr, strlen(cstr));
}

static int compare_strings(rt_string a, rt_string b)
{
    const char *sa = a ? rt_string_cstr(a) : "";
    const char *sb = b ? rt_string_cstr(b) : "";
    return strcmp(sa, sb);
}

/// Binary search for insertion point or element.
/// Returns index where element is or should be inserted.
static int64_t binary_search(rt_sortedset set, rt_string str, int8_t *found)
{
    if (!set || set->len == 0)
    {
        if (found)
            *found = 0;
        return 0;
    }

    int64_t lo = 0;
    int64_t hi = set->len - 1;

    while (lo <= hi)
    {
        int64_t mid = lo + (hi - lo) / 2;
        int cmp = compare_strings(str, set->data[mid]);

        if (cmp == 0)
        {
            if (found)
                *found = 1;
            return mid;
        }
        else if (cmp < 0)
        {
            hi = mid - 1;
        }
        else
        {
            lo = mid + 1;
        }
    }

    if (found)
        *found = 0;
    return lo;
}

static void ensure_capacity(rt_sortedset set, int64_t needed)
{
    if (set->cap >= needed)
        return;

    int64_t new_cap = set->cap == 0 ? 8 : set->cap * 2;
    while (new_cap < needed)
        new_cap *= 2;

    rt_string *new_data = realloc(set->data, sizeof(rt_string) * new_cap);
    if (new_data)
    {
        set->data = new_data;
        set->cap = new_cap;
    }
}

//=============================================================================
// Creation and Lifecycle
//=============================================================================

static void sortedset_finalizer(void *obj)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set)
        return;
    for (int64_t i = 0; i < set->len; i++)
        rt_str_release_maybe(set->data[i]);
    free(set->data);
    set->data = NULL;
    set->len = 0;
    set->cap = 0;
}

void *rt_sortedset_new(void)
{
    rt_sortedset set = (rt_sortedset)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_sortedset_impl));
    if (set)
        rt_obj_set_finalizer(set, sortedset_finalizer);
    return set;
}

int64_t rt_sortedset_len(void *obj)
{
    rt_sortedset set = (rt_sortedset)obj;
    return set ? set->len : 0;
}

int8_t rt_sortedset_is_empty(void *obj)
{
    return rt_sortedset_len(obj) == 0 ? 1 : 0;
}

//=============================================================================
// Basic Operations
//=============================================================================

int8_t rt_sortedset_put(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set)
        return 0;

    int8_t found;
    int64_t idx = binary_search(set, str, &found);

    if (found)
        return 0; // Already present

    ensure_capacity(set, set->len + 1);

    // Shift elements to make room
    for (int64_t i = set->len; i > idx; i--)
    {
        set->data[i] = set->data[i - 1];
    }

    // Insert copy of string
    set->data[idx] = copy_string(str);
    set->len++;
    return 1;
}

int8_t rt_sortedset_drop(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set)
        return 0;

    int8_t found;
    int64_t idx = binary_search(set, str, &found);

    if (!found)
        return 0;

    // Release the string
    rt_str_release_maybe(set->data[idx]);

    // Shift elements down
    for (int64_t i = idx; i < set->len - 1; i++)
    {
        set->data[i] = set->data[i + 1];
    }

    set->len--;
    return 1;
}

int8_t rt_sortedset_has(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set)
        return 0;

    int8_t found;
    binary_search(set, str, &found);
    return found;
}

void rt_sortedset_clear(void *obj)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set)
        return;

    for (int64_t i = 0; i < set->len; i++)
    {
        rt_str_release_maybe(set->data[i]);
    }
    set->len = 0;
}

//=============================================================================
// Ordered Access
//=============================================================================

rt_string rt_sortedset_first(void *obj)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || set->len == 0)
        return rt_const_cstr("");
    return set->data[0];
}

rt_string rt_sortedset_last(void *obj)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || set->len == 0)
        return rt_const_cstr("");
    return set->data[set->len - 1];
}

rt_string rt_sortedset_floor(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || set->len == 0)
        return rt_const_cstr("");

    int8_t found;
    int64_t idx = binary_search(set, str, &found);

    if (found)
        return set->data[idx];
    if (idx == 0)
        return rt_const_cstr("");
    return set->data[idx - 1];
}

rt_string rt_sortedset_ceil(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || set->len == 0)
        return rt_const_cstr("");

    int8_t found;
    int64_t idx = binary_search(set, str, &found);

    if (found)
        return set->data[idx];
    if (idx >= set->len)
        return rt_const_cstr("");
    return set->data[idx];
}

rt_string rt_sortedset_lower(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || set->len == 0)
        return rt_const_cstr("");

    int8_t found;
    int64_t idx = binary_search(set, str, &found);

    // For lower, we want strictly less than
    if (found)
        idx--;
    else
        idx--;

    if (idx < 0)
        return rt_const_cstr("");
    return set->data[idx];
}

rt_string rt_sortedset_higher(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || set->len == 0)
        return rt_const_cstr("");

    int8_t found;
    int64_t idx = binary_search(set, str, &found);

    // For higher, we want strictly greater than
    if (found)
        idx++;

    if (idx >= set->len)
        return rt_const_cstr("");
    return set->data[idx];
}

rt_string rt_sortedset_at(void *obj, int64_t index)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set || index < 0 || index >= set->len)
        return rt_const_cstr("");
    return set->data[index];
}

int64_t rt_sortedset_index_of(void *obj, rt_string str)
{
    rt_sortedset set = (rt_sortedset)obj;
    if (!set)
        return -1;

    int8_t found;
    int64_t idx = binary_search(set, str, &found);
    return found ? idx : -1;
}

//=============================================================================
// Range Operations
//=============================================================================

void *rt_sortedset_range(void *obj, rt_string from, rt_string to)
{
    rt_sortedset set = (rt_sortedset)obj;
    void *seq = rt_seq_new();
    if (!set)
        return seq;

    int8_t found;
    int64_t start = binary_search(set, from, &found);

    for (int64_t i = start; i < set->len; i++)
    {
        if (compare_strings(set->data[i], to) >= 0)
            break;
        rt_seq_push(seq, set->data[i]);
    }

    return seq;
}

void *rt_sortedset_items(void *obj)
{
    rt_sortedset set = (rt_sortedset)obj;
    void *seq = rt_seq_new();
    if (!set)
        return seq;

    for (int64_t i = 0; i < set->len; i++)
    {
        rt_seq_push(seq, set->data[i]);
    }

    return seq;
}

void *rt_sortedset_take(void *obj, int64_t n)
{
    rt_sortedset set = (rt_sortedset)obj;
    void *seq = rt_seq_new();
    if (!set || n <= 0)
        return seq;

    int64_t count = n < set->len ? n : set->len;
    for (int64_t i = 0; i < count; i++)
    {
        rt_seq_push(seq, set->data[i]);
    }

    return seq;
}

void *rt_sortedset_skip(void *obj, int64_t n)
{
    rt_sortedset set = (rt_sortedset)obj;
    void *seq = rt_seq_new();
    if (!set || n >= set->len)
        return seq;

    int64_t start = n < 0 ? 0 : n;
    for (int64_t i = start; i < set->len; i++)
    {
        rt_seq_push(seq, set->data[i]);
    }

    return seq;
}

//=============================================================================
// Set Operations
//=============================================================================

void *rt_sortedset_merge(void *obj, void *other)
{
    void *result = rt_sortedset_new();
    rt_sortedset a = (rt_sortedset)obj;
    rt_sortedset b = (rt_sortedset)other;

    if (a)
    {
        for (int64_t i = 0; i < a->len; i++)
        {
            rt_sortedset_put(result, a->data[i]);
        }
    }

    if (b)
    {
        for (int64_t i = 0; i < b->len; i++)
        {
            rt_sortedset_put(result, b->data[i]);
        }
    }

    return result;
}

void *rt_sortedset_common(void *obj, void *other)
{
    void *result = rt_sortedset_new();
    rt_sortedset a = (rt_sortedset)obj;
    rt_sortedset b = (rt_sortedset)other;

    if (!a || !b)
        return result;

    // Use merge-style intersection for sorted sets
    int64_t i = 0, j = 0;
    while (i < a->len && j < b->len)
    {
        int cmp = compare_strings(a->data[i], b->data[j]);
        if (cmp == 0)
        {
            rt_sortedset_put(result, a->data[i]);
            i++;
            j++;
        }
        else if (cmp < 0)
        {
            i++;
        }
        else
        {
            j++;
        }
    }

    return result;
}

void *rt_sortedset_diff(void *obj, void *other)
{
    void *result = rt_sortedset_new();
    rt_sortedset a = (rt_sortedset)obj;
    rt_sortedset b = (rt_sortedset)other;

    if (!a)
        return result;
    if (!b)
    {
        // All elements in a
        for (int64_t i = 0; i < a->len; i++)
        {
            rt_sortedset_put(result, a->data[i]);
        }
        return result;
    }

    // Use merge-style difference for sorted sets
    int64_t i = 0, j = 0;
    while (i < a->len)
    {
        if (j >= b->len)
        {
            rt_sortedset_put(result, a->data[i]);
            i++;
        }
        else
        {
            int cmp = compare_strings(a->data[i], b->data[j]);
            if (cmp < 0)
            {
                rt_sortedset_put(result, a->data[i]);
                i++;
            }
            else if (cmp > 0)
            {
                j++;
            }
            else
            {
                i++;
                j++;
            }
        }
    }

    return result;
}

int8_t rt_sortedset_is_subset(void *obj, void *other)
{
    rt_sortedset a = (rt_sortedset)obj;
    rt_sortedset b = (rt_sortedset)other;

    if (!a || a->len == 0)
        return 1; // Empty is subset of everything
    if (!b)
        return 0; // Non-empty can't be subset of empty

    // Check all elements of a are in b
    int64_t j = 0;
    for (int64_t i = 0; i < a->len; i++)
    {
        // Find a->data[i] in b
        while (j < b->len && compare_strings(b->data[j], a->data[i]) < 0)
        {
            j++;
        }
        if (j >= b->len || compare_strings(b->data[j], a->data[i]) != 0)
        {
            return 0; // Not found
        }
    }

    return 1;
}
