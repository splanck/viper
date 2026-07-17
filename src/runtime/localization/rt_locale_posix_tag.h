//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_posix_tag.h
// Purpose: Internal validation and normalization helpers for POSIX-style
//          locale environment values used by all locale platform adapters.
//
// Key invariants:
//   - C/POSIX sentinels are recognized case-insensitively before suffixes.
//   - Normalized tags contain only non-empty 1-8 byte ASCII alphanumeric
//     subtags separated by single dashes; the first subtag is alphabetic.
//   - Output is empty on every failure.
//
// Ownership/Lifetime:
//   - Helpers borrow input and write only to caller-owned storage.
//
// Links: src/runtime/localization/rt_locale_platform_posix.c,
//        src/runtime/localization/rt_locale_platform_macos.c,
//        src/runtime/localization/rt_locale_platform_windows.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>

static inline int rt_locale_ascii_lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static inline int rt_locale_ascii_is_alpha(int ch) {
    ch = rt_locale_ascii_lower(ch);
    return ch >= 'a' && ch <= 'z';
}

static inline int rt_locale_ascii_is_alnum(int ch) {
    return rt_locale_ascii_is_alpha(ch) || (ch >= '0' && ch <= '9');
}

/// @brief Recognize C and POSIX invariant-locale values before .encoding/@modifier suffixes.
static inline int rt_locale_posix_value_is_invariant(const char *value) {
    static const char posix[] = "posix";
    size_t len = 0;
    if (!value || !*value)
        return 1;
    while (value[len] && value[len] != '.' && value[len] != '@')
        len++;
    if (len == 1 && rt_locale_ascii_lower((unsigned char)value[0]) == 'c')
        return 1;
    if (len != sizeof(posix) - 1u)
        return 0;
    for (size_t i = 0; i < len; i++) {
        if (rt_locale_ascii_lower((unsigned char)value[i]) != posix[i])
            return 0;
    }
    return 1;
}

/// @brief Normalize one POSIX locale value to a validated near-BCP-47 tag.
/// @details Strips .encoding/@modifier suffixes and maps underscores to dashes.
/// @return 0 on success, -1 on invalid input, malformed subtags, or overflow.
static inline int rt_locale_clean_posix_tag(const char *src, char *out, size_t cap) {
    size_t written = 0;
    size_t subtag_len = 0;
    size_t subtag_index = 0;
    if (out && cap > 0)
        out[0] = '\0';
    if (!src || !out || cap < 2)
        return -1;

    for (const char *p = src; *p && *p != '.' && *p != '@'; ++p) {
        unsigned char byte = (unsigned char)*p;
        char c = byte == '_' ? '-' : (char)byte;
        if (c == '-') {
            if (subtag_len == 0)
                goto fail;
            subtag_index++;
            subtag_len = 0;
        } else {
            if (!rt_locale_ascii_is_alnum(byte) ||
                (subtag_index == 0 && !rt_locale_ascii_is_alpha(byte)) || subtag_len >= 8)
                goto fail;
            subtag_len++;
        }
        if (written + 1 >= cap)
            goto fail;
        out[written++] = c;
    }
    if (written == 0 || subtag_len == 0)
        goto fail;
    out[written] = '\0';
    return 0;

fail:
    out[0] = '\0';
    return -1;
}
