//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_message_bundle.c
// Purpose: Implementation of Viper.Localization.MessageBundle. Stores the
//          translation table as an rt_map<rt_string, rt_string>, walks the
//          fallback chain on lookup, and expands {name}-style placeholder
//          templates via a small purpose-built interpolator (the template
//          engine at rt_template.c is broader than we need here).
//
// Key invariants:
//   - Fallback chain cycles are rejected on SetFallback with an explicit
//     trap message. Max chain depth 16 is enforced on every lookup.
//   - Plural key resolution looks up "<key>.<category>" then falls through
//     to "<key>.other"; trapping only when neither exists.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//   - The internal rt_map retains its entries; freeing the bundle releases
//     the map handle which decrements the entries' refcounts.
//
// Links: src/runtime/localization/rt_message_bundle.h (interface),
//        src/runtime/localization/rt_plural_rules.h (category evaluator),
//        src/runtime/collections/rt_map.h (entry storage).
//
//===----------------------------------------------------------------------===//

#include "rt_message_bundle.h"

#include "rt_asset.h"
#include "rt_bytes.h"
#include "rt_collection_ids.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_list.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_manager.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_plural_rules.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Forward declarations for optional external helpers
//===----------------------------------------------------------------------===//

extern rt_string rt_io_file_read_all_text(rt_string path);
extern void *rt_json_parse_object(rt_string text);

//===----------------------------------------------------------------------===//
// Instance struct
//===----------------------------------------------------------------------===//

#define RT_MSG_BUNDLE_MAX_DEPTH 16

typedef struct rt_message_bundle {
    void *locale;    ///< Locale handle; strong ref through GC
    const rt_locale_data_t *data; ///< retained locale data for plural lookup
    void *entries;   ///< rt_map<rt_string, rt_string>
    void *fallback;  ///< optional bundle to consult on miss
} rt_message_bundle_t;

static rt_message_bundle_t *as_bundle(void *obj) {
    return (rt_message_bundle_t *)obj;
}

static void release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int is_map_object(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_MAP_CLASS_ID;
}

static int is_list_like_object(void *obj) {
    if (!obj)
        return 0;
    if (rt_string_is_handle(obj))
        return 0;
    int64_t cid = rt_obj_class_id(obj);
    return cid == RT_LIST_CLASS_ID;
}

static void bundle_finalizer(void *obj) {
    rt_message_bundle_t *bundle = (rt_message_bundle_t *)obj;
    if (!bundle)
        return;
    rt_locale_manager_release_data(bundle->data);
    release_object(bundle->locale);
    release_object(bundle->entries);
    release_object(bundle->fallback);
    bundle->locale = NULL;
    bundle->data = NULL;
    bundle->entries = NULL;
    bundle->fallback = NULL;
}

static int validate_message_map(void *map) {
    if (!map)
        return 0;
    void *keys = rt_map_keys(map);
    int ok = 1;
    int64_t n = keys ? rt_seq_len(keys) : 0;
    for (int64_t i = 0; i < n; ++i) {
        rt_string key = (rt_string)rt_seq_get(keys, i);
        void *value = rt_map_get(map, key);
        if (!value || !rt_string_is_handle(value)) {
            ok = 0;
            break;
        }
    }
    release_object(keys);
    return ok;
}

static int validate_string_list(void *list) {
    if (!list)
        return 1;
    int64_t n = rt_list_len(list);
    for (int64_t i = 0; i < n; ++i) {
        void *value = rt_list_get(list, i);
        int ok = value && rt_string_is_handle(value);
        release_object(value);
        if (!ok)
            return 0;
    }
    return 1;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

static void *bundle_alloc(void *locale, void *map, int take_map) {
    rt_message_bundle_t *bundle = (rt_message_bundle_t *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_message_bundle_t));
    if (!bundle) {
        rt_trap("Viper.Localization.MessageBundle: allocation failed");
        return NULL;
    }
    bundle->locale = locale;
    if (bundle->locale)
        rt_heap_retain(bundle->locale);
    bundle->data = rt_locale_get_data(locale);
    rt_locale_manager_retain_data(bundle->data);
    bundle->entries = map ? map : rt_map_new();
    if (bundle->entries && !take_map)
        rt_obj_retain_maybe(bundle->entries);
    bundle->fallback = NULL;
    rt_obj_set_finalizer(bundle, bundle_finalizer);
    return bundle;
}

void *rt_message_bundle_new(void) {
    void *current = rt_locale_manager_current();
    void *bundle = bundle_alloc(current, NULL, 1);
    release_object(current);
    return bundle;
}

void *rt_message_bundle_from_map(void *locale, void *map) {
    if (map && !is_map_object(map)) {
        rt_trap("Viper.Localization.MessageBundle: FromMap requires Map[String, String]");
        return NULL;
    }
    if (map && !validate_message_map(map)) {
        rt_trap("Viper.Localization.MessageBundle: map values must be strings");
        return NULL;
    }
    return bundle_alloc(locale, map, 0);
}

void *rt_message_bundle_load_from_json(void *locale, rt_string path) {
    if (!path) {
        rt_trap("Viper.Localization.MessageBundle: LoadFromJson requires a path");
        return NULL;
    }
    rt_string text = rt_io_file_read_all_text(path);
    if (!text) {
        rt_trap("Viper.Localization.MessageBundle: cannot read JSON file");
        return NULL;
    }
    void *map = rt_json_parse_object(text);
    rt_string_unref(text);
    if (!map) {
        rt_trap("Viper.Localization.MessageBundle: malformed JSON");
        return NULL;
    }
    if (!validate_message_map(map)) {
        release_object(map);
        rt_trap("Viper.Localization.MessageBundle: JSON values must be strings");
        return NULL;
    }
    return bundle_alloc(locale, map, 1);
}

void *rt_message_bundle_load_from_asset(void *locale, rt_string name) {
    if (!name) {
        rt_trap("Viper.Localization.MessageBundle: LoadFromAsset requires a name");
        return NULL;
    }
    void *bytes = rt_asset_load_bytes(name);
    if (!bytes) {
        rt_trap("Viper.Localization.MessageBundle: asset not found");
        return NULL;
    }
    rt_string text = rt_bytes_to_str(bytes);
    release_object(bytes);
    if (!text) {
        rt_trap("Viper.Localization.MessageBundle: asset is not valid text");
        return NULL;
    }
    void *map = rt_json_parse_object(text);
    rt_string_unref(text);
    if (!map) {
        rt_trap("Viper.Localization.MessageBundle: malformed JSON asset");
        return NULL;
    }
    if (!validate_message_map(map)) {
        release_object(map);
        rt_trap("Viper.Localization.MessageBundle: JSON asset values must be strings");
        return NULL;
    }
    return bundle_alloc(locale, map, 1);
}

//===----------------------------------------------------------------------===//
// Property accessors
//===----------------------------------------------------------------------===//

void *rt_message_bundle_get_locale(void *self) {
    return self ? as_bundle(self)->locale : NULL;
}

int64_t rt_message_bundle_get_count(void *self) {
    if (!self) return 0;
    extern int64_t rt_map_len(void *);
    return rt_map_len(as_bundle(self)->entries);
}

//===----------------------------------------------------------------------===//
// Lookup with fallback walk
//===----------------------------------------------------------------------===//

/// @brief Look up @p key in @p self, walking up fallbacks. Returns NULL when
///        no bundle in the chain has a matching key. The returned rt_string
///        is retained (caller owns the reference and must unref when done).
static rt_string bundle_lookup_direct(rt_message_bundle_t *self, rt_string key) {
    if (!self || !key || !rt_map_has(self->entries, key))
        return NULL;
    // rt_map_get_str returns a borrowed reference; retain it so the caller owns
    // a proper reference.
    rt_string v = rt_map_get_str(self->entries, key);
    if (v) rt_string_ref(v);
    return v;
}

static rt_string bundle_lookup_locale_qualified(rt_message_bundle_t *self,
                                                rt_string key) {
    if (!self || !self->locale || !key)
        return NULL;
    const char *key_cs = rt_string_cstr(key);
    int64_t key_len = rt_str_len(key);
    if (!key_cs || key_len <= 0)
        return NULL;

    void *fallbacks = rt_locale_fallbacks(self->locale);
    if (!fallbacks)
        return NULL;
    int64_t n = rt_list_len(fallbacks);
    for (int64_t i = 0; i < n; ++i) {
        void *loc = rt_list_get(fallbacks, i);
        rt_string tag = rt_locale_tag(loc);
        const char *tag_cs = rt_string_cstr(tag);
        int64_t tag_len = rt_str_len(tag);
        rt_string qkey = NULL;
        if (tag_cs && tag_len > 0) {
            size_t needed = (size_t)tag_len + 1 + (size_t)key_len;
            char stack[256];
            char *buf = needed + 1 <= sizeof(stack) ? stack : (char *)malloc(needed + 1);
            if (buf) {
                memcpy(buf, tag_cs, (size_t)tag_len);
                buf[tag_len] = ':';
                memcpy(buf + tag_len + 1, key_cs, (size_t)key_len);
                buf[needed] = '\0';
                qkey = rt_string_from_bytes(buf, needed);
                if (buf != stack) free(buf);
            }
        }
        rt_string_unref(tag);
        release_object(loc);
        if (qkey) {
            rt_string v = bundle_lookup_direct(self, qkey);
            rt_string_unref(qkey);
            if (v) {
                release_object(fallbacks);
                return v;
            }
        }
    }
    release_object(fallbacks);
    return NULL;
}

static rt_string bundle_lookup(rt_message_bundle_t *self, rt_string key, int depth) {
    if (!self || depth >= RT_MSG_BUNDLE_MAX_DEPTH)
        return NULL;
    rt_string v = bundle_lookup_direct(self, key);
    if (v)
        return v;
    v = bundle_lookup_locale_qualified(self, key);
    if (v)
        return v;
    if (self->fallback)
        return bundle_lookup(as_bundle(self->fallback), key, depth + 1);
    return NULL;
}

rt_string rt_message_bundle_get(void *self, rt_string key) {
    if (!self || !key) {
        rt_trap("Viper.Localization.MessageBundle: Get requires a non-null key");
        return rt_string_from_bytes("", 0);
    }
    rt_string r = bundle_lookup(as_bundle(self), key, 0);
    if (!r) {
        rt_trap("Viper.Localization.MessageBundle: missing key (no fallback found)");
        return rt_string_from_bytes("", 0);
    }
    return r;
}

rt_string rt_message_bundle_try_get(void *self, rt_string key) {
    if (!self || !key)
        return rt_string_from_bytes("", 0);
    rt_string r = bundle_lookup(as_bundle(self), key, 0);
    return r ? r : rt_string_from_bytes("", 0);
}

int8_t rt_message_bundle_has(void *self, rt_string key) {
    if (!self || !key) return 0;
    rt_string r = bundle_lookup(as_bundle(self), key, 0);
    if (r) {
        rt_string_unref(r); // bundle_lookup now retains; unref to balance
        return 1;
    }
    return 0;
}

//===----------------------------------------------------------------------===//
// Placeholder interpolation
//===----------------------------------------------------------------------===//

/// @brief Emit the value for placeholder @p name from @p vars into @p sb.
/// @details rt_map_get_str returns a borrowed reference — we MUST NOT unref
///          it. The map retains its entries for its own lifetime; our job
///          is to read the cstr contents and move on.
static int append_value(rt_string_builder *sb, const char *name, size_t name_len,
                        void *vars) {
    if (!vars)
        return 0;
    if (!is_map_object(vars))
        return 0;
    rt_string key = rt_string_from_bytes(name, name_len);
    int found = 0;
    // rt_map_has returns 0 when key is absent; without this guard,
    // rt_map_get_str would return a fresh empty string that we'd leak.
    if (rt_map_has(vars, key)) {
        rt_string value = rt_map_get_str(vars, key);
        if (value) {
            found = 1;
            const char *cs = rt_string_cstr(value);
            int64_t vlen = rt_str_len(value);
            if (cs && vlen > 0)
                (void)rt_sb_append_bytes(sb, cs, (size_t)vlen);
        }
    }
    rt_string_unref(key);
    return found;
}

/// @brief Named-placeholder substitution: scans for `{name}` runs.
static rt_string interp_named(rt_string tmpl, void *vars) {
    if (!tmpl)
        return rt_string_from_bytes("", 0);
    const char *cs = rt_string_cstr(tmpl);
    int64_t len = rt_str_len(tmpl);
    if (!cs || len <= 0)
        return rt_string_from_bytes("", 0);

    rt_string_builder sb;
    rt_sb_init(&sb);
    for (int64_t i = 0; i < len;) {
        if (cs[i] == '{' && i + 1 < len && cs[i + 1] == '{') {
            (void)rt_sb_append_bytes(&sb, "{", 1);
            i += 2;
            continue;
        }
        if (cs[i] == '}' && i + 1 < len && cs[i + 1] == '}') {
            (void)rt_sb_append_bytes(&sb, "}", 1);
            i += 2;
            continue;
        }
        if (cs[i] == '{') {
            // Scan for matching '}'.
            int64_t j = i + 1;
            while (j < len && cs[j] != '}')
                ++j;
            if (j < len && j > i + 1) {
                if (!append_value(&sb, cs + i + 1, (size_t)(j - i - 1), vars))
                    (void)rt_sb_append_bytes(&sb, cs + i, (size_t)(j - i + 1));
                i = j + 1;
                continue;
            }
        }
        (void)rt_sb_append_bytes(&sb, cs + i, 1);
        ++i;
    }
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

/// @brief Positional substitution: `{N}` where N parses as an integer index.
static rt_string interp_positional(rt_string tmpl, void *values_list) {
    if (!tmpl)
        return rt_string_from_bytes("", 0);
    const char *cs = rt_string_cstr(tmpl);
    int64_t len = rt_str_len(tmpl);
    if (!cs || len <= 0)
        return rt_string_from_bytes("", 0);
    int64_t list_len = values_list ? rt_list_len(values_list) : 0;

    rt_string_builder sb;
    rt_sb_init(&sb);
    for (int64_t i = 0; i < len;) {
        if (cs[i] == '{' && i + 1 < len && cs[i + 1] == '{') {
            (void)rt_sb_append_bytes(&sb, "{", 1);
            i += 2;
            continue;
        }
        if (cs[i] == '}' && i + 1 < len && cs[i + 1] == '}') {
            (void)rt_sb_append_bytes(&sb, "}", 1);
            i += 2;
            continue;
        }
        if (cs[i] == '{') {
            int64_t j = i + 1;
            int64_t idx = 0;
            int valid = 0;
            int overflow = 0;
            while (j < len && cs[j] >= '0' && cs[j] <= '9') {
                int digit = cs[j] - '0';
                if (idx > (INT64_MAX - digit) / 10)
                    overflow = 1;
                else
                    idx = idx * 10 + digit;
                valid = 1;
                ++j;
            }
            if (valid && !overflow && j < len && cs[j] == '}') {
                if (values_list && idx >= 0 && idx < list_len) {
                    rt_string v = (rt_string)rt_list_get(values_list, idx);
                    if (v) {
                        const char *vcs = rt_string_cstr(v);
                        int64_t vlen = rt_str_len(v);
                        if (vcs && vlen > 0)
                            (void)rt_sb_append_bytes(&sb, vcs, (size_t)vlen);
                        rt_string_unref(v);
                    }
                } else {
                    (void)rt_sb_append_bytes(&sb, cs + i, (size_t)(j - i + 1));
                }
                i = j + 1;
                continue;
            }
        }
        (void)rt_sb_append_bytes(&sb, cs + i, 1);
        ++i;
    }
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_message_bundle_format(void *self, rt_string key, void *vars) {
    if (vars && !is_map_object(vars)) {
        rt_trap("Viper.Localization.MessageBundle: Format vars must be a Map[String, String]");
        return rt_string_from_bytes("", 0);
    }
    if (vars && !validate_message_map(vars)) {
        rt_trap("Viper.Localization.MessageBundle: Format vars values must be strings");
        return rt_string_from_bytes("", 0);
    }
    rt_string tmpl = rt_message_bundle_get(self, key);
    rt_string r = interp_named(tmpl, vars);
    rt_string_unref(tmpl);
    return r;
}

rt_string rt_message_bundle_format_with(void *self, rt_string key, void *values) {
    if (values && !is_list_like_object(values)) {
        rt_trap("Viper.Localization.MessageBundle: FormatWith values must be a List[String]");
        return rt_string_from_bytes("", 0);
    }
    if (values && !validate_string_list(values)) {
        rt_trap("Viper.Localization.MessageBundle: FormatWith values must contain only strings");
        return rt_string_from_bytes("", 0);
    }
    rt_string tmpl = rt_message_bundle_get(self, key);
    rt_string r = interp_positional(tmpl, values);
    rt_string_unref(tmpl);
    return r;
}

//===----------------------------------------------------------------------===//
// Plural
//===----------------------------------------------------------------------===//

rt_string rt_message_bundle_plural(void *self, rt_string key, int64_t n, void *vars) {
    if (!self || !key) {
        rt_trap("Viper.Localization.MessageBundle: Plural requires a non-null key");
        return rt_string_from_bytes("", 0);
    }
    rt_message_bundle_t *bundle = as_bundle(self);
    if (vars && !is_map_object(vars)) {
        rt_trap("Viper.Localization.MessageBundle: Plural vars must be a Map[String, String]");
        return rt_string_from_bytes("", 0);
    }
    if (vars && !validate_message_map(vars)) {
        rt_trap("Viper.Localization.MessageBundle: Plural vars values must be strings");
        return rt_string_from_bytes("", 0);
    }
    rt_plural_category_t cat = rt_plural_rules_select_cardinal_int(bundle->data, n);
    const char *cat_name = rt_plural_rules_category_name(cat);

    // Build "<key>.<category>".
    const char *key_cs = rt_string_cstr(key);
    int64_t key_len = rt_str_len(key);
    if (!key_cs || key_len <= 0) {
        rt_trap("Viper.Localization.MessageBundle: Plural key is empty");
        return rt_string_from_bytes("", 0);
    }
    size_t cat_len = strlen(cat_name);
    char stack_buf[256];
    size_t needed = (size_t)key_len + 1 + cat_len;
    char *buf = needed + 1 <= sizeof(stack_buf) ? stack_buf : (char *)malloc(needed + 1);
    if (!buf) {
        rt_trap("Viper.Localization.MessageBundle: plural key allocation failed");
        return rt_string_from_bytes("", 0);
    }
    memcpy(buf, key_cs, (size_t)key_len);
    buf[key_len] = '.';
    memcpy(buf + key_len + 1, cat_name, cat_len);
    buf[needed] = '\0';
    rt_string full_key = rt_string_from_bytes(buf, needed);

    rt_string tmpl = bundle_lookup(bundle, full_key, 0);
    rt_string_unref(full_key);

    // Fall through to "<key>.other" if the category-specific key is absent.
    if (!tmpl) {
        size_t other_needed = (size_t)key_len + 1 + 5; // "other"
        char other_stack[256];
        char *other_buf = other_needed + 1 <= sizeof(other_stack)
                              ? other_stack
                              : (char *)malloc(other_needed + 1);
        if (!other_buf) {
            if (buf != stack_buf) free(buf);
            rt_trap("Viper.Localization.MessageBundle: plural fallback allocation failed");
            return rt_string_from_bytes("", 0);
        }
        memcpy(other_buf, key_cs, (size_t)key_len);
        other_buf[key_len] = '.';
        memcpy(other_buf + key_len + 1, "other", 5);
        other_buf[other_needed] = '\0';
        rt_string other_key = rt_string_from_bytes(other_buf, other_needed);
        tmpl = bundle_lookup(bundle, other_key, 0);
        rt_string_unref(other_key);
        if (other_buf != other_stack) free(other_buf);
    }
    if (buf != stack_buf) free(buf);

    if (!tmpl) {
        rt_trap("Viper.Localization.MessageBundle: missing plural key (no 'other' form)");
        return rt_string_from_bytes("", 0);
    }

    // Seed `{n}` into the vars map so it's available to the substitutor.
    void *effective_vars = vars ? rt_map_clone(vars) : rt_map_new();
    char num_buf[32];
    int nlen = snprintf(num_buf, sizeof(num_buf), "%lld", (long long)n);
    if (nlen > 0) {
        rt_string n_key = rt_string_from_bytes("n", 1);
        rt_string n_val = rt_string_from_bytes(num_buf, (size_t)nlen);
        rt_map_set_str(effective_vars, n_key, n_val);
        rt_string_unref(n_key);
        rt_string_unref(n_val);
    }

    rt_string out = interp_named(tmpl, effective_vars);
    release_object(effective_vars);
    rt_string_unref(tmpl);
    return out;
}

//===----------------------------------------------------------------------===//
// Fallback chain
//===----------------------------------------------------------------------===//

void *rt_message_bundle_set_fallback(void *self, void *fallback) {
    if (!self)
        return NULL;
    rt_message_bundle_t *bundle = as_bundle(self);
    // Cycle detection: walk the proposed chain and make sure we don't see self.
    void *cur = fallback;
    int depth = 0;
    while (cur && depth < RT_MSG_BUNDLE_MAX_DEPTH) {
        if (cur == self) {
            rt_trap("Viper.Localization.MessageBundle: Fallback would create a cycle");
            return NULL;
        }
        cur = as_bundle(cur)->fallback;
        ++depth;
    }
    if (bundle->fallback == fallback)
        return self;
    if (fallback)
        rt_obj_retain_maybe(fallback);
    void *old = bundle->fallback;
    bundle->fallback = fallback;
    release_object(old);
    return self;
}

void *rt_message_bundle_keys(void *self) {
    if (!self)
        return rt_list_new();
    // rt_map_keys returns a Seq; the plan specifies Keys() as a List so
    // callers can use the same rt_list_* API as the other localization
    // methods. Walk the Seq and copy keys into a fresh List.
    extern int64_t rt_seq_len(void *);
    extern void *rt_seq_get(void *, int64_t);
    void *seq = rt_map_keys(as_bundle(self)->entries);
    void *list = rt_list_new();
    if (!seq) return list;
    int64_t n = rt_seq_len(seq);
    for (int64_t i = 0; i < n; ++i) {
        rt_list_push(list, rt_seq_get(seq, i));
    }
    release_object(seq);
    return list;
}
