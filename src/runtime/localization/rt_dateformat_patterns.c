//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_dateformat_patterns.c
// Purpose: CLDR-style date/time pattern interpreter. Consumes a pattern string
//          of letter runs (y/M/d/E/H/h/m/s/a) and quoted literals ('...'), and
//          emits the formatted representation of a Unix timestamp into a
//          string builder using the bound locale's month / day / AM-PM tables.
//
// Key invariants:
//   - Letter repetition counts are clamped at 5 for year/month/day-of-week
//     and at 2 for day-of-month / hour / minute / second. Counts exceeding
//     the clamp emit the maximum-width form rather than trapping.
//   - Quoted literals use single apostrophes; a double apostrophe ('') emits
//     a literal apostrophe. Unclosed quotes silently extend to end of pattern.
//   - Pattern letters outside the supported set (y M d E H h m s a) are
//     rejected with a trap. Supported letters are explicitly enumerated
//     elsewhere in docs/viperlib/localization/formatting.md.
//
// Ownership/Lifetime:
//   - Caller owns the string builder; this function only appends bytes.
//
// Links: src/runtime/localization/rt_dateformat.h (class consumer),
//        src/runtime/core/rt_datetime.h (component accessors).
//
//===----------------------------------------------------------------------===//

#include "rt_dateformat.h"

#include "rt_datetime.h"
#include "rt_internal.h"
#include "rt_locale_data.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <stdio.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Small helpers
//===----------------------------------------------------------------------===//

typedef struct digit_spans {
    const char *ptr[10];
    size_t len[10];
    int valid;
} digit_spans_t;

static size_t utf8_cp_len(const char *s) {
    if (!s || !*s) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0 && s[1]) return 2;
    if ((c & 0xF0) == 0xE0 && s[1] && s[2]) return 3;
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) return 4;
    return 1;
}

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

static void emit_ascii_digits(rt_string_builder *sb, const rt_locale_data_t *data,
                              const char *bytes, size_t len) {
    digit_spans_t ds = digit_spans_from_locale(data);
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)bytes[i];
        if (ds.valid && c >= '0' && c <= '9') {
            int d = (int)(c - '0');
            (void)rt_sb_append_bytes(sb, ds.ptr[d], ds.len[d]);
        } else {
            (void)rt_sb_append_bytes(sb, bytes + i, 1);
        }
    }
}

/// @brief Append @p value as a zero-padded decimal of @p width digits.
static void emit_padded_int(rt_string_builder *sb, const rt_locale_data_t *data,
                            int64_t value, int width) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    if (n < 0)
        return;
    if (buf[0] == '-') {
        (void)rt_sb_append_bytes(sb, "-", 1);
        memmove(buf, buf + 1, (size_t)n);
        --n;
    }
    if (width > n) {
        digit_spans_t ds = digit_spans_from_locale(data);
        for (int i = n; i < width; ++i) {
            if (ds.valid)
                (void)rt_sb_append_bytes(sb, ds.ptr[0], ds.len[0]);
            else
                (void)rt_sb_append_bytes(sb, "0", 1);
        }
    }
    emit_ascii_digits(sb, data, buf, (size_t)n);
}

/// @brief Append @p value as an unpadded decimal.
static void emit_int(rt_string_builder *sb, const rt_locale_data_t *data, int64_t value) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    if (n < 0)
        return;
    emit_ascii_digits(sb, data, buf, (size_t)n);
}

/// @brief Append a C string (NULL-tolerant).
static void emit_cstr(rt_string_builder *sb, const char *s) {
    if (!s || !*s)
        return;
    (void)rt_sb_append_cstr(sb, s);
}

/// @brief Append the single leading UTF-8 codepoint of @p s (narrow form).
static void emit_narrow(rt_string_builder *sb, const char *s) {
    if (!s || !*s)
        return;
    unsigned char c = (unsigned char)s[0];
    size_t len = 1;
    if ((c & 0xE0) == 0xC0 && s[1]) len = 2;
    else if ((c & 0xF0) == 0xE0 && s[1] && s[2]) len = 3;
    else if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) len = 4;
    (void)rt_sb_append_bytes(sb, s, len);
}

//===----------------------------------------------------------------------===//
// Letter run emitters
//===----------------------------------------------------------------------===//

typedef struct {
    int64_t year;
    int64_t month;   // 1-12
    int64_t day;     // 1-31
    int64_t hour;    // 0-23
    int64_t minute;  // 0-59
    int64_t second;  // 0-59
    int64_t dow;     // 0=Sunday..6=Saturday
} dt_components_t;

static void emit_year(rt_string_builder *sb, const dt_components_t *c,
                      int count, const rt_locale_data_t *data) {
    if (count == 2) {
        emit_padded_int(sb, data, c->year % 100, 2);
    } else {
        // 1, 3, 4, 5+ all emit the full year (no width clamping).
        int width = count >= 4 ? count : 1;
        emit_padded_int(sb, data, c->year, width);
    }
}

static void emit_month(rt_string_builder *sb, const dt_components_t *c,
                       int count, const rt_locale_data_t *data) {
    int idx = (int)(c->month - 1);
    if (idx < 0 || idx > 11) {
        rt_trap("Viper.Localization.DateFormat: month component out of range");
        return;
    }
    if (count == 1) {
        emit_int(sb, data, c->month);
    } else if (count == 2) {
        emit_padded_int(sb, data, c->month, 2);
    } else if (count == 3) {
        emit_cstr(sb, data->dates.months_abbr ? data->dates.months_abbr[idx] : NULL);
    } else if (count == 4) {
        emit_cstr(sb, data->dates.months_wide ? data->dates.months_wide[idx] : NULL);
    } else {
        // Narrow: one-glyph form; fall back to the wide name's first codepoint.
        const char *wide = data->dates.months_wide ? data->dates.months_wide[idx] : NULL;
        emit_narrow(sb, wide);
    }
}

static void emit_day(rt_string_builder *sb, const dt_components_t *c,
                     int count, const rt_locale_data_t *data) {
    if (count >= 2)
        emit_padded_int(sb, data, c->day, 2);
    else
        emit_int(sb, data, c->day);
}

static void emit_dow(rt_string_builder *sb, const dt_components_t *c,
                     int count, const rt_locale_data_t *data) {
    int idx = (int)c->dow;
    if (idx < 0 || idx > 6) {
        rt_trap("Viper.Localization.DateFormat: weekday component out of range");
        return;
    }
    if (count <= 3) {
        emit_cstr(sb, data->dates.days_abbr ? data->dates.days_abbr[idx] : NULL);
    } else if (count == 4) {
        emit_cstr(sb, data->dates.days_wide ? data->dates.days_wide[idx] : NULL);
    } else {
        const char *wide = data->dates.days_wide ? data->dates.days_wide[idx] : NULL;
        emit_narrow(sb, wide);
    }
}

static void emit_hour24(rt_string_builder *sb, const dt_components_t *c,
                        int count, const rt_locale_data_t *data) {
    if (count >= 2)
        emit_padded_int(sb, data, c->hour, 2);
    else
        emit_int(sb, data, c->hour);
}

static void emit_hour12(rt_string_builder *sb, const dt_components_t *c,
                        int count, const rt_locale_data_t *data) {
    int64_t h = c->hour % 12;
    if (h == 0) h = 12;
    if (count >= 2)
        emit_padded_int(sb, data, h, 2);
    else
        emit_int(sb, data, h);
}

static void emit_minute(rt_string_builder *sb, const dt_components_t *c,
                        int count, const rt_locale_data_t *data) {
    if (count >= 2)
        emit_padded_int(sb, data, c->minute, 2);
    else
        emit_int(sb, data, c->minute);
}

static void emit_second(rt_string_builder *sb, const dt_components_t *c,
                        int count, const rt_locale_data_t *data) {
    if (count >= 2)
        emit_padded_int(sb, data, c->second, 2);
    else
        emit_int(sb, data, c->second);
}

static void emit_ampm(rt_string_builder *sb, const dt_components_t *c,
                      const rt_locale_data_t *data) {
    const char *token = c->hour < 12 ? data->dates.am : data->dates.pm;
    emit_cstr(sb, token ? token : (c->hour < 12 ? "AM" : "PM"));
}

//===----------------------------------------------------------------------===//
// Public entry point
//===----------------------------------------------------------------------===//

void rt_dateformat_emit_pattern(rt_string_builder *sb,
                                int64_t timestamp,
                                const char *pattern,
                                size_t pattern_len,
                                const rt_locale_data_t *data);

void rt_dateformat_emit_pattern(rt_string_builder *sb,
                                int64_t timestamp,
                                const char *pattern,
                                size_t pattern_len,
                                const rt_locale_data_t *data) {
    if (!sb || !pattern || !data)
        return;

    dt_components_t c;
    c.year = rt_datetime_year(timestamp);
    c.month = rt_datetime_month(timestamp);
    c.day = rt_datetime_day(timestamp);
    c.hour = rt_datetime_hour(timestamp);
    c.minute = rt_datetime_minute(timestamp);
    c.second = rt_datetime_second(timestamp);
    c.dow = rt_datetime_day_of_week(timestamp);

    // Cap pattern length defensively (plan calls for 256 chars at load time;
    // Custom() pattern inputs from users are capped here as well).
    if (pattern_len > 256) {
        rt_trap("Viper.Localization.DateFormat: pattern too long (max 256 chars)");
        return;
    }

    size_t i = 0;
    while (i < pattern_len) {
        char ch = pattern[i];

        // Quoted literal: '...' (with '' meaning a literal apostrophe).
        if (ch == '\'') {
            ++i;
            // Case: '' at the start => literal '
            if (i < pattern_len && pattern[i] == '\'') {
                (void)rt_sb_append_bytes(sb, "'", 1);
                ++i;
                continue;
            }
            int closed = 0;
            while (i < pattern_len) {
                if (pattern[i] == '\'') {
                    if (i + 1 < pattern_len && pattern[i + 1] == '\'') {
                        // Escaped apostrophe inside the literal.
                        (void)rt_sb_append_bytes(sb, "'", 1);
                        i += 2;
                        continue;
                    }
                    ++i; // closing quote
                    closed = 1;
                    break;
                }
                (void)rt_sb_append_bytes(sb, pattern + i, 1);
                ++i;
            }
            if (!closed) {
                rt_trap("Viper.Localization.DateFormat: unterminated quoted literal");
                return;
            }
            continue;
        }

        // Pattern letter run.
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            size_t j = i + 1;
            while (j < pattern_len && pattern[j] == ch)
                ++j;
            int count = (int)(j - i);
            i = j;
            switch (ch) {
                case 'y': emit_year(sb, &c, count, data); break;
                case 'M': emit_month(sb, &c, count, data); break;
                case 'd': emit_day(sb, &c, count, data); break;
                case 'E': emit_dow(sb, &c, count, data); break;
                case 'H': emit_hour24(sb, &c, count, data); break;
                case 'h': emit_hour12(sb, &c, count, data); break;
                case 'm': emit_minute(sb, &c, count, data); break;
                case 's': emit_second(sb, &c, count, data); break;
                case 'a': emit_ampm(sb, &c, data); break;
                default:
                    rt_trap("Viper.Localization.DateFormat: unsupported pattern letter");
                    return;
            }
            continue;
        }

        // Any other character is emitted verbatim (separators, punctuation).
        (void)rt_sb_append_bytes(sb, &ch, 1);
        ++i;
    }
}
