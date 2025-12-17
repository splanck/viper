//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_parse.c
// Purpose: Safe parsing functions for Viper.Parse namespace.
//
// Key invariants: All functions handle invalid input gracefully without
//                 trapping. NULL output pointers cause immediate false return.
//                 Empty strings are considered invalid for all types.
// Ownership/Lifetime: Functions operate purely on caller-supplied values;
//                     no state is retained between calls.
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt_parse.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Advance a pointer past ASCII whitespace characters.
    static inline const char *skip_whitespace(const char *s)
    {
        while (*s && isspace((unsigned char)*s))
            ++s;
        return s;
    }

    /// @brief Check if string has only trailing whitespace after cursor.
    static inline bool is_end_of_input(const char *s)
    {
        s = skip_whitespace(s);
        return *s == '\0';
    }

    /// @brief Case-insensitive string comparison.
    static inline bool str_eq_ci(const char *a, const char *b)
    {
        while (*a && *b)
        {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
                return false;
            ++a;
            ++b;
        }
        return *a == *b;
    }

    bool rt_parse_try_int(rt_string s, int64_t *out_value)
    {
        if (!out_value)
            return false;

        const char *text = rt_string_cstr(s);
        if (!text)
            return false;

        const char *cursor = skip_whitespace(text);
        if (*cursor == '\0')
            return false;

        errno = 0;
        char *endptr = NULL;
        long long parsed = strtoll(cursor, &endptr, 10);

        if (errno == ERANGE)
            return false;
        if (!endptr || endptr == cursor)
            return false;
        if (!is_end_of_input(endptr))
            return false;

        *out_value = (int64_t)parsed;
        return true;
    }

    bool rt_parse_try_num(rt_string s, double *out_value)
    {
        if (!out_value)
            return false;

        const char *text = rt_string_cstr(s);
        if (!text)
            return false;

        const char *cursor = skip_whitespace(text);
        if (*cursor == '\0')
            return false;

        errno = 0;
        char *endptr = NULL;
        double value = 0.0;

#if defined(_WIN32)
        _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
        if (!c_locale)
            return false;
        value = _strtod_l(cursor, &endptr, c_locale);
        _free_locale(c_locale);
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!c_locale)
        return false;
    locale_t previous = uselocale(c_locale);
    value = strtod(cursor, &endptr);
    uselocale(previous);
    freelocale(c_locale);
#endif

        if (errno == ERANGE || !isfinite(value))
            return false;
        if (!endptr || endptr == cursor)
            return false;
        if (!is_end_of_input(endptr))
            return false;

        *out_value = value;
        return true;
    }

    bool rt_parse_try_bool(rt_string s, bool *out_value)
    {
        if (!out_value)
            return false;

        const char *text = rt_string_cstr(s);
        if (!text)
            return false;

        const char *cursor = skip_whitespace(text);
        if (*cursor == '\0')
            return false;

        // Extract the word (until whitespace or end)
        char word[16];
        size_t i = 0;
        while (*cursor && !isspace((unsigned char)*cursor) && i < sizeof(word) - 1)
            word[i++] = *cursor++;
        word[i] = '\0';

        // Check for trailing non-whitespace
        if (!is_end_of_input(cursor))
            return false;

        // Check for true values
        if (str_eq_ci(word, "true") || str_eq_ci(word, "yes") || str_eq_ci(word, "1") ||
            str_eq_ci(word, "on"))
        {
            *out_value = true;
            return true;
        }

        // Check for false values
        if (str_eq_ci(word, "false") || str_eq_ci(word, "no") || str_eq_ci(word, "0") ||
            str_eq_ci(word, "off"))
        {
            *out_value = false;
            return true;
        }

        return false;
    }

    int64_t rt_parse_int_or(rt_string s, int64_t default_value)
    {
        int64_t result;
        if (rt_parse_try_int(s, &result))
            return result;
        return default_value;
    }

    double rt_parse_num_or(rt_string s, double default_value)
    {
        double result;
        if (rt_parse_try_num(s, &result))
            return result;
        return default_value;
    }

    bool rt_parse_bool_or(rt_string s, bool default_value)
    {
        bool result;
        if (rt_parse_try_bool(s, &result))
            return result;
        return default_value;
    }

    bool rt_parse_is_int(rt_string s)
    {
        int64_t dummy;
        return rt_parse_try_int(s, &dummy);
    }

    bool rt_parse_is_num(rt_string s)
    {
        double dummy;
        return rt_parse_try_num(s, &dummy);
    }

    int64_t rt_parse_int_radix(rt_string s, int64_t radix, int64_t default_value)
    {
        // Validate radix range
        if (radix < 2 || radix > 36)
            return default_value;

        const char *text = rt_string_cstr(s);
        if (!text)
            return default_value;

        const char *cursor = skip_whitespace(text);
        if (*cursor == '\0')
            return default_value;

        errno = 0;
        char *endptr = NULL;
        long long parsed = strtoll(cursor, &endptr, (int)radix);

        if (errno == ERANGE)
            return default_value;
        if (!endptr || endptr == cursor)
            return default_value;
        if (!is_end_of_input(endptr))
            return default_value;

        return (int64_t)parsed;
    }

#ifdef __cplusplus
}
#endif
