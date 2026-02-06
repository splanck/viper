//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/random.c
// Purpose: Cryptographic random number generation for ViperDOS libc.
// Key invariants: Uses VirtIO-RNG via SYS_GETRANDOM syscall.
// Ownership/Lifetime: Library function.
// Links: user/libc/include/sys/random.h
//
//===----------------------------------------------------------------------===//

/**
 * @file random.c
 * @brief Cryptographic random number generation for ViperDOS libc.
 *
 * @details
 * Provides the getrandom() function that obtains cryptographically
 * secure random bytes from the kernel's VirtIO-RNG device.
 */

#include "../include/sys/random.h"
#include "syscall_internal.h"

/* Syscall numbers */
#define SYS_GETRANDOM 0xE4

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;

    if (!buf || buflen == 0)
        return 0;

    long result = __syscall2(SYS_GETRANDOM, (long)buf, (long)buflen);
    return (ssize_t)result;
}
