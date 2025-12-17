//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_treemap.c
// Purpose: Sorted key-value map using a sorted array with binary search.
// Structure: [vptr | entries | capacity | count]
// - vptr: points to class vtable (placeholder for OOP compatibility)
// - entries: sorted array of key-value pairs
// - capacity: allocated size
// - count: number of entries
//
//===----------------------------------------------------------------------===//

#include "rt_treemap.h"

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

/// Initial capacity for the sorted array.
#define TREEMAP_INITIAL_CAPACITY 8

/// @brief Entry in the sorted map.
typedef struct
{
    char *key;     ///< Owned copy of key string.
    size_t keylen; ///< Length of key.
    void *value;   ///< Retained value pointer.
} treemap_entry;

/// @brief TreeMap implementation structure.
typedef struct
{
    void **vptr;            ///< Vtable pointer placeholder.
    treemap_entry *entries; ///< Sorted array of entries.
    size_t capacity;        ///< Allocated size.
    size_t count;           ///< Number of entries.
} treemap_impl;

/// @brief Get key data and length from rt_string.
static const char *get_key_data(rt_string key, size_t *out_len)
{
    const char *cstr = rt_string_cstr(key);
    if (!cstr)
    {
        *out_len = 0;
        return "";
    }
    *out_len = strlen(cstr);
    return cstr;
}

/// @brief Compare two keys (memcmp-style).
static int key_compare(const char *k1, size_t len1, const char *k2, size_t len2)
{
    size_t minlen = len1 < len2 ? len1 : len2;
    int cmp = memcmp(k1, k2, minlen);
    if (cmp != 0)
        return cmp;
    if (len1 < len2)
        return -1;
    if (len1 > len2)
        return 1;
    return 0;
}

/// @brief Binary search for a key. Returns index where key is or should be inserted.
/// @param tm TreeMap.
/// @param key Key data.
/// @param keylen Key length.
/// @param found Output: set to true if exact match found.
/// @return Index of key or insertion point.
static size_t binary_search(treemap_impl *tm, const char *key, size_t keylen, bool *found)
{
    *found = false;
    if (tm->count == 0)
        return 0;

    size_t lo = 0;
    size_t hi = tm->count;

    while (lo < hi)
    {
        size_t mid = lo + (hi - lo) / 2;
        treemap_entry *e = &tm->entries[mid];
        int cmp = key_compare(key, keylen, e->key, e->keylen);
        if (cmp < 0)
        {
            hi = mid;
        }
        else if (cmp > 0)
        {
            lo = mid + 1;
        }
        else
        {
            *found = true;
            return mid;
        }
    }
    return lo;
}

/// @brief Ensure capacity for at least one more entry.
static void ensure_capacity(treemap_impl *tm)
{
    if (tm->count < tm->capacity)
        return;

    size_t new_cap = tm->capacity == 0 ? TREEMAP_INITIAL_CAPACITY : tm->capacity * 2;
    treemap_entry *new_entries =
        (treemap_entry *)realloc(tm->entries, new_cap * sizeof(treemap_entry));
    if (!new_entries)
    {
        rt_trap("TreeMap: memory allocation failed");
        return;
    }
    tm->entries = new_entries;
    tm->capacity = new_cap;
}

/// @brief Free an entry's key and release its value.
static void free_entry_contents(treemap_entry *e)
{
    free(e->key);
    if (e->value && rt_obj_release_check0(e->value))
        rt_obj_free(e->value);
}

//=============================================================================
// Public API
//=============================================================================

void *rt_treemap_new(void)
{
    treemap_impl *tm = (treemap_impl *)rt_obj_new_i64(0, (int64_t)sizeof(treemap_impl));
    if (!tm)
    {
        rt_trap("TreeMap: memory allocation failed");
        return NULL;
    }

    tm->vptr = NULL;
    tm->entries = NULL;
    tm->capacity = 0;
    tm->count = 0;

    return tm;
}

int64_t rt_treemap_len(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    return (int64_t)tm->count;
}

int8_t rt_treemap_is_empty(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    return tm->count == 0 ? 1 : 0;
}

void rt_treemap_set(void *obj, rt_string key, void *value)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
    {
        // Update existing entry
        treemap_entry *e = &tm->entries[idx];
        // Release old value
        if (e->value && rt_obj_release_check0(e->value))
            rt_obj_free(e->value);
        // Retain new value
        if (value)
            rt_heap_retain(value);
        e->value = value;
    }
    else
    {
        // Insert new entry
        ensure_capacity(tm);

        // Make room by shifting entries
        if (idx < tm->count)
        {
            memmove(&tm->entries[idx + 1],
                    &tm->entries[idx],
                    (tm->count - idx) * sizeof(treemap_entry));
        }

        // Create new entry
        treemap_entry *e = &tm->entries[idx];
        e->key = (char *)malloc(keylen + 1);
        if (!e->key)
        {
            rt_trap("TreeMap: memory allocation failed");
            return;
        }
        memcpy(e->key, keydata, keylen);
        e->key[keylen] = '\0';
        e->keylen = keylen;

        // Retain value
        if (value)
            rt_heap_retain(value);
        e->value = value;

        tm->count++;
    }
}

void *rt_treemap_get(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
        return tm->entries[idx].value;
    return NULL;
}

int8_t rt_treemap_has(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    binary_search(tm, keydata, keylen, &found);

    return found ? 1 : 0;
}

int8_t rt_treemap_drop(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (!found)
        return 0;

    // Free the entry
    free_entry_contents(&tm->entries[idx]);

    // Shift remaining entries
    if (idx < tm->count - 1)
    {
        memmove(&tm->entries[idx],
                &tm->entries[idx + 1],
                (tm->count - idx - 1) * sizeof(treemap_entry));
    }

    tm->count--;
    return 1;
}

void rt_treemap_clear(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;

    for (size_t i = 0; i < tm->count; i++)
    {
        free_entry_contents(&tm->entries[i]);
    }

    tm->count = 0;
}

void *rt_treemap_keys(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    void *seq = rt_seq_new();

    for (size_t i = 0; i < tm->count; i++)
    {
        rt_string str = rt_string_from_bytes(tm->entries[i].key, tm->entries[i].keylen);
        rt_seq_push(seq, (void *)str);
    }

    return seq;
}

void *rt_treemap_values(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;
    void *seq = rt_seq_new();

    for (size_t i = 0; i < tm->count; i++)
    {
        rt_seq_push(seq, tm->entries[i].value);
    }

    return seq;
}

rt_string rt_treemap_first(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    return rt_string_from_bytes(tm->entries[0].key, tm->entries[0].keylen);
}

rt_string rt_treemap_last(void *obj)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    size_t last = tm->count - 1;
    return rt_string_from_bytes(tm->entries[last].key, tm->entries[last].keylen);
}

rt_string rt_treemap_floor(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
    {
        // Exact match
        return rt_string_from_bytes(tm->entries[idx].key, tm->entries[idx].keylen);
    }

    // idx is insertion point - floor is the previous entry
    if (idx == 0)
        return rt_const_cstr(""); // No key <= given key

    return rt_string_from_bytes(tm->entries[idx - 1].key, tm->entries[idx - 1].keylen);
}

rt_string rt_treemap_ceil(void *obj, rt_string key)
{
    treemap_impl *tm = (treemap_impl *)obj;

    if (tm->count == 0)
        return rt_const_cstr("");

    size_t keylen;
    const char *keydata = get_key_data(key, &keylen);

    bool found;
    size_t idx = binary_search(tm, keydata, keylen, &found);

    if (found)
    {
        // Exact match
        return rt_string_from_bytes(tm->entries[idx].key, tm->entries[idx].keylen);
    }

    // idx is insertion point - ceiling is the entry at idx (if exists)
    if (idx >= tm->count)
        return rt_const_cstr(""); // No key >= given key

    return rt_string_from_bytes(tm->entries[idx].key, tm->entries[idx].keylen);
}
