//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_rand.c
// Purpose: Implements Zanna.Crypto.Rand.Bytes and Int. Compatibility mode uses
//          platform CSPRNGs (`getrandom` with `/dev/urandom` fallback on Linux,
//          `arc4random_buf` on macOS, BCryptGenRandom on Windows); approved mode
//          uses the locked module HMAC-DRBG.
//
// Key invariants:
//   - Compatibility output comes directly from the OS CSPRNG; approved output
//     comes from an HMAC-DRBG seeded/reseeded by OS entropy. Neither uses rand().
//   - RandomInt(min, max) is inclusive on both ends; bias is eliminated via
//     rejection sampling.
//   - Failure to read from the CSPRNG traps with a descriptive error.
//   - Direct platform calls and approved DRBG access are thread-safe. The cached
//     non-Apple POSIX `/dev/urandom` descriptor has a known first-use data race.
//
// Ownership/Lifetime:
//   - All returned rt_string and rt_bytes values are fresh allocations.
//
// Links: src/runtime/text/rt_rand.h (public API),
//        src/runtime/text/rt_guid.h (UUID generation uses the same CSPRNG)
//
//===----------------------------------------------------------------------===//

#include "rt_rand.h"

#include "rt_bytes.h"
#include "rt_crypto_module.h"
#include "rt_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
extern void arc4random_buf(void *buf, size_t nbytes);
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Use BCrypt on Windows Vista+ (available in all modern Windows)
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
// NT_SUCCESS is defined in ntdef.h but we provide it here to avoid dependency
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#else
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

/// @brief Return a cached `/dev/urandom` descriptor for Unix fallback reads.
/// @details The descriptor is opened once with close-on-exec where supported and
///          retained for process lifetime. Creation is mutex-protected, but the
///          unlocked fast-path read currently races with the first write under
///          concurrent initialization.
/// @return Non-negative file descriptor on success; -1 when opening failed.
static int rand_urandom_fd(void) {
    static int fd = -1; // accessed only via __atomic_* (acquire/release)
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    // Lock-free fast path: an acquire load synchronizes with the release store
    // below, so a concurrent first-use read/write is data-race-free (VDOC-175).
    int cached = __atomic_load_n(&fd, __ATOMIC_ACQUIRE);
    if (cached >= 0)
        return cached;
    pthread_mutex_lock(&lock);
    cached = __atomic_load_n(&fd, __ATOMIC_RELAXED);
    if (cached < 0) {
#ifdef O_CLOEXEC
        cached = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
#else
        cached = open("/dev/urandom", O_RDONLY);
#endif
        __atomic_store_n(&fd, cached, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&lock);
    return cached;
}
#endif

static void rand_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0)
        *p++ = 0;
}

/// @brief Fill a buffer with cryptographically secure random bytes.
/// @details Uses the platform's preferred CSPRNG with documented fallbacks:
///          - **Windows**: `BCryptGenRandom` with `BCRYPT_USE_SYSTEM_PREFERRED_RNG`.
///            Large requests are chunked to the API's 32-bit limit; no fallback.
///          - **macOS / *BSD**: `arc4random_buf` (ChaCha20-based, kernel-seeded,
///            never fails — no return value to check).
///          - **Linux**: `getrandom(2)` syscall first (preferred — never blocks
///            after the kernel pool is initialized at boot, unlike older
///            `/dev/urandom` semantics on first read). Loops on partial reads
///            (the syscall can return fewer bytes than requested when
///            interrupted) and on `EINTR`. Falls back to `/dev/urandom` if
///            `getrandom` returns `ENOSYS` (kernel < 3.17 — extremely rare
///            but possible on long-running enterprise distros).
///          - **Other Unix**: `/dev/urandom` directly. Same loop
///            structure — read in a loop until the requested length is
///            satisfied, retrying on `EINTR`, treating `read() == 0` as a
///            failure (the device should never EOF).
///
///          The `EINTR` retry loops matter because both `getrandom` and
///          `read` on `/dev/urandom` can be interrupted by signals on a
///          process that uses signal handlers, and a single short read
///          would leave the buffer with predictable trailing bytes.
/// @param buf Buffer to fill (must be writable for `len` bytes).
/// @param len Number of bytes to generate (0 is a no-op success).
/// @return 0 on success; -1 on failure (CSPRNG unavailable or read error).
static int secure_random_fill(uint8_t *buf, size_t len) {
    if (len == 0)
        return 0;
    if (!buf)
        return -1;
    if (rt_crypto_module_is_approved_mode()) {
        rt_crypto_module_random_bytes(buf, len);
        return 0;
    }

#ifdef _WIN32
    // Use BCryptGenRandom on Windows
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > ULONG_MAX)
            chunk = ULONG_MAX;
        NTSTATUS status =
            BCryptGenRandom(NULL, buf + off, (ULONG)chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(status)) {
            rand_secure_zero(buf, len);
            return -1;
        }
        off += chunk;
    }
    return 0;
#else
#if defined(__APPLE__)
    arc4random_buf(buf, len);
    return 0;
#elif defined(__linux__)
    size_t bytes_read = 0;
    while (bytes_read < len) {
        ssize_t result = getrandom(buf + bytes_read, len - bytes_read, 0);
        if (result < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ENOSYS)
                break;
            return -1;
        }
        if (result == 0)
            return -1;
        bytes_read += (size_t)result;
    }
    if (bytes_read == len)
        return 0;
#endif
    // Unix: use /dev/urandom.
    int fd = rand_urandom_fd();
    int close_after_read = 0;
    if (fd < 0)
        return -1;

    size_t urandom_bytes_read = 0;
    while (urandom_bytes_read < len) {
        ssize_t result = read(fd, buf + urandom_bytes_read, len - urandom_bytes_read);
        if (result < 0) {
            if (errno == EINTR)
                continue; // Interrupted, retry
            if (close_after_read)
                close(fd);
            return -1;
        }
        if (result == 0) {
            // EOF on /dev/urandom shouldn't happen, but handle it
            if (close_after_read)
                close(fd);
            return -1;
        }
        urandom_bytes_read += (size_t)result;
    }

    if (close_after_read)
        close(fd);
    return 0;
#endif
}

/// @brief Generate cryptographically secure random bytes.
void *rt_crypto_rand_bytes(int64_t count) {
    if (count < 0) {
        rt_trap("Rand.Bytes: count must not be negative");
        return rt_bytes_new(0);
    }
    if (count == 0) {
        return rt_bytes_new(0);
    }
    if ((uint64_t)count > (uint64_t)SIZE_MAX) {
        rt_trap("Rand.Bytes: count is too large");
        return rt_bytes_new(0);
    }

    // Allocate temporary buffer
    uint8_t *buf = (uint8_t *)malloc((size_t)count);
    if (!buf) {
        rt_trap("Rand.Bytes: memory allocation failed");
        return rt_bytes_new(0);
    }

    // Fill with random data
    if (secure_random_fill(buf, (size_t)count) != 0) {
        free(buf);
        rt_trap("Rand.Bytes: failed to generate random bytes");
        return rt_bytes_new(0);
    }

    // Create Bytes object directly from the filled buffer (bulk copy)
    void *result = rt_bytes_from_raw(buf, (size_t)count);

    rand_secure_zero(buf, (size_t)count);
    free(buf);
    return result;
}

/// @brief Generate a cryptographically secure random integer in range [min, max].
///
/// Uses rejection sampling to ensure uniform distribution without bias.
/// The algorithm:
/// 1. Calculate the range size
/// 2. Find the smallest power of 2 >= range
/// 3. Generate random values in [0, 2^k) and reject if >= range
/// 4. Add min to get final result
int64_t rt_crypto_rand_int(int64_t min, int64_t max) {
    if (min > max) {
        rt_trap("Rand.Int: min must not be greater than max");
        return min;
    }

    // Special case: only one possible value
    if (min == max) {
        return min;
    }

    // Calculate range (max - min + 1) in unsigned space to avoid signed overflow.
    uint64_t range = (uint64_t)max - (uint64_t)min + 1;

    // Find number of bits needed
    int bits = 64;
    uint64_t mask = UINT64_MAX;

    if (range != 0) { // range == 0 means full 64-bit range (overflow)
        bits = 0;
        uint64_t r = range - 1;
        while (r > 0) {
            bits++;
            r >>= 1;
        }
        mask = (bits == 64) ? UINT64_MAX : ((1ULL << bits) - 1);
    }

    // Rejection sampling
    uint64_t value;
    do {
        // Generate random bytes
        uint8_t buf[8];
        if (secure_random_fill(buf, 8) != 0) {
            rt_trap("Rand.Int: failed to generate random bytes");
            rt_abort("Rand.Int: failed to generate random bytes");
        }

        // Convert to uint64 (little-endian)
        value = 0;
        for (int i = 0; i < 8; i++) {
            value |= ((uint64_t)buf[i]) << (i * 8);
        }

        // Apply mask
        value &= mask;

    } while (range != 0 && value >= range);

    uint64_t result_bits = (uint64_t)min + value;
    int64_t result;
    memcpy(&result, &result_bits, sizeof(result));
    return result;
}
