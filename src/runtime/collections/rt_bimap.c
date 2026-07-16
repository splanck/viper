//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_bimap.c
// Purpose: Implements a bidirectional string-to-string map (BiMap) using two
//   parallel hash tables: a forward table (key -> value) and an inverse table
//   (value -> key). Both directions support O(1) average-case lookup, insert,
//   and remove. The invariant that each key maps to exactly one value AND each
//   value maps to exactly one key is enforced at insert time.
//
// Key invariants:
//   - Forward and inverse tables each start with BM_INITIAL_CAPACITY (16)
//     buckets and use separate chaining with FNV-1a hashing.
//   - Inserting a (key, value) pair where the key already maps to a different
//     value removes the old pair first (old value loses its inverse mapping).
//     Similarly, if the new value already maps to a different key, that old
//     pair is removed. This preserves the bijection invariant.
//   - Entry nodes are shared between the forward and inverse chain lists to
//     avoid double allocation; each node stores both key and value strings.
//   - Both forward and inverse tables resize independently at 75% load factor.
//   - All operations are O(1) average case; O(n) worst case.
//   - Not thread-safe; external synchronization required.
//
// Ownership/Lifetime:
//   - BiMap objects are GC-managed (rt_obj_new_i64). All entry nodes, bucket
//     arrays, and copied key/value strings are freed by the GC finalizer.
//
// Links: src/runtime/collections/rt_bimap.h (public API),
//        src/runtime/collections/rt_hash_util.h (FNV-1a hash macro)
//
//===----------------------------------------------------------------------===//

#include "rt_bimap.h"

#include "rt_collection_ids.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <stdlib.h>
#include <string.h>

#define BM_INITIAL_CAPACITY 16
#define BM_LOAD_FACTOR_NUM 3
#define BM_LOAD_FACTOR_DEN 4
#include "rt_hash_util.h"

typedef struct rt_bm_entry {
    char *key;
    size_t key_len;
    char *value;
    size_t value_len;
    struct rt_bm_entry *next;
} rt_bm_entry;

typedef struct rt_bimap_impl {
    void **vptr;
    rt_bm_entry **fwd_buckets; // key -> entry
    size_t fwd_capacity;
    size_t inv_capacity;
    size_t count;

    // Separate chains for inverse lookups
    struct rt_bm_inv_link {
        rt_bm_entry *entry;
        struct rt_bm_inv_link *next;
    } **inv_chains;
} rt_bimap_impl;

// Inverse lookup chain node
typedef struct rt_bm_inv_link rt_bm_inv_link;

/// @brief Checked cast of an opaque handle to the BiMap implementation.
/// @details Traps with the @p what message if @p obj is NULL or not a BiMap.
static rt_bimap_impl *as_bimap(void *obj, const char *what) {
    if (!rt_obj_is_instance(obj, RT_BIMAP_CLASS_ID, sizeof(rt_bimap_impl))) {
        rt_trap(what);
        return NULL;
    }
    return (rt_bimap_impl *)obj;
}

/// @brief Borrow the byte buffer + length of an rt_string (empty "" if null).
static const char *get_str_data(rt_string s, size_t *out_len) {
    int64_t len = rt_str_len(s);
    if (len <= 0) {
        *out_len = 0;
        return "";
    }
    const char *data = rt_string_cstr(s);
    if (!data) {
        *out_len = 0;
        return "";
    }
    *out_len = (size_t)len;
    return data;
}

/// @brief Linear scan of a forward bucket chain for an exact key match.
static rt_bm_entry *find_fwd(rt_bm_entry *head, const char *key, size_t key_len) {
    for (rt_bm_entry *e = head; e; e = e->next) {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
    }
    return NULL;
}

/// @brief Linear scan of an inverse chain for the link whose entry has @p val.
static rt_bm_inv_link *find_inv(rt_bm_inv_link *head, const char *val, size_t val_len) {
    for (rt_bm_inv_link *l = head; l; l = l->next) {
        if (l->entry->value_len == val_len && memcmp(l->entry->value, val, val_len) == 0)
            return l;
    }
    return NULL;
}

/// @brief Unlink and free the inverse-chain node for value @p val (if present).
static void remove_inv_link(rt_bimap_impl *bm, const char *val, size_t val_len) {
    uint64_t h = rt_fnv1a(val, val_len);
    size_t idx = (size_t)(h % bm->inv_capacity);
    rt_bm_inv_link **pp = &bm->inv_chains[idx];
    while (*pp) {
        if ((*pp)->entry->value_len == val_len && memcmp((*pp)->entry->value, val, val_len) == 0) {
            rt_bm_inv_link *old = *pp;
            *pp = old->next;
            free(old);
            return;
        }
        pp = &(*pp)->next;
    }
}

/// @brief Push @p link onto the inverse chain bucket for its entry's value.
static void insert_inv_link(rt_bimap_impl *bm, rt_bm_inv_link *link) {
    rt_bm_entry *entry = link->entry;
    uint64_t h = rt_fnv1a(entry->value, entry->value_len);
    size_t idx = (size_t)(h % bm->inv_capacity);
    link->next = bm->inv_chains[idx];
    bm->inv_chains[idx] = link;
}

/// @brief Free a forward entry and its owned key/value buffers (NULL-safe).
static void free_entry(rt_bm_entry *entry) {
    if (!entry)
        return;
    free(entry->key);
    free(entry->value);
    free(entry);
}

/// @brief GC finalizer: free all forward entries, inverse links, and the
///        bucket/chain arrays, then zero the struct fields.
static void bimap_finalizer(void *obj) {
    if (!obj)
        return;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap: invalid BiMap object");

    // Free forward entries
    if (bm->fwd_buckets) {
        for (size_t i = 0; i < bm->fwd_capacity; ++i) {
            rt_bm_entry *e = bm->fwd_buckets[i];
            while (e) {
                rt_bm_entry *next = e->next;
                free_entry(e);
                e = next;
            }
        }
    }
    free(bm->fwd_buckets);

    // Free inverse chains
    if (bm->inv_chains) {
        for (size_t i = 0; i < bm->inv_capacity; ++i) {
            rt_bm_inv_link *l = bm->inv_chains[i];
            while (l) {
                rt_bm_inv_link *next = l->next;
                free(l);
                l = next;
            }
        }
    }
    free(bm->inv_chains);
    bm->fwd_buckets = NULL;
    bm->inv_chains = NULL;
    bm->fwd_capacity = 0;
    bm->inv_capacity = 0;
    bm->count = 0;
}

/// @brief Double the forward bucket array and rehash all entries into it.
/// @details No-op past the SIZE_MAX/2 cap; traps on allocation overflow/OOM.
static int resize_fwd(rt_bimap_impl *bm) {
    if (bm->fwd_capacity > SIZE_MAX / 2)
        return 0;
    size_t new_cap = bm->fwd_capacity * 2;
    if (new_cap > SIZE_MAX / sizeof(rt_bm_entry *)) {
        rt_trap("BiMap: allocation size overflow");
        return 0;
    }
    rt_bm_entry **new_buckets = (rt_bm_entry **)calloc(new_cap, sizeof(rt_bm_entry *));
    if (!new_buckets) {
        rt_trap("BiMap: memory allocation failed");
        return 0;
    }

    for (size_t i = 0; i < bm->fwd_capacity; ++i) {
        rt_bm_entry *e = bm->fwd_buckets[i];
        while (e) {
            rt_bm_entry *next = e->next;
            uint64_t h = rt_fnv1a(e->key, e->key_len);
            size_t idx = (size_t)(h % new_cap);
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }

    free(bm->fwd_buckets);
    bm->fwd_buckets = new_buckets;
    bm->fwd_capacity = new_cap;
    return 1;
}

/// @brief Double the inverse chain array and rehash all links into it.
/// @details No-op past the SIZE_MAX/2 cap; traps on allocation overflow/OOM.
static int resize_inv(rt_bimap_impl *bm) {
    if (bm->inv_capacity > SIZE_MAX / 2)
        return 0;
    size_t new_cap = bm->inv_capacity * 2;
    if (new_cap > SIZE_MAX / sizeof(rt_bm_inv_link *)) {
        rt_trap("BiMap: allocation size overflow");
        return 0;
    }
    rt_bm_inv_link **new_chains = (rt_bm_inv_link **)calloc(new_cap, sizeof(rt_bm_inv_link *));
    if (!new_chains) {
        rt_trap("BiMap: memory allocation failed");
        return 0;
    }

    for (size_t i = 0; i < bm->inv_capacity; ++i) {
        rt_bm_inv_link *l = bm->inv_chains[i];
        while (l) {
            rt_bm_inv_link *next = l->next;
            uint64_t h = rt_fnv1a(l->entry->value, l->entry->value_len);
            size_t idx = (size_t)(h % new_cap);
            l->next = new_chains[idx];
            new_chains[idx] = l;
            l = next;
        }
    }

    free(bm->inv_chains);
    bm->inv_chains = new_chains;
    bm->inv_capacity = new_cap;
    return 1;
}

/// @brief Construct an empty bidirectional map (string ↔ string). Maintains forward and inverse
/// hash tables so both `_get_by_key` and `_get_by_value` are O(1) average. Useful for two-way
/// lookups (e.g., name ↔ id) that would otherwise need two parallel maps.
void *rt_bimap_new(void) {
    rt_bimap_impl *bm = (rt_bimap_impl *)rt_obj_new_i64(RT_BIMAP_CLASS_ID, sizeof(rt_bimap_impl));
    if (!bm) {
        rt_trap("BiMap: memory allocation failed");
        return NULL;
    }

    bm->vptr = NULL;
    bm->fwd_capacity = BM_INITIAL_CAPACITY;
    bm->inv_capacity = BM_INITIAL_CAPACITY;
    bm->count = 0;
    bm->fwd_buckets = (rt_bm_entry **)calloc(BM_INITIAL_CAPACITY, sizeof(rt_bm_entry *));
    bm->inv_chains = (rt_bm_inv_link **)calloc(BM_INITIAL_CAPACITY, sizeof(rt_bm_inv_link *));
    if (!bm->fwd_buckets || !bm->inv_chains) {
        free(bm->fwd_buckets);
        free(bm->inv_chains);
        if (rt_obj_release_check0(bm))
            rt_obj_free(bm);
        rt_trap("BiMap: memory allocation failed");
        return NULL;
    }

    rt_obj_set_finalizer(bm, bimap_finalizer);
    return bm;
}

/// @brief Return the number of entries in the bidirectional map.
int64_t rt_bimap_len(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)as_bimap(obj, "BiMap.Len: invalid BiMap object")->count;
}

/// @brief Check whether the bidirectional map is empty.
int8_t rt_bimap_is_empty(void *obj) {
    return rt_bimap_len(obj) == 0 ? 1 : 0;
}

/// @brief Insert a key-value pair with bidirectional lookup support.
/// @details Maintains two parallel hash tables so both key→value and
///          value→key lookups are O(1). If either the key or value already
///          exists, the conflicting entries are removed first.
void rt_bimap_put(void *obj, rt_string key, rt_string value) {
    if (!obj)
        return;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.Put: invalid BiMap object");
    if (!bm)
        return;

    size_t klen, vlen;
    const char *kdata = get_str_data(key, &klen);
    const char *vdata = get_str_data(value, &vlen);

    // Check load factor on forward table
    if ((long double)bm->count * (long double)BM_LOAD_FACTOR_DEN >=
            (long double)bm->fwd_capacity * (long double)BM_LOAD_FACTOR_NUM &&
        !resize_fwd(bm))
        return;
    if ((long double)bm->count * (long double)BM_LOAD_FACTOR_DEN >=
            (long double)bm->inv_capacity * (long double)BM_LOAD_FACTOR_NUM &&
        !resize_inv(bm))
        return;

    // Create entry
    rt_bm_entry *entry = (rt_bm_entry *)malloc(sizeof(rt_bm_entry));
    if (!entry) {
        rt_trap("BiMap: memory allocation failed");
        return;
    }
    if (klen == SIZE_MAX || vlen == SIZE_MAX) {
        free(entry);
        rt_trap("BiMap: string allocation overflow");
        return;
    }
    entry->key = (char *)malloc(klen + 1);
    entry->value = (char *)malloc(vlen + 1);
    if (!entry->key || !entry->value) {
        free(entry->key);
        free(entry->value);
        free(entry);
        rt_trap("BiMap: memory allocation failed");
        return;
    }
    memcpy(entry->key, kdata, klen);
    entry->key[klen] = '\0';
    entry->key_len = klen;
    memcpy(entry->value, vdata, vlen);
    entry->value[vlen] = '\0';
    entry->value_len = vlen;

    rt_bm_inv_link *inv_link = (rt_bm_inv_link *)malloc(sizeof(rt_bm_inv_link));
    if (!inv_link) {
        free_entry(entry);
        rt_trap("BiMap: memory allocation failed");
        return;
    }
    inv_link->entry = entry;
    inv_link->next = NULL;

    // All allocations for the replacement entry have succeeded. It is now safe
    // to remove any conflicting key or value mappings before committing.
    rt_bimap_remove_by_key(obj, key);
    rt_bimap_remove_by_value(obj, value);

    // Insert into forward table
    uint64_t fh = rt_fnv1a(kdata, klen);
    size_t fidx = (size_t)(fh % bm->fwd_capacity);
    entry->next = bm->fwd_buckets[fidx];
    bm->fwd_buckets[fidx] = entry;

    // Insert into inverse chain
    insert_inv_link(bm, inv_link);

    bm->count++;
}

/// @brief Look up a value by its associated key.
/// @details Returns an owned empty string if the key is not present.
rt_string rt_bimap_get_by_key(void *obj, rt_string key) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.GetByKey: invalid BiMap object");

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = rt_fnv1a(kdata, klen);
    size_t idx = (size_t)(h % bm->fwd_capacity);
    rt_bm_entry *e = find_fwd(bm->fwd_buckets[idx], kdata, klen);
    if (!e)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(e->value, e->value_len);
}

/// @brief Look up a key by its associated value (reverse lookup).
/// @details Returns an owned empty string if the value is not present in the reverse index.
rt_string rt_bimap_get_by_value(void *obj, rt_string value) {
    if (!obj)
        return rt_string_from_bytes("", 0);
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.GetByValue: invalid BiMap object");

    size_t vlen;
    const char *vdata = get_str_data(value, &vlen);

    uint64_t h = rt_fnv1a(vdata, vlen);
    size_t idx = (size_t)(h % bm->inv_capacity);
    rt_bm_inv_link *l = find_inv(bm->inv_chains[idx], vdata, vlen);
    if (!l)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(l->entry->key, l->entry->key_len);
}

/// @brief Check whether a key exists in the bidirectional map.
int8_t rt_bimap_has_key(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.HasKey: invalid BiMap object");

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = rt_fnv1a(kdata, klen);
    size_t idx = (size_t)(h % bm->fwd_capacity);
    return find_fwd(bm->fwd_buckets[idx], kdata, klen) ? 1 : 0;
}

/// @brief Check whether a value exists in the reverse index.
int8_t rt_bimap_has_value(void *obj, rt_string value) {
    if (!obj)
        return 0;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.HasValue: invalid BiMap object");

    size_t vlen;
    const char *vdata = get_str_data(value, &vlen);

    uint64_t h = rt_fnv1a(vdata, vlen);
    size_t idx = (size_t)(h % bm->inv_capacity);
    return find_inv(bm->inv_chains[idx], vdata, vlen) ? 1 : 0;
}

/// @brief Remove an entry by its key, also removing the reverse mapping.
/// @details Both the key and value references are released.
int8_t rt_bimap_remove_by_key(void *obj, rt_string key) {
    if (!obj)
        return 0;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.RemoveByKey: invalid BiMap object");

    size_t klen;
    const char *kdata = get_str_data(key, &klen);

    uint64_t h = rt_fnv1a(kdata, klen);
    size_t idx = (size_t)(h % bm->fwd_capacity);

    rt_bm_entry **pp = &bm->fwd_buckets[idx];
    while (*pp) {
        rt_bm_entry *e = *pp;
        if (e->key_len == klen && memcmp(e->key, kdata, klen) == 0) {
            // Remove from forward chain
            *pp = e->next;
            // Remove from inverse chain
            remove_inv_link(bm, e->value, e->value_len);
            free_entry(e);
            bm->count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

/// @brief Remove an entry by its value, also removing the forward mapping.
/// @details Both the key and value references are released.
int8_t rt_bimap_remove_by_value(void *obj, rt_string value) {
    if (!obj)
        return 0;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.RemoveByValue: invalid BiMap object");

    size_t vlen;
    const char *vdata = get_str_data(value, &vlen);

    // Find entry via inverse lookup
    uint64_t vh = rt_fnv1a(vdata, vlen);
    size_t vidx = (size_t)(vh % bm->inv_capacity);
    rt_bm_inv_link *l = find_inv(bm->inv_chains[vidx], vdata, vlen);
    if (!l)
        return 0;

    rt_bm_entry *entry = l->entry;

    // Remove from forward chain
    uint64_t fh = rt_fnv1a(entry->key, entry->key_len);
    size_t fidx = (size_t)(fh % bm->fwd_capacity);
    rt_bm_entry **pp = &bm->fwd_buckets[fidx];
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->next;
            break;
        }
        pp = &(*pp)->next;
    }

    // Remove from inverse chain
    remove_inv_link(bm, entry->value, entry->value_len);
    free_entry(entry);
    bm->count--;
    return 1;
}

/// @brief Return a Seq of every key (forward-table iteration order). Snapshot of current state.
void *rt_bimap_keys(void *obj) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.Keys: invalid BiMap object");

    for (size_t i = 0; i < bm->fwd_capacity; ++i) {
        for (rt_bm_entry *e = bm->fwd_buckets[i]; e; e = e->next) {
            rt_string k = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(seq, (void *)k);
            rt_str_release_maybe(k);
        }
    }
    return seq;
}

/// @brief Return a Seq of every value (inverse-table iteration order). Snapshot.
void *rt_bimap_values(void *obj) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!obj)
        return seq;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.Values: invalid BiMap object");

    for (size_t i = 0; i < bm->fwd_capacity; ++i) {
        for (rt_bm_entry *e = bm->fwd_buckets[i]; e; e = e->next) {
            rt_string v = rt_string_from_bytes(e->value, e->value_len);
            rt_seq_push(seq, (void *)v);
            rt_str_release_maybe(v);
        }
    }
    return seq;
}

/// @brief Remove all entries from both forward and reverse tables.
/// @details Releases all retained references and resets both tables.
void rt_bimap_clear(void *obj) {
    if (!obj)
        return;
    rt_bimap_impl *bm = as_bimap(obj, "BiMap.Clear: invalid BiMap object");

    for (size_t i = 0; i < bm->fwd_capacity; ++i) {
        rt_bm_entry *e = bm->fwd_buckets[i];
        while (e) {
            rt_bm_entry *next = e->next;
            free_entry(e);
            e = next;
        }
        bm->fwd_buckets[i] = NULL;
    }

    for (size_t i = 0; i < bm->inv_capacity; ++i) {
        rt_bm_inv_link *l = bm->inv_chains[i];
        while (l) {
            rt_bm_inv_link *next = l->next;
            free(l);
            l = next;
        }
        bm->inv_chains[i] = NULL;
    }

    bm->count = 0;
}
