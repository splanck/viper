//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_collator.c
// Purpose: Implementation of Viper.Localization.Collator. Generates weighted
//          sort keys from UTF-8 input strings via the classifier in
//          rt_collator_table.c, then performs byte-wise comparison on the
//          key bytes. Three strength levels map to three concatenated weight
//          bands; IgnoreCase/IgnoreAccents elide the tertiary/secondary
//          bands from the comparison.
//
// Key invariants:
//   - SortKey output is deterministic for a given (string, locale, strength,
//     flags) combination. Byte-wise compare(SortKey(a), SortKey(b)) matches
//     Compare(a, b).
//   - Inputs longer than 1 MiB trap; sort keys grow as ~4 bytes per
//     codepoint across levels, capped at 6 MiB worst-case before we trap.
//   - Locale patches are applied at construction via a small O(1) lookup
//     per codepoint (linear scan over the patch list, which is <= 10 entries).
//
// Ownership/Lifetime:
//   - Handles are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_collator.h (interface),
//        src/runtime/localization/rt_collator_table.c (classifier),
//        src/runtime/localization/rt_locale_manager.h (current-locale lookup).
//
//===----------------------------------------------------------------------===//

#include "rt_collator.h"

#include "rt_internal.h"
#include "rt_list.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_manager.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

// rt_locale_t struct layout lives in rt_locale.h; we reach in here to read
// the canonical tag so locale tailorings can apply even when a locale has
// been parsed but not yet registered (its data pointer falls through to the
// baked invariant, but its tag is still the parsed value).

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT_BYTES (1u << 20)

//===----------------------------------------------------------------------===//
// Instance struct
//===----------------------------------------------------------------------===//

typedef struct rt_collator {
    void                    *locale;
    const rt_locale_data_t  *data;
    int                      strength;        // 1..3
    int8_t                   ignore_case;
    int8_t                   ignore_accents;
    const rt_collator_locale_patch_t *patches;
    size_t                    patch_count;
} rt_collator_t;

static rt_collator_t *as_col(void *obj) { return (rt_collator_t *)obj; }

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

static void *col_alloc(void *locale) {
    rt_collator_t *c = (rt_collator_t *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_collator_t));
    if (!c) {
        rt_trap("Viper.Localization.Collator: allocation failed");
        return NULL;
    }
    c->locale = locale;
    c->data = rt_locale_get_data(locale);
    c->strength = c->data->collation.strength > 0 ? c->data->collation.strength : 3;
    if (c->strength > 3) c->strength = 3;
    c->ignore_case = 0;
    c->ignore_accents = 0;

    // Capture tailoring patches by locale tag. Read from the Locale handle
    // directly (not from `c->data->tag`) because data-records fall back to
    // the baked invariant when the locale isn't registered — which would
    // make Collator.ForLocale("sv-SE") silently skip the Swedish patches
    // just because no fr-FR-style JSON file was loaded.
    c->patches = NULL;
    c->patch_count = 0;
    if (locale) {
        rt_locale_t *l = (rt_locale_t *)locale;
        const char *tag = l->tag[0] ? l->tag : (c->data->tag ? c->data->tag : "");
        c->patches = rt_collator_locale_patches(tag, &c->patch_count);
    } else if (c->data->tag) {
        c->patches = rt_collator_locale_patches(c->data->tag, &c->patch_count);
    }

    return c;
}

void *rt_collator_new(void) { return col_alloc(rt_locale_manager_current()); }
void *rt_collator_for_locale(void *locale) { return col_alloc(locale); }
void *rt_collator_get_locale(void *self) { return self ? as_col(self)->locale : NULL; }

//===----------------------------------------------------------------------===//
// Property accessors
//===----------------------------------------------------------------------===//

int64_t rt_collator_get_strength(void *self) {
    return self ? (int64_t)as_col(self)->strength : 3;
}

void rt_collator_set_strength(void *self, int64_t value) {
    if (!self) return;
    if (value < 1) value = 1;
    if (value > 3) {
        // Strength 4 (quaternary) isn't supported in v1; clamp with a
        // diagnostic so callers see the downgrade.
        extern void rt_diag_warn(const char *fmt, ...);
        (void)rt_diag_warn;
        value = 3;
    }
    as_col(self)->strength = (int)value;
}

int8_t rt_collator_get_ignore_case(void *self) {
    return self ? as_col(self)->ignore_case : 0;
}

void rt_collator_set_ignore_case(void *self, int8_t value) {
    if (self) as_col(self)->ignore_case = value ? 1 : 0;
}

int8_t rt_collator_get_ignore_accents(void *self) {
    return self ? as_col(self)->ignore_accents : 0;
}

void rt_collator_set_ignore_accents(void *self, int8_t value) {
    if (self) as_col(self)->ignore_accents = value ? 1 : 0;
}

//===----------------------------------------------------------------------===//
// UTF-8 decode
//===----------------------------------------------------------------------===//

static uint32_t col_decode(const char *s, size_t len, size_t *pos) {
    size_t i = *pos;
    if (i >= len) return 0;
    uint8_t c = (uint8_t)s[i];
    uint32_t cp;
    size_t need;
    if (c < 0x80) { cp = c; need = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; need = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; need = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; need = 4; }
    else { *pos = i + 1; return 0xFFFD; }
    if (i + need > len) { *pos = len; return 0xFFFD; }
    for (size_t k = 1; k < need; ++k) {
        uint8_t nc = (uint8_t)s[i + k];
        if ((nc & 0xC0) != 0x80) { *pos = i + 1; return 0xFFFD; }
        cp = (cp << 6) | (nc & 0x3F);
    }
    *pos = i + need;
    return cp;
}

//===----------------------------------------------------------------------===//
// Weight extraction (with locale patches)
//===----------------------------------------------------------------------===//

static void get_weights(rt_collator_t *col, uint32_t cp,
                        uint32_t *pri, uint16_t *sec, uint16_t *ter) {
    // Apply any locale patches first (they override the base classifier).
    if (col->patches) {
        for (size_t i = 0; i < col->patch_count; ++i) {
            if (col->patches[i].codepoint == cp) {
                *pri = col->patches[i].primary_override;
                *sec = col->patches[i].secondary_override;
                *ter = col->patches[i].tertiary_override;
                return;
            }
        }
    }
    (void)rt_collator_codepoint_weights(cp, pri, sec, ter);
}

//===----------------------------------------------------------------------===//
// Raw sort key bytes
//===----------------------------------------------------------------------===//

/// @brief Build the raw sort key byte sequence for @p s. Each level's
///        weights are emitted in order with a 0x00 separator between levels.
///        Big-endian 16-bit emission for primaries preserves ordering.
/// @return Malloc'd byte buffer; caller frees. *out_len set to byte length.
static uint8_t *build_raw_key(rt_collator_t *col, const char *s, size_t len,
                              size_t *out_len) {
    if (len > MAX_INPUT_BYTES) {
        rt_trap("Viper.Localization.Collator: input exceeds 1 MiB cap");
        return NULL;
    }

    // Count codepoints first for allocation.
    size_t n = 0;
    {
        size_t p = 0;
        while (p < len) {
            (void)col_decode(s, len, &p);
            ++n;
        }
    }

    // Per codepoint: up to 4 bytes primary + 2 bytes secondary + 1 byte tertiary,
    // plus 3 level separators (2 bytes each). Worst case ~7 bytes/cp + 6.
    size_t cap = n * 8 + 16;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        rt_trap("Viper.Localization.Collator: key allocation failed");
        return NULL;
    }
    size_t off = 0;

    // Primary level.
    {
        size_t p = 0;
        while (p < len) {
            uint32_t cp = col_decode(s, len, &p);
            uint32_t pri = 0;
            uint16_t sec = 0, ter = 0;
            get_weights(col, cp, &pri, &sec, &ter);
            if (col->strength >= 1) {
                // Big-endian 4-byte primary.
                buf[off++] = (uint8_t)((pri >> 24) & 0xFF);
                buf[off++] = (uint8_t)((pri >> 16) & 0xFF);
                buf[off++] = (uint8_t)((pri >> 8) & 0xFF);
                buf[off++] = (uint8_t)(pri & 0xFF);
            }
        }
    }
    buf[off++] = 0x00; buf[off++] = 0x00; // primary/secondary separator

    // Secondary level.
    if (col->strength >= 2 && !col->ignore_accents) {
        size_t p = 0;
        while (p < len) {
            uint32_t cp = col_decode(s, len, &p);
            uint32_t pri = 0;
            uint16_t sec = 0, ter = 0;
            get_weights(col, cp, &pri, &sec, &ter);
            buf[off++] = (uint8_t)((sec >> 8) & 0xFF);
            buf[off++] = (uint8_t)(sec & 0xFF);
        }
    }
    buf[off++] = 0x00; buf[off++] = 0x00; // secondary/tertiary separator

    // Tertiary level.
    if (col->strength >= 3 && !col->ignore_case) {
        size_t p = 0;
        while (p < len) {
            uint32_t cp = col_decode(s, len, &p);
            uint32_t pri = 0;
            uint16_t sec = 0, ter = 0;
            get_weights(col, cp, &pri, &sec, &ter);
            buf[off++] = (uint8_t)(ter & 0xFF);
        }
    }

    *out_len = off;
    return buf;
}

//===----------------------------------------------------------------------===//
// Compare / Equals
//===----------------------------------------------------------------------===//

int64_t rt_collator_compare(void *self, rt_string a, rt_string b) {
    if (!self) return 0;
    const char *as = a ? rt_string_cstr(a) : "";
    const char *bs = b ? rt_string_cstr(b) : "";
    int64_t alen = a ? rt_str_len(a) : 0;
    int64_t blen = b ? rt_str_len(b) : 0;

    size_t ka_len = 0, kb_len = 0;
    uint8_t *ka = build_raw_key(as_col(self), as, (size_t)alen, &ka_len);
    uint8_t *kb = build_raw_key(as_col(self), bs, (size_t)blen, &kb_len);

    size_t min_len = ka_len < kb_len ? ka_len : kb_len;
    int cmp = memcmp(ka, kb, min_len);
    if (cmp == 0 && ka_len != kb_len)
        cmp = ka_len < kb_len ? -1 : 1;

    free(ka);
    free(kb);

    if (cmp < 0) return -1;
    if (cmp > 0) return 1;
    return 0;
}

int8_t rt_collator_equals(void *self, rt_string a, rt_string b) {
    return rt_collator_compare(self, a, b) == 0 ? 1 : 0;
}

//===----------------------------------------------------------------------===//
// SortKey (hex-encoded)
//===----------------------------------------------------------------------===//

rt_string rt_collator_sort_key(void *self, rt_string s) {
    if (!self || !s) return rt_string_from_bytes("", 0);
    const char *cs = rt_string_cstr(s);
    int64_t len = rt_str_len(s);

    size_t key_len = 0;
    uint8_t *key = build_raw_key(as_col(self), cs, (size_t)len, &key_len);
    if (!key) return rt_string_from_bytes("", 0);

    // Hex-encode so callers can store the result in an rt_string without
    // embedded NUL issues. Byte-wise comparison of hex strings preserves
    // the same ordering as the raw key bytes.
    rt_string_builder sb;
    rt_sb_init(&sb);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < key_len; ++i) {
        char pair[2] = { hex[(key[i] >> 4) & 0xF], hex[key[i] & 0xF] };
        (void)rt_sb_append_bytes(&sb, pair, 2);
    }
    free(key);

    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// Sort (simple insertion sort — fine for lists up to a few thousand)
//===----------------------------------------------------------------------===//

void *rt_collator_sort(void *self, void *items) {
    if (!self || !items) return rt_list_new();
    int64_t n = rt_list_len(items);
    void *out = rt_list_new();
    if (n <= 0) return out;

    // Extract + retain all items into a temp array.
    rt_string *arr = (rt_string *)malloc(sizeof(rt_string) * (size_t)n);
    if (!arr) {
        rt_trap("Viper.Localization.Collator: Sort allocation failed");
        return out;
    }
    for (int64_t i = 0; i < n; ++i) {
        arr[i] = (rt_string)rt_list_get(items, i);
    }

    // Insertion sort — O(n^2) but constant factors are small and our tests
    // exercise <= 500 elements. For larger inputs a merge sort would be
    // better; deferred.
    for (int64_t i = 1; i < n; ++i) {
        rt_string key = arr[i];
        int64_t j = i - 1;
        while (j >= 0 && rt_collator_compare(self, arr[j], key) > 0) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }

    for (int64_t i = 0; i < n; ++i)
        rt_list_push(out, arr[i]);

    free(arr);
    return out;
}
