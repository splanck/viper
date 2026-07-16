//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_entropy_platform_posix.c
// Purpose: POSIX OS entropy adapter for runtime cryptography.
//
// Key invariants:
//   - macOS uses arc4random_buf(), which is kernel-seeded and does not fail.
//   - Linux tries getrandom() before falling back to /dev/urandom.
//   - Generic POSIX platforms use /dev/urandom.
//
// Ownership/Lifetime:
//   - File descriptors opened for /dev/urandom are closed before return.
//
// Links: src/runtime/network/rt_entropy_platform.h
//
//===----------------------------------------------------------------------===//

#include "rt_entropy_platform.h"
#include "rt_platform.h"

#include <errno.h>
#include <fcntl.h>
#if RT_PLATFORM_LINUX
#include <sys/random.h>
#endif
#include <unistd.h>

#define RT_ENTROPY_GETRANDOM_CHUNK ((size_t)256u * 1024u)

#if RT_PLATFORM_MACOS
/// @brief Darwin libc CSPRNG entry point.
/// @details Declared locally because older SDK feature sets do not always
///          expose the prototype through the included C headers.
extern void arc4random_buf(void *buf, size_t nbytes);
#endif

/// @brief Fill a buffer from POSIX-style operating-system entropy sources.
/// @details macOS uses arc4random_buf(); Linux first uses getrandom() and falls
///          back to /dev/urandom only when the syscall is unavailable; generic
///          POSIX platforms read /dev/urandom directly.
/// @param buf Destination buffer. May be NULL only for zero-length requests.
/// @param len Number of bytes to produce.
/// @return 0 on success, -1 on invalid arguments or source failure.
int rt_entropy_platform_random_bytes(uint8_t *buf, size_t len) {
    if (!buf && len > 0)
        return -1;
    if (len == 0)
        return 0;

#if RT_PLATFORM_MACOS
    arc4random_buf(buf, len);
    return 0;
#elif RT_PLATFORM_LINUX
    size_t got = 0;
    while (got < len) {
        size_t request = len - got;
        if (request > RT_ENTROPY_GETRANDOM_CHUNK)
            request = RT_ENTROPY_GETRANDOM_CHUNK;
        ssize_t n = getrandom(buf + got, request, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ENOSYS || errno == EPERM || errno == EACCES)
                break;
            return -1;
        }
        if (n == 0)
            return -1;
        got += (size_t)n;
    }
    if (got == len)
        return 0;
#endif

#ifdef O_CLOEXEC
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
#else
    int fd = open("/dev/urandom", O_RDONLY);
#endif
    if (fd < 0)
        return -1;

    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (n == 0) {
            close(fd);
            return -1;
        }
        off += (size_t)n;
    }
    close(fd);
    return 0;
}

/// @brief Fill a 64-bit scalar from the POSIX entropy adapter.
/// @details Keeps temporary-file and atomic-save helpers from opening
///          /dev/urandom directly. The byte-level adapter owns Linux
///          getrandom(), macOS arc4random_buf(), and /dev/urandom fallback
///          policy.
/// @param out Receives the random scalar on success.
/// @return 0 on success, -1 on invalid arguments or entropy failure.
int rt_entropy_platform_random_u64(uint64_t *out) {
    if (!out)
        return -1;
    return rt_entropy_platform_random_bytes((uint8_t *)out, sizeof(*out));
}
