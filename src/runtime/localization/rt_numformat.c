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

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    fmt->data = rt_locale_get_data(locale);
    fmt->min_frac = 0;
    fmt->max_frac = 3;  // default matches common display conventions
    fmt->grouping = 1;
    fmt->strict = 0;
    fmt->rounding = ROUND_HALF_EVEN;
    return fmt;
}

void *rt_numformat_new(void) {
    void *current = rt_locale_manager_current();
    return fmt_alloc(current);
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

    // Render integer + fraction as two buffers with a high-precision snprintf.
    char whole[64];
    int int_len = snprintf(whole, sizeof(whole), "%.0f", floor(abs_val));
    if (int_len < 0)
        int_len = 0;
    if (int_len == 0) {
        whole[0] = '0';
        whole[1] = '\0';
        int_len = 1;
    }

    // Fractional part: snprintf with fixed decimals, then strip the integer part.
    char full[96];
    int full_len = snprintf(full, sizeof(full), "%.*f", digits_used, abs_val);
    if (full_len < 0)
        full_len = 0;

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
    const char *whole_ptr = full;
    int whole_len = frac_ptr ? (int)((frac_ptr - full) - 1) : full_len;
    if (fmt->grouping && nums->group_sep && nums->group_sep[0] != '\0') {
        rt_numfmt_group_digits(sb, whole_ptr, whole_len,
                               nums->group_sep, strlen(nums->group_sep),
                               nums->group_size > 0 ? nums->group_size : 3);
    } else {
        (void)rt_sb_append_bytes(sb, whole_ptr, (size_t)whole_len);
    }

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
        (void)rt_sb_append_bytes(sb, frac_ptr, (size_t)emit_digits);
    }
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
    return rt_numformat_decimal_n(self, (double)value, 0);
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
    if (code_cs && code_len == 3 && fmt->data->currency.default_code
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
    int len = snprintf(buf, sizeof(buf), "%.*e", d, value);
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
            (void)rt_sb_append_bytes(&sb, buf + i, 1);
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
} parse_result_t;

/// @brief Parse a locale-formatted decimal number from @p input.
/// @details Strips leading/trailing whitespace, optional sign, recognizes the
///          locale's minus/plus tokens, decimal separator, and group separators
///          (tolerated in lenient mode; position-checked in strict mode).
static parse_result_t parse_decimal(const char *input, size_t input_len,
                                    const rt_numformat_t *fmt,
                                    int allow_fraction) {
    parse_result_t pr = {0};
    pr.success = 0;

    // Skip leading whitespace.
    size_t i = 0;
    while (i < input_len && isspace((unsigned char)input[i])) ++i;
    if (i >= input_len)
        return pr;

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
        pr.had_sign = 1;
        pr.negative = 1;
        i += adv;
    } else {
        adv = match_prefix(input + i, input_len - i, plus);
        if (adv) {
            pr.had_sign = 1;
            i += adv;
        }
    }

    // Integer part: digits, possibly interspersed with group separators.
    char digits[96];
    size_t digit_count = 0;
    int group_run = 0;          // digits since last separator
    int had_group_sep = 0;
    int first_group_len = -1;   // length of leading group when we hit first sep

    while (i < input_len) {
        if (input[i] >= '0' && input[i] <= '9') {
            if (digit_count + 1 >= sizeof(digits))
                return pr;
            digits[digit_count++] = input[i];
            ++i;
            ++group_run;
        } else if (grp_len && i + grp_len <= input_len
                   && memcmp(input + i, grp_sep, grp_len) == 0) {
            if (digit_count == 0)
                return pr; // leading group sep
            if (!had_group_sep) {
                first_group_len = group_run;
                had_group_sep = 1;
            } else if (fmt->strict && group_run != (nums->group_size > 0
                                                     ? nums->group_size
                                                     : 3)) {
                return pr; // strict: inner groups must be exactly group_size
            }
            group_run = 0;
            i += grp_len;
        } else {
            break;
        }
    }

    if (fmt->strict && had_group_sep) {
        int expected = nums->group_size > 0 ? nums->group_size : 3;
        if (group_run != expected)
            return pr;
        if (first_group_len <= 0 || first_group_len > expected)
            return pr;
    }

    digits[digit_count] = '\0';

    // Fractional part.
    char frac[32];
    size_t frac_count = 0;
    int had_frac = 0;
    if (allow_fraction && i + dec_len <= input_len
        && memcmp(input + i, dec_sep, dec_len) == 0) {
        i += dec_len;
        had_frac = 1;
        while (i < input_len) {
            if (input[i] >= '0' && input[i] <= '9') {
                if (frac_count + 1 >= sizeof(frac))
                    return pr;
                frac[frac_count++] = input[i];
                ++i;
            } else {
                break;
            }
        }
        if (frac_count == 0)
            return pr; // bare separator with no digits
    }
    frac[frac_count] = '\0';

    // Trailing whitespace tolerated; anything else is a failure.
    while (i < input_len && isspace((unsigned char)input[i])) ++i;
    if (i != input_len)
        return pr;

    if (digit_count == 0 && frac_count == 0)
        return pr;

    // Build a canonical "." decimal string and strtod it.
    char canonical[160];
    size_t cpos = 0;
    if (pr.negative && cpos + 1 < sizeof(canonical))
        canonical[cpos++] = '-';
    if (digit_count == 0)
        canonical[cpos++] = '0';
    else {
        for (size_t k = 0; k < digit_count && cpos + 1 < sizeof(canonical); ++k)
            canonical[cpos++] = digits[k];
    }
    if (had_frac) {
        if (cpos + 1 < sizeof(canonical))
            canonical[cpos++] = '.';
        for (size_t k = 0; k < frac_count && cpos + 1 < sizeof(canonical); ++k)
            canonical[cpos++] = frac[k];
    }
    canonical[cpos] = '\0';

    char *end = NULL;
    double v = strtod(canonical, &end);
    if (end == canonical)
        return pr;

    pr.value = v;
    pr.success = 1;
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
    parse_result_t pr = parse_decimal(cs, (size_t)len, as_fmt(self),
                                       /*allow_fraction=*/0);
    if (!pr.success) {
        rt_trap("Viper.Localization.NumberFormat: cannot parse input as an integer");
        return 0;
    }
    return (int64_t)pr.value;
}

void *rt_numformat_try_parse_integer(void *self, rt_string input) {
    if (!self || !input)
        return rt_option_none();
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    parse_result_t pr = parse_decimal(cs, (size_t)len, as_fmt(self),
                                       /*allow_fraction=*/0);
    if (!pr.success)
        return rt_option_none();
    return rt_option_some_i64((int64_t)pr.value);
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
    strip_currency_affixes(fmt, cs, (size_t)len, &inner, &inner_len);
    parse_result_t pr = parse_decimal(inner, inner_len, fmt,
                                       /*allow_fraction=*/1);
    if (!pr.success) {
        rt_trap("Viper.Localization.NumberFormat: cannot parse input as currency");
        return 0;
    }
    return pr.value;
}

void *rt_numformat_try_parse_currency(void *self, rt_string input) {
    if (!self || !input)
        return rt_option_none();
    rt_numformat_t *fmt = as_fmt(self);
    const char *cs = rt_string_cstr(input);
    int64_t len = rt_str_len(input);
    const char *inner = NULL;
    size_t inner_len = 0;
    strip_currency_affixes(fmt, cs, (size_t)len, &inner, &inner_len);
    parse_result_t pr = parse_decimal(inner, inner_len, fmt,
                                       /*allow_fraction=*/1);
    if (!pr.success)
        return rt_option_none();
    return rt_option_some_f64(pr.value);
}
