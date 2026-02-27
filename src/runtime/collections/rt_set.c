//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_set.c
// Purpose: Implements a generic hash set supporting heterogeneous element types.
//   Uses content-aware hashing and equality: boxed integers, floats, booleans,
//   and strings are compared by value; non-boxed objects fall back to pointer
//   identity. Supports add, remove, contains, intersection, union, and
//   difference operations.
//
// Key invariants:
//   - Backed by a hash table with initial capacity SET_INITIAL_CAPACITY (16)
//     and separate chaining.
//   - Resizes (doubles) at 75% load factor (SET_LOAD_FACTOR 3/4).
//   - Hash dispatch: RT_ELEM_BOX elements use content hash (FNV-1a for strings,
//     bit-mix for integers/floats); all other elements use pointer address.
//   - Equality dispatch matches the hash dispatch to ensure correctness.
//   - Elements are stored as raw void* pointers; the set does NOT retain them.
//   - Contains, add, and remove are O(1) average case; O(n) worst case.
//   - Set algebra (union, intersection, difference) iterates all buckets: O(n+m).
//   - Not thread-safe; external synchronization required for concurrent access.
//
// Ownership/Lifetime:
//   - Set objects are GC-managed (rt_obj_new_i64). The bucket array and all
//     entry nodes are freed by the GC finalizer (set_finalizer).
//
// Links: src/runtime/collections/rt_set.h (public API),
//        src/runtime/rt_box.h (boxed value type inspection)
//
//===----------------------------------------------------------------------===//

#include "rt_set.h"

#include "rt_box.h"
#include "rt_object.h"
#include "rt_seq.h"

#include <stdlib.h>

/// Initial number of buckets.
#define SET_INITIAL_CAPACITY 16

/// Load factor threshold for resizing (0.75 = 75%).
#define SET_LOAD_FACTOR_NUM 3
#define SET_LOAD_FACTOR_DEN 4

/// @brief Entry in the hash set (collision chain node).
typedef struct rt_set_entry
{
    void *elem;                ///< Element pointer (retained).
    struct rt_set_entry *next; ///< Next entry in collision chain (or NULL).
} rt_set_entry;

/// @brief Set implementation structure.
typedef struct rt_set_impl
{
    void **vptr;            ///< Vtable pointer placeholder (for OOP compatibility).
    rt_set_entry **buckets; ///< Array of bucket heads (collision chain pointers).
    size_t capacity;        ///< Number of buckets in the hash table.
    size_t count;           ///< Number of elements currently in the Set.
} rt_set_impl;

/// @brief Find an entry in a bucket's collision chain using content equality.
static rt_set_entry *find_entry(rt_set_entry *head, void *elem)
{
    for (rt_set_entry *e = head; e; e = e->next)
    {
        if (rt_box_equal(e->elem, elem))
            return e;
    }
    return NULL;
}

/// @brief Resize the hash table when load factor is exceeded.
static void resize_set(rt_set_impl *set)
{
    size_t new_capacity = set->capacity * 2;
    rt_set_entry **new_buckets = calloc(new_capacity, sizeof(rt_set_entry *));
    if (!new_buckets)
        return; // Allocation failed, keep old table

    // Rehash all entries
    for (size_t i = 0; i < set->capacity; ++i)
    {
        rt_set_entry *e = set->buckets[i];
        while (e)
        {
            rt_set_entry *next = e->next;
            size_t new_idx = rt_box_hash(e->elem) % new_capacity;
            e->next = new_buckets[new_idx];
            new_buckets[new_idx] = e;
            e = next;
        }
    }

    free(set->buckets);
    set->buckets = new_buckets;
    set->capacity = new_capacity;
}

/// @brief Finalizer callback invoked when a Set is garbage collected.
static void rt_set_finalize(void *obj)
{
    if (!obj)
        return;
    rt_set_impl *set = obj;
    if (!set->buckets || set->capacity == 0)
        return;
    rt_set_clear(obj);
    free(set->buckets);
    set->buckets = NULL;
    set->capacity = 0;
    set->count = 0;
}

void *rt_set_new(void)
{
    rt_set_impl *set = (rt_set_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_set_impl));
    if (!set)
        return NULL;

    set->vptr = NULL; // Placeholder for OOP vtable
    set->capacity = SET_INITIAL_CAPACITY;
    set->count = 0;
    set->buckets = calloc(SET_INITIAL_CAPACITY, sizeof(rt_set_entry *));

    if (!set->buckets)
    {
        if (rt_obj_release_check0(set))
            rt_obj_free(set);
        return NULL;
    }

    rt_obj_set_finalizer(set, rt_set_finalize);
    return set;
}

int64_t rt_set_len(void *obj)
{
    if (!obj)
        return 0;
    rt_set_impl *set = obj;
    return (int64_t)set->count;
}

int8_t rt_set_is_empty(void *obj)
{
    if (!obj)
        return 1;
    rt_set_impl *set = obj;
    return set->count == 0 ? 1 : 0;
}

int8_t rt_set_put(void *obj, void *elem)
{
    if (!obj)
        return 0;
    rt_set_impl *set = obj;

    // Check load factor and resize if needed
    if (set->count * SET_LOAD_FACTOR_DEN >= set->capacity * SET_LOAD_FACTOR_NUM)
    {
        resize_set(set);
    }

    size_t idx = rt_box_hash(elem) % set->capacity;

    // Check if already present
    if (find_entry(set->buckets[idx], elem))
        return 0; // Already exists

    // Create new entry
    rt_set_entry *entry = malloc(sizeof(rt_set_entry));
    if (!entry)
        return 0;

    entry->elem = elem;
    entry->next = set->buckets[idx];
    set->buckets[idx] = entry;
    set->count++;

    // Retain the element
    rt_obj_retain_maybe(elem);

    return 1;
}

int8_t rt_set_drop(void *obj, void *elem)
{
    if (!obj)
        return 0;
    rt_set_impl *set = obj;

    size_t idx = rt_box_hash(elem) % set->capacity;

    rt_set_entry *prev = NULL;
    for (rt_set_entry *e = set->buckets[idx]; e; prev = e, e = e->next)
    {
        if (rt_box_equal(e->elem, elem))
        {
            // Remove from chain
            if (prev)
                prev->next = e->next;
            else
                set->buckets[idx] = e->next;

            // Release and free
            if (e->elem && rt_obj_release_check0(e->elem))
                rt_obj_free(e->elem);
            free(e);
            set->count--;
            return 1;
        }
    }

    return 0;
}

int8_t rt_set_has(void *obj, void *elem)
{
    if (!obj)
        return 0;
    rt_set_impl *set = obj;

    size_t idx = rt_box_hash(elem) % set->capacity;
    return find_entry(set->buckets[idx], elem) ? 1 : 0;
}

void rt_set_clear(void *obj)
{
    if (!obj)
        return;
    rt_set_impl *set = obj;

    for (size_t i = 0; i < set->capacity; ++i)
    {
        rt_set_entry *e = set->buckets[i];
        while (e)
        {
            rt_set_entry *next = e->next;
            if (e->elem && rt_obj_release_check0(e->elem))
                rt_obj_free(e->elem);
            free(e);
            e = next;
        }
        set->buckets[i] = NULL;
    }
    set->count = 0;
}

void *rt_set_items(void *obj)
{
    if (!obj)
        return rt_seq_new();

    rt_set_impl *set = obj;
    void *seq = rt_seq_new();

    for (size_t i = 0; i < set->capacity; ++i)
    {
        for (rt_set_entry *e = set->buckets[i]; e; e = e->next)
        {
            rt_seq_push(seq, e->elem);
        }
    }

    return seq;
}

void *rt_set_union(void *obj, void *other)
{
    void *result = rt_set_new();
    if (!result)
        return NULL;

    // Add all from first set
    if (obj)
    {
        rt_set_impl *set = obj;
        for (size_t i = 0; i < set->capacity; ++i)
        {
            for (rt_set_entry *e = set->buckets[i]; e; e = e->next)
            {
                rt_set_put(result, e->elem);
            }
        }
    }

    // Add all from second set
    if (other)
    {
        rt_set_impl *set2 = other;
        for (size_t i = 0; i < set2->capacity; ++i)
        {
            for (rt_set_entry *e = set2->buckets[i]; e; e = e->next)
            {
                rt_set_put(result, e->elem);
            }
        }
    }

    return result;
}

void *rt_set_intersect(void *obj, void *other)
{
    void *result = rt_set_new();
    if (!result)
        return NULL;

    if (!obj || !other)
        return result; // Empty intersection

    rt_set_impl *set = obj;
    for (size_t i = 0; i < set->capacity; ++i)
    {
        for (rt_set_entry *e = set->buckets[i]; e; e = e->next)
        {
            if (rt_set_has(other, e->elem))
            {
                rt_set_put(result, e->elem);
            }
        }
    }

    return result;
}

void *rt_set_diff(void *obj, void *other)
{
    void *result = rt_set_new();
    if (!result)
        return NULL;

    if (!obj)
        return result; // Empty difference

    rt_set_impl *set = obj;
    for (size_t i = 0; i < set->capacity; ++i)
    {
        for (rt_set_entry *e = set->buckets[i]; e; e = e->next)
        {
            if (!other || !rt_set_has(other, e->elem))
            {
                rt_set_put(result, e->elem);
            }
        }
    }

    return result;
}

int8_t rt_set_is_subset(void *obj, void *other)
{
    if (!obj)
        return 1; // Empty set is subset of everything
    if (!other)
        return 0; // Non-empty set can't be subset of empty

    rt_set_impl *set = obj;
    for (size_t i = 0; i < set->capacity; ++i)
    {
        for (rt_set_entry *e = set->buckets[i]; e; e = e->next)
        {
            if (!rt_set_has(other, e->elem))
                return 0;
        }
    }
    return 1;
}

int8_t rt_set_is_superset(void *obj, void *other)
{
    return rt_set_is_subset(other, obj);
}

int8_t rt_set_is_disjoint(void *obj, void *other)
{
    if (!obj || !other)
        return 1; // Empty sets are disjoint

    rt_set_impl *set = obj;
    for (size_t i = 0; i < set->capacity; ++i)
    {
        for (rt_set_entry *e = set->buckets[i]; e; e = e->next)
        {
            if (rt_set_has(other, e->elem))
                return 0;
        }
    }
    return 1;
}
