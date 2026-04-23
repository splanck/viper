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
//          locale state. Supports the baked en-US locale, system-detection
//          bootstrap, JSON/VPA locale loading, and filesystem search paths.
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
#include "rt_asset.h"
#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_file_ext.h"
#include "rt_heap.h"
#include "rt_json.h"
#include "rt_list.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_platform.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_threads.h"
#include "rt_trap.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
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

typedef struct loc_arena_alloc {
    void *ptr;
    struct loc_arena_alloc *next;
} loc_arena_alloc_t;

typedef struct loc_arena {
    loc_arena_alloc_t *allocs;
} loc_arena_t;

static struct {
    void                  *lock;
    int                    initialized;
    loc_registry_entry_t  *entries;
    size_t                 count;
    size_t                 capacity;

    void                  *current;   ///< Locale handle; strong ref
    void                  *system;    ///< Locale handle; strong ref

    char                 **search_paths;
    size_t                 search_count;
    size_t                 search_capacity;
} g_mgr;

extern int rt_locale_internal_parse_into(const char *input, size_t input_len,
                                         rt_locale_t *out);
extern void rt_locale_internal_finalizer(void *obj);

static int loc_register_entry_locked(const rt_locale_data_t *data, int is_baked);

static int64_t loc_data_ref_count(const rt_locale_data_t *data) {
    if (!data || data->arena == NULL)
        return 0;
    const int64_t *counter = (const int64_t *)(const void *)&data->formatter_refs;
    return __atomic_load_n(counter, __ATOMIC_ACQUIRE);
}

//===----------------------------------------------------------------------===//
// Arena + JSON schema helpers
//===----------------------------------------------------------------------===//

static loc_arena_t *loc_arena_new(void) {
    loc_arena_t *arena = (loc_arena_t *)calloc(1, sizeof(*arena));
    if (!arena)
        rt_trap("Viper.Localization.LocaleManager: locale arena allocation failed");
    return arena;
}

static void *loc_arena_alloc(loc_arena_t *arena, size_t size) {
    if (!arena || size == 0)
        return NULL;
    void *ptr = calloc(1, size);
    loc_arena_alloc_t *node = (loc_arena_alloc_t *)malloc(sizeof(*node));
    if (!ptr || !node) {
        free(ptr);
        free(node);
        rt_trap("Viper.Localization.LocaleManager: locale arena allocation failed");
        return NULL;
    }
    node->ptr = ptr;
    node->next = arena->allocs;
    arena->allocs = node;
    return ptr;
}

static char *loc_arena_strndup(loc_arena_t *arena, const char *s, size_t len) {
    if (!s)
        s = "";
    if (len > 256)
        return NULL;
    char *copy = (char *)loc_arena_alloc(arena, len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

static char *loc_arena_strdup(loc_arena_t *arena, const char *s) {
    return loc_arena_strndup(arena, s ? s : "", strlen(s ? s : ""));
}

static void loc_arena_free(loc_arena_t *arena) {
    if (!arena)
        return;
    loc_arena_alloc_t *node = arena->allocs;
    while (node) {
        loc_arena_alloc_t *next = node->next;
        free(node->ptr);
        free(node);
        node = next;
    }
    free(arena);
}

static void loc_free_loaded_data(const rt_locale_data_t *data) {
    if (!data || !data->arena)
        return;
    loc_arena_free((loc_arena_t *)data->arena);
}

static void loc_release_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int loc_fail(int trap_on_error, const char *msg) {
    if (trap_on_error)
        rt_trap(msg);
    return 0;
}

static int loc_is_string(void *obj) {
    return obj && rt_string_is_handle(obj);
}

static int loc_is_map(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_MAP_CLASS_ID;
}

static int loc_is_seq(void *obj) {
    return obj && rt_obj_class_id(obj) == RT_SEQ_CLASS_ID;
}

static void loc_release_handle(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void *json_get(void *map, const char *key) {
    if (!loc_is_map(map) || !key)
        return NULL;
    rt_string k = rt_string_from_bytes(key, strlen(key));
    void *v = rt_map_get(map, k);
    rt_string_unref(k);
    return v;
}

static const char *json_string_cstr(void *obj, int *ok) {
    if (ok) *ok = 0;
    if (!loc_is_string(obj))
        return NULL;
    const char *s = rt_string_cstr((rt_string)obj);
    if (!s)
        return NULL;
    if (ok) *ok = 1;
    return s;
}

static size_t loc_utf8_cp_len(const char *s) {
    if (!s || !*s) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0 && s[1]) return 2;
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) return 3;
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) return 4;
    return 0;
}

static int loc_digits_are_valid(const char *digits) {
    if (!digits || !*digits)
        return 0;
    const char *p = digits;
    for (int i = 0; i < 10; ++i) {
        size_t n = loc_utf8_cp_len(p);
        if (n == 0)
            return 0;
        p += n;
    }
    return *p == '\0';
}

static int loc_currency_code_valid(const char *code) {
    if (!code || strlen(code) != 3)
        return 0;
    return code[0] >= 'A' && code[0] <= 'Z' &&
           code[1] >= 'A' && code[1] <= 'Z' &&
           code[2] >= 'A' && code[2] <= 'Z';
}

static int loc_currency_pattern_valid(const char *pattern) {
    if (!pattern || !*pattern)
        return 0;
    int saw_n = 0;
    int saw_s = 0;
    for (const char *p = pattern; *p; ++p) {
        if (p[0] == '{') {
            if (p[1] == 'n' && p[2] == '}') {
                saw_n = 1;
                p += 2;
            } else if (p[1] == 's' && p[2] == '}') {
                saw_s = 1;
                p += 2;
            } else {
                return 0;
            }
        } else if (p[0] == '}') {
            return 0;
        }
    }
    return saw_n && saw_s;
}

static char *json_dup_string(loc_arena_t *arena, void *map, const char *key,
                             const char *fallback, int required,
                             int trap_on_error, int *ok) {
    void *v = json_get(map, key);
    if (!v) {
        if (required) {
            if (ok) *ok = 0;
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON missing string field");
            return NULL;
        }
        return loc_arena_strdup(arena, fallback ? fallback : "");
    }
    int is_str = 0;
    const char *cs = json_string_cstr(v, &is_str);
    if (!is_str) {
        if (ok) *ok = 0;
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON field must be a string");
        return NULL;
    }
    int64_t len = rt_str_len((rt_string)v);
    if (len < 0 || len > 256) {
        if (ok) *ok = 0;
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON string too long");
        return NULL;
    }
    return loc_arena_strndup(arena, cs, (size_t)len);
}

static int32_t json_get_i32(void *map, const char *key, int32_t fallback,
                            int required, int trap_on_error, int *ok) {
    void *v = json_get(map, key);
    if (!v) {
        if (required) {
            if (ok) *ok = 0;
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON missing numeric field");
        }
        return fallback;
    }
    int64_t type = rt_box_type(v);
    double d = 0.0;
    if (type == RT_BOX_F64)
        d = rt_unbox_f64(v);
    else if (type == RT_BOX_I64)
        d = (double)rt_unbox_i64(v);
    else {
        if (ok) *ok = 0;
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON field must be numeric");
        return fallback;
    }
    if (d < (double)INT32_MIN || d > (double)INT32_MAX || d != (double)(int32_t)d) {
        if (ok) *ok = 0;
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON integer out of range");
        return fallback;
    }
    return (int32_t)d;
}

static rt_plural_category_t parse_category(const char *s) {
    if (!s) return RT_PLURAL_OTHER;
    if (strcmp(s, "zero") == 0) return RT_PLURAL_ZERO;
    if (strcmp(s, "one") == 0) return RT_PLURAL_ONE;
    if (strcmp(s, "two") == 0) return RT_PLURAL_TWO;
    if (strcmp(s, "few") == 0) return RT_PLURAL_FEW;
    if (strcmp(s, "many") == 0) return RT_PLURAL_MANY;
    return RT_PLURAL_OTHER;
}

static int valid_category_name(const char *s) {
    return s &&
           (strcmp(s, "zero") == 0 ||
            strcmp(s, "one") == 0 ||
            strcmp(s, "two") == 0 ||
            strcmp(s, "few") == 0 ||
            strcmp(s, "many") == 0 ||
            strcmp(s, "other") == 0);
}

typedef struct rule_parser {
    const char *s;
    size_t pos;
    size_t len;
    loc_arena_t *arena;
    int failed;
} rule_parser_t;

static void rp_skip(rule_parser_t *p) {
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        ++p->pos;
    }
}

static int rp_word(rule_parser_t *p, const char *word) {
    rp_skip(p);
    size_t wl = strlen(word);
    if (p->pos + wl > p->len || memcmp(p->s + p->pos, word, wl) != 0)
        return 0;
    if (p->pos + wl < p->len) {
        char next = p->s[p->pos + wl];
        if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') ||
            (next >= '0' && next <= '9') || next == '_')
            return 0;
    }
    p->pos += wl;
    return 1;
}

static rt_plural_rule_node_t *rp_node(rule_parser_t *p, rt_plural_rule_kind_t kind) {
    rt_plural_rule_node_t *n =
        (rt_plural_rule_node_t *)loc_arena_alloc(p->arena, sizeof(*n));
    if (!n) {
        p->failed = 1;
        return NULL;
    }
    n->kind = kind;
    return n;
}

static rt_plural_rule_node_t *rp_expr(rule_parser_t *p) {
    rp_skip(p);
    if (p->pos >= p->len) {
        p->failed = 1;
        return NULL;
    }
    char c = p->s[p->pos];
    if (c >= '0' && c <= '9') {
        int64_t value = 0;
        while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') {
            int d = p->s[p->pos++] - '0';
            if (value > (INT64_MAX - d) / 10) {
                p->failed = 1;
                return NULL;
            }
            value = value * 10 + d;
        }
        rt_plural_rule_node_t *n = rp_node(p, RT_PRN_INT);
        if (n) n->u.int_val = value;
        return n;
    }
    rt_plural_var_t var;
    if (c == 'n') var = RT_PVAR_N;
    else if (c == 'i') var = RT_PVAR_I;
    else if (c == 'v') var = RT_PVAR_V;
    else if (c == 'f') var = RT_PVAR_F;
    else if (c == 't') var = RT_PVAR_T;
    else {
        p->failed = 1;
        return NULL;
    }
    ++p->pos;
    int32_t mod = 0;
    if (rp_word(p, "mod")) {
        rp_skip(p);
        int64_t value = 0;
        int saw = 0;
        while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') {
            saw = 1;
            int d = p->s[p->pos++] - '0';
            if (value > (INT32_MAX - d) / 10) {
                p->failed = 1;
                return NULL;
            }
            value = value * 10 + d;
        }
        if (!saw || value <= 0) {
            p->failed = 1;
            return NULL;
        }
        mod = (int32_t)value;
    }
    rt_plural_rule_node_t *n = rp_node(p, RT_PRN_VAR);
    if (n) {
        n->u.var.var = var;
        n->u.var.mod = mod;
    }
    return n;
}

static int rp_integer(rule_parser_t *p, int64_t *out) {
    rp_skip(p);
    int saw = 0;
    int64_t value = 0;
    while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') {
        saw = 1;
        int d = p->s[p->pos++] - '0';
        if (value > (INT64_MAX - d) / 10) {
            p->failed = 1;
            return 0;
        }
        value = value * 10 + d;
    }
    if (!saw)
        return 0;
    if (out)
        *out = value;
    return 1;
}

static rt_plural_rule_range_t *rp_range_list(rule_parser_t *p, size_t *out_count) {
    if (out_count) *out_count = 0;
    size_t cap = 4;
    size_t count = 0;
    rt_plural_rule_range_t *ranges =
        (rt_plural_rule_range_t *)loc_arena_alloc(p->arena, cap * sizeof(*ranges));
    if (!ranges) {
        p->failed = 1;
        return NULL;
    }

    while (!p->failed) {
        int64_t start = 0;
        if (!rp_integer(p, &start)) {
            p->failed = 1;
            return NULL;
        }
        int64_t end = start;
        rp_skip(p);
        if (p->pos + 1 < p->len && p->s[p->pos] == '.' && p->s[p->pos + 1] == '.') {
            p->pos += 2;
            if (!rp_integer(p, &end)) {
                p->failed = 1;
                return NULL;
            }
            if (end < start) {
                p->failed = 1;
                return NULL;
            }
        }
        if (count == cap) {
            size_t new_cap = cap * 2;
            if (new_cap > 64) {
                p->failed = 1;
                return NULL;
            }
            rt_plural_rule_range_t *grown =
                (rt_plural_rule_range_t *)loc_arena_alloc(p->arena, new_cap * sizeof(*grown));
            if (!grown) {
                p->failed = 1;
                return NULL;
            }
            memcpy(grown, ranges, count * sizeof(*grown));
            ranges = grown;
            cap = new_cap;
        }
        ranges[count].start = start;
        ranges[count].end = end;
        ++count;

        rp_skip(p);
        if (p->pos < p->len && p->s[p->pos] == ',') {
            ++p->pos;
            continue;
        }
        break;
    }

    if (out_count) *out_count = count;
    return ranges;
}

static rt_plural_rule_node_t *rp_comparison(rule_parser_t *p) {
    if (rp_word(p, "true"))
        return rp_node(p, RT_PRN_TRUE);
    rt_plural_rule_node_t *left = rp_expr(p);
    rp_skip(p);
    rt_plural_rule_kind_t kind;
    int range_op = 0;
    if (rp_word(p, "not")) {
        if (rp_word(p, "within")) {
            kind = RT_PRN_NOT_WITHIN;
            range_op = 1;
        } else if (rp_word(p, "in")) {
            kind = RT_PRN_NOT_IN;
            range_op = 1;
        } else {
            p->failed = 1;
            return NULL;
        }
    } else if (rp_word(p, "within")) {
        kind = RT_PRN_WITHIN;
        range_op = 1;
    } else if (rp_word(p, "in")) {
        kind = RT_PRN_IN;
        range_op = 1;
    } else if (p->pos + 1 < p->len && p->s[p->pos] == '!' && p->s[p->pos + 1] == '=') {
        kind = RT_PRN_NE;
        p->pos += 2;
    } else if (p->pos < p->len && p->s[p->pos] == '=') {
        kind = RT_PRN_EQ;
        p->pos += 1;
    } else {
        p->failed = 1;
        return NULL;
    }

    if (range_op) {
        size_t range_count = 0;
        rt_plural_rule_range_t *ranges = rp_range_list(p, &range_count);
        if (!ranges || range_count == 0) {
            p->failed = 1;
            return NULL;
        }
        rt_plural_rule_node_t *n = rp_node(p, kind);
        if (n) {
            n->u.range.expr = left;
            n->u.range.ranges = ranges;
            n->u.range.range_count = range_count;
        }
        return n;
    }

    rp_skip(p);
    if (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') {
        size_t range_count = 0;
        rt_plural_rule_range_t *ranges = rp_range_list(p, &range_count);
        if (!ranges || range_count == 0) {
            p->failed = 1;
            return NULL;
        }
        rt_plural_rule_node_t *n = rp_node(p, kind == RT_PRN_NE ? RT_PRN_NOT_WITHIN
                                                               : RT_PRN_WITHIN);
        if (n) {
            n->u.range.expr = left;
            n->u.range.ranges = ranges;
            n->u.range.range_count = range_count;
        }
        return n;
    }

    rt_plural_rule_node_t *right = rp_expr(p);
    rt_plural_rule_node_t *n = rp_node(p, kind);
    if (n) {
        n->u.bin.l = left;
        n->u.bin.r = right;
    }
    return n;
}

static rt_plural_rule_node_t *rp_and(rule_parser_t *p) {
    rt_plural_rule_node_t *left = rp_comparison(p);
    while (!p->failed && rp_word(p, "and")) {
        rt_plural_rule_node_t *right = rp_comparison(p);
        rt_plural_rule_node_t *n = rp_node(p, RT_PRN_AND);
        if (!n) return left;
        n->u.bin.l = left;
        n->u.bin.r = right;
        left = n;
    }
    return left;
}

static rt_plural_rule_node_t *rp_or(rule_parser_t *p) {
    rt_plural_rule_node_t *left = rp_and(p);
    while (!p->failed && rp_word(p, "or")) {
        rt_plural_rule_node_t *right = rp_and(p);
        rt_plural_rule_node_t *n = rp_node(p, RT_PRN_OR);
        if (!n) return left;
        n->u.bin.l = left;
        n->u.bin.r = right;
        left = n;
    }
    return left;
}

static rt_plural_rule_node_t *parse_rule(loc_arena_t *arena, const char *rule) {
    if (!rule)
        rule = "true";
    size_t len = strlen(rule);
    if (len > 256)
        return NULL;
    rule_parser_t p = { rule, 0, len, arena, 0 };
    rt_plural_rule_node_t *head = rp_or(&p);
    rp_skip(&p);
    if (p.failed || p.pos != p.len)
        return NULL;
    return head;
}

static int load_string_array(loc_arena_t *arena, void *map, const char *key,
                             const char *const *fallback, size_t expected,
                             const char *const **out,
                             int trap_on_error) {
    void *v = json_get(map, key);
    if (!v) {
        *out = fallback;
        return 1;
    }
    if (!loc_is_seq(v) || rt_seq_len(v) != (int64_t)expected)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON array has wrong length");
    const char **arr = (const char **)loc_arena_alloc(arena, expected * sizeof(char *));
    for (size_t i = 0; i < expected; ++i) {
        void *item = rt_seq_get(v, (int64_t)i);
        int ok = 0;
        const char *cs = json_string_cstr(item, &ok);
        if (!ok)
            return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON array item must be string");
        int64_t len = rt_str_len((rt_string)item);
        if (len < 0 || len > 256)
            return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON array string too long");
        arr[i] = loc_arena_strndup(arena, cs, (size_t)len);
    }
    *out = arr;
    return 1;
}

static int load_plural_chain(loc_arena_t *arena, void *root, const char *key,
                             const rt_plural_rule_entry_t *fallback_entries,
                             size_t fallback_count,
                             const rt_plural_rule_entry_t **out_entries,
                             size_t *out_count,
                             int trap_on_error) {
    void *v = json_get(root, key);
    if (!v) {
        *out_entries = fallback_entries;
        *out_count = fallback_count;
        return 1;
    }
    if (!loc_is_seq(v))
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: plural rule field must be an array");
    int64_t n = rt_seq_len(v);
    if (n <= 0 || n > 32)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: plural rule count out of range");
    rt_plural_rule_entry_t *entries =
        (rt_plural_rule_entry_t *)loc_arena_alloc(arena, (size_t)n * sizeof(*entries));
    for (int64_t i = 0; i < n; ++i) {
        void *entry = rt_seq_get(v, i);
        if (!loc_is_map(entry))
            return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: plural rule entry must be object");
        int ok = 1;
        char *cat = json_dup_string(arena, entry, "category", "other", 1, trap_on_error, &ok);
        char *rule = json_dup_string(arena, entry, "rule", "true", 1, trap_on_error, &ok);
        if (!ok)
            return 0;
        if (!valid_category_name(cat))
            return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: invalid plural category");
        rt_plural_rule_node_t *head = parse_rule(arena, rule);
        if (!head)
            return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: invalid plural rule");
        entries[i].category = parse_category(cat);
        entries[i].head = head;
    }
    *out_entries = entries;
    *out_count = (size_t)n;
    return 1;
}

static int load_reltime_unit(loc_arena_t *arena, void *units,
                             const char *name,
                             const rt_locdata_reltime_unit_t *fallback,
                             rt_locdata_reltime_unit_t *out,
                             int trap_on_error) {
    *out = *fallback;
    void *u = json_get(units, name);
    if (!u)
        return 1;
    if (!loc_is_map(u))
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: relative_time unit must be object");
    int ok = 1;
    out->other = json_dup_string(arena, u, "other", fallback->other, 0, trap_on_error, &ok);
    out->zero  = json_dup_string(arena, u, "zero",  fallback->zero,  0, trap_on_error, &ok);
    out->one   = json_dup_string(arena, u, "one",   fallback->one,   0, trap_on_error, &ok);
    out->two   = json_dup_string(arena, u, "two",   fallback->two,   0, trap_on_error, &ok);
    out->few   = json_dup_string(arena, u, "few",   fallback->few,   0, trap_on_error, &ok);
    out->many  = json_dup_string(arena, u, "many",  fallback->many,  0, trap_on_error, &ok);
    return ok;
}

static int load_list_style(loc_arena_t *arena, void *parent, const char *key,
                           const rt_locdata_list_style_t *fallback,
                           rt_locdata_list_style_t *out,
                           int trap_on_error) {
    *out = *fallback;
    void *style = json_get(parent, key);
    if (!style)
        return 1;
    if (!loc_is_map(style))
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: list style must be object");
    int ok = 1;
    out->pair   = json_dup_string(arena, style, "pair",   fallback->pair,   0, trap_on_error, &ok);
    out->start  = json_dup_string(arena, style, "start",  fallback->start,  0, trap_on_error, &ok);
    out->middle = json_dup_string(arena, style, "middle", fallback->middle, 0, trap_on_error, &ok);
    out->end    = json_dup_string(arena, style, "end",    fallback->end,    0, trap_on_error, &ok);
    return ok;
}

static rt_locale_data_t *loc_data_from_json(void *root, int trap_on_error) {
    if (!loc_is_map(root)) {
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON root must be object");
        return NULL;
    }

    loc_arena_t *arena = loc_arena_new();
    rt_locale_data_t *data =
        (rt_locale_data_t *)loc_arena_alloc(arena, sizeof(*data));
    *data = *rt_locale_data_en_us();
    data->arena = arena;
    data->formatter_refs = 0;

    int ok = 1;
    char *raw_tag = json_dup_string(arena, root, "tag", NULL, 1, trap_on_error, &ok);
    if (!ok || !raw_tag) {
        loc_arena_free(arena);
        return NULL;
    }
    rt_locale_t parsed;
    if (rt_locale_internal_parse_into(raw_tag, strlen(raw_tag), &parsed) != 0 ||
        strcmp(parsed.tag, "root") == 0) {
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: invalid locale tag in JSON");
        loc_arena_free(arena);
        return NULL;
    }
    data->tag = loc_arena_strdup(arena, parsed.tag);

    void *names = json_get(root, "names");
    if (names && !loc_is_map(names)) {
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: names must be object");
        loc_arena_free(arena);
        return NULL;
    }
    if (names) {
        data->names.language = json_dup_string(arena, names, "language", data->names.language, 0, trap_on_error, &ok);
        data->names.region   = json_dup_string(arena, names, "region",   data->names.region,   0, trap_on_error, &ok);
        data->names.display  = json_dup_string(arena, names, "display",  data->names.display,  0, trap_on_error, &ok);
    }

    char *text_dir = json_dup_string(arena, root, "text_direction", data->text_direction, 0, trap_on_error, &ok);
    if (text_dir && (strcmp(text_dir, "ltr") == 0 || strcmp(text_dir, "rtl") == 0)) {
        memset(data->text_direction, 0, sizeof(data->text_direction));
        memcpy(data->text_direction, text_dir, strlen(text_dir));
    } else if (text_dir) {
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: text_direction must be ltr or rtl");
        loc_arena_free(arena);
        return NULL;
    }
    data->first_day_of_week =
        json_get_i32(root, "first_day_of_week", data->first_day_of_week, 0, trap_on_error, &ok);
    if (data->first_day_of_week < 0 || data->first_day_of_week > 6) {
        loc_fail(trap_on_error, "Viper.Localization.LocaleManager: first_day_of_week out of range");
        loc_arena_free(arena);
        return NULL;
    }
    char *measurement = json_dup_string(arena, root, "measurement", data->measurement, 0, trap_on_error, &ok);
    if (measurement) {
        if (strcmp(measurement, "metric") != 0 &&
            strcmp(measurement, "us") != 0 &&
            strcmp(measurement, "uk") != 0) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: measurement must be metric/us/uk");
            loc_arena_free(arena);
            return NULL;
        }
        memset(data->measurement, 0, sizeof(data->measurement));
        strncpy(data->measurement, measurement, sizeof(data->measurement) - 1);
    }

    void *numbers = json_get(root, "numbers");
    if (numbers) {
        if (!loc_is_map(numbers)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: numbers must be object");
            loc_arena_free(arena);
            return NULL;
        }
        data->numbers.decimal_sep = json_dup_string(arena, numbers, "decimal_sep", data->numbers.decimal_sep, 0, trap_on_error, &ok);
        data->numbers.group_sep   = json_dup_string(arena, numbers, "group_sep",   data->numbers.group_sep,   0, trap_on_error, &ok);
        data->numbers.group_size  = json_get_i32(numbers, "group_size", data->numbers.group_size, 0, trap_on_error, &ok);
        data->numbers.secondary_group_size =
            json_get_i32(numbers, "secondary_group_size",
                         data->numbers.secondary_group_size, 0, trap_on_error, &ok);
        data->numbers.minus       = json_dup_string(arena, numbers, "minus",       data->numbers.minus,       0, trap_on_error, &ok);
        data->numbers.plus        = json_dup_string(arena, numbers, "plus",        data->numbers.plus,        0, trap_on_error, &ok);
        data->numbers.percent     = json_dup_string(arena, numbers, "percent",     data->numbers.percent,     0, trap_on_error, &ok);
        data->numbers.infinity    = json_dup_string(arena, numbers, "infinity",    data->numbers.infinity,    0, trap_on_error, &ok);
        data->numbers.nan         = json_dup_string(arena, numbers, "nan",         data->numbers.nan,         0, trap_on_error, &ok);
        data->numbers.exponent    = json_dup_string(arena, numbers, "exponent",    data->numbers.exponent,    0, trap_on_error, &ok);
        data->numbers.digits      = json_dup_string(arena, numbers, "digits",      data->numbers.digits,      0, trap_on_error, &ok);
        if (data->numbers.group_size < 1 || data->numbers.group_size > 9 ||
            data->numbers.secondary_group_size < 0 ||
            data->numbers.secondary_group_size > 9 ||
            !data->numbers.decimal_sep || !*data->numbers.decimal_sep ||
            !data->numbers.minus || !*data->numbers.minus ||
            !data->numbers.plus || !*data->numbers.plus ||
            !loc_digits_are_valid(data->numbers.digits)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: invalid numbers schema");
            loc_arena_free(arena);
            return NULL;
        }
    }

    void *currency = json_get(root, "currency");
    if (currency) {
        if (!loc_is_map(currency)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: currency must be object");
            loc_arena_free(arena);
            return NULL;
        }
        data->currency.default_code     = json_dup_string(arena, currency, "default_code",     data->currency.default_code,     0, trap_on_error, &ok);
        data->currency.symbol           = json_dup_string(arena, currency, "symbol",           data->currency.symbol,           0, trap_on_error, &ok);
        data->currency.pattern_positive = json_dup_string(arena, currency, "pattern_positive", data->currency.pattern_positive, 0, trap_on_error, &ok);
        data->currency.pattern_negative = json_dup_string(arena, currency, "pattern_negative", data->currency.pattern_negative, 0, trap_on_error, &ok);
        data->currency.fraction_digits  = json_get_i32(currency, "fraction_digits", data->currency.fraction_digits, 0, trap_on_error, &ok);
        if (!loc_currency_code_valid(data->currency.default_code) ||
            !loc_currency_pattern_valid(data->currency.pattern_positive) ||
            !loc_currency_pattern_valid(data->currency.pattern_negative) ||
            data->currency.fraction_digits < 0 ||
            data->currency.fraction_digits > 9) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: invalid currency schema");
            loc_arena_free(arena);
            return NULL;
        }
    }

    void *dates = json_get(root, "dates");
    if (dates) {
        if (!loc_is_map(dates)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: dates must be object");
            loc_arena_free(arena);
            return NULL;
        }
        if (!load_string_array(arena, dates, "months_wide", data->dates.months_wide, 12, &data->dates.months_wide, trap_on_error) ||
            !load_string_array(arena, dates, "months_abbr", data->dates.months_abbr, 12, &data->dates.months_abbr, trap_on_error) ||
            !load_string_array(arena, dates, "days_wide", data->dates.days_wide, 7, &data->dates.days_wide, trap_on_error) ||
            !load_string_array(arena, dates, "days_abbr", data->dates.days_abbr, 7, &data->dates.days_abbr, trap_on_error)) {
            loc_arena_free(arena);
            return NULL;
        }
        data->dates.am = json_dup_string(arena, dates, "am", data->dates.am, 0, trap_on_error, &ok);
        data->dates.pm = json_dup_string(arena, dates, "pm", data->dates.pm, 0, trap_on_error, &ok);
        void *patterns = json_get(dates, "patterns");
        if (patterns) {
            if (!loc_is_map(patterns)) {
                loc_fail(trap_on_error, "Viper.Localization.LocaleManager: date patterns must be object");
                loc_arena_free(arena);
                return NULL;
            }
            data->dates.patterns.short_p     = json_dup_string(arena, patterns, "short",       data->dates.patterns.short_p,     0, trap_on_error, &ok);
            data->dates.patterns.medium_p    = json_dup_string(arena, patterns, "medium",      data->dates.patterns.medium_p,    0, trap_on_error, &ok);
            data->dates.patterns.long_p      = json_dup_string(arena, patterns, "long",        data->dates.patterns.long_p,      0, trap_on_error, &ok);
            data->dates.patterns.full_p      = json_dup_string(arena, patterns, "full",        data->dates.patterns.full_p,      0, trap_on_error, &ok);
            data->dates.patterns.time_short  = json_dup_string(arena, patterns, "time_short",  data->dates.patterns.time_short,  0, trap_on_error, &ok);
            data->dates.patterns.time_medium = json_dup_string(arena, patterns, "time_medium", data->dates.patterns.time_medium, 0, trap_on_error, &ok);
            data->dates.patterns.datetime_short =
                json_dup_string(arena, patterns, "datetime_short",
                                data->dates.patterns.datetime_short, 0, trap_on_error, &ok);
            data->dates.patterns.datetime_medium =
                json_dup_string(arena, patterns, "datetime_medium",
                                data->dates.patterns.datetime_medium, 0, trap_on_error, &ok);
        }
    }

    void *rt = json_get(root, "relative_time");
    if (rt) {
        if (!loc_is_map(rt)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: relative_time must be object");
            loc_arena_free(arena);
            return NULL;
        }
        data->reltime.now = json_dup_string(arena, rt, "now", data->reltime.now, 0, trap_on_error, &ok);
        data->reltime.past = json_dup_string(arena, rt, "past", data->reltime.past, 0, trap_on_error, &ok);
        data->reltime.future = json_dup_string(arena, rt, "future", data->reltime.future, 0, trap_on_error, &ok);
        data->reltime.short_past =
            json_dup_string(arena, rt, "short_past", data->reltime.short_past, 0, trap_on_error, &ok);
        data->reltime.short_future =
            json_dup_string(arena, rt, "short_future", data->reltime.short_future, 0, trap_on_error, &ok);
        void *units = json_get(rt, "units");
        static const char *const unit_names[7] = {
            "second", "minute", "hour", "day", "week", "month", "year"
        };
        if (units) {
            if (!loc_is_map(units)) {
                loc_fail(trap_on_error, "Viper.Localization.LocaleManager: relative_time.units must be object");
                loc_arena_free(arena);
                return NULL;
            }
            for (size_t i = 0; i < 7; ++i) {
                if (!load_reltime_unit(arena, units, unit_names[i], &data->reltime.units[i],
                                       &data->reltime.units[i], trap_on_error)) {
                    loc_arena_free(arena);
                    return NULL;
                }
            }
        }
        void *short_units = json_get(rt, "short_units");
        if (short_units) {
            if (!loc_is_map(short_units)) {
                loc_fail(trap_on_error, "Viper.Localization.LocaleManager: relative_time.short_units must be object");
                loc_arena_free(arena);
                return NULL;
            }
            for (size_t i = 0; i < 7; ++i) {
                if (!load_reltime_unit(arena, short_units, unit_names[i],
                                       &data->reltime.short_units[i],
                                       &data->reltime.short_units[i], trap_on_error)) {
                    loc_arena_free(arena);
                    return NULL;
                }
            }
        }
        if (!data->reltime.now || !*data->reltime.now ||
            !data->reltime.past || !strstr(data->reltime.past, "{n}") ||
            !strstr(data->reltime.past, "{unit}") ||
            !data->reltime.future || !strstr(data->reltime.future, "{n}") ||
            !strstr(data->reltime.future, "{unit}")) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: invalid relative_time schema");
            loc_arena_free(arena);
            return NULL;
        }
    }

    if (!load_plural_chain(arena, root, "plural_cardinal",
                           data->plural_cardinal, data->cardinal_count,
                           &data->plural_cardinal, &data->cardinal_count,
                           trap_on_error) ||
        !load_plural_chain(arena, root, "plural_ordinal",
                           data->plural_ordinal, data->ordinal_count,
                           &data->plural_ordinal, &data->ordinal_count,
                           trap_on_error)) {
        loc_arena_free(arena);
        return NULL;
    }

    void *list_format = json_get(root, "list_format");
    if (list_format) {
        if (!loc_is_map(list_format)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: list_format must be object");
            loc_arena_free(arena);
            return NULL;
        }
        if (!load_list_style(arena, list_format, "and", &data->list_format.and_p,
                             &data->list_format.and_p, trap_on_error) ||
            !load_list_style(arena, list_format, "or", &data->list_format.or_p,
                             &data->list_format.or_p, trap_on_error) ||
            !load_list_style(arena, list_format, "unit", &data->list_format.unit_p,
                             &data->list_format.unit_p, trap_on_error)) {
            loc_arena_free(arena);
            return NULL;
        }
    }

    void *collation = json_get(root, "collation");
    if (collation) {
        if (!loc_is_map(collation)) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: collation must be object");
            loc_arena_free(arena);
            return NULL;
        }
        data->collation.strength =
            json_get_i32(collation, "strength", data->collation.strength, 0, trap_on_error, &ok);
        if (data->collation.strength < 1 || data->collation.strength > 4) {
            loc_fail(trap_on_error, "Viper.Localization.LocaleManager: collation.strength out of range");
            loc_arena_free(arena);
            return NULL;
        }
        data->collation.reorder = NULL;
        data->collation.reorder_len = 0;
    }

    if (!ok) {
        loc_arena_free(arena);
        return NULL;
    }
    return data;
}

static int loc_text_starts_object(rt_string text) {
    if (!text)
        return 0;
    const char *cs = rt_string_cstr(text);
    int64_t len = rt_str_len(text);
    for (int64_t i = 0; cs && i < len; ++i) {
        char c = cs[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        return c == '{';
    }
    return 0;
}

static int loc_register_json_text(rt_string text, int trap_on_error) {
    if (!text)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: empty locale JSON");
    int64_t len = rt_str_len(text);
    if (len <= 0 || len > 256 * 1024)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: locale JSON size out of range");
    if (!loc_text_starts_object(text) || rt_json_is_valid(text) != 1)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: malformed locale JSON");

    void *root = rt_json_parse_object(text);
    if (!root)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: malformed locale JSON object");
    rt_locale_data_t *data = loc_data_from_json(root, trap_on_error);
    loc_release_object(root);
    if (!data)
        return 0;

    rt_rwlock_write_enter(g_mgr.lock);
    int registered = loc_register_entry_locked(data, /*is_baked=*/0);
    rt_rwlock_write_exit(g_mgr.lock);
    if (!registered)
        return loc_fail(trap_on_error, "Viper.Localization.LocaleManager: cannot replace locale while in use");
    return 1;
}

//===----------------------------------------------------------------------===//
// Init and teardown
//===----------------------------------------------------------------------===//

static void loc_registry_grow(void) {
    size_t new_cap = g_mgr.capacity == 0 ? 4 : g_mgr.capacity * 2;
    if (g_mgr.capacity > SIZE_MAX / 2 ||
        new_cap > SIZE_MAX / sizeof(*g_mgr.entries)) {
        rt_trap("Viper.Localization.LocaleManager: registry capacity overflow");
        return;
    }
    loc_registry_entry_t *new_entries =
        (loc_registry_entry_t *)realloc(g_mgr.entries, new_cap * sizeof(*new_entries));
    if (!new_entries) {
        rt_trap("Viper.Localization.LocaleManager: registry allocation failed");
        return;
    }
    g_mgr.entries = new_entries;
    g_mgr.capacity = new_cap;
}

static int loc_register_entry_locked(const rt_locale_data_t *data, int is_baked) {
    if (!data || !data->tag)
        return 0;
    // Replace existing entry (idempotent load).
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, data->tag) == 0) {
            if (g_mgr.entries[i].is_baked && !is_baked) {
                loc_free_loaded_data(data);
                return 1; // baked en-US remains authoritative
            }
            if (!g_mgr.entries[i].is_baked && g_mgr.entries[i].data != data) {
                if (loc_data_ref_count(g_mgr.entries[i].data) != 0) {
                    loc_free_loaded_data(data);
                    return 0;
                }
                loc_free_loaded_data(g_mgr.entries[i].data);
            }
            g_mgr.entries[i].data = data;
            g_mgr.entries[i].is_baked = is_baked;
            g_mgr.entries[i].tag = data->tag;
            return 1;
        }
    }
    if (g_mgr.count == g_mgr.capacity)
        loc_registry_grow();
    g_mgr.entries[g_mgr.count].tag = data->tag;
    g_mgr.entries[g_mgr.count].data = data;
    g_mgr.entries[g_mgr.count].is_baked = is_baked;
    g_mgr.count++;
    return 1;
}

/// @brief Construct a Locale handle for the given tag, resolving its data
///        pointer from the current registry. Caller holds the write lock.
/// @details Intentionally does NOT go through rt_locale_parse / try_parse
///          because those re-enter the manager's init path, causing
///          infinite recursion during bootstrap. We build the handle
///          directly and bind the data pointer via internal helpers.
static void *loc_make_handle_locked(const char *tag) {
    // Use a direct construction path: allocate + set fields + walk registry.
    void *loc = NULL;
    // Inline parse without registry touch:
    extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
    rt_locale_t *l = (rt_locale_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_locale_t));
    if (!l) {
        return NULL;
    }
    memset(l, 0, sizeof(*l));
    rt_obj_set_finalizer(l, rt_locale_internal_finalizer);
    // Parse directly into the struct — uses the same helper as public parse.
    if (rt_locale_internal_parse_into(tag, strlen(tag), l) != 0) {
        if (rt_obj_release_check0(l))
            rt_obj_free(l);
        return NULL;
    }
    loc = l;
    // Registry lookup without calling the public API (we already hold the lock).
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            l->data = g_mgr.entries[i].data;
            rt_locale_manager_retain_data(l->data);
            break;
        }
    }
    return loc;
}

static void loc_mgr_ensure_init(void) {
    // Fast path: double-checked.
    if (__atomic_load_n(&g_mgr.initialized, __ATOMIC_ACQUIRE))
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
            if (rt_obj_release_check0(fresh))
                rt_obj_free(fresh);
        }
    }

    rt_rwlock_write_enter(g_mgr.lock);
    if (__atomic_load_n(&g_mgr.initialized, __ATOMIC_ACQUIRE)) {
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
    }
    if (!g_mgr.system) {
        g_mgr.system = loc_make_handle_locked("en-US");
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

    __atomic_store_n(&g_mgr.initialized, 1, __ATOMIC_RELEASE);
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

const rt_locale_data_t *rt_locale_manager_lookup_data_retained(const char *tag) {
    if (!tag || !*tag)
        return NULL;
    loc_mgr_ensure_init();
    rt_rwlock_read_enter(g_mgr.lock);
    const rt_locale_data_t *data = NULL;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            data = g_mgr.entries[i].data;
            rt_locale_manager_retain_data(data);
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
    int64_t old = __atomic_fetch_sub(counter, 1, __ATOMIC_ACQ_REL);
    if (old <= 0) {
        __atomic_fetch_add(counter, 1, __ATOMIC_ACQ_REL);
        rt_trap("Viper.Localization.LocaleManager: locale data refcount underflow");
    }
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
    const rt_locale_data_t *bound_data = NULL;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, tag) == 0) {
            loaded = 1;
            bound_data = g_mgr.entries[i].data;
            break;
        }
    }
    if (!loaded) {
        rt_rwlock_write_exit(g_mgr.lock);
        rt_trap("Viper.Localization.LocaleManager: locale not loaded (call Load* first)");
        return;
    }
    rt_locale_bind_data(locale, bound_data);
    void *old = g_mgr.current;
    g_mgr.current = locale;
    rt_heap_retain(locale);
    rt_rwlock_write_exit(g_mgr.lock);

    if (old)
        loc_release_handle(old);
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
        rt_string_unref(s);
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
    loc_mgr_ensure_init();
    if (!path) {
        rt_trap("Viper.Localization.LocaleManager: LoadFromJson requires a path");
        return;
    }
    rt_string text = rt_io_file_read_all_text(path);
    if (!text) {
        rt_trap("Viper.Localization.LocaleManager: cannot read locale JSON");
        return;
    }
    (void)loc_register_json_text(text, /*trap_on_error=*/1);
    rt_string_unref(text);
}

int8_t rt_locale_manager_try_load_from_json(rt_string path) {
    loc_mgr_ensure_init();
    if (!path || rt_io_file_exists(path) != 1)
        return 0;
    rt_string text = rt_io_file_read_all_text(path);
    if (!text)
        return 0;
    int ok = loc_register_json_text(text, /*trap_on_error=*/0);
    rt_string_unref(text);
    return ok ? 1 : 0;
}

void rt_locale_manager_load_from_asset(rt_string name) {
    loc_mgr_ensure_init();
    if (!name) {
        rt_trap("Viper.Localization.LocaleManager: LoadFromAsset requires a name");
        return;
    }
    void *bytes = rt_asset_load_bytes(name);
    if (!bytes) {
        rt_trap("Viper.Localization.LocaleManager: locale asset not found");
        return;
    }
    rt_string text = rt_bytes_to_str(bytes);
    loc_release_object(bytes);
    (void)loc_register_json_text(text, /*trap_on_error=*/1);
    rt_string_unref(text);
}

int8_t rt_locale_manager_try_load_from_asset(rt_string name) {
    loc_mgr_ensure_init();
    if (!name)
        return 0;
    void *bytes = rt_asset_load_bytes(name);
    if (!bytes)
        return 0;
    rt_string text = rt_bytes_to_str(bytes);
    loc_release_object(bytes);
    int ok = loc_register_json_text(text, /*trap_on_error=*/0);
    rt_string_unref(text);
    return ok ? 1 : 0;
}

void rt_locale_manager_load_builtin(rt_string tag) {
    loc_mgr_ensure_init();
    const char *bytes = tag ? rt_string_cstr(tag) : NULL;
    int64_t len = tag ? rt_str_len(tag) : 0;
    if (!bytes || len <= 0) {
        rt_trap("Viper.Localization.LocaleManager: LoadBuiltin requires a non-empty tag");
        return;
    }

    char input[RT_LOCALE_TAG_CAP * 2];
    if ((size_t)len >= sizeof(input)) {
        rt_trap("Viper.Localization.LocaleManager: builtin tag too long");
        return;
    }
    memcpy(input, bytes, (size_t)len);
    input[len] = '\0';

    rt_locale_t parsed;
    if (rt_locale_internal_parse_into(input, (size_t)len, &parsed) != 0) {
        rt_trap("Viper.Localization.LocaleManager: invalid builtin locale tag");
        return;
    }

    if (strcmp(parsed.tag, "en-US") == 0) {
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
    char input[RT_LOCALE_TAG_CAP * 2];
    if ((size_t)len >= sizeof(input))
        return NULL;
    memcpy(input, bytes, (size_t)len);
    input[len] = '\0';

    rt_locale_t parsed;
    if (rt_locale_internal_parse_into(input, (size_t)len, &parsed) != 0)
        return NULL;

    rt_rwlock_read_enter(g_mgr.lock);
    int registered = 0;
    for (size_t i = 0; i < g_mgr.count; ++i) {
        if (strcmp(g_mgr.entries[i].tag, parsed.tag) == 0) {
            registered = 1;
            break;
        }
    }
    rt_rwlock_read_exit(g_mgr.lock);
    if (!registered) {
        size_t path_index = 0;
        while (!registered) {
            rt_rwlock_read_enter(g_mgr.lock);
            if (path_index >= g_mgr.search_count) {
                rt_rwlock_read_exit(g_mgr.lock);
                break;
            }
            const char *base = g_mgr.search_paths[path_index++];
            char *base_copy = NULL;
            if (base) {
                size_t base_len = strlen(base);
                base_copy = (char *)malloc(base_len + 1);
                if (base_copy) {
                    memcpy(base_copy, base, base_len);
                    base_copy[base_len] = '\0';
                }
            }
            rt_rwlock_read_exit(g_mgr.lock);
            if (!base_copy)
                break;
            size_t bl = strlen(base_copy);
            size_t tl = strlen(parsed.tag);
            if (bl <= 4096 && tl <= 128) {
                char candidate[4608];
                const char sep =
#if RT_PLATFORM_WINDOWS
                    (bl > 0 && (base_copy[bl - 1] == '/' || base_copy[bl - 1] == '\\')) ? '\0' : '\\';
#else
                    (bl > 0 && base_copy[bl - 1] == '/') ? '\0' : '/';
#endif
                if (sep)
                    snprintf(candidate, sizeof(candidate), "%s%c%s.json", base_copy, sep, parsed.tag);
                else
                    snprintf(candidate, sizeof(candidate), "%s%s.json", base_copy, parsed.tag);
                rt_string path = rt_string_from_bytes(candidate, strlen(candidate));
                if (rt_locale_manager_try_load_from_json(path) == 1)
                    registered = 1;
                rt_string_unref(path);
            }
            free(base_copy);
        }
    }
    if (!registered)
        return NULL;

    rt_string s = rt_string_from_bytes(parsed.tag, strlen(parsed.tag));
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
        if (g_mgr.search_capacity > SIZE_MAX / 2 ||
            new_cap > SIZE_MAX / sizeof(*g_mgr.search_paths)) {
            rt_rwlock_write_exit(g_mgr.lock);
            free(copy);
            rt_trap("Viper.Localization.LocaleManager: search path capacity overflow");
            return;
        }
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
    const rt_locale_data_t *data = g_mgr.entries[idx].data;
    int64_t refs = loc_data_ref_count(data);
    if (refs == 1 && target->data == data) {
        rt_locale_manager_release_data(target->data);
        target->data = NULL;
        refs = 0;
    }
    if (refs != 0) {
        rt_rwlock_write_exit(g_mgr.lock);
        return 0;
    }
    loc_registry_entry_t removed = g_mgr.entries[idx];
    for (size_t j = idx + 1; j < g_mgr.count; ++j)
        g_mgr.entries[j - 1] = g_mgr.entries[j];
    g_mgr.count--;
    rt_rwlock_write_exit(g_mgr.lock);
    loc_free_loaded_data(removed.data);
    return 1;
}

void rt_locale_manager_reset(void) {
    loc_mgr_ensure_init();
    rt_rwlock_write_enter(g_mgr.lock);

    void *old_current = g_mgr.current;
    void *old_system = g_mgr.system;
    g_mgr.current = loc_make_handle_locked("en-US");
    g_mgr.system = loc_make_handle_locked("en-US");

    // Clear extra search paths (keep zero — user must re-add).
    for (size_t i = 0; i < g_mgr.search_count; ++i)
        free(g_mgr.search_paths[i]);
    g_mgr.search_count = 0;
    rt_rwlock_write_exit(g_mgr.lock);

    loc_release_handle(old_current);
    loc_release_handle(old_system);

    rt_rwlock_write_enter(g_mgr.lock);
    // Remove every non-baked entry not still retained by user-visible handles.
    // Baked entries survive Reset.
    size_t w = 0;
    for (size_t r = 0; r < g_mgr.count; ++r) {
        if (g_mgr.entries[r].is_baked) {
            g_mgr.entries[w++] = g_mgr.entries[r];
        } else if (g_mgr.entries[r].data && loc_data_ref_count(g_mgr.entries[r].data) == 0) {
            loc_free_loaded_data(g_mgr.entries[r].data);
        } else if (g_mgr.entries[r].data) {
            g_mgr.entries[w++] = g_mgr.entries[r];
        }
    }
    g_mgr.count = w;
    rt_rwlock_write_exit(g_mgr.lock);
}
