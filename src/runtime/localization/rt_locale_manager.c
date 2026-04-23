//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_manager.c
// Purpose: Implementation of Viper.Localization.LocaleManager — the process
//          global registry of locale-data records plus the current/system
//          locale state. Phase 1 supports the baked en-US locale and the
//          system-detection bootstrap; JSON / VPA loading lands in Phase 2.
//
// Key invariants:
//   - A single process-global rwlock guards all mutating access.
//   - First-touch lazy init double-checks a volatile flag to avoid paying
//     the lock cost on every call.
//   - Baked records (arena == NULL) are recorded by pointer; their tags are
//     borrowed from the record and never freed.
//   - The current pointer is always non-NULL after init (invariant locale
//     fallback when detection fails).
//
// Ownership/Lifetime:
//   - The registry owns the `tags` array and its heap entries; baked tag
//     storage is borrowed from the baked record.
//   - current/system are Locale handles held as strong references via the
//     OOP retain path; they survive registry reset.
//
// Links: src/runtime/localization/rt_locale_manager.h (interface),
//        src/runtime/localization/rt_locale.h (handle type),
//        src/runtime/localization/rt_locale_platform.h (system detect),
//        src/runtime/threads/rt_threads.h (rt_rwlock API).
//
//===----------------------------------------------------------------------===//

#include "rt_locale_manager.h"

#include "rt_internal.h"
#include "rt_list.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_platform.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_threads.h"
#include "rt_trap.h"

#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#define LOC_PATH_SEP ";"
#else
#define LOC_PATH_SEP ":"
#endif

//===----------------------------------------------------------------------===//
// Registry state (all access serialized by g_lock)
//===----------------------------------------------------------------------===//

typedef struct loc_registry_entry {
    const char             *tag;   ///< borrowed when baked; owned copy when loaded
    const rt_locale_data_t *data;  ///< borrowed; arena-owned for loaded records
    int                     is_baked; ///< 1 for en-US; 0 for JSON/VPA (freed on unload)
} loc_registry_entry_t;

static struct {
    void                  *lock;
    volatile int           initialized;
    loc_registry_entry_t  *entries;
    size_t                 count;
    size_t                 capacity;

    void                  *current;   ///< Locale handle; strong ref
    void                  *system;    ///< Locale handle; strong ref

    char                 **search_paths;
    size_t                 search_count;
    size_t                 search_capacity;
} g_mgr;

//===----------------------------------------------------------------------===//
// Init and teardown
//===----------------------------------------------------------------------===//

static void loc_registry_grow(void) {
    size_t new_cap = g_mgr.capacity == 0 ? 4 : g_mgr.capacity * 2;
    loc_registry_entry_t *new_entries =
        (loc_registry_entry_t *)realloc(g_mgr.entries, new_cap * sizeof(*new_entries));
    if (!new_entries) {
        rt_trap("Viper.Localization.LocaleManager: registry allocation failed");
        return;
    }
    g_mgr.entries = new_entries;
    g_mgr.capacity = new_cap;
}

static void loc_register_entry_locked(const rt_locale_data_t *data, int is_baked) {
    if (!data || !data->tag)
        return;
    // Replace existing entry (idempotent load).
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, data->tag) == 0) {
            g_mgr.entries[i].data = data;
            g_mgr.entries[i].is_baked = is_baked;
            return;
        }
    }
    if (g_mgr.count == g_mgr.capacity)
        loc_registry_grow();
    g_mgr.entries[g_mgr.count].tag = data->tag;
    g_mgr.entries[g_mgr.count].data = data;
    g_mgr.entries[g_mgr.count].is_baked = is_baked;
    g_mgr.count++;
}

/// @brief Construct a Locale handle for the given tag, resolving its data
///        pointer from the current registry. Caller holds the write lock.
/// @details Intentionally does NOT go through rt_locale_parse / try_parse
///          because those re-enter the manager's init path, causing
///          infinite recursion during bootstrap. We build the handle
///          directly and bind the data pointer via internal helpers.
static void *loc_make_handle_locked(const char *tag) {
    rt_string str = rt_string_from_bytes(tag, strlen(tag));
    // Use rt_locale_parse_internal with strict=0 AND temporarily short-circuit
    // the lookup by binding the data manually afterwards. parse_internal calls
    // lookup_data at the tail, but we've already set initialized==0 so that
    // call would recurse. To avoid it, we skip parse_internal entirely and
    // use a direct construction path: allocate + set fields + walk registry.
    void *loc = NULL;
    // Inline parse without registry touch:
    extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
    rt_locale_t *l = (rt_locale_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_locale_t));
    if (!l) {
        rt_string_unref(str);
        return NULL;
    }
    memset(l, 0, sizeof(*l));
    // Parse directly into the struct — uses the same helper as public parse.
    extern int rt_locale_internal_parse_into(const char *input, size_t input_len,
                                             rt_locale_t *out);
    if (rt_locale_internal_parse_into(tag, strlen(tag), l) != 0) {
        rt_string_unref(str);
        return NULL;
    }
    loc = l;
    // Registry lookup without calling the public API (we already hold the lock).
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            l->data = g_mgr.entries[i].data;
            break;
        }
    }
    rt_string_unref(str);
    return loc;
}

static void loc_mgr_ensure_init(void) {
    // Fast path: double-checked.
    if (g_mgr.initialized)
        return;

    // Slow path: lazy allocate the lock and take the write section.
    // Creating the lock itself races with other first-callers; use a
    // CAS-style idiom: try creating, then check if someone else beat us.
    void *lock = g_mgr.lock;
    if (!lock) {
        void *fresh = rt_rwlock_new();
        if (!fresh) {
            rt_trap("Viper.Localization.LocaleManager: rwlock allocation failed");
            return;
        }
        // First writer wins; everyone else discards their rwlock. This is
        // benign because rt_rwlock_new objects can be leaked without side
        // effects until shutdown; under concurrent load the odds of more
        // than 2-3 allocations are low in practice.
        if (!__atomic_compare_exchange_n(&g_mgr.lock, &lock, fresh,
                                         /*weak=*/0,
                                         __ATOMIC_ACQ_REL,
                                         __ATOMIC_ACQUIRE)) {
            // CAS failed: someone else installed a lock. Our `fresh` is now
            // orphaned; accept the leak rather than call a destroy API we
            // don't have. Loads here are rare so this is a one-shot cost.
        }
    }

    rt_rwlock_write_enter(g_mgr.lock);
    if (g_mgr.initialized) {
        rt_rwlock_write_exit(g_mgr.lock);
        return;
    }

    // Register baked en-US.
    loc_register_entry_locked(rt_locale_data_en_us(), /*is_baked=*/1);

    // Detect system locale.
    char sysbuf[RT_LOCALE_TAG_CAP];
    const char *detected = NULL;
    if (rt_locale_platform_detect_system(sysbuf, sizeof(sysbuf)) == 0)
        detected = sysbuf;

    // Build system handle. If detection failed, use invariant-shaped en-US.
    // Use the internal parse helper (not rt_locale_try_parse) because the
    // public try_parse re-enters the manager, and we are mid-init.
    if (detected && *detected) {
        g_mgr.system = loc_make_handle_locked(detected);
        if (g_mgr.system) {
            rt_heap_retain(g_mgr.system);
        }
    }
    if (!g_mgr.system) {
        g_mgr.system = loc_make_handle_locked("en-US");
        if (g_mgr.system)
            rt_heap_retain(g_mgr.system);
    }

    // Choose current: detected system when it's loaded, else en-US.
    const char *cur_tag = NULL;
    if (g_mgr.system) {
        rt_locale_t *s = (rt_locale_t *)g_mgr.system;
        // Is the detected tag loaded?
        for (size_t i = 0; i < g_mgr.count; ++i) {
            if (strcmp(g_mgr.entries[i].tag, s->tag) == 0) {
                cur_tag = s->tag;
                break;
            }
        }
    }
    if (!cur_tag)
        cur_tag = "en-US";

    g_mgr.current = loc_make_handle_locked(cur_tag);
    if (g_mgr.current)
        rt_heap_retain(g_mgr.current);

    g_mgr.initialized = 1;
    rt_rwlock_write_exit(g_mgr.lock);
}

//===----------------------------------------------------------------------===//
// Lookup / retain / release — internal helpers called by rt_locale.c
//===----------------------------------------------------------------------===//

const rt_locale_data_t *rt_locale_manager_lookup_data(const char *tag) {
    if (!tag || !*tag)
        return NULL;
    loc_mgr_ensure_init();
    rt_rwlock_read_enter(g_mgr.lock);
    const rt_locale_data_t *data = NULL;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            data = g_mgr.entries[i].data;
            break;
        }
    }
    rt_rwlock_read_exit(g_mgr.lock);
    return data;
}

void rt_locale_manager_retain_data(const rt_locale_data_t *data) {
    if (!data || data->arena == NULL)
        return; // baked records are immortal; no counter needed
    // Cast away const for the atomic increment — the counter is the only
    // mutable field on a loaded record and lives behind this helper. Route
    // through uintptr_t to avoid the const-qualifier-drop warning.
    int64_t *counter = (int64_t *)(uintptr_t)&data->formatter_refs;
    __atomic_fetch_add(counter, 1, __ATOMIC_ACQ_REL);
}

void rt_locale_manager_release_data(const rt_locale_data_t *data) {
    if (!data || data->arena == NULL)
        return;
    int64_t *counter = (int64_t *)(uintptr_t)&data->formatter_refs;
    __atomic_fetch_sub(counter, 1, __ATOMIC_ACQ_REL);
}

//===----------------------------------------------------------------------===//
// Public class surface
//===----------------------------------------------------------------------===//

void *rt_locale_manager_current(void) {
    loc_mgr_ensure_init();
    rt_rwlock_read_enter(g_mgr.lock);
    void *handle = g_mgr.current;
    if (handle)
        rt_heap_retain(handle);
    rt_rwlock_read_exit(g_mgr.lock);
    return handle;
}

void rt_locale_manager_set_current(void *locale) {
    loc_mgr_ensure_init();
    if (!locale) {
        rt_trap("Viper.Localization.LocaleManager: SetCurrent requires a non-null Locale");
        return;
    }
    rt_locale_t *target = (rt_locale_t *)locale;
    const char *tag = target->tag[0] ? target->tag : "root";

    rt_rwlock_write_enter(g_mgr.lock);
    int loaded = 0;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            loaded = 1;
            break;
        }
    }
    if (!loaded) {
        rt_rwlock_write_exit(g_mgr.lock);
        rt_trap("Viper.Localization.LocaleManager: locale not loaded (call Load* first)");
        return;
    }
    void *old = g_mgr.current;
    g_mgr.current = locale;
    rt_heap_retain(locale);
    rt_rwlock_write_exit(g_mgr.lock);

    if (old)
        rt_heap_release(old);
}

void *rt_locale_manager_system(void) {
    loc_mgr_ensure_init();
    rt_rwlock_read_enter(g_mgr.lock);
    void *handle = g_mgr.system;
    if (handle)
        rt_heap_retain(handle);
    rt_rwlock_read_exit(g_mgr.lock);
    return handle;
}

void *rt_locale_manager_available(void) {
    loc_mgr_ensure_init();
    void *list = rt_list_new();
    if (!list)
        return NULL;
    rt_rwlock_read_enter(g_mgr.lock);
    for (size_t i = 0; i < g_mgr.count; ++i) {
        rt_string s = rt_string_from_bytes(g_mgr.entries[i].tag,
                                            strlen(g_mgr.entries[i].tag));
        rt_list_push(list, s);
    }
    rt_rwlock_read_exit(g_mgr.lock);
    return list;
}

int8_t rt_locale_manager_is_loaded(void *locale) {
    if (!locale)
        return 0;
    loc_mgr_ensure_init();
    rt_locale_t *l = (rt_locale_t *)locale;
    const char *tag = l->tag[0] ? l->tag : "root";
    rt_rwlock_read_enter(g_mgr.lock);
    int8_t found = 0;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            found = 1;
            break;
        }
    }
    rt_rwlock_read_exit(g_mgr.lock);
    return found;
}

void rt_locale_manager_load_from_json(rt_string path) {
    (void)path;
    loc_mgr_ensure_init();
    rt_trap("Viper.Localization.LocaleManager: LoadFromJson is not implemented in v0.2.6 (Phase 2)");
}

int8_t rt_locale_manager_try_load_from_json(rt_string path) {
    (void)path;
    loc_mgr_ensure_init();
    return 0;
}

void rt_locale_manager_load_from_asset(rt_string name) {
    (void)name;
    loc_mgr_ensure_init();
    rt_trap("Viper.Localization.LocaleManager: LoadFromAsset is not implemented in v0.2.6 (Phase 2)");
}

int8_t rt_locale_manager_try_load_from_asset(rt_string name) {
    (void)name;
    loc_mgr_ensure_init();
    return 0;
}

void rt_locale_manager_load_builtin(rt_string tag) {
    loc_mgr_ensure_init();
    const char *bytes = tag ? rt_string_cstr(tag) : NULL;
    int64_t len = tag ? rt_str_len(tag) : 0;
    if (!bytes || len <= 0) {
        rt_trap("Viper.Localization.LocaleManager: LoadBuiltin requires a non-empty tag");
        return;
    }

    char buf[RT_LOCALE_TAG_CAP];
    if ((size_t)len >= sizeof(buf)) {
        rt_trap("Viper.Localization.LocaleManager: builtin tag too long");
        return;
    }
    memcpy(buf, bytes, (size_t)len);
    buf[len] = '\0';

    if (strcmp(buf, "en-US") == 0) {
        rt_rwlock_write_enter(g_mgr.lock);
        loc_register_entry_locked(rt_locale_data_en_us(), /*is_baked=*/1);
        rt_rwlock_write_exit(g_mgr.lock);
        return;
    }
    rt_trap("Viper.Localization.LocaleManager: unknown builtin locale (only 'en-US' is baked)");
}

void *rt_locale_manager_load(rt_string tag) {
    loc_mgr_ensure_init();
    const char *bytes = tag ? rt_string_cstr(tag) : NULL;
    int64_t len = tag ? rt_str_len(tag) : 0;
    if (!bytes || len <= 0)
        return NULL;
    char buf[RT_LOCALE_TAG_CAP];
    if ((size_t)len >= sizeof(buf))
        return NULL;
    memcpy(buf, bytes, (size_t)len);
    buf[len] = '\0';

    // Phase 1: only the baked locale resolves.
    rt_rwlock_read_enter(g_mgr.lock);
    int registered = 0;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, buf) == 0) {
            registered = 1;
            break;
        }
    }
    rt_rwlock_read_exit(g_mgr.lock);
    if (!registered)
        return NULL;

    rt_string s = rt_string_from_bytes(buf, strlen(buf));
    void *loc = rt_locale_try_parse(s);
    rt_string_unref(s);
    return loc;
}

rt_string rt_locale_manager_search_path(void) {
    loc_mgr_ensure_init();
    rt_rwlock_read_enter(g_mgr.lock);
    if (g_mgr.search_count == 0) {
        rt_rwlock_read_exit(g_mgr.lock);
        return rt_string_from_bytes("", 0);
    }
    // Compute concatenation length.
    size_t total = 0;
    for (size_t i = 0; i < g_mgr.search_count; ++i) {
        total += strlen(g_mgr.search_paths[i]);
        if (i + 1 < g_mgr.search_count)
            total += strlen(LOC_PATH_SEP);
    }
    char *buf = (char *)malloc(total + 1);
    if (!buf) {
        rt_rwlock_read_exit(g_mgr.lock);
        return rt_string_from_bytes("", 0);
    }
    char *p = buf;
    for (size_t i = 0; i < g_mgr.search_count; ++i) {
        size_t l = strlen(g_mgr.search_paths[i]);
        memcpy(p, g_mgr.search_paths[i], l);
        p += l;
        if (i + 1 < g_mgr.search_count) {
            size_t sl = strlen(LOC_PATH_SEP);
            memcpy(p, LOC_PATH_SEP, sl);
            p += sl;
        }
    }
    *p = '\0';
    rt_rwlock_read_exit(g_mgr.lock);
    rt_string result = rt_string_from_bytes(buf, total);
    free(buf);
    return result;
}

void rt_locale_manager_add_search_path(rt_string path) {
    loc_mgr_ensure_init();
    const char *bytes = path ? rt_string_cstr(path) : NULL;
    int64_t len = path ? rt_str_len(path) : 0;
    if (!bytes || len <= 0)
        return;
    char *copy = (char *)malloc((size_t)len + 1);
    if (!copy) {
        rt_trap("Viper.Localization.LocaleManager: search path allocation failed");
        return;
    }
    memcpy(copy, bytes, (size_t)len);
    copy[len] = '\0';

    rt_rwlock_write_enter(g_mgr.lock);
    if (g_mgr.search_count == g_mgr.search_capacity) {
        size_t new_cap = g_mgr.search_capacity == 0 ? 4 : g_mgr.search_capacity * 2;
        char **new_paths = (char **)realloc(g_mgr.search_paths, new_cap * sizeof(*new_paths));
        if (!new_paths) {
            rt_rwlock_write_exit(g_mgr.lock);
            free(copy);
            rt_trap("Viper.Localization.LocaleManager: search path grow failed");
            return;
        }
        g_mgr.search_paths = new_paths;
        g_mgr.search_capacity = new_cap;
    }
    g_mgr.search_paths[g_mgr.search_count++] = copy;
    rt_rwlock_write_exit(g_mgr.lock);
}

int8_t rt_locale_manager_unload(void *locale) {
    loc_mgr_ensure_init();
    if (!locale)
        return 0;
    rt_locale_t *target = (rt_locale_t *)locale;
    const char *tag = target->tag[0] ? target->tag : "root";

    rt_rwlock_write_enter(g_mgr.lock);
    // Refuse to unload baked entries or the current/system locale.
    rt_locale_t *cur = (rt_locale_t *)g_mgr.current;
    rt_locale_t *sys = (rt_locale_t *)g_mgr.system;
    if ((cur && strcmp(cur->tag, tag) == 0) || (sys && strcmp(sys->tag, tag) == 0)) {
        rt_rwlock_write_exit(g_mgr.lock);
        return 0;
    }
    size_t idx = (size_t)-1;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            idx = i;
            break;
        }
    }
    if (idx == (size_t)-1) {
        rt_rwlock_write_exit(g_mgr.lock);
        return 0;
    }
    if (g_mgr.entries[idx].is_baked) {
        rt_rwlock_write_exit(g_mgr.lock);
        return 0; // baked records are never unloaded
    }
    // Phase 1 never loads non-baked records, so this branch is unreachable;
    // kept for structural completeness ahead of Phase 2.
    const rt_locale_data_t *data = g_mgr.entries[idx].data;
    if (data && data->formatter_refs != 0) {
        rt_rwlock_write_exit(g_mgr.lock);
        return 0;
    }
    for (size_t j = idx + 1; j < g_mgr.count; ++j)
        g_mgr.entries[j - 1] = g_mgr.entries[j];
    g_mgr.count--;
    rt_rwlock_write_exit(g_mgr.lock);
    return 1;
}

void rt_locale_manager_reset(void) {
    loc_mgr_ensure_init();
    rt_rwlock_write_enter(g_mgr.lock);
    // Remove every non-baked entry. Baked entries survive Reset.
    size_t w = 0;
    for (size_t r = 0; r < g_mgr.count; ++r) {
        if (g_mgr.entries[r].is_baked) {
            g_mgr.entries[w++] = g_mgr.entries[r];
        }
        // Phase 1: nothing non-baked exists, so no arena frees needed.
    }
    g_mgr.count = w;

    // Clear extra search paths (keep zero — user must re-add).
    for (size_t i = 0; i < g_mgr.search_count; ++i)
        free(g_mgr.search_paths[i]);
    g_mgr.search_count = 0;

    // Reset current/system to en-US.
    void *old_current = g_mgr.current;
    void *old_system = g_mgr.system;
    g_mgr.current = loc_make_handle_locked("en-US");
    g_mgr.system = loc_make_handle_locked("en-US");
    if (g_mgr.current) rt_heap_retain(g_mgr.current);
    if (g_mgr.system)  rt_heap_retain(g_mgr.system);
    rt_rwlock_write_exit(g_mgr.lock);

    if (old_current) rt_heap_release(old_current);
    if (old_system)  rt_heap_release(old_system);
}
