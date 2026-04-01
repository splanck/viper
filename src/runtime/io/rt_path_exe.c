//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_path_exe.c
// Purpose: Cross-platform detection of the running executable's directory.
//          Provides Path.ExeDir() for Zia/BASIC and a C helper for the asset
//          manager's auto-discovery of .vpa pack files.
//
// Key invariants:
//   - Returns the directory containing the executable, not the exe path itself.
//   - macOS: uses _NSGetExecutablePath + realpath + dirname.
//   - Windows: uses GetModuleFileNameA + strip filename.
//   - Linux: uses readlink("/proc/self/exe") + dirname.
//   - ViperDOS: returns "." (no meaningful exe path).
//   - Returned C strings are malloc'd; caller must free.
//   - Returned runtime strings are GC-managed.
//
// Ownership/Lifetime:
//   - rt_path_exe_dir_cstr() returns a malloc'd string (caller frees).
//   - rt_path_exe_dir_str() returns a GC-managed runtime string.
//
// Links: rt_asset.c (consumer), rt_path.c (path utilities)
//
//===----------------------------------------------------------------------===//

#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <limits.h>
#elif defined(__linux__)
#include <limits.h>
#include <unistd.h>
#elif defined(__viperdos__)
// No exe path detection on ViperDOS.
#else
#include <limits.h>
#include <unistd.h>
#endif

// ─── dirname helper ─────────────────────────────────────────────────────────

/// @brief Strip the last path component from a path string (in-place).
/// @param path  Mutable null-terminated string. Modified to contain only the
///              directory portion.
static void strip_filename(char *path) {
    if (!path)
        return;
    size_t len = strlen(path);
    // Walk backwards to find the last separator.
    while (len > 0) {
        len--;
        if (path[len] == '/' || path[len] == '\\') {
            path[len] = '\0';
            return;
        }
    }
    // No separator found — return "." for current directory.
    path[0] = '.';
    path[1] = '\0';
}

// ─── rt_path_exe_dir_cstr ───────────────────────────────────────────────────

/// @brief Get the directory of the running executable as a C string.
/// @return malloc'd string (caller must free), or NULL on failure.
char *rt_path_exe_dir_cstr(void) {
#if defined(_WIN32)
    // Windows: GetModuleFileNameA
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return NULL;
    buf[len] = '\0';
    strip_filename(buf);
    return strdup(buf);

#elif defined(__APPLE__)
    // macOS: use /proc/self alternative — _NSGetExecutablePath
    // via dlsym to avoid native linker binding issues.
    {
        typedef int (*nsget_fn)(char *, uint32_t *);
        void *handle = dlopen(NULL, RTLD_LAZY);
        if (!handle)
            return strdup(".");
        nsget_fn fn = (nsget_fn)dlsym(handle, "_NSGetExecutablePath");
        if (!fn) {
            dlclose(handle);
            return strdup(".");
        }

        char raw[PATH_MAX];
        uint32_t size = sizeof(raw);
        if (fn(raw, &size) != 0) {
            dlclose(handle);
            return strdup(".");
        }
        dlclose(handle);

        char resolved[PATH_MAX];
        if (!realpath(raw, resolved))
            return strdup(".");

        strip_filename(resolved);
        return strdup(resolved);
    }

#elif defined(__linux__)
    // Linux: readlink("/proc/self/exe") → dirname
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return NULL;
    buf[len] = '\0';
    strip_filename(buf);
    return strdup(buf);

#elif defined(__viperdos__)
    // ViperDOS: no meaningful exe path; return current directory.
    return strdup(".");

#else
    // Unknown platform fallback.
    return strdup(".");
#endif
}

// ─── rt_path_exe_dir_str (runtime API) ──────────────────────────────────────

/// @brief Viper.IO.Path.ExeDir() — returns directory of the running executable.
/// @return Runtime string (GC-managed). Returns "." if detection fails.
rt_string rt_path_exe_dir_str(void) {
    char *dir = rt_path_exe_dir_cstr();
    if (!dir)
        return rt_string_from_bytes(".", 1);

    rt_string result = rt_string_from_bytes(dir, strlen(dir));
    free(dir);
    return result;
}
