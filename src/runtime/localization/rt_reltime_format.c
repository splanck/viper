//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_reltime_format.c
// Purpose: Implementation of Viper.Localization.RelativeTimeFormat. Selects a
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
    void                   *locale;
    const rt_locale_data_t *data;
    rtf_style_t             style;
} rt_reltimefmt_inst_t;

static rt_reltimefmt_inst_t *as_fmt(void *obj) {
    return (rt_reltimefmt_inst_t *)obj;
}

static void rtf_finalizer(void *obj) {
    rt_reltimefmt_inst_t *fmt = (rt_reltimefmt_inst_t *)obj;
    if (!fmt)
        return;
    rt_locale_manager_release_data(fmt->data);
    if (fmt->locale)
        rt_heap_release(fmt->locale);
    fmt->locale = NULL;
    fmt->data = NULL;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

static void *rtf_alloc(void *locale) {
    rt_reltimefmt_inst_t *fmt = (rt_reltimefmt_inst_t *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_reltimefmt_inst_t));
    if (!fmt) {
        rt_trap("Viper.Localization.RelativeTimeFormat: allocation failed");
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
    if (current)
        rt_heap_release(current);
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
    if (!self || !style) return;
    const char *cs = rt_string_cstr(style);
    if (!cs) return;
    if (strcmp(cs, "short") == 0)
        as_fmt(self)->style = RTF_STYLE_SHORT;
    else
        as_fmt(self)->style = RTF_STYLE_LONG;
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

static const char *unit_name(rtf_unit_t u) {
    switch (u) {
        case UNIT_SECOND: return "second";
        case UNIT_MINUTE: return "minute";
        case UNIT_HOUR:   return "hour";
        case UNIT_DAY:    return "day";
        case UNIT_WEEK:   return "week";
        case UNIT_MONTH:  return "month";
        case UNIT_YEAR:   return "year";
    }
    return "second";
}

static int unit_from_name(const char *name, rtf_unit_t *out) {
    if (!name) return 0;
    if (strcmp(name, "second") == 0) { *out = UNIT_SECOND; return 1; }
    if (strcmp(name, "minute") == 0) { *out = UNIT_MINUTE; return 1; }
    if (strcmp(name, "hour")   == 0) { *out = UNIT_HOUR;   return 1; }
    if (strcmp(name, "day")    == 0) { *out = UNIT_DAY;    return 1; }
    if (strcmp(name, "week")   == 0) { *out = UNIT_WEEK;   return 1; }
    if (strcmp(name, "month")  == 0) { *out = UNIT_MONTH;  return 1; }
    if (strcmp(name, "year")   == 0) { *out = UNIT_YEAR;   return 1; }
    return 0;
}

//===----------------------------------------------------------------------===//
// Unit selection from milliseconds
//===----------------------------------------------------------------------===//

typedef struct {
    rtf_unit_t unit;
    int64_t    count;   // absolute count in the selected unit
} unit_pick_t;

static unit_pick_t pick_unit(int64_t abs_ms) {
    unit_pick_t p;
    const int64_t MS_SEC   = 1000LL;
    const int64_t MS_MIN   = 60LL * MS_SEC;
    const int64_t MS_HOUR  = 60LL * MS_MIN;
    const int64_t MS_DAY   = 24LL * MS_HOUR;
    const int64_t MS_WEEK  = 7LL  * MS_DAY;
    const int64_t MS_MONTH = 30LL * MS_DAY;
    const int64_t MS_YEAR  = 365LL * MS_DAY;

    if (abs_ms >= MS_YEAR)  { p.unit = UNIT_YEAR;   p.count = abs_ms / MS_YEAR;  return p; }
    if (abs_ms >= MS_MONTH) { p.unit = UNIT_MONTH;  p.count = abs_ms / MS_MONTH; return p; }
    if (abs_ms >= MS_WEEK)  { p.unit = UNIT_WEEK;   p.count = abs_ms / MS_WEEK;  return p; }
    if (abs_ms >= MS_DAY)   { p.unit = UNIT_DAY;    p.count = abs_ms / MS_DAY;   return p; }
    if (abs_ms >= MS_HOUR)  { p.unit = UNIT_HOUR;   p.count = abs_ms / MS_HOUR;  return p; }
    if (abs_ms >= MS_MIN)   { p.unit = UNIT_MINUTE; p.count = abs_ms / MS_MIN;   return p; }
    p.unit = UNIT_SECOND;
    p.count = abs_ms / MS_SEC;
    return p;
}

//===----------------------------------------------------------------------===//
// Plural category -> unit string lookup
//===----------------------------------------------------------------------===//

static const char *unit_plural_form(const rt_locdata_reltime_unit_t *u,
                                    rt_plural_category_t cat) {
    const char *result = NULL;
    switch (cat) {
        case RT_PLURAL_ZERO:  result = u->zero;  break;
        case RT_PLURAL_ONE:   result = u->one;   break;
        case RT_PLURAL_TWO:   result = u->two;   break;
        case RT_PLURAL_FEW:   result = u->few;   break;
        case RT_PLURAL_MANY:  result = u->many;  break;
        case RT_PLURAL_OTHER:
        default:              result = u->other; break;
    }
    if (!result) result = u->other;
    if (!result) result = "";
    return result;
}

//===----------------------------------------------------------------------===//
// Template expansion: "{n} {unit} ago" etc.
//===----------------------------------------------------------------------===//

static void expand_template(rt_string_builder *sb, const char *tmpl,
                            int64_t n, const char *unit_form) {
    if (!tmpl || !*tmpl)
        return;
    const char *p = tmpl;
    while (*p) {
        if (p[0] == '{' && p[1] == 'n' && p[2] == '}') {
            char num[32];
            int k = snprintf(num, sizeof(num), "%lld", (long long)n);
            if (k > 0)
                (void)rt_sb_append_bytes(sb, num, (size_t)k);
            p += 3;
        } else if (p[0] == '{' && p[1] == 'u' && p[2] == 'n' && p[3] == 'i'
                   && p[4] == 't' && p[5] == '}') {
            (void)rt_sb_append_cstr(sb, unit_form);
            p += 6;
        } else {
            (void)rt_sb_append_bytes(sb, p, 1);
            ++p;
        }
    }
}

//===----------------------------------------------------------------------===//
// Core formatter
//===----------------------------------------------------------------------===//

static rt_string format_core(rt_reltimefmt_inst_t *fmt, int64_t duration) {
    int is_past = duration >= 0;
    int64_t abs_ms = duration < 0
                         ? (duration == INT64_MIN ? INT64_MAX : -duration)
                         : duration;
    if (abs_ms < 1000)
        return rt_string_from_bytes("now", 3);

    unit_pick_t pick = pick_unit(abs_ms);

    rt_plural_category_t cat =
        rt_plural_rules_select_cardinal_int(fmt->data, pick.count);
    const rt_locdata_reltime_unit_t *u = &fmt->data->reltime.units[(int)pick.unit];
    const char *unit_form = unit_plural_form(u, cat);

    const char *tmpl = is_past ? fmt->data->reltime.past : fmt->data->reltime.future;
    if (!tmpl || !*tmpl)
        tmpl = is_past ? "{n} {unit} ago" : "in {n} {unit}";

    rt_string_builder sb;
    rt_sb_init(&sb);
    expand_template(&sb, tmpl, pick.count, unit_form);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// Public format methods
//===----------------------------------------------------------------------===//

rt_string rt_reltimefmt_format(void *self, int64_t duration) {
    if (!self) return rt_string_from_bytes("", 0);
    return format_core(as_fmt(self), duration);
}

rt_string rt_reltimefmt_format_from(void *self, int64_t then_ts, int64_t now_ts) {
    if (!self) return rt_string_from_bytes("", 0);
    // Both timestamps are Unix seconds; convert the delta to milliseconds so
    // the core formatter sees the same unit as rt_duration's total_millis.
    // Check for overflow before the multiply by 1000.
    if ((then_ts < 0 && now_ts > INT64_MAX + then_ts) ||
        (then_ts > 0 && now_ts < INT64_MIN + then_ts)) {
        rt_trap("Viper.Localization.RelativeTimeFormat: FormatFrom delta overflow");
        return rt_string_from_bytes("", 0);
    }
    int64_t delta_sec = now_ts - then_ts;
    int64_t delta_ms;
    if (delta_sec > INT64_MAX / 1000 || delta_sec < INT64_MIN / 1000) {
        rt_trap("Viper.Localization.RelativeTimeFormat: FormatFrom delta overflow");
        return rt_string_from_bytes("", 0);
    }
    delta_ms = delta_sec * 1000;
    return format_core(as_fmt(self), delta_ms);
}

rt_string rt_reltimefmt_short(void *self, int64_t duration) {
    // Phase 3: short and long share the same template set in the baked
    // en-US record. When JSON-loaded locales provide separate tables in a
    // future phase, route here via the style flag.
    return rt_reltimefmt_format(self, duration);
}

rt_string rt_reltimefmt_long(void *self, int64_t duration) {
    return rt_reltimefmt_format(self, duration);
}

rt_string rt_reltimefmt_numeric(void *self, int64_t value, rt_string unit) {
    if (!self || !unit) {
        rt_trap("Viper.Localization.RelativeTimeFormat: Numeric requires a unit");
        return rt_string_from_bytes("", 0);
    }
    const char *unit_cs = rt_string_cstr(unit);
    rtf_unit_t u;
    if (!unit_from_name(unit_cs, &u)) {
        rt_trap("Viper.Localization.RelativeTimeFormat: unknown unit name");
        return rt_string_from_bytes("", 0);
    }
    rt_reltimefmt_inst_t *fmt = as_fmt(self);

    int is_past = value >= 0;
    int64_t count = value < 0 ? (value == INT64_MIN ? INT64_MAX : -value) : value;
    if (count == 0)
        return rt_string_from_bytes("now", 3);

    rt_plural_category_t cat =
        rt_plural_rules_select_cardinal_int(fmt->data, count);
    const rt_locdata_reltime_unit_t *ud = &fmt->data->reltime.units[(int)u];
    const char *unit_form = unit_plural_form(ud, cat);
    (void)unit_name; // silence unused-static warning on strict builds

    const char *tmpl = is_past ? fmt->data->reltime.past : fmt->data->reltime.future;
    if (!tmpl || !*tmpl)
        tmpl = is_past ? "{n} {unit} ago" : "in {n} {unit}";

    rt_string_builder sb;
    rt_sb_init(&sb);
    expand_template(&sb, tmpl, count, unit_form);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}
