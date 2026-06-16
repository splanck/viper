//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTFilePathUtf8WideShim.c
// Purpose: Provide the Windows UTF-8 path conversion helper for isolated
//          runtime contract tests that compile file-loading modules without
//          linking the full runtime library.
//
// Key invariants:
//   - Only Windows builds emit the helper; non-Windows tests use POSIX paths
//     directly through rt_file_stdio.h.
//   - Conversion mirrors the runtime helper: strict UTF-8 sizing first,
//     lenient retry for legacy byte sequences, and caller-owned allocation.
//
// Ownership/Lifetime:
//   - Returned buffers are allocated with malloc and must be released with free.
//
// Links: src/runtime/io/rt_file_path.c (production helper),
//        src/runtime/io/rt_file_stdio.h (isolated caller)
//
//===----------------------------------------------------------------------===//

#include "rt_file_path.h"
#include "rt_platform.h"

#if RT_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <windows.h>

wchar_t *rt_file_path_utf8_to_wide(const char *utf8) {
    if (!utf8)
        return NULL;

    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if (needed <= 0)
        needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (needed <= 0)
        return NULL;

    wchar_t *wide = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!wide)
        return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, needed) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}
#endif
