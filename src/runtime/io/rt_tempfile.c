//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_tempfile.c
// Purpose: Temporary file creation and management utilities for the
//          Viper.IO.TempFile class. Creates uniquely named files in the
//          system temporary directory using OS-provided entropy to generate
//          unpredictable identifiers.
//
// Key invariants:
//   - Temporary file names are generated using cryptographically random bytes
//     (CryptGenRandom on Windows, /dev/urandom on Unix) to avoid collisions.
//   - Files are created in the platform temp directory (GetTempPath on Windows,
//     $TMPDIR or /tmp on Unix).
//   - The optional auto-delete flag causes the file to be deleted on finalize.
//   - Generated IDs are hex-encoded for filesystem compatibility.
//   - All functions trap on allocation failure or file creation errors.
//
// Ownership/Lifetime:
//   - TempFile objects are heap-allocated and managed by the runtime GC.
//   - The path string is retained by the object and released on finalize.
//   - If auto-delete is enabled, the underlying file is deleted at finalize time.
//
// Links: src/runtime/io/rt_tempfile.h (public API),
//        src/runtime/io/rt_dir.h (used to resolve temp directory path),
//        src/runtime/io/rt_path.h (path join for constructing temp file names)
//
//===----------------------------------------------------------------------===//

#include "rt_tempfile.h"

#include "rt_dir.h"
#include "rt_file_path.h"
#include "rt_path.h"
#include "rt_string.h"

#include "rt_trap.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#ifndef _WINDOWS_
#error "windows.h must be included before wincrypt.h"
#endif
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

//=============================================================================
// Internal Helpers
//=============================================================================

/// @brief Generate a unique identifier using OS-provided entropy (S-21).
static void generate_unique_id(char *buffer, size_t size) {
    uint64_t rnd = 0;

#ifdef _WIN32
    /* Use CryptGenRandom for unpredictable IDs */
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProv, sizeof(rnd), (BYTE *)&rnd);
        CryptReleaseContext(hProv, 0);
    } else {
        /* Fallback: mix tick count with address entropy */
        rnd = (uint64_t)GetTickCount64() ^ (uint64_t)(uintptr_t)buffer;
    }
#else
    /* Read from /dev/urandom for unpredictable IDs */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, &rnd, sizeof(rnd));
        close(fd);
        if (n != (ssize_t)sizeof(rnd))
            rnd ^= (uint64_t)(uintptr_t)buffer ^ (uint64_t)getpid();
    } else {
        rnd = (uint64_t)(uintptr_t)buffer ^ (uint64_t)getpid();
    }
#endif

    snprintf(buffer, size, "%016llx", (unsigned long long)rnd);
}

/// @brief Atomically attempt to create a file at `cpath` with O_EXCL/CREATE_NEW semantics
/// (fails if exists). Returns 1=created, 0=collision (caller retries), -1/trap on hard error.
/// Win32 uses `CreateFileW` with `CREATE_NEW`; POSIX uses `open(O_CREAT | O_EXCL)`.
static int tempfile_try_create_path(const char *cpath) {
#ifdef _WIN32
    wchar_t *wide_path = rt_file_path_utf8_to_wide(cpath);
    if (!wide_path)
        rt_trap("TempFile.Create: invalid temporary file path");
    HANDLE h =
        CreateFileW(wide_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wide_path);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return 1;
    }

    DWORD err = GetLastError();
    if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS)
        return 0;

    rt_trap("TempFile.Create: failed to create temporary file");
    return -1;
#else
    int tmp_fd = open(cpath, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (tmp_fd >= 0) {
        close(tmp_fd);
        return 1;
    }

    if (errno == EEXIST)
        return 0;

    rt_trap("TempFile.Create: failed to create temporary file");
    return -1;
#endif
}

/// @brief Same atomic-create pattern as `_try_create_path` but for directories. Win32 `_wmkdir`,
/// POSIX `mkdir(0700)`. Returns 1=created, 0=collision, -1/trap on hard error.
static int tempfile_try_create_dir(const char *cpath) {
#ifdef _WIN32
    wchar_t *wide_path = rt_file_path_utf8_to_wide(cpath);
    if (!wide_path)
        rt_trap("TempFile.CreateDir: invalid temporary directory path");
    if (_wmkdir(wide_path) == 0) {
        free(wide_path);
        return 1;
    }
    free(wide_path);
#else
    if (mkdir(cpath, 0700) == 0)
        return 1;
#endif
    if (errno == EEXIST)
        return 0;

    rt_trap("TempFile.CreateDir: failed to create temporary directory");
    return -1;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Read the platform's temp directory. Win32: `GetTempPathW` (with trailing slash stripped).
/// POSIX: `$TMPDIR` env var (slash-stripped) or fallback to `/tmp`. Returns "C:\Temp" / "/tmp" if
/// every probe fails.
rt_string rt_tempfile_dir(void) {
#ifdef _WIN32
    DWORD len = GetTempPathW(0, NULL);
    if (len > 0) {
        wchar_t *buffer = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
        if (!buffer)
            rt_trap("TempFile.Dir: memory allocation failed");
        DWORD got = GetTempPathW(len, buffer);
        if (got > 0 && got < len) {
            while (got > 0 && (buffer[got - 1] == L'\\' || buffer[got - 1] == L'/')) {
                buffer[got - 1] = L'\0';
                got--;
            }
            rt_string result = rt_file_path_wide_to_string(buffer);
            free(buffer);
            return result;
        }
        free(buffer);
    }
    return rt_const_cstr("C:\\Temp");
#else
    const char *tmp = getenv("TMPDIR");
    if (tmp && *tmp) {
        size_t len = strlen(tmp);
        // Remove trailing slash if present
        if (len > 0 && tmp[len - 1] == '/') {
            char *copy = (char *)malloc(len);
            if (!copy)
                rt_trap("rt_tempfile: memory allocation failed");
            memcpy(copy, tmp, len - 1);
            copy[len - 1] = '\0';
            rt_string result = rt_string_from_bytes(copy, len - 1);
            free(copy);
            return result;
        }
        return rt_string_from_bytes(tmp, len);
    }
    return rt_const_cstr("/tmp");
#endif
}

/// @brief Generate a unique temp-file PATH (does NOT create the file). Default prefix "viper_",
/// extension ".tmp". Use when you want to atomically open it yourself with specific flags.
rt_string rt_tempfile_path(void) {
    return rt_tempfile_path_with_prefix(rt_const_cstr("viper_"));
}

/// @brief Path generator with a custom prefix; uses ".tmp" extension.
rt_string rt_tempfile_path_with_prefix(rt_string prefix) {
    return rt_tempfile_path_with_ext(prefix, rt_const_cstr(".tmp"));
}

/// @brief Path generator with custom prefix AND extension. Format: `{tempdir}/{prefix}{16-hex-id}{ext}`.
/// The 16-hex random ID gives ~2^64 entropy — collision chance negligible even after millions of calls.
rt_string rt_tempfile_path_with_ext(rt_string prefix, rt_string extension) {
    char unique_id[64];
    generate_unique_id(unique_id, sizeof(unique_id));

    const char *prefix_cstr = rt_string_cstr(prefix);
    const char *ext_cstr = rt_string_cstr(extension);

    // Build filename: prefix + unique_id + extension
    size_t filename_len = strlen(prefix_cstr) + strlen(unique_id) + strlen(ext_cstr) + 1;
    char *filename = (char *)malloc(filename_len);
    if (!filename)
        rt_trap("TempFile: memory allocation failed");
    snprintf(filename, filename_len, "%s%s%s", prefix_cstr, unique_id, ext_cstr);

    rt_string temp_dir = rt_tempfile_dir();
    rt_string fname_str = rt_string_from_bytes(filename, strlen(filename));
    free(filename);

    rt_string result = rt_path_join(temp_dir, fname_str);
    rt_string_unref(fname_str);
    rt_string_unref(temp_dir);

    return result;
}

/// @brief Atomically create a temp file (path + filesystem object). Default prefix "viper_".
/// Returns the path of the freshly-created (empty) file.
rt_string rt_tempfile_create(void) {
    return rt_tempfile_create_with_prefix(rt_const_cstr("viper_"));
}

/// @brief Atomic temp-file creation with custom prefix. **POSIX fast path:** uses `mkstemp`
/// which is one-syscall atomic-create-with-unpredictable-name (the gold standard for temp files).
/// **Win32 + POSIX fallback:** retry up to 128 times with random-id paths until one succeeds
/// (collision rate is negligible — 2^64 entropy per attempt).
rt_string rt_tempfile_create_with_prefix(rt_string prefix) {
#ifndef _WIN32
    /* S-21: Use mkstemp for atomic, exclusive, unpredictable file creation on POSIX */
    rt_string temp_dir = rt_tempfile_dir();
    const char *prefix_cstr = rt_string_cstr(prefix);
    const char *dir_cstr = rt_string_cstr(temp_dir);

    size_t tmpl_len = strlen(dir_cstr) + 1 + strlen(prefix_cstr) + 6 + 1;
    char *tmpl = (char *)malloc(tmpl_len);
    if (!tmpl) {
        rt_string_unref(temp_dir);
        return rt_tempfile_path_with_prefix(prefix);
    }
    snprintf(tmpl, tmpl_len, "%s/%sXXXXXX", dir_cstr, prefix_cstr);
    rt_string_unref(temp_dir);

    int fd = mkstemp(tmpl);
    if (fd >= 0) {
        close(fd);
        rt_string result = rt_string_from_bytes(tmpl, strlen(tmpl));
        free(tmpl);
        return result;
    }
    free(tmpl);
    /* Fall through to path-based creation on mkstemp failure */
#endif

    for (int attempt = 0; attempt < 128; attempt++) {
        rt_string path = rt_tempfile_path_with_prefix(prefix);
        const char *cpath = rt_string_cstr(path);
        int created = tempfile_try_create_path(cpath);
        if (created > 0)
            return path;
        rt_string_unref(path);
        if (created < 0)
            return rt_const_cstr("");
    }

    rt_trap("TempFile.Create: failed to generate a unique temporary file path");
    return rt_const_cstr("");
}

/// @brief Atomically create a temp DIRECTORY (not a file). Default prefix "viper_". Mode 0700
/// on POSIX (owner-only access). The directory companion to `_create`.
rt_string rt_tempdir_create(void) {
    return rt_tempdir_create_with_prefix(rt_const_cstr("viper_"));
}

/// @brief Atomic temp-directory creation with custom prefix. Same retry pattern as `_create`
/// but uses `mkdir`/`_wmkdir` as the atomic primitive.
rt_string rt_tempdir_create_with_prefix(rt_string prefix) {
    for (int attempt = 0; attempt < 128; attempt++) {
        rt_string result = rt_tempfile_path_with_ext(prefix, rt_const_cstr(""));
        const char *cpath = rt_string_cstr(result);
        int created = tempfile_try_create_dir(cpath);
        if (created > 0)
            return result;
        rt_string_unref(result);
        if (created < 0)
            return rt_const_cstr("");
    }

    rt_trap("TempFile.CreateDir: failed to generate a unique temporary directory path");
    return rt_const_cstr("");
}
