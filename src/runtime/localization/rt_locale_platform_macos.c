//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_platform_macos.c
// Purpose: macOS implementation of rt_locale_platform_detect_system. Uses the
//          same POSIX env var cascade as Linux (not CoreFoundation) so the
//          localization module stays independent of Foundation.framework. v0.2.5
//          removed Security.framework for the same reason; this preserves the
//          zero-framework story for the locale path as well.
//
// Key invariants:
//   - Only compiled when RT_PLATFORM_MACOS is set.
//   - No CoreFoundation / Foundation dependency. Users who need the
//     preferredLanguages array can set $LANG in their launch environment.
//
// Ownership/Lifetime:
//   - Output buffer is caller-owned; adapter writes at most cap-1 bytes.
//
// Links: src/runtime/localization/rt_locale_platform.h (interface).
//
//===----------------------------------------------------------------------===//

#include "rt_locale_platform.h"
#include "rt_platform.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_MACOS

/// @brief Return 1 if @p s is a C/POSIX no-locale sentinel value.
/// @details "C", "c", "POSIX", and "posix" are treated as "no real locale" so
///          the caller falls back to the invariant instead of trying to parse them.
///          NULL and empty strings are also treated as no-locale.
/// @param s Environment variable value to check; may be NULL.
/// @return 1 if @p s is a no-locale sentinel; 0 otherwise.
static int is_c_or_posix(const char *s) {
    if (!s || !*s)
        return 1;
    return (strcmp(s, "C") == 0) || (strcmp(s, "POSIX") == 0) || (strcmp(s, "c") == 0) ||
           (strcmp(s, "posix") == 0);
}

/// @brief Strip encoding suffixes and convert a POSIX locale string to a near-BCP-47 tag.
/// @details Copies @p src into @p out until a `.` (encoding, e.g., `.UTF-8`) or `@`
///          (modifier, e.g., `@latin`) is encountered. Underscores are converted to
///          dashes so the result matches BCP-47 conventions (e.g., "en_US" → "en-US").
/// @param src POSIX locale string (e.g., "en_US.UTF-8"); may be NULL (-1 returned).
/// @param out Caller-provided output buffer for the cleaned tag.
/// @param cap Capacity of @p out in bytes (must be ≥ 2).
/// @return 0 on success; -1 if @p src/@p out is NULL, @p cap is too small, the result
///         would overflow, or the cleaned result is empty.
static int clean_posix_tag(const char *src, char *out, size_t cap) {
    if (!src || !out || cap < 2)
        return -1;
    size_t i = 0;
    for (const char *p = src; *p; ++p) {
        char c = *p;
        if (c == '.' || c == '@')
            break;
        if (c == '_')
            c = '-';
        if (i + 1 >= cap)
            return -1;
        out[i++] = c;
    }
    if (i == 0)
        return -1;
    out[i] = '\0';
    return 0;
}

/// @brief Detect the macOS system locale via the POSIX environment variable cascade.
/// @details Polls `LC_ALL`, then `LANG`, then `LC_MESSAGES` in that order of
///          precedence. Skips values that are C/POSIX sentinels. Cleans each
///          candidate with `clean_posix_tag` to produce a BCP-47-compatible tag.
///          Uses env vars rather than CoreFoundation to avoid a framework dependency.
/// @param out  Caller-provided buffer to receive the BCP-47 tag.
/// @param cap  Capacity of @p out in bytes (must be ≥ 2).
/// @return 0 if a usable tag was written to @p out; -1 if detection failed.
int rt_locale_platform_detect_system(char *out, size_t cap) {
    if (!out || cap < 2)
        return -1;

    // macOS does honor POSIX env vars when launched from a terminal or shell,
    // and most developers set them. GUI-launched apps get "C" unless the user
    // explicitly sets them; in that case we fall back to the invariant locale,
    // which is honest: we don't have Foundation available to ask for
    // preferredLanguages.
    static const char *const kVars[] = {"LC_ALL", "LANG", "LC_MESSAGES", NULL};
    for (size_t i = 0; kVars[i]; ++i) {
        const char *val = getenv(kVars[i]);
        if (is_c_or_posix(val))
            continue;
        if (clean_posix_tag(val, out, cap) == 0)
            return 0;
    }
    return -1;
}

#endif // RT_PLATFORM_MACOS
