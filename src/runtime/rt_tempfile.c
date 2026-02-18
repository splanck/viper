//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_tempfile.c
// Purpose: Temporary file utilities implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_tempfile.h"

#include "rt_dir.h"
#include "rt_path.h"
#include "rt_string.h"

extern void rt_trap(const char *msg);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
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
static void generate_unique_id(char *buffer, size_t size)
{
    uint64_t rnd = 0;

#ifdef _WIN32
    /* Use CryptGenRandom for unpredictable IDs */
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hProv, sizeof(rnd), (BYTE *)&rnd);
        CryptReleaseContext(hProv, 0);
    }
    else
    {
        /* Fallback: mix tick count with address entropy */
        rnd = (uint64_t)GetTickCount64() ^ (uint64_t)(uintptr_t)buffer;
    }
#else
    /* Read from /dev/urandom for unpredictable IDs */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0)
    {
        ssize_t n = read(fd, &rnd, sizeof(rnd));
        close(fd);
        if (n != (ssize_t)sizeof(rnd))
            rnd ^= (uint64_t)(uintptr_t)buffer ^ (uint64_t)getpid();
    }
    else
    {
        rnd = (uint64_t)(uintptr_t)buffer ^ (uint64_t)getpid();
    }
#endif

    snprintf(buffer, size, "%016llx", (unsigned long long)rnd);
}

//=============================================================================
// Public API
//=============================================================================

rt_string rt_tempfile_dir(void)
{
#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, buffer);
    if (len > 0 && len < MAX_PATH)
    {
        // Remove trailing backslash if present
        if (len > 0 && (buffer[len - 1] == '\\' || buffer[len - 1] == '/'))
        {
            buffer[len - 1] = '\0';
            len--;
        }
        return rt_string_from_bytes(buffer, len);
    }
    return rt_const_cstr("C:\\Temp");
#else
    const char *tmp = getenv("TMPDIR");
    if (tmp && *tmp)
    {
        size_t len = strlen(tmp);
        // Remove trailing slash if present
        if (len > 0 && tmp[len - 1] == '/')
        {
            char *copy = (char *)malloc(len);
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

rt_string rt_tempfile_path(void)
{
    return rt_tempfile_path_with_prefix(rt_const_cstr("viper_"));
}

rt_string rt_tempfile_path_with_prefix(rt_string prefix)
{
    return rt_tempfile_path_with_ext(prefix, rt_const_cstr(".tmp"));
}

rt_string rt_tempfile_path_with_ext(rt_string prefix, rt_string extension)
{
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

rt_string rt_tempfile_create(void)
{
    return rt_tempfile_create_with_prefix(rt_const_cstr("viper_"));
}

rt_string rt_tempfile_create_with_prefix(rt_string prefix)
{
#ifndef _WIN32
    /* S-21: Use mkstemp for atomic, exclusive, unpredictable file creation on POSIX */
    rt_string temp_dir = rt_tempfile_dir();
    const char *prefix_cstr = rt_string_cstr(prefix);
    const char *dir_cstr = rt_string_cstr(temp_dir);

    size_t tmpl_len = strlen(dir_cstr) + 1 + strlen(prefix_cstr) + 6 + 1;
    char *tmpl = (char *)malloc(tmpl_len);
    if (!tmpl)
    {
        rt_string_unref(temp_dir);
        return rt_tempfile_path_with_prefix(prefix);
    }
    snprintf(tmpl, tmpl_len, "%s/%sXXXXXX", dir_cstr, prefix_cstr);
    rt_string_unref(temp_dir);

    int fd = mkstemp(tmpl);
    if (fd >= 0)
    {
        close(fd);
        rt_string result = rt_string_from_bytes(tmpl, strlen(tmpl));
        free(tmpl);
        return result;
    }
    free(tmpl);
    /* Fall through to path-based creation on mkstemp failure */
#endif

    rt_string path = rt_tempfile_path_with_prefix(prefix);

    // Create empty file
    FILE *f = fopen(rt_string_cstr(path), "wb");
    if (f)
    {
        fclose(f);
    }

    return path;
}

rt_string rt_tempdir_create(void)
{
    return rt_tempdir_create_with_prefix(rt_const_cstr("viper_"));
}

rt_string rt_tempdir_create_with_prefix(rt_string prefix)
{
    char unique_id[64];
    generate_unique_id(unique_id, sizeof(unique_id));

    const char *prefix_cstr = rt_string_cstr(prefix);

    // Build dirname: prefix + unique_id
    size_t dirname_len = strlen(prefix_cstr) + strlen(unique_id) + 1;
    char *dirname = (char *)malloc(dirname_len);
    if (!dirname)
        rt_trap("TempFile: memory allocation failed");
    snprintf(dirname, dirname_len, "%s%s", prefix_cstr, unique_id);

    rt_string temp_dir = rt_tempfile_dir();
    rt_string dname_str = rt_string_from_bytes(dirname, strlen(dirname));
    free(dirname);

    rt_string result = rt_path_join(temp_dir, dname_str);
    rt_string_unref(dname_str);
    rt_string_unref(temp_dir);

    // Create directory
    rt_dir_make(result);

    return result;
}
