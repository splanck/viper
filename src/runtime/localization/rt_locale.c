//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale.c
// Purpose: Implementation of the Viper.Localization.Locale class: BCP-47
//          parsing, canonicalization, and the fallback-chain walk. Integrates
//          with LocaleManager only loosely — registry lookup happens via
//          rt_locale_manager_lookup_data which may return NULL for tags that
//          haven't been loaded yet; in that case the Locale's `data` pointer
//          stays NULL and queries fall through to the invariant.
//
// Key invariants:
//   - Parsing is strict about structure but lenient about case and separators:
//     accepts underscores, accepts mixed case, rejects content that can't be
//     canonicalized to a valid BCP-47 shape.
//   - Fallbacks always terminate in the invariant ("root") entry; the walk
//     never loops even on malformed handles because each step strips one
//     subtag at a minimum.
//   - Locale handles are allocated via rt_obj_new_i64 so they participate in
//     the GC lifetime system; finalizers are no-ops (no heap resources held).
//
// Ownership/Lifetime:
//   - Handles own their subtag string bytes (embedded in the struct).
//   - `data` is non-owning; the LocaleManager registry is the sole owner.
//
// Links: src/runtime/localization/rt_locale.h (interface),
//        src/runtime/localization/rt_locale_manager.h (registry bridge),
//        src/runtime/localization/rt_locale_data_en_us.c (invariant fallback).
//
//===----------------------------------------------------------------------===//

#include "rt_locale.h"

#include "rt_internal.h"
#include "rt_list.h"
#include "rt_locale_data.h"
#include "rt_locale_manager.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Static helpers: character classification / case normalization
//===----------------------------------------------------------------------===//

/// @brief Check whether @p c is ASCII alpha. BCP-47 only accepts ASCII.
static int loc_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int loc_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int loc_is_alnum(char c) {
    return loc_is_alpha(c) || loc_is_digit(c);
}

static char loc_to_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static char loc_to_upper(char c) {
    if (c >= 'a' && c <= 'z')
        return (char)(c - 'a' + 'A');
    return c;
}

static int loc_is_separator(char c) {
    return c == '-' || c == '_';
}

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// @brief Split the input tag into subtags and classify each.
/// @details Skips duplicate separators. Rejects subtags that exceed fixed
///          storage or that fail their shape test. On success, populates the
///          language/script/region fields of @p out and builds the canonical
///          `tag`. Variant / extension subtags are accepted and appended to
///          `tag` as-is, lower-cased; they are preserved for Equals/ToString
///          but do not affect Locale's behavioural queries.
/// @return 0 on success, -1 on structural failure.
int rt_locale_internal_parse_into(const char *input, size_t input_len, rt_locale_t *out);
int rt_locale_internal_parse_into(const char *input, size_t input_len, rt_locale_t *out) {
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    if (!input || input_len == 0)
        return -1;
    if (input_len > RT_LOCALE_TAG_CAP * 2) // allow some slack for case/underscore input
        return -1;

    size_t i = 0;
    int subtag_idx = 0;
    size_t canonical_len = 0;
    char canonical[RT_LOCALE_TAG_CAP];
    canonical[0] = '\0';

    while (i < input_len) {
        // Skip leading separators (tolerate "en--US", " en _us ", etc.)
        while (i < input_len && loc_is_separator(input[i]))
            ++i;
        if (i >= input_len)
            break;

        size_t start = i;
        while (i < input_len && !loc_is_separator(input[i]))
            ++i;
        size_t sub_len = i - start;
        if (sub_len == 0 || sub_len > 8)
            return -1;

        // Copy + classify
        char sub[16];
        for (size_t k = 0; k < sub_len; ++k) {
            if (!loc_is_alnum(input[start + k]))
                return -1;
            sub[k] = input[start + k];
        }
        sub[sub_len] = '\0';

        // Classification: position-sensitive, per RFC 5646 conventions
        if (subtag_idx == 0) {
            // Primary language: 2-3 alpha (occasionally 4-8 for registered)
            if (sub_len < 2 || sub_len > 8)
                return -1;
            for (size_t k = 0; k < sub_len; ++k)
                if (!loc_is_alpha(sub[k]))
                    return -1;
            if (sub_len + 1 > RT_LOCALE_LANG_CAP)
                return -1;
            for (size_t k = 0; k < sub_len; ++k)
                out->language[k] = loc_to_lower(sub[k]);
            out->language[sub_len] = '\0';
        } else if (sub_len == 4 && out->script[0] == '\0' && out->region[0] == '\0'
                   && loc_is_alpha(sub[0]) && loc_is_alpha(sub[1])
                   && loc_is_alpha(sub[2]) && loc_is_alpha(sub[3])) {
            // Script: exactly 4 letters, Title-case
            out->script[0] = loc_to_upper(sub[0]);
            for (size_t k = 1; k < 4; ++k)
                out->script[k] = loc_to_lower(sub[k]);
            out->script[4] = '\0';
        } else if (out->region[0] == '\0'
                   && ((sub_len == 2 && loc_is_alpha(sub[0]) && loc_is_alpha(sub[1]))
                       || (sub_len == 3 && loc_is_digit(sub[0])
                           && loc_is_digit(sub[1]) && loc_is_digit(sub[2])))) {
            // Region: 2 letters or 3 digits
            if (sub_len + 1 > RT_LOCALE_REGION_CAP)
                return -1;
            for (size_t k = 0; k < sub_len; ++k)
                out->region[k] = loc_is_alpha(sub[k]) ? loc_to_upper(sub[k]) : sub[k];
            out->region[sub_len] = '\0';
        }
        // Variant/extension subtags are accepted but don't populate the
        // structured fields. They're still written to `canonical` below so
        // ToString round-trips the user's input.

        // Append subtag to canonical tag with '-' separator and proper casing.
        if (canonical_len > 0) {
            if (canonical_len + 1 >= RT_LOCALE_TAG_CAP)
                return -1;
            canonical[canonical_len++] = '-';
            canonical[canonical_len] = '\0';
        }
        if (canonical_len + sub_len + 1 > RT_LOCALE_TAG_CAP)
            return -1;
        for (size_t k = 0; k < sub_len; ++k) {
            char c = sub[k];
            // Apply canonical casing by position.
            if (subtag_idx == 0)
                c = loc_to_lower(c);
            else if (sub_len == 4 && out->script[0] != '\0' && k == 0
                     && subtag_idx == 1)
                c = loc_to_upper(c);
            else if (sub_len == 4 && out->script[0] != '\0' && k > 0
                     && subtag_idx == 1)
                c = loc_to_lower(c);
            else if ((sub_len == 2 || sub_len == 3)
                     && loc_is_alpha(c))
                c = loc_to_upper(c);
            else
                c = loc_to_lower(c);
            canonical[canonical_len + k] = c;
        }
        canonical_len += sub_len;
        canonical[canonical_len] = '\0';

        ++subtag_idx;
    }

    if (out->language[0] == '\0')
        return -1;

    if (canonical_len + 1 > RT_LOCALE_TAG_CAP)
        return -1;
    memcpy(out->tag, canonical, canonical_len + 1);
    return 0;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

static void *loc_alloc(void) {
    rt_locale_t *loc = (rt_locale_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_locale_t));
    if (!loc) {
        rt_trap("Viper.Localization.Locale: memory allocation failed");
        return NULL; // unreachable after trap
    }
    memset(loc, 0, sizeof(*loc));
    return loc;
}

/// @brief Produce the canonical invariant ("root") locale handle.
static void *loc_make_invariant(void) {
    rt_locale_t *loc = (rt_locale_t *)loc_alloc();
    memcpy(loc->tag, "root", 5);
    loc->data = rt_locale_data_en_us();
    return loc;
}

void *rt_locale_new(void) {
    return loc_make_invariant();
}

void *rt_locale_invariant(void) {
    return loc_make_invariant();
}

void *rt_locale_parse_internal(rt_string tag, int strict) {
    const char *bytes = tag ? rt_string_cstr(tag) : NULL;
    int64_t sl = tag ? rt_str_len(tag) : 0;

    // Accept empty/NULL as the invariant locale when non-strict, or trap
    // when strict (matches the plan's "Parse traps, TryParse returns NULL").
    if (!bytes || sl <= 0) {
        if (strict) {
            rt_trap("Viper.Localization.Locale: invalid BCP-47 tag '' (empty)");
            return NULL;
        }
        return NULL;
    }

    rt_locale_t parsed;
    if (rt_locale_internal_parse_into(bytes, (size_t)sl, &parsed) != 0) {
        if (strict) {
            rt_trap("Viper.Localization.Locale: invalid BCP-47 tag (parse failed)");
            return NULL;
        }
        return NULL;
    }

    // Special case: "root" is allowed and maps to the invariant locale.
    if (strcmp(parsed.language, "root") == 0 && parsed.script[0] == '\0'
        && parsed.region[0] == '\0') {
        return loc_make_invariant();
    }

    rt_locale_t *loc = (rt_locale_t *)loc_alloc();
    *loc = parsed;

    // Try to resolve registered locale-data.
    loc->data = rt_locale_manager_lookup_data(loc->tag);
    return loc;
}

void *rt_locale_parse(rt_string tag) {
    return rt_locale_parse_internal(tag, /*strict=*/1);
}

void *rt_locale_try_parse(rt_string tag) {
    return rt_locale_parse_internal(tag, /*strict=*/0);
}

void *rt_locale_from_parts(rt_string language, rt_string script, rt_string region) {
    const char *ls = language ? rt_string_cstr(language) : NULL;
    int64_t ll = language ? rt_str_len(language) : 0;
    if (!ls || ll <= 0) {
        rt_trap("Viper.Localization.Locale: language subtag required");
        return NULL;
    }

    // Build a canonical string and reuse the parser — keeps validation logic
    // in one place and ensures FromParts and Parse agree byte-for-byte.
    char buf[RT_LOCALE_TAG_CAP];
    size_t pos = 0;
    if ((size_t)ll >= sizeof(buf)) {
        rt_trap("Viper.Localization.Locale: language subtag too long");
        return NULL;
    }
    memcpy(buf, ls, (size_t)ll);
    pos = (size_t)ll;

    if (script) {
        const char *ss = rt_string_cstr(script);
        int64_t sll = rt_str_len(script);
        if (ss && sll > 0) {
            if (pos + 1 + (size_t)sll >= sizeof(buf)) {
                rt_trap("Viper.Localization.Locale: script subtag overflow");
                return NULL;
            }
            buf[pos++] = '-';
            memcpy(buf + pos, ss, (size_t)sll);
            pos += (size_t)sll;
        }
    }
    if (region) {
        const char *rs = rt_string_cstr(region);
        int64_t rl = rt_str_len(region);
        if (rs && rl > 0) {
            if (pos + 1 + (size_t)rl >= sizeof(buf)) {
                rt_trap("Viper.Localization.Locale: region subtag overflow");
                return NULL;
            }
            buf[pos++] = '-';
            memcpy(buf + pos, rs, (size_t)rl);
            pos += (size_t)rl;
        }
    }
    buf[pos] = '\0';

    rt_locale_t parsed;
    if (rt_locale_internal_parse_into(buf, pos, &parsed) != 0) {
        rt_trap("Viper.Localization.Locale: invalid subtag combination");
        return NULL;
    }
    rt_locale_t *loc = (rt_locale_t *)loc_alloc();
    *loc = parsed;
    loc->data = rt_locale_manager_lookup_data(loc->tag);
    return loc;
}

//===----------------------------------------------------------------------===//
// Accessors
//===----------------------------------------------------------------------===//

static rt_string loc_str(const char *s) {
    if (!s)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(s, strlen(s));
}

rt_string rt_locale_language(void *locale) {
    if (!locale)
        return rt_string_from_bytes("", 0);
    return loc_str(((rt_locale_t *)locale)->language);
}

rt_string rt_locale_script(void *locale) {
    if (!locale)
        return rt_string_from_bytes("", 0);
    return loc_str(((rt_locale_t *)locale)->script);
}

rt_string rt_locale_region(void *locale) {
    if (!locale)
        return rt_string_from_bytes("", 0);
    return loc_str(((rt_locale_t *)locale)->region);
}

rt_string rt_locale_tag(void *locale) {
    if (!locale)
        return rt_string_from_bytes("root", 4);
    rt_locale_t *l = (rt_locale_t *)locale;
    if (l->tag[0] == '\0')
        return rt_string_from_bytes("root", 4);
    return loc_str(l->tag);
}

rt_string rt_locale_to_string(void *locale) {
    return rt_locale_tag(locale);
}

int8_t rt_locale_equals(void *a, void *b) {
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    rt_locale_t *la = (rt_locale_t *)a;
    rt_locale_t *lb = (rt_locale_t *)b;
    // Compare canonical tags directly; both sides are already normalized so
    // strcmp suffices. Tag "root" and empty tag both represent invariant.
    const char *ta = la->tag[0] ? la->tag : "root";
    const char *tb = lb->tag[0] ? lb->tag : "root";
    return strcmp(ta, tb) == 0 ? (int8_t)1 : (int8_t)0;
}

//===----------------------------------------------------------------------===//
// Fallback chain
//===----------------------------------------------------------------------===//

void *rt_locale_fallbacks(void *locale) {
    void *list = rt_list_new();
    if (!list)
        return NULL;

    if (!locale) {
        // Invariant only.
        rt_list_push(list, loc_make_invariant());
        return list;
    }

    rt_locale_t *l = (rt_locale_t *)locale;

    // Short-circuit: the invariant ("root") locale's chain is just [root].
    if (l->language[0] == '\0') {
        rt_list_push(list, loc_make_invariant());
        return list;
    }

    // Emit the full handle first (a fresh copy so mutation on the original
    // doesn't leak through the list).
    rt_locale_t *full = (rt_locale_t *)loc_alloc();
    *full = *l;
    rt_list_push(list, full);

    // If a script is present, produce a `<lang>-<region>` step (drop script).
    if (l->script[0] != '\0' && l->region[0] != '\0') {
        rt_locale_t *step = (rt_locale_t *)loc_alloc();
        memcpy(step->language, l->language, RT_LOCALE_LANG_CAP);
        memcpy(step->region, l->region, RT_LOCALE_REGION_CAP);
        size_t pos = 0;
        size_t ll = strnlen(l->language, RT_LOCALE_LANG_CAP);
        memcpy(step->tag, l->language, ll);
        pos += ll;
        step->tag[pos++] = '-';
        size_t rl = strnlen(l->region, RT_LOCALE_REGION_CAP);
        memcpy(step->tag + pos, l->region, rl);
        pos += rl;
        step->tag[pos] = '\0';
        step->data = rt_locale_manager_lookup_data(step->tag);
        rt_list_push(list, step);
    }

    // Language-only step, unless the original was already language-only.
    if (l->language[0] != '\0' && (l->script[0] != '\0' || l->region[0] != '\0')) {
        rt_locale_t *step = (rt_locale_t *)loc_alloc();
        memcpy(step->language, l->language, RT_LOCALE_LANG_CAP);
        size_t ll = strnlen(l->language, RT_LOCALE_LANG_CAP);
        memcpy(step->tag, l->language, ll);
        step->tag[ll] = '\0';
        step->data = rt_locale_manager_lookup_data(step->tag);
        rt_list_push(list, step);
    }

    // Invariant terminator.
    rt_list_push(list, loc_make_invariant());
    return list;
}

//===----------------------------------------------------------------------===//
// Internal helpers used by LocaleManager / LocaleInfo
//===----------------------------------------------------------------------===//

void rt_locale_bind_data(void *locale, const rt_locale_data_t *data) {
    if (!locale)
        return;
    ((rt_locale_t *)locale)->data = data;
}

const rt_locale_data_t *rt_locale_get_data(void *locale) {
    if (!locale)
        return rt_locale_data_en_us();
    rt_locale_t *l = (rt_locale_t *)locale;
    if (l->data)
        return l->data;
    return rt_locale_data_en_us();
}
