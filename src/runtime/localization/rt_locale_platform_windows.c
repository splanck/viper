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

static int is_c_or_posix(const char *s) {
    if (!s || !*s)
        return 1;
    return (strcmp(s, "C") == 0) || (strcmp(s, "POSIX") == 0) ||
           (strcmp(s, "c") == 0) || (strcmp(s, "posix") == 0);
}

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
