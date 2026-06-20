//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_timezone.c
// Purpose: Implements deterministic IANA time-zone lookup and DateTime
//          formatting using a checked-in embedded transition subset.
// Key invariants:
//   - No OS zoneinfo or registry data is consulted; output is host-independent.
//   - Transition lookup treats transition timestamps as inclusive UTC instants.
//   - Formatted wall time is computed as UTC timestamp plus active offset.
// Ownership/Lifetime:
//   - TimeZone handles are pointers to static rt_tz_zone_t records.
//   - Returned strings are freshly allocated rt_string values.
// Links: src/runtime/localization/rt_timezone.h,
//        src/runtime/localization/rt_tzdata_generated.inc,
//        docs/viperlib/time.md
//
//===----------------------------------------------------------------------===//

#include "rt_timezone.h"

#include "rt_platform.h"
#include "rt_trap.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct rt_tz_transition {
    int64_t utc_start;
    int32_t offset_seconds;
    int8_t is_dst;
    const char *abbreviation;
} rt_tz_transition_t;

typedef struct rt_tz_zone {
    const char *name;
    int32_t base_offset_seconds;
    int8_t base_is_dst;
    const char *base_abbreviation;
    const rt_tz_transition_t *transitions;
    size_t transition_count;
} rt_tz_zone_t;

typedef struct rt_tz_rule_at {
    int32_t offset_seconds;
    int8_t is_dst;
    const char *abbreviation;
} rt_tz_rule_at_t;

#include "rt_tzdata_generated.inc"

static int tz_checked_add_i64(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return 1;
    *out = a + b;
    return 0;
}

static int tz_i64_to_time_t(int64_t value, time_t *out) {
    time_t t = (time_t)value;
    if ((int64_t)t != value)
        return 0;
    *out = t;
    return 1;
}

static const char *tz_cstr(rt_string s, size_t *len_out) {
    if (!s)
        return NULL;
    int64_t len64 = rt_str_len(s);
    if (len64 <= 0 || (uint64_t)len64 > (uint64_t)SIZE_MAX)
        return NULL;
    const char *text = rt_string_cstr(s);
    if (!text)
        return NULL;
    size_t len = (size_t)len64;
    if (memchr(text, '\0', len) != NULL)
        return NULL;
    if (len_out)
        *len_out = len;
    return text;
}

static const rt_tz_zone_t *tz_expect(void *timezone_obj) {
    if (!timezone_obj)
        rt_trap("Viper.Time.TimeZone: null TimeZone");
    return (const rt_tz_zone_t *)timezone_obj;
}

static rt_tz_rule_at_t tz_rule_at(const rt_tz_zone_t *zone, int64_t utc_timestamp) {
    rt_tz_rule_at_t result;
    result.offset_seconds = zone->base_offset_seconds;
    result.is_dst = zone->base_is_dst;
    result.abbreviation = zone->base_abbreviation;

    for (size_t i = 0; i < zone->transition_count; ++i) {
        const rt_tz_transition_t *transition = &zone->transitions[i];
        if (utc_timestamp < transition->utc_start)
            break;
        result.offset_seconds = transition->offset_seconds;
        result.is_dst = transition->is_dst;
        result.abbreviation = transition->abbreviation;
    }
    return result;
}

static int tz_wall_tm(const rt_tz_zone_t *zone,
                      int64_t utc_timestamp,
                      struct tm *out_tm,
                      rt_tz_rule_at_t *out_rule) {
    rt_tz_rule_at_t rule = tz_rule_at(zone, utc_timestamp);
    int64_t wall_timestamp;
    if (tz_checked_add_i64(utc_timestamp, (int64_t)rule.offset_seconds, &wall_timestamp))
        return 0;
    time_t wall_time;
    if (!tz_i64_to_time_t(wall_timestamp, &wall_time))
        return 0;
    if (!rt_gmtime_r(&wall_time, out_tm))
        return 0;
    if (out_rule)
        *out_rule = rule;
    return 1;
}

static int tz_append(char *buffer, size_t capacity, size_t *pos, const char *text) {
    if (!text)
        text = "";
    size_t len = strlen(text);
    if (*pos > capacity || len > capacity - *pos)
        return 0;
    memcpy(buffer + *pos, text, len);
    *pos += len;
    return 1;
}

static int tz_append_n(char *buffer, size_t capacity, size_t *pos, const char *text, size_t len) {
    if (*pos > capacity || len > capacity - *pos)
        return 0;
    memcpy(buffer + *pos, text, len);
    *pos += len;
    return 1;
}

static int tz_append_2(char *buffer, size_t capacity, size_t *pos, int value) {
    char tmp[32];
    int len = snprintf(tmp, sizeof(tmp), "%02d", value);
    if (len < 0 || (size_t)len >= sizeof(tmp))
        return 0;
    return tz_append_n(buffer, capacity, pos, tmp, (size_t)len);
}

static int tz_append_4(char *buffer, size_t capacity, size_t *pos, int value) {
    char tmp[32];
    int len = snprintf(tmp, sizeof(tmp), "%04d", value);
    if (len < 0 || (size_t)len >= sizeof(tmp))
        return 0;
    return tz_append_n(buffer, capacity, pos, tmp, (size_t)len);
}

static void tz_offset_text(int32_t offset_seconds, char out[7]) {
    char sign = '+';
    int32_t magnitude = offset_seconds;
    if (magnitude < 0) {
        sign = '-';
        magnitude = -magnitude;
    }
    int32_t minutes_total = magnitude / 60;
    int32_t hours = minutes_total / 60;
    int32_t minutes = minutes_total % 60;
    (void)snprintf(out, 7, "%c%02d:%02d", sign, (int)hours, (int)minutes);
}

static void tz_offset_compact_text(int32_t offset_seconds, char out[6]) {
    char sign = '+';
    int32_t magnitude = offset_seconds;
    if (magnitude < 0) {
        sign = '-';
        magnitude = -magnitude;
    }
    int32_t minutes_total = magnitude / 60;
    int32_t hours = minutes_total / 60;
    int32_t minutes = minutes_total % 60;
    (void)snprintf(out, 6, "%c%02d%02d", sign, (int)hours, (int)minutes);
}

static int tz_append_spec(char *buffer,
                          size_t capacity,
                          size_t *pos,
                          char spec,
                          const struct tm *tm,
                          const rt_tz_rule_at_t *rule) {
    switch (spec) {
        case '%':
            return tz_append_n(buffer, capacity, pos, "%", 1);
        case 'Y':
            return tz_append_4(buffer, capacity, pos, tm->tm_year + 1900);
        case 'm':
            return tz_append_2(buffer, capacity, pos, tm->tm_mon + 1);
        case 'd':
            return tz_append_2(buffer, capacity, pos, tm->tm_mday);
        case 'H':
            return tz_append_2(buffer, capacity, pos, tm->tm_hour);
        case 'M':
            return tz_append_2(buffer, capacity, pos, tm->tm_min);
        case 'S':
            return tz_append_2(buffer, capacity, pos, tm->tm_sec);
        case 'F':
            return tz_append_spec(buffer, capacity, pos, 'Y', tm, rule) &&
                   tz_append_n(buffer, capacity, pos, "-", 1) &&
                   tz_append_spec(buffer, capacity, pos, 'm', tm, rule) &&
                   tz_append_n(buffer, capacity, pos, "-", 1) &&
                   tz_append_spec(buffer, capacity, pos, 'd', tm, rule);
        case 'T':
            return tz_append_spec(buffer, capacity, pos, 'H', tm, rule) &&
                   tz_append_n(buffer, capacity, pos, ":", 1) &&
                   tz_append_spec(buffer, capacity, pos, 'M', tm, rule) &&
                   tz_append_n(buffer, capacity, pos, ":", 1) &&
                   tz_append_spec(buffer, capacity, pos, 'S', tm, rule);
        case 'z': {
            char offset[6];
            tz_offset_compact_text(rule->offset_seconds, offset);
            return tz_append(buffer, capacity, pos, offset);
        }
        case 'Z':
            return tz_append(buffer, capacity, pos, rule->abbreviation);
        default:
            return 0;
    }
}

void *rt_tz_find(rt_string name) {
    size_t len = 0;
    const char *text = tz_cstr(name, &len);
    if (!text)
        rt_trap("Viper.Time.TimeZone.Find: zone name required");

    for (size_t i = 0; i < sizeof(g_tz_zones) / sizeof(g_tz_zones[0]); ++i) {
        rt_tz_zone_t *zone = &g_tz_zones[i];
        if (strlen(zone->name) == len && memcmp(zone->name, text, len) == 0)
            return zone;
    }
    rt_trap("Viper.Time.TimeZone.Find: unknown IANA zone");
    return NULL;
}

rt_string rt_tz_name(void *timezone_obj) {
    const rt_tz_zone_t *zone = tz_expect(timezone_obj);
    return rt_string_from_bytes(zone->name, strlen(zone->name));
}

int64_t rt_tz_offset_at(void *timezone_obj, int64_t utc_timestamp) {
    const rt_tz_zone_t *zone = tz_expect(timezone_obj);
    return (int64_t)tz_rule_at(zone, utc_timestamp).offset_seconds;
}

int8_t rt_tz_is_dst_at(void *timezone_obj, int64_t utc_timestamp) {
    const rt_tz_zone_t *zone = tz_expect(timezone_obj);
    return tz_rule_at(zone, utc_timestamp).is_dst ? 1 : 0;
}

rt_string rt_datetime_to_zone(int64_t timestamp, void *timezone_obj) {
    const rt_tz_zone_t *zone = tz_expect(timezone_obj);
    struct tm tm_buf;
    rt_tz_rule_at_t rule;
    if (!tz_wall_tm(zone, timestamp, &tm_buf, &rule))
        return rt_string_from_bytes("", 0);

    char offset[7];
    tz_offset_text(rule.offset_seconds, offset);
    char buffer[40];
    int len = snprintf(buffer,
                       sizeof(buffer),
                       "%04d-%02d-%02dT%02d:%02d:%02d%s",
                       tm_buf.tm_year + 1900,
                       tm_buf.tm_mon + 1,
                       tm_buf.tm_mday,
                       tm_buf.tm_hour,
                       tm_buf.tm_min,
                       tm_buf.tm_sec,
                       offset);
    if (len < 0 || (size_t)len >= sizeof(buffer)) {
        rt_trap("Viper.Time.DateTime.ToZone: formatted output truncated");
        return rt_string_from_bytes("", 0);
    }
    return rt_string_from_bytes(buffer, (size_t)len);
}

rt_string rt_datetime_format_in_zone(int64_t timestamp, void *timezone_obj, rt_string format) {
    const rt_tz_zone_t *zone = tz_expect(timezone_obj);
    size_t fmt_len = 0;
    const char *fmt = tz_cstr(format, &fmt_len);
    if (!fmt)
        return rt_string_from_bytes("", 0);

    struct tm tm_buf;
    rt_tz_rule_at_t rule;
    if (!tz_wall_tm(zone, timestamp, &tm_buf, &rule))
        return rt_string_from_bytes("", 0);

    char buffer[512];
    size_t pos = 0;
    for (size_t i = 0; i < fmt_len; ++i) {
        if (fmt[i] != '%') {
            if (!tz_append_n(buffer, sizeof(buffer), &pos, &fmt[i], 1))
                return rt_string_from_bytes("", 0);
            continue;
        }
        if (++i >= fmt_len)
            return rt_string_from_bytes("", 0);
        if (!tz_append_spec(buffer, sizeof(buffer), &pos, fmt[i], &tm_buf, &rule))
            return rt_string_from_bytes("", 0);
    }
    return rt_string_from_bytes(buffer, pos);
}
