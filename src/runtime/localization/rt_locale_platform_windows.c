//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include "rt_locale_posix_tag.h"
#include "rt_platform.h"

#include <stddef.h>
#include <stdlib.h>

#if RT_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Detect the Windows system locale and write a BCP-47 tag to @p out.
/// @details Primary path: calls `GetUserDefaultLocaleName` (Vista+) which returns a
///          BCP-47 tag as UTF-16. Since BCP-47 is ASCII by construction, the
///          UTF-16 → UTF-8 transcoding is a safe byte truncation. Rejects any
///          non-ASCII wide character as a corrupt locale indicator.
///          Fallback path: when the Win32 call returns empty (MSYS/MinGW environments),
///          polls `LC_ALL`, `LC_MESSAGES`, and `LANG` env vars and applies the
///          POSIX cleanup path.
/// @param out  Caller-provided buffer to receive the BCP-47 tag.
/// @param cap  Capacity of @p out in bytes (must be ≥ 2).
/// @return 0 if a usable tag was written to @p out; -1 if detection failed.
int rt_locale_platform_detect_system(char *out, size_t cap) {
    if (out && cap > 0)
        out[0] = '\0';
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
    static const char *const kVars[] = {"LC_ALL", "LC_MESSAGES", "LANG", NULL};
    for (size_t i = 0; kVars[i]; ++i) {
        const char *val = getenv(kVars[i]);
        if (rt_locale_posix_value_is_invariant(val))
            continue;
        if (rt_locale_clean_posix_tag(val, out, cap) == 0)
            return 0;
    }
    return -1;
}

#endif // RT_PLATFORM_WINDOWS
