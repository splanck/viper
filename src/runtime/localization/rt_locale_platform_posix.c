//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_platform_posix.c
// Purpose: POSIX (Linux, BSD) implementation of rt_locale_platform_detect_system.
//          Walks the standard POSIX env var cascade ($LC_ALL -> $LC_MESSAGES ->
//          $LANG) and cleans the result into a near-BCP-47 shape by
//          stripping encoding suffixes (.UTF-8) and modifier suffixes
//          (@latin), then normalizing underscores to dashes.
//
// Key invariants:
//   - Only compiled when the build target is not Windows and not APPLE
//     (Linux, FreeBSD, OpenBSD, NetBSD, etc.).
//   - "C" and "POSIX" env values are treated as detection failure so the
//     caller falls back to the invariant locale rather than trying to
//     format numbers with C-locale separators.
//   - No allocation; works entirely with the caller's output buffer and the
//     process environment block.
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

#if !RT_PLATFORM_WINDOWS && !RT_PLATFORM_MACOS

/// @brief Return 1 if @p s is a C/POSIX no-locale sentinel value.
/// @details "C", "c", "POSIX", and "posix" are treated as "no real locale" so
///          the caller falls back to the invariant locale. NULL and empty strings
///          are also treated as no-locale.
/// @param s Environment variable value to check; may be NULL.
/// @return 1 if @p s is a no-locale sentinel; 0 otherwise.
static int is_c_or_posix(const char *s) {
    if (!s || !*s)
        return 1;
    return (strcmp(s, "C") == 0) || (strcmp(s, "POSIX") == 0) || (strcmp(s, "c") == 0) ||
           (strcmp(s, "posix") == 0);
}

/// @brief Copy @p src into @p out, stripping trailing @c .suffix and @c \@modifier
///        and converting underscores to dashes along the way. Returns 0 on
///        success; -1 if the cleaned result would be empty or overflow @p cap.
static int clean_posix_tag(const char *src, char *out, size_t cap) {
    if (!src || !out || cap < 2)
        return -1;

    size_t i = 0;
    for (const char *p = src; *p; ++p) {
        char c = *p;
        // Encoding suffix (.UTF-8, .ISO-8859-1, ...) or modifier (@latin, @pinyin)
        // end the useful portion of the tag.
        if (c == '.' || c == '@')
            break;
        // POSIX locale names use underscore between lang and region; BCP-47
        // uses dash. Normalize here so the Locale parser sees consistent input.
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

/// @brief Detect the system locale on POSIX platforms via the standard env var cascade.
/// @details Polls `LC_ALL`, then `LANG`, then `LC_MESSAGES` per IEEE Std 1003.1.
///          LC_ALL and LC_MESSAGES are preferred over LC_NUMERIC/LC_COLLATE because
///          the message/display locale is what users expect as "the system locale".
///          Skips C/POSIX sentinels and cleans each candidate with `clean_posix_tag`.
/// @param out  Caller-provided buffer to receive the BCP-47 tag.
/// @param cap  Capacity of @p out in bytes (must be ≥ 2).
/// @return 0 if a usable tag was written to @p out; -1 if detection failed.
int rt_locale_platform_detect_system(char *out, size_t cap) {
    if (!out || cap < 2)
        return -1;

    // POSIX precedence per IEEE Std 1003.1: LC_ALL overrides category vars,
    // which override LANG. We poll LC_ALL and LC_MESSAGES specifically because
    // message / display locale is what users generally mean by "the system
    // locale"; LC_NUMERIC or LC_COLLATE could legitimately differ.
    static const char *const kVars[] = {"LC_ALL", "LC_MESSAGES", "LANG", NULL};
    for (size_t i = 0; kVars[i]; ++i) {
        const char *val = getenv(kVars[i]);
        if (is_c_or_posix(val))
            continue;
        if (clean_posix_tag(val, out, cap) == 0)
            return 0;
    }
    return -1;
}

#else

// Placeholder symbol when this TU is accidentally compiled on non-POSIX
// platforms (e.g., a build misconfiguration that pulls in more than one
// adapter). Keeps the link graph sane while the real adapter owns the symbol.
int rt_locale_platform_detect_system_posix_unused_(char *out, size_t cap) {
    (void)out;
    (void)cap;
    return -1;
}

#endif // !RT_PLATFORM_WINDOWS && !RT_PLATFORM_MACOS
