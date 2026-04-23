//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_numformat.c
// Purpose: Implementation of Viper.Localization.NumberFormat. Formats and
//          parses locale-aware numeric strings using the bound Locale's
//          rt_locale_data_t. Shares digit-grouping machinery with
//          Viper.Text.NumberFormat via rt_numfmt_internal.h to avoid
//          duplicating the group-every-N-digits logic.
//
// Key invariants:
//   - Strict-mode parse rejects ambiguous grouping; lenient mode accepts
//     anything that a human reading in the bound locale would interpret
//     as the intended number.
//   - Every format path runs through a common sign-then-integer-then-fraction
//     builder so sign handling and separator insertion stay in one place.
//   - Currency patterns use the locale's `pattern_positive` / `pattern_negative`
//     templates with {n} and {s} substitution; inputs must not introduce any
//     other placeholders.
//
// Ownership/Lifetime:
//   - Instances are rt_obj_new_i64-allocated; GC-managed.
//
// Links: src/runtime/localization/rt_numformat.h (interface),
//        src/runtime/text/rt_numfmt_internal.h (shared grouping helper),
//        src/runtime/localization/rt_locale_manager.h (current-locale lookup),
//        docs/viperlib/localization/formatting.md (user documentation).
//
//===----------------------------------------------------------------------===//

#include "rt_numformat.h"

#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_locale.h"
#include "rt_locale_data.h"
#include "rt_locale_manager.h"
#include "rt_numfmt_internal.h"
#include "rt_object.h"
#include "rt_option.h"
#include "rt_string.h"
#include "rt_string_builder.h"
#include "rt_trap.h"

#include <errno.h>
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#if defined(_WIN32)
#include <locale.h>
#else
#include <locale.h>
#endif

//===----------------------------------------------------------------------===//
// Rounding modes
//===----------------------------------------------------------------------===//

typedef enum {
    ROUND_HALF_EVEN = 0,
    ROUND_HALF_UP,
    ROUND_HALF_DOWN,
    ROUND_UP,
    ROUND_DOWN,
    ROUND_CEILING,
    ROUND_FLOOR,
} rounding_mode_t;

static const char *rounding_mode_name(rounding_mode_t m) {
    switch (m) {
        case ROUND_HALF_UP:   return "halfUp";
        case ROUND_HALF_DOWN: return "halfDown";
        case ROUND_UP:        return "up";
        case ROUND_DOWN:      return "down";
        case ROUND_CEILING:   return "ceiling";
        case ROUND_FLOOR:     return "floor";
        case ROUND_HALF_EVEN:
        default:              return "halfEven";
    }
}

static rounding_mode_t rounding_mode_parse(const char *s) {
    if (!s)
        return ROUND_HALF_EVEN;
    if (strcmp(s, "halfUp") == 0)   return ROUND_HALF_UP;
    if (strcmp(s, "halfDown") == 0) return ROUND_HALF_DOWN;
    if (strcmp(s, "up") == 0)       return ROUND_UP;
    if (strcmp(s, "down") == 0)     return ROUND_DOWN;
    if (strcmp(s, "ceiling") == 0)  return ROUND_CEILING;
    if (strcmp(s, "floor") == 0)    return ROUND_FLOOR;
    return ROUND_HALF_EVEN;
}

static int loc_vsnprintf_c(char *out, size_t cap, const char *fmt, va_list args) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    int n = c_locale ? _vsnprintf_l(out, cap, fmt, c_locale, args)
                     : vsnprintf(out, cap, fmt, args);
    if (c_locale)
        _free_locale(c_locale);
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    int n;
    if (!c_locale) {
        n = vsnprintf(out, cap, fmt, args);
    } else {
        locale_t old = uselocale(c_locale);
        if (!old) {
            n = vsnprintf(out, cap, fmt, args);
        } else {
            n = vsnprintf(out, cap, fmt, args);
            uselocale(old);
        }
        freelocale(c_locale);
    }
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    return n;
}

static int loc_snprintf_c(char *out, size_t cap, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = loc_vsnprintf_c(out, cap, fmt, args);
    va_end(args);
    return n;
}

static char *loc_sprintf_alloc_c(size_t *out_len, const char *fmt, ...) {
    if (out_len) *out_len = 0;
    size_t cap = 128;
    for (;;) {
        char *buf = (char *)malloc(cap);
        if (!buf)
            return NULL;
        va_list args;
        va_start(args, fmt);
        int n = loc_vsnprintf_c(buf, cap, fmt, args);
        va_end(args);
        if (n >= 0 && (size_t)n < cap) {
            if (out_len) *out_len = (size_t)n;
            return buf;
        }
        free(buf);
        if (n >= 0)
            cap = (size_t)n + 1;
        else
            cap *= 2;
        if (cap > 4096)
            return NULL;
    }
}

static double loc_strtod_c(const char *input, char **endptr) {
#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (!c_locale)
        return strtod(input, endptr);
    double v = _strtod_l(input, endptr, c_locale);
    _free_locale(c_locale);
    return v;
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!c_locale)
        return strtod(input, endptr);
    locale_t old = uselocale(c_locale);
    if (!old) {
        freelocale(c_locale);
        return strtod(input, endptr);
    }
    double v = strtod(input, endptr);
    uselocale(old);
    freelocale(c_locale);
    return v;
#endif
}

//===----------------------------------------------------------------------===//
// Instance struct
//===----------------------------------------------------------------------===//

typedef struct rt_numformat {
    void                   *locale;       ///< strong Locale handle ref
    const rt_locale_data_t *data;         ///< non-owning
    int64_t                 min_frac;
    int64_t                 max_frac;
    int8_t                  grouping;
    int8_t                  strict;
    rounding_mode_t         rounding;
} rt_numformat_t;

static rt_numformat_t *as_fmt(void *obj) {
    return (rt_numformat_t *)obj;
}

static void fmt_release_handle(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void fmt_finalizer(void *obj) {
    rt_numformat_t *fmt = (rt_numformat_t *)obj;
    if (!fmt)
        return;
    rt_locale_manager_release_data(fmt->data);
    fmt_release_handle(fmt->locale);
    fmt->locale = NULL;
    fmt->data = NULL;
}

//===----------------------------------------------------------------------===//
// Constructors
//===----------------------------------------------------------------------===//

static rt_numformat_t *fmt_alloc(void *locale) {
    rt_numformat_t *fmt = (rt_numformat_t *)rt_obj_new_i64(
        0, (int64_t)sizeof(rt_numformat_t));
    if (!fmt) {
        rt_trap("Viper.Localization.NumberFormat: allocation failed");
        return NULL;
    }
    memset(fmt, 0, sizeof(*fmt));
    fmt->locale = locale;
    if (fmt->locale)
        rt_heap_retain(fmt->locale);
    fmt->data = rt_locale_get_data(locale);
    rt_locale_manager_retain_data(fmt->data);
    fmt->min_frac = 0;
    fmt->max_frac = 3;  // default matches common display conventions
    fmt->grouping = 1;
    fmt->strict = 0;
    fmt->rounding = ROUND_HALF_EVEN;
    rt_obj_set_finalizer(fmt, fmt_finalizer);
    return fmt;
}

void *rt_numformat_new(void) {
    void *current = rt_locale_manager_current();
    void *fmt = fmt_alloc(current);
    fmt_release_handle(current);
    return fmt;
}

void *rt_numformat_for_locale(void *locale) {
    return fmt_alloc(locale);
}

//===----------------------------------------------------------------------===//
// Property accessors
//===----------------------------------------------------------------------===//

void *rt_numformat_get_locale(void *self)      { return self ? as_fmt(self)->locale : NULL; }
int64_t rt_numformat_get_min_frac(void *self)  { return self ? as_fmt(self)->min_frac : 0; }
int64_t rt_numformat_get_max_frac(void *self)  { return self ? as_fmt(self)->max_frac : 3; }
int8_t  rt_numformat_get_grouping(void *self)  { return self ? as_fmt(self)->grouping : 1; }
int8_t  rt_numformat_get_strict(void *self)    { return self ? as_fmt(self)->strict : 0; }

void rt_numformat_set_min_frac(void *self, int64_t value) {
    if (!self) return;
    if (value < 0) value = 0;
    if (value > 20) value = 20;
    as_fmt(self)->min_frac = value;
    if (as_fmt(self)->max_frac < value)
        as_fmt(self)->max_frac = value;
}

void rt_numformat_set_max_frac(void *self, int64_t value) {
    if (!self) return;
    if (value < 0) value = 0;
    if (value > 20) value = 20;
    as_fmt(self)->max_frac = value;
    if (as_fmt(self)->min_frac > value)
        as_fmt(self)->min_frac = value;
}

void rt_numformat_set_grouping(void *self, int8_t value) {
    if (self) as_fmt(self)->grouping = value ? 1 : 0;
}

void rt_numformat_set_strict(void *self, int8_t value) {
    if (self) as_fmt(self)->strict = value ? 1 : 0;
}

rt_string rt_numformat_get_rounding(void *self) {
    rounding_mode_t m = self ? as_fmt(self)->rounding : ROUND_HALF_EVEN;
    const char *name = rounding_mode_name(m);
    return rt_string_from_bytes(name, strlen(name));
}

void rt_numformat_set_rounding(void *self, rt_string mode) {
    if (!self || !mode)
        return;
    const char *cs = rt_string_cstr(mode);
    as_fmt(self)->rounding = rounding_mode_parse(cs);
}

//===----------------------------------------------------------------------===//
// Rounding
//===----------------------------------------------------------------------===//

static double apply_rounding(double value, int digits, rounding_mode_t mode) {
    if (!isfinite(value))
        return value;
    double scale = pow(10.0, (double)digits);
    if (!isfinite(scale) || fabs(value) > DBL_MAX / scale)
        return value;
    double scaled = value * scale;
    double rounded;
    switch (mode) {
        case ROUND_HALF_UP: {
            double s = value < 0 ? -1.0 : 1.0;
            rounded = s * floor(fabs(scaled) + 0.5);
            break;
        }
        case ROUND_HALF_DOWN: {
            double s = value < 0 ? -1.0 : 1.0;
            double f = fabs(scaled);
            double fr = f - floor(f);
            if (fr > 0.5)
                rounded = s * (floor(f) + 1.0);
            else
                rounded = s * floor(f);
            break;
        }
        case ROUND_UP: {
            double s = value < 0 ? -1.0 : 1.0;
            rounded = s * ceil(fabs(scaled));
            break;
        }
        case ROUND_DOWN: {
            double s = value < 0 ? -1.0 : 1.0;
            rounded = s * floor(fabs(scaled));
            break;
        }
        case ROUND_CEILING:
            rounded = ceil(scaled);
            break;
        case ROUND_FLOOR:
            rounded = floor(scaled);
            break;
        case ROUND_HALF_EVEN:
        default:
            // rint with fegetround default gives banker's rounding on most
            // libcs. snprintf("%.*f") already does half-to-even on glibc.
            // Use the simple portable approximation here.
            rounded = rint(scaled);
            break;
    }
    return rounded / scale;
}

//===----------------------------------------------------------------------===//
// Core format helper: render a double with locale decimal + group separators
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

static digit_spans_t digit_spans_from_locale(const rt_locdata_numbers_t *nums) {
    digit_spans_t ds;
    memset(&ds, 0, sizeof(ds));
    const char *digits = nums && nums->digits ? nums->digits : "0123456789";
    const char *p = digits;
    for (int i = 0; i < 10; ++i) {
        size_t l = utf8_cp_len(p);
        if (l == 0) {
            ds.valid = 0;
            return ds;
        }
        ds.ptr[i] = p;
        ds.len[i] = l;
        p += l;
    }
    ds.valid = *p == '\0';
    return ds;
}

static void append_localized_bytes(rt_string_builder *sb,
                                   const rt_locdata_numbers_t *nums,
                                   const char *bytes,
                                   size_t len) {
    digit_spans_t ds = digit_spans_from_locale(nums);
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

static void append_grouped_ascii_number(rt_string_builder *sb,
                                        const rt_numformat_t *fmt,
                                        const char *digits,
                                        int digit_len) {
    const rt_locdata_numbers_t *nums = &fmt->data->numbers;
    rt_string_builder tmp;
    rt_sb_init(&tmp);
    if (fmt->grouping && nums->group_sep && nums->group_sep[0] != '\0') {
        int primary = nums->group_size > 0 ? nums->group_size : 3;
        int secondary = nums->secondary_group_size > 0
                            ? nums->secondary_group_size
                            : primary;
        size_t sep_len = strlen(nums->group_sep);
        if (primary == secondary) {
            rt_numfmt_group_digits(&tmp, digits, digit_len,
                                   nums->group_sep, sep_len, primary);
        } else if (digit_len <= primary || primary <= 0 || secondary <= 0) {
            (void)rt_sb_append_bytes(&tmp, digits, (size_t)digit_len);
        } else {
            int prefix_len = digit_len - primary;
            int first = prefix_len % secondary;
            if (first == 0)
                first = secondary;
            (void)rt_sb_append_bytes(&tmp, digits, (size_t)first);
            int pos = first;
            while (pos < prefix_len) {
                (void)rt_sb_append_bytes(&tmp, nums->group_sep, sep_len);
                (void)rt_sb_append_bytes(&tmp, digits + pos, (size_t)secondary);
                pos += secondary;
            }
            (void)rt_sb_append_bytes(&tmp, nums->group_sep, sep_len);
            (void)rt_sb_append_bytes(&tmp, digits + prefix_len, (size_t)primary);
        }
    } else {
        (void)rt_sb_append_bytes(&tmp, digits, (size_t)digit_len);
    }
    append_localized_bytes(sb, nums, tmp.data, tmp.len);
    rt_sb_free(&tmp);
}

/// @brief Emit @p value into @p sb using locale numeric conventions and
///        effective fraction-digit limits.
/// @param sb         destination
/// @param fmt        formatter (for locale data + options)
/// @param value      real-valued input
/// @param override_digits    >= 0 to force exactly this many fraction digits
///                           (used by DecimalN); -1 to use min/max
static void fmt_render_number(rt_string_builder *sb, const rt_numformat_t *fmt,
                              double value, int override_digits) {
    const rt_locdata_numbers_t *nums = &fmt->data->numbers;

    if (!isfinite(value)) {
        if (isnan(value)) {
            const char *s = nums->nan ? nums->nan : "NaN";
            (void)rt_sb_append_cstr(sb, s);
            return;
        }
        // Infinity: handle sign via the locale's minus/plus (rarely used).
        const char *inf = nums->infinity ? nums->infinity : "\xE2\x88\x9E";
        if (value < 0) {
            const char *minus = nums->minus ? nums->minus : "-";
            (void)rt_sb_append_cstr(sb, minus);
        }
        (void)rt_sb_append_cstr(sb, inf);
        return;
    }

    int digits_used;
    if (override_digits >= 0) {
        digits_used = override_digits;
        if (digits_used > 20)
            digits_used = 20;
    } else {
        int64_t mx = fmt->max_frac;
        if (mx < 0) mx = 0;
        if (mx > 20) mx = 20;
        digits_used = (int)mx;
    }

    // Round to the target precision.
    double rounded = apply_rounding(value, digits_used, fmt->rounding);
    int negative = rounded < 0;
    double abs_val = negative ? -rounded : rounded;

    // Render integer + fraction with a growable buffer. DBL_MAX with fixed
    // decimals needs hundreds of bytes; fixed stack buffers truncated this
    // path and let later grouping code read past the terminator.
    size_t full_size = 0;
    char *full = loc_sprintf_alloc_c(&full_size, "%.*f", digits_used, abs_val);
    if (!full) {
        (void)rt_sb_append_cstr(sb, nums->nan ? nums->nan : "NaN");
        return;
    }
    int full_len = full_size > (size_t)INT_MAX ? INT_MAX : (int)full_size;

    // Locate the decimal point in the "full" string.
    const char *frac_ptr = NULL;
    int frac_len = 0;
    for (int i = 0; i < full_len; ++i) {
        if (full[i] == '.') {
            frac_ptr = full + i + 1;
            frac_len = full_len - (i + 1);
            break;
        }
    }

    // Emit sign.
    if (negative) {
        const char *minus = nums->minus ? nums->minus : "-";
        (void)rt_sb_append_cstr(sb, minus);
    }

    // Emit integer part with optional grouping.
    int whole_len = frac_ptr ? (int)((frac_ptr - full) - 1) : full_len;
    append_grouped_ascii_number(sb, fmt, full, whole_len);

    // Determine how many fraction digits to emit: clamp to [min_frac, digits_used]
    // and strip trailing zeros past min_frac.
    int emit_digits = digits_used;
    int min_f = override_digits >= 0 ? digits_used : (int)fmt->min_frac;
    if (min_f < 0) min_f = 0;
    if (min_f > digits_used) min_f = digits_used;
    if (frac_ptr && frac_len > 0 && override_digits < 0) {
        while (emit_digits > min_f && frac_ptr[emit_digits - 1] == '0')
            --emit_digits;
    }
    if (emit_digits > 0 && frac_ptr) {
        const char *dec_sep = nums->decimal_sep ? nums->decimal_sep : ".";
        (void)rt_sb_append_cstr(sb, dec_sep);
        append_localized_bytes(sb, nums, frac_ptr, (size_t)emit_digits);
    }
    free(full);
}

static void fmt_render_integer_exact(rt_string_builder *sb, const rt_numformat_t *fmt,
                                     int64_t value) {
    const rt_locdata_numbers_t *nums = &fmt->data->numbers;
    uint64_t mag;
    if (value < 0) {
        const char *minus = nums->minus ? nums->minus : "-";
        (void)rt_sb_append_cstr(sb, minus);
        mag = (uint64_t)(-(value + 1)) + 1u;
    } else {
        mag = (uint64_t)value;
    }

    char digits[32];
    size_t pos = sizeof(digits);
    digits[--pos] = '\0';
    if (mag == 0) {
        digits[--pos] = '0';
    } else {
        while (mag > 0) {
            digits[--pos] = (char)('0' + (mag % 10u));
            mag /= 10u;
        }
    }
    append_grouped_ascii_number(sb, fmt, digits + pos, (int)(sizeof(digits) - pos - 1));
}

//===----------------------------------------------------------------------===//
// Format surface
//===----------------------------------------------------------------------===//

rt_string rt_numformat_decimal(void *self, double value) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_numformat_t *fmt = as_fmt(self);
    rt_string_builder sb;
    rt_sb_init(&sb);
    fmt_render_number(&sb, fmt, value, /*override_digits=*/-1);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_numformat_decimal_n(void *self, double value, int64_t digits) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_numformat_t *fmt = as_fmt(self);
    if (digits < 0) digits = 0;
    if (digits > 20) digits = 20;
    rt_string_builder sb;
    rt_sb_init(&sb);
    fmt_render_number(&sb, fmt, value, (int)digits);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_numformat_integer(void *self, int64_t value) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_string_builder sb;
    rt_sb_init(&sb);
    fmt_render_integer_exact(&sb, as_fmt(self), value);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_numformat_percent(void *self, double value) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_numformat_t *fmt = as_fmt(self);
    rt_string_builder sb;
    rt_sb_init(&sb);
    fmt_render_number(&sb, fmt, value * 100.0, /*override_digits=*/-1);
    const char *pct = fmt->data->numbers.percent ? fmt->data->numbers.percent : "%";
    (void)rt_sb_append_cstr(&sb, pct);
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

/// @brief Expand a currency pattern ("{s}{n}" or similar) into @p sb.
static void expand_currency(rt_string_builder *sb, const char *pattern,
                            const char *symbol, rt_string number) {
    if (!pattern)
        pattern = "{s}{n}";
    const char *p = pattern;
    while (*p) {
        if (p[0] == '{' && p[1] == 's' && p[2] == '}') {
            (void)rt_sb_append_cstr(sb, symbol ? symbol : "$");
            p += 3;
        } else if (p[0] == '{' && p[1] == 'n' && p[2] == '}') {
            const char *cs = rt_string_cstr(number);
            int64_t len = rt_str_len(number);
            if (cs && len > 0)
                (void)rt_sb_append_bytes(sb, cs, (size_t)len);
            p += 3;
        } else {
            char c = *p++;
            (void)rt_sb_append_bytes(sb, &c, 1);
        }
    }
}

rt_string rt_numformat_currency(void *self, double value) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_numformat_t *fmt = as_fmt(self);
    const rt_locdata_currency_t *cur = &fmt->data->currency;

    // Render absolute number at the currency's fraction-digit precision.
    int digits = cur->fraction_digits >= 0 ? cur->fraction_digits : 2;
    rt_string_builder num_sb;
    rt_sb_init(&num_sb);
    fmt_render_number(&num_sb, fmt, value < 0 ? -value : value, digits);
    rt_string num = rt_string_from_bytes(num_sb.data, num_sb.len);
    rt_sb_free(&num_sb);

    rt_string_builder sb;
    rt_sb_init(&sb);
    const char *pattern = value < 0 ? cur->pattern_negative : cur->pattern_positive;
    expand_currency(&sb, pattern, cur->symbol, num);
    rt_string_unref(num);

    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_numformat_currency_of(void *self, double value, rt_string code) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_numformat_t *fmt = as_fmt(self);
    const rt_locdata_currency_t *cur = &fmt->data->currency;

    // Currency code must be 3 chars (ISO-4217) — use it as the symbol if
    // no locale-specific override is available. A future phase can maintain
    // a baked ISO-code -> symbol table.
    const char *sym = cur->symbol ? cur->symbol : "$";
    const char *code_cs = code ? rt_string_cstr(code) : NULL;
    int64_t code_len = code ? rt_str_len(code) : 0;
    if (!code_cs || code_len != 3 ||
        code_cs[0] < 'A' || code_cs[0] > 'Z' ||
        code_cs[1] < 'A' || code_cs[1] > 'Z' ||
        code_cs[2] < 'A' || code_cs[2] > 'Z') {
        rt_trap("Viper.Localization.NumberFormat: CurrencyOf requires a 3-letter uppercase ISO code");
        return rt_string_from_bytes("", 0);
    }
    if (fmt->data->currency.default_code
        && strncmp(code_cs, fmt->data->currency.default_code, 3) != 0) {
        // Non-default code — use the code itself as the symbol placeholder.
        // Phase 2 ships without the full ISO symbol table; treating it as
        // the literal code is honest and safe.
        sym = code_cs;
    }

    int digits = cur->fraction_digits >= 0 ? cur->fraction_digits : 2;
    rt_string_builder num_sb;
    rt_sb_init(&num_sb);
    fmt_render_number(&num_sb, fmt, value < 0 ? -value : value, digits);
    rt_string num = rt_string_from_bytes(num_sb.data, num_sb.len);
    rt_sb_free(&num_sb);

    rt_string_builder sb;
    rt_sb_init(&sb);
    const char *pattern = value < 0 ? cur->pattern_negative : cur->pattern_positive;
    // Emulate expand_currency with the overridden symbol.
    if (!pattern)
        pattern = "{s}{n}";
    const char *p = pattern;
    while (*p) {
        if (p[0] == '{' && p[1] == 's' && p[2] == '}') {
            (void)rt_sb_append_cstr(&sb, sym);
            p += 3;
        } else if (p[0] == '{' && p[1] == 'n' && p[2] == '}') {
            const char *cs = rt_string_cstr(num);
            int64_t len = rt_str_len(num);
            if (cs && len > 0)
                (void)rt_sb_append_bytes(&sb, cs, (size_t)len);
            p += 3;
        } else {
            char c = *p++;
            (void)rt_sb_append_bytes(&sb, &c, 1);
        }
    }
    rt_string_unref(num);

    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_numformat_scientific(void *self, double value, int64_t digits) {
    if (!self)
        return rt_string_from_bytes("", 0);
    rt_numformat_t *fmt = as_fmt(self);
    (void)fmt; // scientific notation is mostly locale-invariant for ASCII;
               // apply only decimal separator substitution.

    int d = (int)digits;
    if (d < 0) d = 0;
    if (d > 20) d = 20;

    char buf[64];
    int len = loc_snprintf_c(buf, sizeof(buf), "%.*e", d, value);
    if (len < 0) len = 0;

    // Substitute decimal separator if the locale differs from ".".
    const char *dec_sep = fmt->data->numbers.decimal_sep;
    const char *exp_char = fmt->data->numbers.exponent;
    rt_string_builder sb;
    rt_sb_init(&sb);
    for (int i = 0; i < len; ++i) {
        if (buf[i] == '.' && dec_sep && strcmp(dec_sep, ".") != 0) {
            (void)rt_sb_append_cstr(&sb, dec_sep);
        } else if ((buf[i] == 'e' || buf[i] == 'E') && exp_char) {
            (void)rt_sb_append_cstr(&sb, exp_char);
        } else {
            append_localized_bytes(&sb, &fmt->data->numbers, buf + i, 1);
        }
    }
    rt_string r = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return r;
}

rt_string rt_numformat_ordinal(void *self, int64_t value) {
    (void)self;
    // v1: delegate to the existing Viper.Text.NumberFormat.Ordinal which
    // implements English suffixes (st/nd/rd/th). Locale-specific ordinal
    // rendering uses plural category lookup and is deferred to a later
    // phase alongside JSON-loaded ordinal suffix tables.
    extern rt_string rt_numfmt_ordinal(int64_t);
    return rt_numfmt_ordinal(value);
}

//===----------------------------------------------------------------------===//
// Parse helpers
//===----------------------------------------------------------------------===//

/// @brief Try to match @p prefix at the start of @p input; return length
///        consumed on success, 0 on no match. NULL/empty prefix matches 0.
static size_t match_prefix(const char *input, size_t input_len,
                           const char *prefix) {
    if (!prefix || !*prefix)
        return 0;
    size_t plen = strlen(prefix);
    if (input_len < plen)
        return 0;
    if (memcmp(input, prefix, plen) == 0)
        return plen;
    return 0;
}

typedef struct {
    int     success;        ///< 1 on full parse, 0 on failure
    double  value;          ///< parsed value (double precision)
    int     had_sign;       ///< 1 if input had an explicit sign
    int     negative;
    int64_t int_value;      ///< exact integer value when allow_fraction == 0
} parse_result_t;

static int match_locale_digit(const char *input, size_t input_len,
                              const rt_locdata_numbers_t *nums,
                              size_t *consumed) {
    if (consumed) *consumed = 0;
    if (input_len == 0)
        return -1;
    if (input[0] >= '0' && input[0] <= '9') {
        if (consumed) *consumed = 1;
        return input[0] - '0';
    }
    digit_spans_t ds = digit_spans_from_locale(nums);
    if (!ds.valid)
        return -1;
    for (int d = 0; d < 10; ++d) {
        if (ds.len[d] > 0 && input_len >= ds.len[d] &&
            memcmp(input, ds.ptr[d], ds.len[d]) == 0) {
            if (consumed) *consumed = ds.len[d];
            return d;
        }
    }
    return -1;
}

typedef struct {
    int success;
    int negative;
    int had_sign;
    int had_frac;
    rt_string_builder digits;
    rt_string_builder frac;
} scan_number_t;

static void scan_number_free(scan_number_t *sn) {
    rt_sb_free(&sn->digits);
    rt_sb_free(&sn->frac);
}

static scan_number_t scan_number_parts(const char *input, size_t input_len,
                                       const rt_numformat_t *fmt,
                                       int allow_fraction) {
    scan_number_t sn;
    memset(&sn, 0, sizeof(sn));
    rt_sb_init(&sn.digits);
    rt_sb_init(&sn.frac);

    size_t i = 0;
    while (i < input_len && isspace((unsigned char)input[i])) ++i;
    if (i >= input_len)
        return sn;

    const rt_locdata_numbers_t *nums = &fmt->data->numbers;
    const char *minus = nums->minus ? nums->minus : "-";
    const char *plus = nums->plus ? nums->plus : "+";
    const char *dec_sep = nums->decimal_sep ? nums->decimal_sep : ".";
    const char *grp_sep = nums->group_sep ? nums->group_sep : "";
    size_t dec_len = strlen(dec_sep);
    size_t grp_len = strlen(grp_sep);

    // Sign.
    size_t adv = match_prefix(input + i, input_len - i, minus);
    if (adv) {
        sn.had_sign = 1;
        sn.negative = 1;
        i += adv;
    } else {
        adv = match_prefix(input + i, input_len - i, plus);
        if (adv) {
            sn.had_sign = 1;
            i += adv;
        }
    }

    size_t digit_count = 0;
    int group_run = 0;          // digits since last separator
    int had_group_sep = 0;
    int first_group_len = -1;   // length of leading group when we hit first sep
    int primary_group = nums->group_size > 0 ? nums->group_size : 3;
    int secondary_group = nums->secondary_group_size > 0
                              ? nums->secondary_group_size
                              : primary_group;

    while (i < input_len) {
        size_t consumed = 0;
        int digit = match_locale_digit(input + i, input_len - i, nums, &consumed);
        if (digit >= 0) {
            char c = (char)('0' + digit);
            (void)rt_sb_append_bytes(&sn.digits, &c, 1);
            digit_count++;
            i += consumed;
            ++group_run;
        } else if (grp_len && i + grp_len <= input_len
                   && memcmp(input + i, grp_sep, grp_len) == 0) {
            if (digit_count == 0)
                return sn; // leading group sep
            if (!had_group_sep) {
                first_group_len = group_run;
                had_group_sep = 1;
            } else if (fmt->strict && group_run != secondary_group) {
                return sn; // strict: inner groups must be exactly secondary_group
            }
            group_run = 0;
            i += grp_len;
        } else {
            break;
        }
    }

    if (fmt->strict && had_group_sep) {
        if (group_run != primary_group)
            return sn;
        if (first_group_len <= 0 || first_group_len > secondary_group)
            return sn;
    }

    // Fractional part.
    size_t frac_count = 0;
    if (allow_fraction && i + dec_len <= input_len
        && memcmp(input + i, dec_sep, dec_len) == 0) {
        i += dec_len;
        sn.had_frac = 1;
        while (i < input_len) {
            size_t consumed = 0;
            int digit = match_locale_digit(input + i, input_len - i, nums, &consumed);
            if (digit >= 0) {
                char c = (char)('0' + digit);
                (void)rt_sb_append_bytes(&sn.frac, &c, 1);
                frac_count++;
                i += consumed;
            } else {
                break;
            }
        }
        if (frac_count == 0)
            return sn; // bare separator with no digits
    }

    // Trailing whitespace tolerated; anything else is a failure.
    while (i < input_len && isspace((unsigned char)input[i])) ++i;
    if (i != input_len)
        return sn;

    if (digit_count == 0 && frac_count == 0)
        return sn;

    sn.success = 1;
    return sn;
}

/// @brief Parse a locale-formatted decimal number from @p input.
static parse_result_t parse_decimal(const char *input, size_t input_len,
                                    const rt_numformat_t *fmt,
                                    int allow_fraction) {
    parse_result_t pr = {0};
    scan_number_t sn = scan_number_parts(input, input_len, fmt, allow_fraction);
    if (!sn.success) {
        scan_number_free(&sn);
        return pr;
    }

    // Build a canonical "." decimal string and strtod it.
    rt_string_builder canonical;
    rt_sb_init(&canonical);
    if (sn.negative)
        (void)rt_sb_append_bytes(&canonical, "-", 1);
    if (sn.digits.len == 0)
        (void)rt_sb_append_bytes(&canonical, "0", 1);
    else
        (void)rt_sb_append_bytes(&canonical, sn.digits.data, sn.digits.len);
    if (sn.had_frac) {
        (void)rt_sb_append_bytes(&canonical, ".", 1);
        (void)rt_sb_append_bytes(&canonical, sn.frac.data, sn.frac.len);
    }
    (void)rt_sb_append_bytes(&canonical, "\0", 1);

    char *end = NULL;
    errno = 0;
    double v = loc_strtod_c(canonical.data, &end);
    if (end == canonical.data || errno == ERANGE || !isfinite(v)) {
        rt_sb_free(&canonical);
        scan_number_free(&sn);
        return pr;
    }

    pr.value = v;
    pr.had_sign = sn.had_sign;
    pr.negative = sn.negative;
    pr.success = 1;
    rt_sb_free(&canonical);
    scan_number_free(&sn);
    return pr;
}

static parse_result_t parse_integer_exact(const char *input, size_t input_len,
                                          const rt_numformat_t *fmt) {
    parse_result_t pr = {0};
    scan_number_t sn = scan_number_parts(input, input_len, fmt, /*allow_fraction=*/0);
    if (!sn.success || sn.digits.len == 0) {
        scan_number_free(&sn);
        return pr;
    }
    uint64_t limit = sn.negative ? ((uint64_t)INT64_MAX + 1u) : (uint64_t)INT64_MAX;
    uint64_t acc = 0;
    for (size_t i = 0; i < sn.digits.len; ++i) {
        unsigned digit = (unsigned)(sn.digits.data[i] - '0');
        if (acc > (limit - digit) / 10u) {
            scan_number_free(&sn);
            return pr;
        }
        acc = acc * 10u + digit;
    }
    if (sn.negative) {
        if (acc == ((uint64_t)INT64_MAX + 1u))
            pr.int_value = INT64_MIN;
        else
            pr.int_value = -(int64_t)acc;
    } else {
        pr.int_value = (int64_t)acc;
    }
    pr.value = (double)pr.int_value;
    pr.had_sign = sn.had_sign;
    pr.negative = sn.negative;
    pr.success = 1;
    scan_number_free(&sn);
    return pr;
}

//===----------------------------------------------------------------------===//
// Parse API
//===----------------------------------------------------------------------===//

double rt_numformat_parse_decimal(void *self, rt_string input) {
    if (!self || !input) {
        rt_trap("Viper.Localization.NumberFormat: ParseDecimal received null input");
        return 0;
    }
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    parse_result_t pr = parse_decimal(cs, (size_t)len, as_fmt(self),
                                       /*allow_fraction=*/1);
    if (!pr.success) {
        rt_trap("Viper.Localization.NumberFormat: cannot parse input as a decimal");
        return 0;
    }
    return pr.value;
}

void *rt_numformat_try_parse_decimal(void *self, rt_string input) {
    if (!self || !input)
        return rt_option_none();
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    parse_result_t pr = parse_decimal(cs, (size_t)len, as_fmt(self),
                                       /*allow_fraction=*/1);
    if (!pr.success)
        return rt_option_none();
    return rt_option_some_f64(pr.value);
}

int64_t rt_numformat_parse_integer(void *self, rt_string input) {
    if (!self || !input) {
        rt_trap("Viper.Localization.NumberFormat: ParseInteger received null input");
        return 0;
    }
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    parse_result_t pr = parse_integer_exact(cs, (size_t)len, as_fmt(self));
    if (!pr.success) {
        rt_trap("Viper.Localization.NumberFormat: cannot parse input as an integer");
        return 0;
    }
    return pr.int_value;
}

void *rt_numformat_try_parse_integer(void *self, rt_string input) {
    if (!self || !input)
        return rt_option_none();
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    parse_result_t pr = parse_integer_exact(cs, (size_t)len, as_fmt(self));
    if (!pr.success)
        return rt_option_none();
    return rt_option_some_i64(pr.int_value);
}

/// @brief Trim the locale's currency symbol (and the default ISO code) from
///        either end of @p input, returning the trimmed slice via @p out_start
///        and @p out_len.
static void strip_currency_affixes(const rt_numformat_t *fmt,
                                   const char *input, size_t input_len,
                                   const char **out_start, size_t *out_len) {
    const char *symbol = fmt->data->currency.symbol;
    const char *code = fmt->data->currency.default_code;
    const char *p = input;
    size_t len = input_len;

    // Leading whitespace.
    while (len > 0 && isspace((unsigned char)p[0])) { ++p; --len; }

    // Leading symbol / code.
    for (int attempt = 0; attempt < 2; ++attempt) {
        size_t adv = match_prefix(p, len, symbol);
        if (adv) { p += adv; len -= adv; continue; }
        adv = match_prefix(p, len, code);
        if (adv) { p += adv; len -= adv; continue; }
        break;
    }

    // Leading whitespace again.
    while (len > 0 && isspace((unsigned char)p[0])) { ++p; --len; }

    // Trailing whitespace.
    while (len > 0 && isspace((unsigned char)p[len - 1])) { --len; }

    // Trailing symbol / code.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (symbol) {
            size_t slen = strlen(symbol);
            if (slen && len >= slen && memcmp(p + len - slen, symbol, slen) == 0) {
                len -= slen;
                continue;
            }
        }
        if (code) {
            size_t clen = strlen(code);
            if (clen && len >= clen && memcmp(p + len - clen, code, clen) == 0) {
                len -= clen;
                continue;
            }
        }
        break;
    }

    // Re-trim whitespace around inner content.
    while (len > 0 && isspace((unsigned char)p[0])) { ++p; --len; }
    while (len > 0 && isspace((unsigned char)p[len - 1])) { --len; }

    *out_start = p;
    *out_len = len;
}

static int expand_currency_pattern_affix(rt_string_builder *sb, const char *p,
                                         size_t len, const char *symbol) {
    for (size_t i = 0; i < len;) {
        if (i + 3 <= len && p[i] == '{' && p[i + 1] == 's' && p[i + 2] == '}') {
            if (symbol)
                (void)rt_sb_append_cstr(sb, symbol);
            i += 3;
        } else if (i + 3 <= len && p[i] == '{' && p[i + 1] == 'n' && p[i + 2] == '}') {
            return 0;
        } else {
            (void)rt_sb_append_bytes(sb, p + i, 1);
            ++i;
        }
    }
    return 1;
}

static int match_currency_pattern(const char *pattern, const char *symbol,
                                  const char *input, size_t input_len,
                                  const char **out_start, size_t *out_len) {
    if (!pattern)
        return 0;
    const char *nph = strstr(pattern, "{n}");
    if (!nph)
        return 0;
    rt_string_builder pre;
    rt_string_builder suf;
    rt_sb_init(&pre);
    rt_sb_init(&suf);
    if (!expand_currency_pattern_affix(&pre, pattern, (size_t)(nph - pattern), symbol) ||
        !expand_currency_pattern_affix(&suf, nph + 3, strlen(nph + 3), symbol)) {
        rt_sb_free(&pre);
        rt_sb_free(&suf);
        return 0;
    }
    if (input_len < pre.len + suf.len ||
        (pre.len && memcmp(input, pre.data, pre.len) != 0) ||
        (suf.len && memcmp(input + input_len - suf.len, suf.data, suf.len) != 0)) {
        rt_sb_free(&pre);
        rt_sb_free(&suf);
        return 0;
    }
    *out_start = input + pre.len;
    *out_len = input_len - pre.len - suf.len;
    rt_sb_free(&pre);
    rt_sb_free(&suf);
    return 1;
}

static int extract_currency_number(const rt_numformat_t *fmt,
                                   const char *input, size_t input_len,
                                   const char **out_start, size_t *out_len,
                                   int *out_negative) {
    const char *p = input;
    size_t len = input_len;
    while (len > 0 && isspace((unsigned char)p[0])) { ++p; --len; }
    while (len > 0 && isspace((unsigned char)p[len - 1])) { --len; }

    int accounting = 0;
    if (len >= 2 && p[0] == '(' && p[len - 1] == ')') {
        accounting = 1;
        ++p;
        len -= 2;
        while (len > 0 && isspace((unsigned char)p[0])) { ++p; --len; }
        while (len > 0 && isspace((unsigned char)p[len - 1])) { --len; }
    }

    const char *inner = NULL;
    size_t inner_len = 0;
    const char *symbol = fmt->data->currency.symbol;
    const char *code = fmt->data->currency.default_code;
    if (match_currency_pattern(fmt->data->currency.pattern_negative, symbol, p, len, &inner, &inner_len) ||
        match_currency_pattern(fmt->data->currency.pattern_negative, code, p, len, &inner, &inner_len)) {
        *out_start = inner;
        *out_len = inner_len;
        *out_negative = 1;
        return 1;
    }
    if (match_currency_pattern(fmt->data->currency.pattern_positive, symbol, p, len, &inner, &inner_len) ||
        match_currency_pattern(fmt->data->currency.pattern_positive, code, p, len, &inner, &inner_len)) {
        *out_start = inner;
        *out_len = inner_len;
        *out_negative = accounting;
        return 1;
    }
    strip_currency_affixes(fmt, p, len, out_start, out_len);
    *out_negative = accounting;
    return 1;
}

double rt_numformat_parse_currency(void *self, rt_string input) {
    if (!self || !input) {
        rt_trap("Viper.Localization.NumberFormat: ParseCurrency received null input");
        return 0;
    }
    rt_numformat_t *fmt = as_fmt(self);
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    const char *inner = NULL;
    size_t inner_len = 0;
    int negative_pattern = 0;
    extract_currency_number(fmt, cs, (size_t)len, &inner, &inner_len, &negative_pattern);
    parse_result_t pr = parse_decimal(inner, inner_len, fmt,
                                       /*allow_fraction=*/1);
    if (!pr.success) {
        rt_trap("Viper.Localization.NumberFormat: cannot parse input as currency");
        return 0;
    }
    return negative_pattern && pr.value > 0 ? -pr.value : pr.value;
}

void *rt_numformat_try_parse_currency(void *self, rt_string input) {
    if (!self || !input)
        return rt_option_none();
    rt_numformat_t *fmt = as_fmt(self);
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    const char *inner = NULL;
    size_t inner_len = 0;
    int negative_pattern = 0;
    extract_currency_number(fmt, cs, (size_t)len, &inner, &inner_len, &negative_pattern);
    parse_result_t pr = parse_decimal(inner, inner_len, fmt,
                                       /*allow_fraction=*/1);
    if (!pr.success)
        return rt_option_none();
    return rt_option_some_f64(negative_pattern && pr.value > 0 ? -pr.value : pr.value);
}
