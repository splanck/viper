//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_file_stdio.h
// Purpose: Small shared helper for opening UTF-8 file paths as non-inheritable
//          stdio FILE* handles.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_file_path.h"
#include "rt_platform.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RT_PLATFORM_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <wchar.h>
#ifndef O_RDONLY
#define O_RDONLY _O_RDONLY
#endif
#ifndef O_WRONLY
#define O_WRONLY _O_WRONLY
#endif
#ifndef O_RDWR
#define O_RDWR _O_RDWR
#endif
#ifndef O_CREAT
#define O_CREAT _O_CREAT
#endif
#ifndef O_TRUNC
#define O_TRUNC _O_TRUNC
#endif
#ifndef O_APPEND
#define O_APPEND _O_APPEND
#endif
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static inline int rt_file_stdio_mode_flags(const char *mode, int *out_flags, int *out_pmode) {
    if (!mode || !out_flags || !out_pmode)
        return 0;

    int flags = 0;
    int pmode = 0666;
    if (strcmp(mode, "rb") == 0) {
        flags = O_RDONLY;
    } else if (strcmp(mode, "wb") == 0) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "ab") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (strcmp(mode, "r+b") == 0 || strcmp(mode, "rb+") == 0) {
        flags = O_RDWR;
    } else if (strcmp(mode, "w+b") == 0 || strcmp(mode, "wb+") == 0) {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    } else if (strcmp(mode, "a+b") == 0 || strcmp(mode, "ab+") == 0) {
        flags = O_RDWR | O_CREAT | O_APPEND;
    } else {
        return 0;
    }

#if defined(O_BINARY)
    flags |= O_BINARY;
#elif defined(_O_BINARY)
    flags |= _O_BINARY;
#endif
#if RT_PLATFORM_WINDOWS
    flags |= _O_NOINHERIT;
    pmode = _S_IREAD | _S_IWRITE;
#endif

    *out_flags = flags;
    *out_pmode = pmode;
    return 1;
}

static inline FILE *rt_file_stdio_open_utf8(const char *path, const char *mode) {
    if (!path || !mode) {
        errno = EINVAL;
        return NULL;
    }

    int flags = 0;
    int pmode = 0;
    if (!rt_file_stdio_mode_flags(mode, &flags, &pmode)) {
        errno = EINVAL;
        return NULL;
    }

#if RT_PLATFORM_WINDOWS
    wchar_t *wide_path = rt_file_path_utf8_to_wide(path);
    if (!wide_path) {
        errno = EINVAL;
        return NULL;
    }
    int fd = _wopen(wide_path, flags, pmode);
    free(wide_path);
    if (fd < 0)
        return NULL;
    FILE *fp = _fdopen(fd, mode);
    if (!fp)
        _close(fd);
    return fp;
#else
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    int fd = open(path, flags, (mode_t)pmode);
    if (fd < 0)
        return NULL;
#if defined(FD_CLOEXEC) && !defined(O_CLOEXEC)
    int fd_flags = fcntl(fd, F_GETFD);
    if (fd_flags >= 0)
        (void)fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
#endif
    FILE *fp = fdopen(fd, mode);
    if (!fp)
        close(fd);
    return fp;
#endif
}

/// @brief Seek a stdio stream with a signed 64-bit byte offset.
/// @details Centralizes the platform-specific large-file API used by runtime
///          modules that still parse through `FILE *`. Windows uses `_fseeki64`;
///          POSIX platforms use `fseeko`. Callers pass the usual `SEEK_SET`,
///          `SEEK_CUR`, or `SEEK_END` origin and receive the underlying API's
///          zero-on-success result.
/// @param fp     Open stream to reposition.
/// @param offset Signed byte offset interpreted relative to @p origin.
/// @param origin Standard stdio seek origin.
/// @return 0 on success; non-zero on invalid input or seek failure.
static inline int rt_file_stdio_seek64(FILE *fp, int64_t offset, int origin) {
    if (!fp)
        return -1;
#if RT_PLATFORM_WINDOWS
    return _fseeki64(fp, offset, origin);
#else
    return fseeko(fp, (off_t)offset, origin);
#endif
}

/// @brief Report the current stdio stream offset as a signed 64-bit value.
/// @details Pairs with rt_file_stdio_seek64 so callers do not need to carry
///          their own `_ftelli64`/`ftello` preprocessor branches. Returns -1
///          when @p fp is NULL or when the underlying API reports failure.
/// @param fp Open stream to query.
/// @return Current byte offset from the beginning of the stream, or -1 on failure.
static inline int64_t rt_file_stdio_tell64(FILE *fp) {
    if (!fp)
        return -1;
#if RT_PLATFORM_WINDOWS
    return (int64_t)_ftelli64(fp);
#else
    return (int64_t)ftello(fp);
#endif
}
