//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_dateformat.c
// Purpose: Implementation of Viper.Localization.DateFormat. Delegates the
//          heavy lifting to rt_dateformat_patterns.c's emit engine; this
//          file wires the class lifecycle, resolves style-name patterns
//          from the bound Locale's table, and exposes the MonthName/
//          DayName/AmPm convenience queries.
//
// Key invariants:
//   - Each method captures the bound rt_locale_data_t pointer from the
//     Locale at construction; hot-path formatting never takes a lock.
//   - Style-name lookup (Short/Medium/Long/Full) pulls patterns from the
//     locale data's dates.patterns structure. Unknown style names trap.
//
// Ownership/Lifetime:
//   - Instances are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_dateformat.h (interface),
//        src/runtime/localization/rt_dateformat_patterns.c (emitter).
//
//===----------------------------------------------------------------------===//

#include "rt_dateformat.h"

#include "rt_internal.h"
#include "rt_heap.h"
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
// Forward declaration for the pattern emitter (implemented in
// rt_dateformat_patterns.c).
//===----------------------------------------------------------------------===//
void rt_dateformat_emit_pattern(rt_string_builder *sb,
                                int64_t timestamp,
                                const char *pattern,
                                size_t pattern_len,
                                const rt_locale_data_t *data);

//===----------------------------------------------------------------------===//
// Instance struct
//===----------------------------------------------------------------------===//

typedef struct rt_dateformat_inst {
    void                   *locale;
    const rt_locale_data_t *data;
} rt_dateformat_inst_t;

static rt_dateformat_inst_t *as_fmt(void *obj) {
    return (rt_dateformat_inst_t *)obj;
}

static void df_release_handle(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void df_finalizer(void *obj) {
    rt_dateformat_inst_t *fmt = (rt_dateformat_inst_t *)obj;
    if (!fmt)
        return;
    rt_locale_manager_release_data(fmt->data);
    df_release_handle(fmt->locale);
    fmt->locale = NULL;
    fmt->data = NULL;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

static void *df_alloc(void *locale) {
    rt_dateformat_inst_t *fmt = (rt_dateformat_inst_t *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_dateformat_inst_t));
    if (!fmt) {
        rt_trap("Viper.Localization.DateFormat: allocation failed");
        return NULL;
    }
    fmt->locale = locale;
    if (fmt->locale)
        rt_heap_retain(fmt->locale);
    fmt->data = rt_locale_get_data(locale);
    rt_locale_manager_retain_data(fmt->data);
    rt_obj_set_finalizer(fmt, df_finalizer);
    return fmt;
}

void *rt_dateformat_new(void) {
    void *current = rt_locale_manager_current();
    void *fmt = df_alloc(current);
    df_release_handle(current);
    return fmt;
}

void *rt_dateformat_for_locale(void *locale) {
    return df_alloc(locale);
}

void *rt_dateformat_get_locale(void *self) {
    return self ? as_fmt(self)->locale : NULL;
}

//===----------------------------------------------------------------------===//
// Shared style renderer
//===----------------------------------------------------------------------===//

static rt_string render_with_pattern(void *self, int64_t timestamp, const char *pattern) {
    if (!self || !pattern)
        return rt_string_from_bytes("", 0);
    rt_dateformat_inst_t *fmt = as_fmt(self);
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_dateformat_emit_pattern(&sb, timestamp, pattern, strlen(pattern), fmt->data);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// Canonical style methods
//===----------------------------------------------------------------------===//

rt_string rt_dateformat_short(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    return render_with_pattern(self, ts, as_fmt(self)->data->dates.patterns.short_p);
}

rt_string rt_dateformat_medium(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    return render_with_pattern(self, ts, as_fmt(self)->data->dates.patterns.medium_p);
}

rt_string rt_dateformat_long(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    return render_with_pattern(self, ts, as_fmt(self)->data->dates.patterns.long_p);
}

rt_string rt_dateformat_full(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    return render_with_pattern(self, ts, as_fmt(self)->data->dates.patterns.full_p);
}

rt_string rt_dateformat_time_short(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    return render_with_pattern(self, ts, as_fmt(self)->data->dates.patterns.time_short);
}

rt_string rt_dateformat_time_medium(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    return render_with_pattern(self, ts, as_fmt(self)->data->dates.patterns.time_medium);
}

rt_string rt_dateformat_datetime_short(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    rt_dateformat_inst_t *fmt = as_fmt(self);
    if (fmt->data->dates.patterns.datetime_short &&
        *fmt->data->dates.patterns.datetime_short)
        return render_with_pattern(self, ts, fmt->data->dates.patterns.datetime_short);
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_dateformat_emit_pattern(&sb, ts,
                                fmt->data->dates.patterns.short_p,
                                strlen(fmt->data->dates.patterns.short_p),
                                fmt->data);
    (void)rt_sb_append_cstr(&sb, " ");
    rt_dateformat_emit_pattern(&sb, ts,
                                fmt->data->dates.patterns.time_short,
                                strlen(fmt->data->dates.patterns.time_short),
                                fmt->data);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_dateformat_datetime_medium(void *self, int64_t ts) {
    if (!self) return rt_string_from_bytes("", 0);
    rt_dateformat_inst_t *fmt = as_fmt(self);
    if (fmt->data->dates.patterns.datetime_medium &&
        *fmt->data->dates.patterns.datetime_medium)
        return render_with_pattern(self, ts, fmt->data->dates.patterns.datetime_medium);
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_dateformat_emit_pattern(&sb, ts,
                                fmt->data->dates.patterns.medium_p,
                                strlen(fmt->data->dates.patterns.medium_p),
                                fmt->data);
    (void)rt_sb_append_cstr(&sb, " ");
    rt_dateformat_emit_pattern(&sb, ts,
                                fmt->data->dates.patterns.time_medium,
                                strlen(fmt->data->dates.patterns.time_medium),
                                fmt->data);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_dateformat_custom(void *self, int64_t ts, rt_string pattern) {
    if (!self || !pattern) {
        rt_trap("Viper.Localization.DateFormat: Custom requires non-null pattern");
        return rt_string_from_bytes("", 0);
    }
    rt_dateformat_inst_t *fmt = as_fmt(self);
    const char *cs = rt_string_cstr(pattern);
    int64_t len = rt_str_len(pattern);
    if (!cs || len < 0)
        return rt_string_from_bytes("", 0);
    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_dateformat_emit_pattern(&sb, ts, cs, (size_t)len, fmt->data);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// DateOnly variant
//===----------------------------------------------------------------------===//

/// Forward-declare the accessors we need from rt_dateonly (which exposes
/// Year/Month/Day getters on an rt_dateonly_t handle). We don't include
/// rt_dateonly.h to avoid pulling the full runtime class in this TU;
/// extern declarations match its public signatures.
extern int64_t rt_dateonly_year(void *handle);
extern int64_t rt_dateonly_month(void *handle);
extern int64_t rt_dateonly_day(void *handle);

rt_string rt_dateformat_date_only(void *self, void *dateonly, rt_string style) {
    if (!self || !dateonly) {
        rt_trap("Viper.Localization.DateFormat: DateOnly requires non-null arguments");
        return rt_string_from_bytes("", 0);
    }
    rt_dateformat_inst_t *fmt = as_fmt(self);

    // Pick the pattern by style name, defaulting to medium.
    const char *pattern = fmt->data->dates.patterns.medium_p;
    const char *style_cs = style ? rt_string_cstr(style) : NULL;
    if (style_cs) {
        if (strcmp(style_cs, "short") == 0)
            pattern = fmt->data->dates.patterns.short_p;
        else if (strcmp(style_cs, "medium") == 0)
            pattern = fmt->data->dates.patterns.medium_p;
        else if (strcmp(style_cs, "long") == 0)
            pattern = fmt->data->dates.patterns.long_p;
        else if (strcmp(style_cs, "full") == 0)
            pattern = fmt->data->dates.patterns.full_p;
        else {
            rt_trap("Viper.Localization.DateFormat: unknown style (expected short/medium/long/full)");
            return rt_string_from_bytes("", 0);
        }
    }

    // Synthesize a Unix timestamp from DateOnly components at 00:00:00 UTC.
    // rt_datetime_create exists but requires linking rt_datetime.h; avoid the
    // coupling by computing via standard timegm-style logic. For Phase 3 we
    // reuse a simple computation: date components at midnight.
    extern int64_t rt_datetime_create(int64_t year, int64_t month, int64_t day,
                                      int64_t hour, int64_t minute, int64_t second);
    int64_t y = rt_dateonly_year(dateonly);
    int64_t m = rt_dateonly_month(dateonly);
    int64_t d = rt_dateonly_day(dateonly);
    int64_t ts = rt_datetime_create(y, m, d, 0, 0, 0);

    rt_string_builder sb;
    rt_sb_init(&sb);
    rt_dateformat_emit_pattern(&sb, ts, pattern, strlen(pattern), fmt->data);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

//===----------------------------------------------------------------------===//
// Name queries
//===----------------------------------------------------------------------===//

rt_string rt_dateformat_month_name(void *self, int64_t month, int8_t abbreviated) {
    if (!self || month < 1 || month > 12) {
        rt_trap("Viper.Localization.DateFormat: month out of range (1..12)");
        return rt_string_from_bytes("", 0);
    }
    const rt_locale_data_t *data = as_fmt(self)->data;
    const char *const *arr = abbreviated ? data->dates.months_abbr
                                          : data->dates.months_wide;
    const char *s = arr ? arr[month - 1] : NULL;
    if (!s) s = "";
    return rt_string_from_bytes(s, strlen(s));
}

rt_string rt_dateformat_day_name(void *self, int64_t dow, int8_t abbreviated) {
    if (!self || dow < 0 || dow > 6) {
        rt_trap("Viper.Localization.DateFormat: weekday out of range (0..6)");
        return rt_string_from_bytes("", 0);
    }
    const rt_locale_data_t *data = as_fmt(self)->data;
    const char *const *arr = abbreviated ? data->dates.days_abbr
                                          : data->dates.days_wide;
    const char *s = arr ? arr[dow] : NULL;
    if (!s) s = "";
    return rt_string_from_bytes(s, strlen(s));
}

rt_string rt_dateformat_am_pm(void *self, int8_t is_pm) {
    if (!self)
        return rt_string_from_bytes("", 0);
    const rt_locale_data_t *data = as_fmt(self)->data;
    const char *s = is_pm ? data->dates.pm : data->dates.am;
    if (!s) s = is_pm ? "PM" : "AM";
    return rt_string_from_bytes(s, strlen(s));
}
