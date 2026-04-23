//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_data.h
// Purpose: Internal record for per-locale data (number separators, currency
//          symbols, date names and patterns, relative-time strings, plural
//          rule trees, list-join templates, collation tailorings). Shared
//          between the C-baked en-US table (rt_locale_data_en_us.c) and the
//          (Phase 2+) JSON/VPA loader so both paths produce structurally
//          identical records consumable by NumberFormat, DateFormat, etc.
//
// Key invariants:
//   - Every pointer field is either a static immortal string literal (baked
//     locale) or an arena-allocated copy whose lifetime matches the struct's
//     `arena` field. Never mix ownership models within a single record.
//   - `arena == NULL` is the sentinel for a statically baked record and
//     signals "do not free on Unload".
//   - months_wide/months_abbr arrays carry exactly 12 entries. days_wide /
//     days_abbr carry exactly 7 (index 0 = Sunday, per ISO weekday
//     convention used across the runtime).
//   - Plural rule heads may be NULL only for the invariant ("root") locale;
//     every real locale MUST populate both cardinal and ordinal rule chains.
//
// Ownership/Lifetime:
//   - Records are owned by LocaleManager's registry once registered.
//   - The registry never frees baked records (arena == NULL); JSON-loaded
//     records free the arena on Unload().
//
// Links: src/runtime/localization/rt_locale_data_en_us.c (baked seed),
//        src/runtime/localization/rt_locale_manager.c (registry owner).
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Categories for plural rule evaluation.
/// @details Matches the six CLDR plural categories. A locale only populates
///          the categories it uses; queries against missing categories fall
///          through to RT_PLURAL_OTHER.
typedef enum {
    RT_PLURAL_OTHER = 0,
    RT_PLURAL_ZERO,
    RT_PLURAL_ONE,
    RT_PLURAL_TWO,
    RT_PLURAL_FEW,
    RT_PLURAL_MANY,
} rt_plural_category_t;

/// @brief AST node kind for the plural rule mini-language.
/// @details Documented grammar in docs/viperlib/localization/data-files.md.
///          Kept as a small enum so the evaluator switches on it without
///          touching branch predictors beyond the 8-node working set.
typedef enum {
    RT_PRN_TRUE = 0,   ///< always-matches literal
    RT_PRN_OR,         ///< logical OR of two children
    RT_PRN_AND,        ///< logical AND of two children
    RT_PRN_EQ,         ///< equality comparison between two expressions
    RT_PRN_NE,         ///< inequality comparison
    RT_PRN_VAR,        ///< variable expression (possibly with a `mod N` tail)
    RT_PRN_INT,        ///< integer literal
} rt_plural_rule_kind_t;

/// @brief Variable identifiers used by the plural-rule mini-language.
/// @details Computed once from the numeric input before rule evaluation.
typedef enum {
    RT_PVAR_N = 0, ///< absolute value
    RT_PVAR_I,     ///< integer part
    RT_PVAR_V,     ///< visible fraction digit count with trailing zeros
    RT_PVAR_F,     ///< visible fraction digits with trailing zeros (as int)
    RT_PVAR_T,     ///< visible fraction digits without trailing zeros (as int)
} rt_plural_var_t;

/// @brief Plural rule AST node.
typedef struct rt_plural_rule_node {
    rt_plural_rule_kind_t kind;
    union {
        struct {
            struct rt_plural_rule_node *l;
            struct rt_plural_rule_node *r;
        } bin;
        struct {
            rt_plural_var_t var;
            int32_t mod;    ///< 0 means no `mod` operator
        } var;
        int64_t int_val;
    } u;
} rt_plural_rule_node_t;

/// @brief One entry in a locale's plural rule chain.
/// @details The chain is walked in array order; first rule whose `head`
///          evaluates truthy wins. A catch-all RT_PRN_TRUE terminator is
///          required for every chain.
typedef struct rt_plural_rule_entry {
    rt_plural_category_t     category;
    rt_plural_rule_node_t   *head;    ///< NULL is illegal
} rt_plural_rule_entry_t;

/// @brief Locale's numeric formatting conventions.
typedef struct rt_locdata_numbers {
    const char *decimal_sep;  ///< e.g. "." (en-US) or "," (fr-FR)
    const char *group_sep;    ///< e.g. "," (en-US) or U+00A0 (fr-FR)
    int32_t     group_size;   ///< typically 3; some locales use 3-then-2
    const char *minus;
    const char *plus;
    const char *percent;
    const char *infinity;
    const char *nan;
    const char *exponent;
    const char *digits;       ///< 10-codepoint digit set; ASCII 0-9 for Latin locales
} rt_locdata_numbers_t;

/// @brief Locale's currency defaults.
typedef struct rt_locdata_currency {
    const char *default_code;      ///< ISO-4217 three-letter code (e.g. "USD")
    const char *symbol;            ///< currency symbol (e.g. "$", "€")
    const char *pattern_positive;  ///< layout template with {n} (number) and {s} (symbol)
    const char *pattern_negative;
    int32_t     fraction_digits;
} rt_locdata_currency_t;

/// @brief CLDR-style date / time pattern bundle for a locale.
typedef struct rt_locdata_dates_patterns {
    const char *short_p;
    const char *medium_p;
    const char *long_p;
    const char *full_p;
    const char *time_short;
    const char *time_medium;
} rt_locdata_dates_patterns_t;

/// @brief Locale's calendar and date-formatting data.
typedef struct rt_locdata_dates {
    const char *const *months_wide; ///< pointer to 12-entry array
    const char *const *months_abbr;
    const char *const *days_wide;   ///< pointer to 7-entry array (index 0 = Sunday)
    const char *const *days_abbr;
    const char *am;
    const char *pm;
    rt_locdata_dates_patterns_t patterns;
} rt_locdata_dates_t;

/// @brief Per-unit plural forms for relative-time formatting.
/// @details Not every category is populated for every locale; the formatter
///          falls through to RT_PLURAL_OTHER when a specific category is NULL.
typedef struct rt_locdata_reltime_unit {
    const char *other;
    const char *zero;
    const char *one;
    const char *two;
    const char *few;
    const char *many;
} rt_locdata_reltime_unit_t;

/// @brief Locale's relative-time formatting strings.
/// @details units index: 0=second, 1=minute, 2=hour, 3=day, 4=week, 5=month,
///          6=year. `past` and `future` are the enclosing templates with
///          {n} (number) and {unit} (selected unit string) placeholders.
typedef struct rt_locdata_reltime {
    const char *past;
    const char *future;
    rt_locdata_reltime_unit_t units[7];
} rt_locdata_reltime_t;

/// @brief List-joining templates for one style (and/or/unit).
/// @details Each template is a two-placeholder {0} and {1} string.
typedef struct rt_locdata_list_style {
    const char *pair;    ///< exactly 2 items
    const char *start;   ///< first item + rest
    const char *middle;  ///< middle combinations in 3+ lists
    const char *end;     ///< last item
} rt_locdata_list_style_t;

/// @brief Locale's list-formatting templates across styles.
typedef struct rt_locdata_list {
    rt_locdata_list_style_t and_p;  ///< "A, B, and C"
    rt_locdata_list_style_t or_p;   ///< "A, B, or C"
    rt_locdata_list_style_t unit_p; ///< "A B C" (no conjunction)
} rt_locdata_list_t;

/// @brief Collation tailoring metadata.
/// @details v1 stores strength and a reorder array placeholder; actual
///          collation weight overrides are applied inside rt_collator_table.c
///          via locale-specific patch tables keyed by `tag`.
typedef struct rt_locdata_collation {
    int32_t     strength;    ///< 1-3 valid; 4 warned and clamped to 3
    const int32_t *reorder;  ///< optional array of codepoints; may be NULL
    size_t      reorder_len;
} rt_locdata_collation_t;

/// @brief Top-level locale-data record referenced by Locale handles.
typedef struct rt_locale_data {
    const char *tag;                 ///< canonical BCP-47 identifier
    struct {
        const char *language;        ///< display name in native language
        const char *region;
        const char *display;         ///< combined display name
    } names;
    char         text_direction[4];  ///< "ltr" or "rtl"
    int32_t      first_day_of_week;  ///< 0=Sun..6=Sat
    char         measurement[8];     ///< "metric" / "us" / "uk"

    rt_locdata_numbers_t    numbers;
    rt_locdata_currency_t   currency;
    rt_locdata_dates_t      dates;
    rt_locdata_reltime_t    reltime;

    const rt_plural_rule_entry_t *plural_cardinal;  ///< NULL-terminator-free; `cardinal_count` is authoritative
    size_t                         cardinal_count;
    const rt_plural_rule_entry_t *plural_ordinal;
    size_t                         ordinal_count;

    rt_locdata_list_t       list_format;
    rt_locdata_collation_t  collation;

    /// @brief Opaque arena pointer for JSON-loaded records; NULL for baked.
    /// @details When non-NULL, LocaleManager.Unload frees this pointer via
    ///          free() after removing the registry entry.
    void *arena;

    /// @brief Live formatter/collator count holding this data pointer.
    /// @details Incremented by every NumberFormat/DateFormat/etc. constructor
    ///          that captures this record; decremented on instance finalize.
    ///          Atomic in rt_locale_manager.c. Non-zero at Unload() time
    ///          traps with a "locale in use" diagnostic.
    int64_t formatter_refs;
} rt_locale_data_t;

/// @brief Retrieve the statically baked en-US data record.
/// @details Returns a pointer to a process-lifetime constant; the pointer
///          is valid for the entire program run. Used by LocaleManager
///          bootstrap and by `LoadBuiltin("en-US")`.
const rt_locale_data_t *rt_locale_data_en_us(void);

#ifdef __cplusplus
}
#endif
