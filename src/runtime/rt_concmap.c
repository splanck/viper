//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_concmap.c
/// @brief Thread-safe concurrent hash map implementation.
///
/// Uses FNV-1a hashing with separate chaining, protected by a single mutex.
/// All public operations are thread-safe via lock/unlock around access.
///
//===----------------------------------------------------------------------===//

#include "rt_concmap.h"

#include "rt_hash_util.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

#define CM_INITIAL_CAPACITY 16
#define CM_LOAD_FACTOR_NUM 3
#define CM_LOAD_FACTOR_DEN 4

//=============================================================================
// Internal types
//=============================================================================

typedef struct cm_entry
{
    char *key;
    size_t key_len;
    void *value;
    struct cm_entry *next;
} cm_entry;

typedef struct
{
    void *vptr;
    cm_entry **buckets;
    size_t capacity;
    size_t count;
#if defined(_WIN32)
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
} rt_concmap_impl;

//=============================================================================
// Platform abstraction
//=============================================================================

#if defined(_WIN32)
#define CM_LOCK(cm) EnterCriticalSection(&(cm)->mutex)
#define CM_UNLOCK(cm) LeaveCriticalSection(&(cm)->mutex)
#else
#define CM_LOCK(cm) pthread_mutex_lock(&(cm)->mutex)
#define CM_UNLOCK(cm) pthread_mutex_unlock(&(cm)->mutex)
#endif

//=============================================================================
// Internal helpers
//=============================================================================

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

static cm_entry *find_entry(cm_entry *head, const char *key, size_t key_len)
{
    cm_entry *e = head;
    while (e)
    {
        if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
            return e;
        e = e->next;
    }
    return NULL;
}

static void free_entry(cm_entry *e)
{
    if (e)
    {
        free(e->key);
        rt_obj_release_check0(e->value);
        free(e);
    }
}

static void cm_resize(rt_concmap_impl *cm)
{
    size_t new_cap = cm->capacity * 2;
    cm_entry **new_buckets = (cm_entry **)calloc(new_cap, sizeof(cm_entry *));
    if (!new_buckets)
        return;

    for (size_t i = 0; i < cm->capacity; i++)
    {
        cm_entry *e = cm->buckets[i];
        while (e)
        {
            cm_entry *next = e->next;
            uint64_t h = rt_fnv1a(e->key, e->key_len);
            size_t idx = (size_t)(h % new_cap);
            e->next = new_buckets[idx];
            new_buckets[idx] = e;
            e = next;
        }
    }
    free(cm->buckets);
    cm->buckets = new_buckets;
    cm->capacity = new_cap;
}

static void maybe_resize(rt_concmap_impl *cm)
{
    if (cm->count * CM_LOAD_FACTOR_DEN > cm->capacity * CM_LOAD_FACTOR_NUM)
    {
        cm_resize(cm);
    }
}

static void cm_clear_unlocked(rt_concmap_impl *cm)
{
    for (size_t i = 0; i < cm->capacity; i++)
    {
        cm_entry *e = cm->buckets[i];
        while (e)
        {
            cm_entry *next = e->next;
            free_entry(e);
            e = next;
        }
        cm->buckets[i] = NULL;
    }
    cm->count = 0;
}

static void cm_finalizer(void *obj)
{
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    CM_LOCK(cm);
    cm_clear_unlocked(cm);
    free(cm->buckets);
    cm->buckets = NULL;
    CM_UNLOCK(cm);

#if defined(_WIN32)
    DeleteCriticalSection(&cm->mutex);
#else
    pthread_mutex_destroy(&cm->mutex);
#endif
}

//=============================================================================
// Public API
//=============================================================================

void *rt_concmap_new(void)
{
    rt_concmap_impl *cm = (rt_concmap_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_concmap_impl));
    if (!cm)
    {
        rt_trap("ConcurrentMap: memory allocation failed");
        return NULL;
    }
    cm->vptr = NULL;
    cm->capacity = CM_INITIAL_CAPACITY;
    cm->count = 0;
    cm->buckets = (cm_entry **)calloc(CM_INITIAL_CAPACITY, sizeof(cm_entry *));
    if (!cm->buckets)
    {
        rt_trap("ConcurrentMap: memory allocation failed");
        return NULL;
    }

#if defined(_WIN32)
    InitializeCriticalSection(&cm->mutex);
#else
    pthread_mutex_init(&cm->mutex, NULL);
#endif

    rt_obj_set_finalizer(cm, cm_finalizer);
    return cm;
}

int64_t rt_concmap_len(void *obj)
{
    if (!obj)
        return 0;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    CM_LOCK(cm);
    int64_t len = (int64_t)cm->count;
    CM_UNLOCK(cm);
    return len;
}

int8_t rt_concmap_is_empty(void *obj)
{
    return rt_concmap_len(obj) == 0 ? 1 : 0;
}

void rt_concmap_set(void *obj, rt_string key, void *value)
{
    if (!obj)
        return;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    size_t key_len = 0;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t h = rt_fnv1a(key_data, key_len);

    CM_LOCK(cm);

    size_t idx = (size_t)(h % cm->capacity);
    cm_entry *existing = find_entry(cm->buckets[idx], key_data, key_len);

    if (existing)
    {
        /* Update existing entry. */
        rt_obj_retain_maybe(value);
        rt_obj_release_check0(existing->value);
        existing->value = value;
        CM_UNLOCK(cm);
        return;
    }

    /* Insert new entry. */
    cm_entry *e = (cm_entry *)malloc(sizeof(cm_entry));
    if (!e)
    {
        CM_UNLOCK(cm);
        rt_trap("ConcurrentMap.Set: memory allocation failed");
        return;
    }
    e->key = (char *)malloc(key_len + 1);
    if (!e->key)
    {
        free(e);
        CM_UNLOCK(cm);
        rt_trap("ConcurrentMap.Set: memory allocation failed");
        return;
    }
    memcpy(e->key, key_data, key_len);
    e->key[key_len] = '\0';
    e->key_len = key_len;
    e->value = value;
    rt_obj_retain_maybe(value);
    e->next = cm->buckets[idx];
    cm->buckets[idx] = e;
    cm->count++;

    maybe_resize(cm);
    CM_UNLOCK(cm);
}

void *rt_concmap_get(void *obj, rt_string key)
{
    if (!obj)
        return NULL;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    size_t key_len = 0;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t h = rt_fnv1a(key_data, key_len);

    CM_LOCK(cm);
    size_t idx = (size_t)(h % cm->capacity);
    cm_entry *e = find_entry(cm->buckets[idx], key_data, key_len);
    void *result = e ? e->value : NULL;
    CM_UNLOCK(cm);
    return result;
}

void *rt_concmap_get_or(void *obj, rt_string key, void *default_value)
{
    if (!obj)
        return default_value;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    size_t key_len = 0;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t h = rt_fnv1a(key_data, key_len);

    CM_LOCK(cm);
    size_t idx = (size_t)(h % cm->capacity);
    cm_entry *e = find_entry(cm->buckets[idx], key_data, key_len);
    void *result = e ? e->value : default_value;
    CM_UNLOCK(cm);
    return result;
}

int8_t rt_concmap_has(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    size_t key_len = 0;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t h = rt_fnv1a(key_data, key_len);

    CM_LOCK(cm);
    size_t idx = (size_t)(h % cm->capacity);
    cm_entry *e = find_entry(cm->buckets[idx], key_data, key_len);
    int8_t found = e ? 1 : 0;
    CM_UNLOCK(cm);
    return found;
}

int8_t rt_concmap_set_if_missing(void *obj, rt_string key, void *value)
{
    if (!obj)
        return 0;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    size_t key_len = 0;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t h = rt_fnv1a(key_data, key_len);

    CM_LOCK(cm);

    size_t idx = (size_t)(h % cm->capacity);
    cm_entry *existing = find_entry(cm->buckets[idx], key_data, key_len);
    if (existing)
    {
        CM_UNLOCK(cm);
        return 0;
    }

    cm_entry *e = (cm_entry *)malloc(sizeof(cm_entry));
    if (!e)
    {
        CM_UNLOCK(cm);
        rt_trap("ConcurrentMap.SetIfMissing: memory allocation failed");
        return 0;
    }
    e->key = (char *)malloc(key_len + 1);
    if (!e->key)
    {
        free(e);
        CM_UNLOCK(cm);
        rt_trap("ConcurrentMap.SetIfMissing: memory allocation failed");
        return 0;
    }
    memcpy(e->key, key_data, key_len);
    e->key[key_len] = '\0';
    e->key_len = key_len;
    e->value = value;
    rt_obj_retain_maybe(value);
    e->next = cm->buckets[idx];
    cm->buckets[idx] = e;
    cm->count++;

    maybe_resize(cm);
    CM_UNLOCK(cm);
    return 1;
}

int8_t rt_concmap_remove(void *obj, rt_string key)
{
    if (!obj)
        return 0;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    size_t key_len = 0;
    const char *key_data = get_key_data(key, &key_len);
    uint64_t h = rt_fnv1a(key_data, key_len);

    CM_LOCK(cm);
    size_t idx = (size_t)(h % cm->capacity);
    cm_entry **prev = &cm->buckets[idx];
    cm_entry *e = cm->buckets[idx];

    while (e)
    {
        if (e->key_len == key_len && memcmp(e->key, key_data, key_len) == 0)
        {
            *prev = e->next;
            free_entry(e);
            cm->count--;
            CM_UNLOCK(cm);
            return 1;
        }
        prev = &e->next;
        e = e->next;
    }

    CM_UNLOCK(cm);
    return 0;
}

void rt_concmap_clear(void *obj)
{
    if (!obj)
        return;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;
    CM_LOCK(cm);
    cm_clear_unlocked(cm);
    CM_UNLOCK(cm);
}

void *rt_concmap_keys(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;

    CM_LOCK(cm);
    for (size_t i = 0; i < cm->capacity; i++)
    {
        cm_entry *e = cm->buckets[i];
        while (e)
        {
            rt_string s = rt_string_from_bytes(e->key, e->key_len);
            rt_seq_push(seq, (void *)s);
            e = e->next;
        }
    }
    CM_UNLOCK(cm);
    return seq;
}

void *rt_concmap_values(void *obj)
{
    void *seq = rt_seq_new();
    if (!obj)
        return seq;
    rt_concmap_impl *cm = (rt_concmap_impl *)obj;

    CM_LOCK(cm);
    for (size_t i = 0; i < cm->capacity; i++)
    {
        cm_entry *e = cm->buckets[i];
        while (e)
        {
            rt_seq_push(seq, e->value);
            e = e->next;
        }
    }
    CM_UNLOCK(cm);
    return seq;
}
