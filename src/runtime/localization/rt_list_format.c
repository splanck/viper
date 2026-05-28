//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_list_format.c
// Purpose: Implementation of Viper.Localization.ListFormat. Joins a list of
//          rt_string items using the bound locale's list_format templates
//          (pair / start / middle / end) per CLDR conventions.
//
// Key invariants:
//   - Joining algorithm (for n >= 3): begin with end(items[n-2], items[n-1]),
//     then wrap leftwards: result = middle(items[k], result) for k in
//     n-3 .. 1, then start(items[0], result).
//   - Templates are simple {0}/{1} substitutions. Missing templates fall
//     back to ", " concatenation to avoid producing empty output on
//     misconfigured locale data.
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_list_format.h (interface),
//        src/runtime/localization/rt_locale_data.h (template storage).
//
//===----------------------------------------------------------------------===//

#include "rt_list_format.h"

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_list.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_manager.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <stdint.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Instance struct
//===----------------------------------------------------------------------===//

typedef struct rt_list_format_inst {
    void *locale;
    const rt_locale_data_t *data;
} rt_list_format_inst_t;

/// @brief Unchecked cast of an opaque handle to the ListFormat instance.
static rt_list_format_inst_t *as_fmt(void *obj) {
    return (rt_list_format_inst_t *)obj;
}

/// @brief Drop one GC reference to @p obj and free it if the count hit zero.
static void lf_release_handle(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief GC finalizer: release the formatter's locale data and locale handle.
static void lf_finalizer(void *obj) {
    rt_list_format_inst_t *f = (rt_list_format_inst_t *)obj;
    if (!f)
        return;
    rt_locale_manager_release_data(f->data);
    lf_release_handle(f->locale);
    f->locale = NULL;
    f->data = NULL;
}

//===----------------------------------------------------------------------===//
// Constructors / properties
//===----------------------------------------------------------------------===//

/// @brief Allocate and initialize a GC-managed ListFormat for @p locale.
/// @details Retains the locale handle + its data. Traps on allocation failure;
///          installs @ref lf_finalizer.
static void *lf_alloc(void *locale) {
    rt_list_format_inst_t *f =
        (rt_list_format_inst_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_list_format_inst_t));
    if (!f) {
        rt_trap("Viper.Localization.ListFormat: allocation failed");
        return NULL;
    }
    f->locale = locale;
    if (f->locale)
        rt_heap_retain(f->locale);
    f->data = rt_locale_get_data(locale);
    rt_locale_manager_retain_data(f->data);
    rt_obj_set_finalizer(f, lf_finalizer);
    return f;
}

void *rt_list_format_new(void) {
    void *current = rt_locale_manager_current();
    void *fmt = lf_alloc(current);
    lf_release_handle(current);
    return fmt;
}

void *rt_list_format_for_locale(void *locale) {
    return lf_alloc(locale);
}

void *rt_list_format_get_locale(void *self) {
    return self ? as_fmt(self)->locale : NULL;
}

//===----------------------------------------------------------------------===//
// Template expansion: {0} and {1} positional placeholders
//===----------------------------------------------------------------------===//

/// @brief Substitute {0}→@p a and {1}→@p b into @p tmpl (default "{0}, {1}").
/// @return A new string; NULL operands contribute empty text.
static rt_string expand_pair(const char *tmpl, rt_string a, rt_string b) {
    if (!tmpl)
        tmpl = "{0}, {1}";
    rt_string_builder sb;
    rt_sb_init(&sb);
    const char *p = tmpl;
    const char *a_cs = a ? rt_string_cstr(a) : "";
    const char *b_cs = b ? rt_string_cstr(b) : "";
    int64_t a_len = a ? rt_str_len(a) : 0;
    int64_t b_len = b ? rt_str_len(b) : 0;
    while (*p) {
        if (p[0] == '{' && p[1] == '0' && p[2] == '}') {
            if (a_cs && a_len > 0)
                (void)rt_sb_append_bytes(&sb, a_cs, (size_t)a_len);
            p += 3;
        } else if (p[0] == '{' && p[1] == '1' && p[2] == '}') {
            if (b_cs && b_len > 0)
                (void)rt_sb_append_bytes(&sb, b_cs, (size_t)b_len);
            p += 3;
        } else {
            (void)rt_sb_append_bytes(&sb, p, 1);
            ++p;
        }
    }
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// Join algorithm
//===----------------------------------------------------------------------===//

/// @brief Join @p items per a CLDR list @p style (start/middle/end/pair).
/// @details 0/1 items are trivial; 2 use the "pair" template; 3+ fold from the
///          right ("end", then "middle"s, then "start") so nested expansions
///          place separators exactly as CLDR specifies. Returns a new string.
static rt_string join_with_style(void *items, const rt_locdata_list_style_t *style) {
    if (!items)
        return rt_string_from_bytes("", 0);
    int64_t n = rt_list_len(items);
    if (n <= 0)
        return rt_string_from_bytes("", 0);
    if (n == 1) {
        rt_string a = (rt_string)rt_list_get(items, 0);
        if (!a)
            return rt_string_from_bytes("", 0);
        rt_string_ref(a);
        return a;
    }
    if (n == 2) {
        rt_string a = (rt_string)rt_list_get(items, 0);
        rt_string b = (rt_string)rt_list_get(items, 1);
        return expand_pair(style->pair, a, b);
    }
    // n >= 3: build from the right.
    rt_string last_a = (rt_string)rt_list_get(items, n - 2);
    rt_string last_b = (rt_string)rt_list_get(items, n - 1);
    rt_string result = expand_pair(style->end, last_a, last_b);

    for (int64_t k = n - 3; k > 0; --k) {
        rt_string item = (rt_string)rt_list_get(items, k);
        rt_string next = expand_pair(style->middle, item, result);
        rt_string_unref(result);
        result = next;
    }

    rt_string first = (rt_string)rt_list_get(items, 0);
    rt_string final = expand_pair(style->start, first, result);
    rt_string_unref(result);
    return final;
}

//===----------------------------------------------------------------------===//
// Public API
//===----------------------------------------------------------------------===//

rt_string rt_list_format_and(void *self, void *items) {
    if (!self)
        return rt_string_from_bytes("", 0);
    return join_with_style(items, &as_fmt(self)->data->list_format.and_p);
}

rt_string rt_list_format_or(void *self, void *items) {
    if (!self)
        return rt_string_from_bytes("", 0);
    return join_with_style(items, &as_fmt(self)->data->list_format.or_p);
}

rt_string rt_list_format_unit(void *self, void *items) {
    if (!self)
        return rt_string_from_bytes("", 0);
    return join_with_style(items, &as_fmt(self)->data->list_format.unit_p);
}

rt_string rt_list_format_short(void *self, void *items) {
    // Phase 4: baked en-US has a single set of and_p templates used for both
    // the default and the short form. A future phase will introduce
    // ampersand-style short templates as a separate list_format sub-record.
    return rt_list_format_and(self, items);
}
