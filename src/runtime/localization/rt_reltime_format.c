//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_reltime_format.c
// Purpose: Implementation of Zanna.Localization.RelativeTimeFormat. Selects a
//          unit from a duration, looks up the locale's plural category for
//          the unit magnitude, and expands the locale's past/future template
//          ("{n} {unit} ago" etc.).
//
// Key invariants:
//   - Duration values use the same millisecond encoding as rt_duration's
//     total_millis; thresholds match exactly to keep "2 hours ago" and
//     "in 2 hours" symmetric around sign.
//   - Plural category is resolved via PluralRules against the integer
//     count for the selected unit (e.g., "1" -> "one"; "5" -> "other").
//   - Unit tokens are looked up from the locale's reltime.units[].<cat>
//     map with fallback to the "other" form when a specific category is
//     absent in the locale data.
//
// Ownership/Lifetime:
//   - Instances are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_reltime_format.h (interface),
//        src/runtime/localization/rt_plural_rules.h (category evaluator),
//        src/runtime/localization/rt_locale_data.h (reltime templates).
//
//===----------------------------------------------------------------------===//

#include "rt_reltime_format.h"

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_manager.h"
#include "rt_object.h"
#include "rt_plural_rules.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Instance struct
//===----------------------------------------------------------------------===//

typedef enum {
    RTF_STYLE_LONG = 0,
    RTF_STYLE_SHORT = 1,
} rtf_style_t;

typedef struct rt_reltimefmt_inst {
    void *locale;
    const rt_locale_data_t *data;
    rtf_style_t style;
} rt_reltimefmt_inst_t;

/// @brief Unchecked cast of an opaque handle to the RelativeTimeFormat inst.
static rt_reltimefmt_inst_t *as_fmt(void *obj) {
    return (rt_reltimefmt_inst_t *)obj;
}

/// @brief Drop one GC reference to @p obj and free it if the count hit zero.
static void rtf_release_handle(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief GC finalizer: release the format's locale data and locale handle.
static void rtf_finalizer(void *obj) {
    rt_reltimefmt_inst_t *fmt = (rt_reltimefmt_inst_t *)obj;
    if (!fmt)
        return;
    rt_locale_manager_release_data(fmt->data);
    rtf_release_handle(fmt->locale);
    fmt->locale = NULL;
    fmt->data = NULL;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

/// @brief Allocate and initialize a GC-managed RelativeTimeFormat for @p locale.
/// @details Retains the locale handle + its data, defaults to the long style.
///          Traps on allocation failure; installs @ref rtf_finalizer.
static void *rtf_alloc(void *locale) {
    rt_reltimefmt_inst_t *fmt =
        (rt_reltimefmt_inst_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_reltimefmt_inst_t));
    if (!fmt) {
        rt_trap("Zanna.Localization.RelativeTimeFormat: allocation failed");
        return NULL;
    }
    fmt->locale = locale;
    if (fmt->locale)
        rt_heap_retain(fmt->locale);
    fmt->data = rt_locale_get_data(locale);
    rt_locale_manager_retain_data(fmt->data);
    fmt->style = RTF_STYLE_LONG;
    rt_obj_set_finalizer(fmt, rtf_finalizer);
    return fmt;
}

void *rt_reltimefmt_new(void) {
    void *current = rt_locale_manager_current();
    void *fmt = rtf_alloc(current);
    rtf_release_handle(current);
    return fmt;
}

void *rt_reltimefmt_for_locale(void *locale) {
    return rtf_alloc(locale);
}

void *rt_reltimefmt_get_locale(void *self) {
    return self ? as_fmt(self)->locale : NULL;
}

rt_string rt_reltimefmt_get_style(void *self) {
    rtf_style_t s = self ? as_fmt(self)->style : RTF_STYLE_LONG;
    const char *name = s == RTF_STYLE_SHORT ? "short" : "long";
    return rt_string_from_bytes(name, strlen(name));
}

void rt_reltimefmt_set_style(void *self, rt_string style) {
    if (!self || !style)
        return;
    const char *cs = rt_string_cstr(style);
    if (!cs)
        return;
    if (strcmp(cs, "short") == 0)
        as_fmt(self)->style = RTF_STYLE_SHORT;
    else if (strcmp(cs, "long") == 0)
        as_fmt(self)->style = RTF_STYLE_LONG;
    else
        rt_trap("Zanna.Localization.RelativeTimeFormat: unknown style (expected long/short)");
}

//===----------------------------------------------------------------------===//
// Unit table
//===----------------------------------------------------------------------===//

typedef enum {
    UNIT_SECOND = 0,
    UNIT_MINUTE = 1,
    UNIT_HOUR = 2,
    UNIT_DAY = 3,
    UNIT_WEEK = 4,
    UNIT_MONTH = 5,
    UNIT_YEAR = 6,
} rtf_unit_t;

/// @brief CLDR keyword for a time unit ("second".."year"); defaults "second".
static const char *unit_name(rtf_unit_t u) {
    switch (u) {
        case UNIT_SECOND:
            return "second";
        case UNIT_MINUTE:
            return "minute";
        case UNIT_HOUR:
            return "hour";
        case UNIT_DAY:
            return "day";
        case UNIT_WEEK:
            return "week";
        case UNIT_MONTH:
            return "month";
        case UNIT_YEAR:
            return "year";
    }
    return "second";
}

/// @brief Parse a unit keyword into @p out; returns 1 on match, 0 if unknown.
static int unit_from_name(const char *name, rtf_unit_t *out) {
    if (!name)
        return 0;
    if (strcmp(name, "second") == 0) {
        *out = UNIT_SECOND;
        return 1;
    }
    if (strcmp(name, "minute") == 0) {
        *out = UNIT_MINUTE;
        return 1;
    }
    if (strcmp(name, "hour") == 0) {
        *out = UNIT_HOUR;
        return 1;
    }
    if (strcmp(name, "day") == 0) {
        *out = UNIT_DAY;
        return 1;
    }
    if (strcmp(name, "week") == 0) {
        *out = UNIT_WEEK;
        return 1;
    }
    if (strcmp(name, "month") == 0) {
        *out = UNIT_MONTH;
        return 1;
    }
    if (strcmp(name, "year") == 0) {
        *out = UNIT_YEAR;
        return 1;
    }
    return 0;
}

//===----------------------------------------------------------------------===//
// Unit selection from milliseconds
//===----------------------------------------------------------------------===//

typedef struct {
    rtf_unit_t unit;
    int64_t count; // absolute count in the selected unit
} unit_pick_t;

/// @brief Choose the largest unit whose threshold @p abs_ms reaches and the
///        integer count in that unit (CLDR-style coarsening; month≈30d,
///        year≈365d), e.g. 90 min → {hour, 1}.
static unit_pick_t pick_unit(uint64_t abs_ms) {
    unit_pick_t p;
    const uint64_t MS_SEC = 1000ULL;
    const uint64_t MS_MIN = 60ULL * MS_SEC;
    const uint64_t MS_HOUR = 60ULL * MS_MIN;
    const uint64_t MS_DAY = 24ULL * MS_HOUR;
    const uint64_t MS_WEEK = 7ULL * MS_DAY;
    const uint64_t MS_MONTH = 30ULL * MS_DAY;
    const uint64_t MS_YEAR = 365ULL * MS_DAY;

    if (abs_ms >= MS_YEAR) {
        p.unit = UNIT_YEAR;
        p.count = abs_ms / MS_YEAR;
        return p;
    }
    if (abs_ms >= MS_MONTH) {
        p.unit = UNIT_MONTH;
        p.count = abs_ms / MS_MONTH;
        return p;
    }
    if (abs_ms >= MS_WEEK) {
        p.unit = UNIT_WEEK;
        p.count = abs_ms / MS_WEEK;
        return p;
    }
    if (abs_ms >= MS_DAY) {
        p.unit = UNIT_DAY;
        p.count = abs_ms / MS_DAY;
        return p;
    }
    if (abs_ms >= MS_HOUR) {
        p.unit = UNIT_HOUR;
        p.count = abs_ms / MS_HOUR;
        return p;
    }
    if (abs_ms >= MS_MIN) {
        p.unit = UNIT_MINUTE;
        p.count = abs_ms / MS_MIN;
        return p;
    }
    p.unit = UNIT_SECOND;
    p.count = abs_ms / MS_SEC;
    return p;
}

//===----------------------------------------------------------------------===//
// Plural category -> unit string lookup
//===----------------------------------------------------------------------===//

/// @brief Select the unit phrase for plural category @p cat, falling back to
///        the "other" form (then the empty string) when the category form is
///        absent.
static const char *unit_plural_form(const rt_locdata_reltime_unit_t *u, rt_plural_category_t cat) {
    const char *result = NULL;
    switch (cat) {
        case RT_PLURAL_ZERO:
            result = u->zero;
            break;
        case RT_PLURAL_ONE:
            result = u->one;
            break;
        case RT_PLURAL_TWO:
            result = u->two;
            break;
        case RT_PLURAL_FEW:
            result = u->few;
            break;
        case RT_PLURAL_MANY:
            result = u->many;
            break;
        case RT_PLURAL_OTHER:
        default:
            result = u->other;
            break;
    }
    if (!result)
        result = u->other;
    if (!result)
        result = "";
    return result;
}

//===----------------------------------------------------------------------===//
// Template expansion: "{n} {unit} ago" etc.
//===----------------------------------------------------------------------===//

typedef struct digit_spans {
    const char *ptr[10];
    size_t len[10];
    int valid;
} digit_spans_t;

/// @brief Byte length of the leading UTF-8 codepoint in @p s (0 if empty/NULL,
///        1 on a malformed lead byte so callers always make forward progress).
static size_t utf8_cp_len(const char *s) {
    if (!s || !*s)
        return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0 && s[1])
        return 2;
    if ((c & 0xF0) == 0xE0 && s[1] && s[2])
        return 3;
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3])
        return 4;
    return 1;
}

/// @brief Slice the locale's 10 numbering-system digit glyphs into a span table
///        (falls back to ASCII; @c ds.valid only when exactly ten consumed).
static digit_spans_t digit_spans_from_locale(const rt_locale_data_t *data) {
    digit_spans_t ds;
    memset(&ds, 0, sizeof(ds));
    const char *digits = data && data->numbers.digits ? data->numbers.digits : "0123456789";
    const char *p = digits;
    for (int i = 0; i < 10; ++i) {
        size_t n = utf8_cp_len(p);
        if (n == 0) {
            ds.valid = 0;
            return ds;
        }
        ds.ptr[i] = p;
        ds.len[i] = n;
        p += n;
    }
    ds.valid = *p == '\0';
    return ds;
}

/// @brief Append @p n as decimal digits, transliterated to locale digit glyphs.
/// @details Returns a status instead of silently ignoring builder growth
///          failures so callers can avoid emitting partial relative-time text
///          under allocation pressure.
/// @param sb Destination builder.
/// @param data Locale data supplying native digit glyphs.
/// @param n Integer count to append.
/// @return 1 on success, 0 when formatting or append work failed.
static int append_localized_int(rt_string_builder *sb, const rt_locale_data_t *data, uint64_t n) {
    char num[32];
    int k = snprintf(num, sizeof(num), "%llu", (unsigned long long)n);
    if (k <= 0)
        return 0;
    if ((size_t)k >= sizeof(num))
        return 0;
    digit_spans_t ds = digit_spans_from_locale(data);
    for (int i = 0; i < k; ++i) {
        unsigned char c = (unsigned char)num[i];
        if (ds.valid && c >= '0' && c <= '9') {
            int d = (int)(c - '0');
            if (rt_sb_append_bytes(sb, ds.ptr[d], ds.len[d]) != RT_SB_OK)
                return 0;
        } else {
            if (rt_sb_append_bytes(sb, num + i, 1) != RT_SB_OK)
                return 0;
        }
    }
    return 1;
}

/// @brief Expand a relative-time template into @p sb, substituting "{n}" with
///        the localized count and "{unit}" with the resolved unit phrase.
/// @return 1 on success, 0 if an append operation failed.
static int expand_template(rt_string_builder *sb,
                           const char *tmpl,
                           const rt_locale_data_t *data,
                           uint64_t n,
                           const char *unit_form) {
    if (!tmpl || !*tmpl)
        return 1;
    const char *p = tmpl;
    while (*p) {
        if (p[0] == '{' && p[1] == 'n' && p[2] == '}') {
            if (!append_localized_int(sb, data, n))
                return 0;
            p += 3;
        } else if (p[0] == '{' && p[1] == 'u' && p[2] == 'n' && p[3] == 'i' && p[4] == 't' &&
                   p[5] == '}') {
            if (rt_sb_append_cstr(sb, unit_form) != RT_SB_OK)
                return 0;
            p += 6;
        } else {
            if (rt_sb_append_bytes(sb, p, 1) != RT_SB_OK)
                return 0;
            ++p;
        }
    }
    return 1;
}

//===----------------------------------------------------------------------===//
// Core formatter
//===----------------------------------------------------------------------===//

/// @brief Render a signed millisecond @p duration as a relative-time phrase.
/// @details Sub-second magnitudes yield the locale's "now". Otherwise picks the
///          coarsest unit (@ref pick_unit), resolves the plural form for that
///          count, selects the past (duration >= 0, "ago") vs. future
///          (duration < 0, "in") template at the requested @p style, and
///          expands it. Magnitudes are tracked unsigned so INT64_MIN keeps
///          its exact absolute value (VDOC-073).
static rt_string format_core(rt_reltimefmt_inst_t *fmt, int64_t duration, rtf_style_t style) {
    int is_past = duration >= 0;
    uint64_t abs_ms = duration == INT64_MIN ? (uint64_t)INT64_MAX + 1
                                            : (uint64_t)(duration < 0 ? -duration : duration);
    if (abs_ms < 1000) {
        const char *now = fmt->data->reltime.now ? fmt->data->reltime.now : "now";
        return rt_string_from_bytes(now, strlen(now));
    }

    unit_pick_t pick = pick_unit(abs_ms);

    rt_plural_category_t cat = rt_plural_rules_select_cardinal_int(fmt->data, pick.count);
    const rt_locdata_reltime_unit_t *units =
        style == RTF_STYLE_SHORT ? fmt->data->reltime.short_units : fmt->data->reltime.units;
    const rt_locdata_reltime_unit_t *u = &units[(int)pick.unit];
    const char *unit_form = unit_plural_form(u, cat);

    const char *tmpl = NULL;
    if (style == RTF_STYLE_SHORT)
        tmpl = is_past ? fmt->data->reltime.short_past : fmt->data->reltime.short_future;
    if (!tmpl || !*tmpl)
        tmpl = is_past ? fmt->data->reltime.past : fmt->data->reltime.future;
    if (!tmpl || !*tmpl)
        tmpl = is_past ? "{n} {unit} ago" : "in {n} {unit}";

    rt_string_builder sb;
    rt_sb_init(&sb);
    if (!expand_template(&sb, tmpl, fmt->data, pick.count, unit_form)) {
        rt_sb_free(&sb);
        return rt_string_from_bytes("", 0);
    }
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// Public format methods
//===----------------------------------------------------------------------===//

rt_string rt_reltimefmt_format(void *self, int64_t duration) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_reltimefmt_inst_t *fmt = as_fmt(self);
    return format_core(fmt, duration, fmt->style);
}

rt_string rt_reltimefmt_format_from(void *self, int64_t then_ts, int64_t now_ts) {
    if (!self)
        return rt_string_from_bytes("", 0);
    // Both timestamps are Unix seconds; convert the delta to milliseconds so
    // the core formatter sees the same unit as rt_duration's total_millis.
    // Check for overflow before the multiply by 1000.
    if ((then_ts < 0 && now_ts > INT64_MAX + then_ts) ||
        (then_ts > 0 && now_ts < INT64_MIN + then_ts)) {
        rt_trap("Zanna.Localization.RelativeTimeFormat: FormatFrom delta overflow");
        return rt_string_from_bytes("", 0);
    }
    int64_t delta_sec = now_ts - then_ts;
    int64_t delta_ms;
    if (delta_sec > INT64_MAX / 1000 || delta_sec < INT64_MIN / 1000) {
        rt_trap("Zanna.Localization.RelativeTimeFormat: FormatFrom delta overflow");
        return rt_string_from_bytes("", 0);
    }
    delta_ms = delta_sec * 1000;
    rt_reltimefmt_inst_t *fmt = as_fmt(self);
    return format_core(fmt, delta_ms, fmt->style);
}

rt_string rt_reltimefmt_short(void *self, int64_t duration) {
    if (!self)
        return rt_string_from_bytes("", 0);
    return format_core(as_fmt(self), duration, RTF_STYLE_SHORT);
}

rt_string rt_reltimefmt_long(void *self, int64_t duration) {
    if (!self)
        return rt_string_from_bytes("", 0);
    return format_core(as_fmt(self), duration, RTF_STYLE_LONG);
}

rt_string rt_reltimefmt_numeric(void *self, int64_t value, rt_string unit) {
    if (!self || !unit) {
        rt_trap("Zanna.Localization.RelativeTimeFormat: Numeric requires a unit");
        return rt_string_from_bytes("", 0);
    }
    const char *unit_cs = rt_string_cstr(unit);
    rtf_unit_t u;
    if (!unit_from_name(unit_cs, &u)) {
        rt_trap("Zanna.Localization.RelativeTimeFormat: unknown unit name");
        return rt_string_from_bytes("", 0);
    }
    rt_reltimefmt_inst_t *fmt = as_fmt(self);

    int is_past = value >= 0;
    // Track the magnitude unsigned so INT64_MIN keeps its exact absolute
    // value (VDOC-073).
    uint64_t count = value == INT64_MIN ? (uint64_t)INT64_MAX + 1
                                        : (uint64_t)(value < 0 ? -value : value);
    if (count == 0) {
        const char *now = fmt->data->reltime.now ? fmt->data->reltime.now : "now";
        return rt_string_from_bytes(now, strlen(now));
    }

    // Plural selection is signed; clamp the one value beyond INT64_MAX
    // (categories for magnitudes this large are indistinguishable).
    int64_t plural_n = count > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)count;
    rt_plural_category_t cat = rt_plural_rules_select_cardinal_int(fmt->data, plural_n);
    const rt_locdata_reltime_unit_t *units =
        fmt->style == RTF_STYLE_SHORT ? fmt->data->reltime.short_units : fmt->data->reltime.units;
    const rt_locdata_reltime_unit_t *ud = &units[(int)u];
    const char *unit_form = unit_plural_form(ud, cat);
    (void)unit_name; // silence unused-static warning on strict builds

    const char *tmpl = NULL;
    if (fmt->style == RTF_STYLE_SHORT)
        tmpl = is_past ? fmt->data->reltime.short_past : fmt->data->reltime.short_future;
    if (!tmpl || !*tmpl)
        tmpl = is_past ? fmt->data->reltime.past : fmt->data->reltime.future;
    if (!tmpl || !*tmpl)
        tmpl = is_past ? "{n} {unit} ago" : "in {n} {unit}";

    rt_string_builder sb;
    rt_sb_init(&sb);
    if (!expand_template(&sb, tmpl, fmt->data, count, unit_form)) {
        rt_sb_free(&sb);
        return rt_string_from_bytes("", 0);
    }
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}
