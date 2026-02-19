//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_string_intern.c
// Purpose: Global string interning table — O(1) equality via pointer identity (P2-3.8).
//
// Design:
//   Open-addressing hash table with linear probing.  Power-of-two capacity for
//   fast modular arithmetic via bitwise AND.  Grows when load > 5/8 (62.5%).
//   Strings held by the table are retained; they become effectively immortal
//   (their refcount never reaches zero) until rt_string_intern_drain() is called.
//
//   After interning, two equal strings share the same rt_string pointer, so
//   equality reduces from O(n) memcmp to O(1) pointer comparison.
//
//===----------------------------------------------------------------------===//

#include "rt_string_intern.h"

#include "rt_internal.h" // struct rt_string_impl (data, literal_len fields)
#include "rt_string.h"   // rt_string_ref, rt_string_unref, rt_str_len

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// FNV-1a 64-bit hash
// ============================================================================

static uint64_t hash_bytes(const char *data, size_t len)
{
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++)
    {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// ============================================================================
// Hash table internals
// ============================================================================

/// One slot in the intern table.  Deleted/empty slots have str == NULL.
typedef struct
{
    uint64_t hash; ///< Cached hash to avoid recomputing on probe.
    rt_string str; ///< Retained canonical string; NULL = empty slot.
} InternSlot;

#define INTERN_INIT_CAP 256u ///< Initial capacity (must be a power of two).

static InternSlot *g_slots_ = NULL;
static size_t g_cap_ = 0;
static size_t g_count_ = 0;
static pthread_mutex_t g_lock_ = PTHREAD_MUTEX_INITIALIZER;

/// @brief Grow and rehash the table when load factor exceeds 5/8.
/// @details Called while holding g_lock_.  On allocation failure the table is
///          left at the current (high-load) state; correctness is preserved but
///          performance may degrade.
static void intern_ensure_capacity(void)
{
    // Grow at 5/8 load factor: count*8 >= cap*5.
    if (g_cap_ > 0 && g_count_ * 8 < g_cap_ * 5)
        return;

    size_t new_cap = g_cap_ ? g_cap_ * 2 : INTERN_INIT_CAP;
    InternSlot *new_slots = (InternSlot *)calloc(new_cap, sizeof(InternSlot));
    if (!new_slots)
        return; // out of memory — leave table at high load

    // Rehash all live entries into the new table.
    for (size_t i = 0; i < g_cap_; i++)
    {
        if (!g_slots_[i].str)
            continue;

        size_t slot = (size_t)(g_slots_[i].hash & (new_cap - 1));
        while (new_slots[slot].str)
            slot = (slot + 1) & (new_cap - 1);

        new_slots[slot] = g_slots_[i];
    }

    free(g_slots_);
    g_slots_ = new_slots;
    g_cap_ = new_cap;
}

// ============================================================================
// Public API
// ============================================================================

/// @brief Intern @p s, returning the canonical rt_string for its byte content.
rt_string rt_string_intern(rt_string s)
{
    if (!s)
        return NULL;

    const char *data = s->data;
    size_t len = (size_t)rt_str_len(s);
    uint64_t h = hash_bytes(data, len);

    pthread_mutex_lock(&g_lock_);
    intern_ensure_capacity();

    size_t slot = (size_t)(h & (g_cap_ - 1));
    for (;;)
    {
        InternSlot *e = &g_slots_[slot];

        if (!e->str)
        {
            // Empty slot: insert s as the canonical copy.
            e->hash = h;
            e->str = rt_string_ref(s); // table holds one reference
            g_count_++;

            rt_string result = rt_string_ref(s); // caller's reference
            pthread_mutex_unlock(&g_lock_);
            return result;
        }

        if (e->hash == h)
        {
            size_t entry_len = (size_t)rt_str_len(e->str);
            if (entry_len == len && memcmp(e->str->data, data, len) == 0)
            {
                // Hit: return a retained reference to the canonical string.
                rt_string result = rt_string_ref(e->str);
                pthread_mutex_unlock(&g_lock_);
                return result;
            }
        }

        slot = (slot + 1) & (g_cap_ - 1);
    }
}

/// @brief Release all interned strings and free the table.
void rt_string_intern_drain(void)
{
    pthread_mutex_lock(&g_lock_);

    for (size_t i = 0; i < g_cap_; i++)
    {
        if (g_slots_[i].str)
        {
            rt_string_unref(g_slots_[i].str);
            g_slots_[i].str = NULL;
        }
    }

    free(g_slots_);
    g_slots_ = NULL;
    g_cap_ = 0;
    g_count_ = 0;

    pthread_mutex_unlock(&g_lock_);
}
