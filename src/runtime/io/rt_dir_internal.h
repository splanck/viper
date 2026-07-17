//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_dir_internal.h
// Purpose: Shared platform layer for the directory runtime, included by
//   rt_dir.c (create/remove/move + recursive delete) and rt_dir_list.c
//   (enumeration). Centralizes the UTF-8<->UTF-16 Windows path helpers, the
//   POSIX path predicate, separator/join utilities, and typed trap wrappers.
//
// Key invariants:
//   - Helpers are static inline: each translation unit gets an internal-linkage
//     copy, so no platform helper symbol is exported (and none can drift).
//   - Windows wide-string helpers compile only under _WIN32; the POSIX path
//     predicate only under !_WIN32. PATH_SEP / PATH_MAX are defined per platform.
//
// Ownership/Lifetime:
//   - Allocating helpers (join, wide<->utf8) return malloc-owned buffers the
//     caller frees.
//
// Links: rt_dir.c, rt_dir_list.c, rt_dir.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_error.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <wchar.h>
#include <windows.h>
#define PATH_SEP '\\'
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PATH_SEP '/'
#endif

#if !defined(_WIN32)
/// @brief Return 1 if `path` is an existing directory (POSIX `stat`), 0 otherwise.
static inline int rt_dir_posix_path_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
#endif

/// @brief Test whether `ch` is a path separator (accepts both `/` and `\`).
///
/// Both forms are accepted everywhere (Windows uses `\` natively
/// but tolerates `/`; POSIX treats `\` as a literal but the runtime
/// canonicalizes here for consistency).
static inline int rt_dir_is_sep_char(char ch) {
#ifdef _WIN32
    return ch == '/' || ch == '\\';
#else
    return ch == '/';
#endif
}

/// @brief Concatenate `base` + `PATH_SEP` + `child` into a new heap string.
///
/// Skips the inserted separator if `base` already ends in one.
/// Caller owns the returned buffer (`free`). Returns NULL on
/// allocation failure.
static inline char *rt_dir_join_child_alloc(const char *base, const char *child) {
    size_t base_len = strlen(base);
    size_t child_len = strlen(child);
    int needs_sep = base_len > 0 && !rt_dir_is_sep_char(base[base_len - 1]);
    if (base_len > SIZE_MAX - child_len - (size_t)needs_sep - 1)
        return NULL;
    char *joined = (char *)malloc(base_len + (size_t)needs_sep + child_len + 1);
    if (!joined)
        return NULL;
    memcpy(joined, base, base_len);
    if (needs_sep)
        joined[base_len++] = PATH_SEP;
    memcpy(joined + base_len, child, child_len);
    joined[base_len + child_len] = '\0';
    return joined;
}

/// @brief Compute the length of the absolute-root prefix at the start of `path`.
///
/// Recognises:
///   - POSIX absolute: leading `/` → returns 1.
///   - Windows drive-absolute: e.g. `C:\` → returns 3.
///   - Windows UNC: `\\server\share` (with optional trailing
///     separator) → returns the index past `share[\]`.
/// Used to detect when traversal must stop unwinding "..".
/// @return Number of leading bytes that constitute the root, or 0 if relative.
static inline size_t rt_dir_root_prefix_len(const char *path, size_t len) {
    if (!path || len == 0)
        return 0;
#ifdef _WIN32
    if (len >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && rt_dir_is_sep_char(path[2])) {
        return 3;
    }
    if (len >= 2 && rt_dir_is_sep_char(path[0]) && rt_dir_is_sep_char(path[1])) {
        size_t i = 2;
        while (i < len && !rt_dir_is_sep_char(path[i]))
            i++;
        if (i == len)
            return len;
        i++;
        while (i < len && !rt_dir_is_sep_char(path[i]))
            i++;
        if (i < len && rt_dir_is_sep_char(path[i]))
            i++;
        return i;
    }
#endif
    return rt_dir_is_sep_char(path[0]) ? 1 : 0;
}

// Trap helpers — wrap rt_trap_raise_kind so individual callers
// don't have to repeat the boilerplate of choosing a kind / error
// code. Each emits a typed trap that user code can catch with the
// matching exception type.

/// @brief Raise a Domain-Error trap (e.g. invalid argument).
static inline void rt_dir_trap_domain(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_DOMAIN_ERROR, Err_DomainError, -1, msg);
}

/// @brief Raise a generic Runtime-Error trap.
static inline void rt_dir_trap_runtime(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_RUNTIME_ERROR, Err_RuntimeError, -1, msg);
}

/// @brief Raise an IO-Error trap (e.g. permission denied, disk full).
static inline void rt_dir_trap_io(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_IO_ERROR, Err_IOError, -1, msg);
}

/// @brief Raise a File-Not-Found trap.
static inline void rt_dir_trap_not_found(const char *msg) {
    rt_trap_raise_kind(RT_TRAP_KIND_FILE_NOT_FOUND, Err_FileNotFound, -1, msg);
}

#ifdef _WIN32
//=============================================================================
// Windows wide-string helpers
//
// All of the rt_dir_win_* helpers exist because Windows file APIs
// natively take UTF-16 paths. The rest of the runtime stores paths
// as UTF-8, so we round-trip through `MultiByteToWideChar` /
// `WideCharToMultiByte` at the boundary. The "extended-length"
// prefix `\\?\` lets us bypass the legacy MAX_PATH (260) limit.
//=============================================================================

/// @brief Wide-char separator predicate (L'\\' or L'/').
static inline int rt_dir_win_is_sep(wchar_t ch) {
    return ch == L'\\' || ch == L'/';
}

/// @brief True if `path` already starts with the `\\?\` or `\\.\` prefix.
///
/// `\\?\` (extended-length) and `\\.\` (device namespace) paths
/// must not be re-prefixed, so callers check this before adding `\\?\`.
static inline int rt_dir_win_has_extended_prefix(const wchar_t *path) {
    return path && (wcsncmp(path, L"\\\\?\\", 4) == 0 || wcsncmp(path, L"\\\\.\\", 4) == 0);
}

/// @brief Convert a UTF-8 C string to a freshly-allocated wide-char string.
///
/// Tries strict conversion first (`MB_ERR_INVALID_CHARS`), falling
/// back to lenient mode if the first call rejects malformed input.
/// Caller owns the returned buffer (`free`). NULL on alloc/conv failure.
static inline wchar_t *rt_dir_win_utf8_to_wide(const char *utf8) {
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

/// @brief Convert a wide-char string to a Zanna rt_string (UTF-8).
/// Returns the empty string on conversion / allocation failure.
static inline rt_string rt_dir_win_wide_to_string(const wchar_t *wide) {
    if (!wide)
        return rt_str_empty();

    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (needed <= 0)
        return rt_str_empty();

    char *utf8 = (char *)malloc((size_t)needed);
    if (!utf8)
        return rt_str_empty();

    if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, needed, NULL, NULL) <= 0) {
        free(utf8);
        return rt_str_empty();
    }

    rt_string s = rt_string_from_bytes(utf8, strlen(utf8));
    free(utf8);
    return s ? s : rt_str_empty();
}

/// @brief Resolve a wide path to an absolute form via `GetFullPathNameW`.
/// Caller owns the returned wide-char buffer (`free`). NULL on failure.
static inline wchar_t *rt_dir_win_absolute_path(const wchar_t *wide) {
    DWORD needed = GetFullPathNameW(wide, 0, NULL, NULL);
    if (needed == 0)
        return NULL;

    wchar_t *full = (wchar_t *)malloc(((size_t)needed + 1) * sizeof(wchar_t));
    if (!full)
        return NULL;

    DWORD got = GetFullPathNameW(wide, needed + 1, full, NULL);
    if (got == 0 || got > needed) {
        free(full);
        return NULL;
    }
    return full;
}

/// @brief Convert a UTF-8 path to wide form, made absolute and `\\?\`-prefixed.
///
/// The `\\?\` prefix lifts the legacy 260-character `MAX_PATH` cap
/// and disables Win32 path normalization, giving us reliable
/// access to deeply nested or odd paths. UNC paths get the
/// `\\?\UNC\server\share\…` form. Already-prefixed paths are
/// returned unchanged.
/// @return Heap-allocated wide string (caller `free`s) or NULL on failure.
static inline wchar_t *rt_dir_win_prepare_path(const char *utf8) {
    wchar_t *wide = rt_dir_win_utf8_to_wide(utf8);
    if (!wide)
        return NULL;
    if (rt_dir_win_has_extended_prefix(wide))
        return wide;

    wchar_t *full = rt_dir_win_absolute_path(wide);
    free(wide);
    if (!full)
        return NULL;

    size_t full_len = wcslen(full);
    wchar_t *prefixed = NULL;
    if (wcsncmp(full, L"\\\\", 2) == 0) {
        static const wchar_t kUncPrefix[] = L"\\\\?\\UNC\\";
        size_t prefix_len = wcslen(kUncPrefix);
        prefixed = (wchar_t *)malloc((prefix_len + full_len - 1) * sizeof(wchar_t));
        if (prefixed) {
            wcscpy(prefixed, kUncPrefix);
            wcscpy(prefixed + prefix_len, full + 2);
        }
    } else {
        static const wchar_t kPathPrefix[] = L"\\\\?\\";
        size_t prefix_len = wcslen(kPathPrefix);
        prefixed = (wchar_t *)malloc((prefix_len + full_len + 1) * sizeof(wchar_t));
        if (prefixed) {
            wcscpy(prefixed, kPathPrefix);
            wcscpy(prefixed + prefix_len, full);
        }
    }

    free(full);
    return prefixed;
}

/// @brief Wide-char `base + L"\\" + child` join. Caller `free`s the result.
static inline wchar_t *rt_dir_win_join(const wchar_t *base, const wchar_t *child) {
    size_t base_len = wcslen(base);
    size_t child_len = wcslen(child);
    int needs_sep = base_len > 0 && !rt_dir_win_is_sep(base[base_len - 1]);
    if (base_len > (SIZE_MAX / sizeof(wchar_t)) - child_len - (needs_sep ? 1U : 0U) - 1U)
        return NULL;
    wchar_t *joined =
        (wchar_t *)malloc((base_len + (needs_sep ? 1 : 0) + child_len + 1) * sizeof(wchar_t));
    if (!joined)
        return NULL;

    wcscpy(joined, base);
    if (needs_sep) {
        joined[base_len] = L'\\';
        joined[base_len + 1] = L'\0';
    }
    wcscpy(joined + wcslen(joined), child);
    return joined;
}

/// @brief Build a `FindFirstFileW` glob pattern (`<dir>\\*`) for `utf8`.
static inline wchar_t *rt_dir_win_make_pattern(const char *utf8) {
    wchar_t *base = rt_dir_win_prepare_path(utf8);
    if (!base)
        return NULL;
    wchar_t *pattern = rt_dir_win_join(base, L"*");
    free(base);
    return pattern;
}

/// @brief Check whether the path exists *and* is a directory (Windows path).
/// @return 1 if directory, 0 otherwise (including non-existent).
static inline int64_t rt_dir_win_exists_dir(const char *utf8) {
    wchar_t *path = rt_dir_win_prepare_path(utf8);
    if (!path)
        return 0;
    DWORD attrs = GetFileAttributesW(path);
    free(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

/// @brief Create a directory (no parents). Treats existing-dir as success.
/// @return 1 on success, 0 on failure.
static inline int rt_dir_win_create_dir(const char *utf8) {
    wchar_t *path = rt_dir_win_prepare_path(utf8);
    if (!path)
        return 0;
    BOOL ok = CreateDirectoryW(path, NULL);
    if (!ok && GetLastError() == ERROR_ALREADY_EXISTS)
        ok = rt_dir_win_exists_dir(utf8) ? TRUE : FALSE;
    free(path);
    return ok ? 1 : 0;
}

/// @brief Remove an empty directory. Returns 1 on success, 0 on failure.
static inline int rt_dir_win_remove_dir(const char *utf8) {
    wchar_t *path = rt_dir_win_prepare_path(utf8);
    if (!path)
        return 0;
    BOOL ok = RemoveDirectoryW(path);
    free(path);
    return ok ? 1 : 0;
}

/// @brief Atomically rename/move a directory. Uses `MoveFileExW(WRITE_THROUGH)`.
/// @return 1 on success, 0 on failure (e.g. cross-volume, dst exists).
static inline int rt_dir_win_move_dir(const char *src, const char *dst) {
    wchar_t *wsrc = rt_dir_win_prepare_path(src);
    wchar_t *wdst = rt_dir_win_prepare_path(dst);
    if (!wsrc || !wdst) {
        free(wsrc);
        free(wdst);
        return 0;
    }
    BOOL ok = MoveFileExW(wsrc, wdst, MOVEFILE_WRITE_THROUGH);
    free(wsrc);
    free(wdst);
    return ok ? 1 : 0;
}

/// @brief Return 1 if the UTF-8 `utf8` path resolves to the current working directory (Windows).
///
/// Used by the RemoveAll protection guard to block deletion of the cwd.
static inline int rt_dir_win_path_matches_cwd_or_ancestor(const char *utf8) {
    wchar_t *wide = rt_dir_win_utf8_to_wide(utf8);
    wchar_t *full = wide ? rt_dir_win_absolute_path(wide) : NULL;
    free(wide);
    if (!full)
        return 0;

    DWORD need = GetCurrentDirectoryW(0, NULL);
    if (need == 0) {
        free(full);
        return 0;
    }
    wchar_t *cwd = (wchar_t *)malloc(((size_t)need + 1) * sizeof(wchar_t));
    if (!cwd) {
        free(full);
        return 0;
    }
    DWORD got = GetCurrentDirectoryW(need + 1, cwd);
    int matches = 0;
    if (got > 0) {
        size_t full_len = wcslen(full);
        size_t cwd_len = wcslen(cwd);
        while (full_len > 0 && rt_dir_win_is_sep(full[full_len - 1]))
            --full_len;
        while (cwd_len > 0 && rt_dir_win_is_sep(cwd[cwd_len - 1]))
            --cwd_len;
        matches = full_len == cwd_len && _wcsnicmp(full, cwd, full_len) == 0;
        if (!matches && full_len < cwd_len && _wcsnicmp(full, cwd, full_len) == 0 &&
            rt_dir_win_is_sep(cwd[full_len]))
            matches = 1;
    }
    free(cwd);
    free(full);
    return matches;
}

/// @brief True if `name` is `.` or `..` (the synthetic directory entries).
static inline int rt_dir_win_is_dot_name(const wchar_t *name) {
    return wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0;
}

static inline int rt_dir_win_delete_error_may_be_transient(DWORD err) {
    return err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION ||
           err == ERROR_LOCK_VIOLATION || err == ERROR_DIR_NOT_EMPTY;
}

static inline int rt_dir_win_missing_error(DWORD err) {
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND;
}

/// @brief Delete a file during recursive remove.
static inline int rt_dir_win_delete_file_w(const wchar_t *path) {
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (DeleteFileW(path))
            return 1;
        DWORD err = GetLastError();
        if (rt_dir_win_missing_error(err))
            return 1;
        if (!rt_dir_win_delete_error_may_be_transient(err))
            return 0;
        Sleep(10);
    }
    if (DeleteFileW(path))
        return 1;
    return rt_dir_win_missing_error(GetLastError()) ? 1 : 0;
}

/// @brief Remove a directory during recursive remove.
static inline int rt_dir_win_remove_directory_w(const wchar_t *path) {
    for (int attempt = 0; attempt < 25; ++attempt) {
        if (RemoveDirectoryW(path))
            return 1;
        DWORD err = GetLastError();
        if (rt_dir_win_missing_error(err))
            return 1;
        if (!rt_dir_win_delete_error_may_be_transient(err))
            return 0;
        Sleep(10);
    }
    if (RemoveDirectoryW(path))
        return 1;
    return rt_dir_win_missing_error(GetLastError()) ? 1 : 0;
}
#endif
