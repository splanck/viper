//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_locale_platform_windows.c
// Purpose: Windows implementation of rt_locale_platform_detect_system. Uses
//          GetUserDefaultLocaleName (Vista+) which already returns a BCP-47
//          tag ("en-US", "zh-Hans-CN") as UTF-16; we just transcode to UTF-8
//          and pass through. Falls back to the %LANG% env var (for MinGW/MSYS
//          POSIX-flavored environments) when the Win32 call returns an empty
//          result.
//
// Key invariants:
//   - Only compiled when RT_PLATFORM_WINDOWS is set.
//   - GetUserDefaultLocaleName output is already BCP-47 per the Windows API
//     contract (LOCALE_NAME_MAX_LENGTH = 85); no underscore-to-dash conversion
//     is needed. Casing is already the canonical form.
//   - UTF-16 -> UTF-8 transcoding is restricted to BCP-47 content which is
//     ASCII by construction; the transcoder is a simple truncation.
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

#if RT_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Return 1 if @p s is a C/POSIX no-locale sentinel value.
/// @details Used in the MinGW/MSYS fallback path where POSIX env vars may be set.
///          "C", "c", "POSIX", and "posix" map to no-locale. NULL and empty are also no-locale.
/// @param s Environment variable value to check; may be NULL.
/// @return 1 if @p s is a no-locale sentinel; 0 otherwise.
static int is_c_or_posix(const char *s) {
    if (!s || !*s)
        return 1;
    return (strcmp(s, "C") == 0) || (strcmp(s, "POSIX") == 0) ||
           (strcmp(s, "c") == 0) || (strcmp(s, "posix") == 0);
}

/// @brief Strip encoding/modifier suffixes from a POSIX-style locale string and normalize.
/// @details Used in the MinGW/MSYS fallback path. Strips `.encoding` and `@modifier`
///          suffixes and converts underscores to dashes (e.g., "fr_FR.UTF-8" → "fr-FR").
/// @param src POSIX locale string; may be NULL (-1 returned).
/// @param out Caller-provided buffer for the cleaned BCP-47-compatible tag.
/// @param cap Capacity of @p out in bytes (must be ≥ 2).
/// @return 0 on success; -1 if input/output are NULL, @p cap is too small, or result overflows.
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

/// @brief Detect the Windows system locale and write a BCP-47 tag to @p out.
/// @details Primary path: calls `GetUserDefaultLocaleName` (Vista+) which returns a
///          BCP-47 tag as UTF-16. Since BCP-47 is ASCII by construction, the
///          UTF-16 → UTF-8 transcoding is a safe byte truncation. Rejects any
///          non-ASCII wide character as a corrupt locale indicator.
///          Fallback path: when the Win32 call returns empty (MSYS/MinGW environments),
///          polls `LC_ALL`, `LANG`, and `LC_MESSAGES` env vars and applies the
///          POSIX cleanup path.
/// @param out  Caller-provided buffer to receive the BCP-47 tag.
/// @param cap  Capacity of @p out in bytes (must be ≥ 2).
/// @return 0 if a usable tag was written to @p out; -1 if detection failed.
int rt_locale_platform_detect_system(char *out, size_t cap) {
    if (!out || cap < 2)
        return -1;

    // Try Win32 first. LOCALE_NAME_MAX_LENGTH is 85 (wide chars) which is far
    // larger than any real BCP-47 tag, so we size the local buffer to match.
    wchar_t wbuf[LOCALE_NAME_MAX_LENGTH];
    int n = GetUserDefaultLocaleName(wbuf, LOCALE_NAME_MAX_LENGTH);
    if (n > 1) {
        // n includes the terminator. BCP-47 tags are ASCII so the UTF-16 ->
        // UTF-8 transcoding is a simple byte truncation for anything valid.
        // Any non-ASCII byte here would indicate a corrupt locale; reject to
        // keep downstream parsing simple.
        size_t need = (size_t)(n - 1);
        if (need + 1 > cap)
            return -1;
        for (int i = 0; i < n - 1; ++i) {
            wchar_t wc = wbuf[i];
            if (wc > 0x7F)
                return -1;
            out[i] = (char)wc;
        }
        out[n - 1] = '\0';
        return 0;
    }

    // Fallback: MSYS/MinGW environments commonly set LANG. Reuse the POSIX
    // cleanup path for that case.
    static const char *const kVars[] = { "LC_ALL", "LANG", "LC_MESSAGES", NULL };
    for (size_t i = 0; kVars[i]; ++i) {
        const char *val = getenv(kVars[i]);
        if (is_c_or_posix(val))
            continue;
        if (clean_posix_tag(val, out, cap) == 0)
            return 0;
    }
    return -1;
}

#endif // RT_PLATFORM_WINDOWS
