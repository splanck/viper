#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

/**
 * @file sys/random.h
 * @brief Random number generation interface for ViperDOS.
 *
 * @details
 * Provides the getrandom() function for obtaining cryptographically
 * secure random bytes from the VirtIO-RNG hardware device.
 */

/** @brief Use /dev/urandom-equivalent source (default). */
#define GRND_NONBLOCK 0x0001
/** @brief Use /dev/random-equivalent source. */
#define GRND_RANDOM 0x0002

/**
 * @brief Obtain random bytes from the kernel.
 *
 * @param buf Buffer to fill with random bytes.
 * @param buflen Number of bytes to fill.
 * @param flags Flags (GRND_NONBLOCK, GRND_RANDOM; currently ignored).
 * @return Number of bytes written on success, -1 on error.
 */
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_RANDOM_H */
