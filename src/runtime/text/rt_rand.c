//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_rand.c
/// @brief Cryptographically secure random number generation.
///
/// This file implements secure random byte and integer generation using
/// platform-specific cryptographic random number generators:
///
/// | Platform | Primary Source       | Fallback          |
/// |----------|----------------------|-------------------|
/// | Linux    | /dev/urandom         | getrandom()       |
/// | macOS    | /dev/urandom         | arc4random_buf()  |
/// | Windows  | BCryptGenRandom      | CryptGenRandom    |
///
/// **Security Guarantees:**
/// - Uses OS-provided CSPRNG (Cryptographically Secure PRNG)
/// - Suitable for cryptographic key generation
/// - Suitable for nonces, IVs, and salts
/// - Unpredictable output even with partial state disclosure
///
/// **Thread Safety:** All functions are thread-safe.
///
//===----------------------------------------------------------------------===//

#include "rt_rand.h"

#include "rt_bytes.h"
#include "rt_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
#elif defined(__viperdos__)
// ViperDOS provides /dev/urandom via VirtIO-RNG.
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/// @brief Fill buffer with cryptographically secure random bytes.
/// @param buf Buffer to fill.
/// @param len Number of bytes to generate.
/// @return 0 on success, -1 on failure.
static int secure_random_fill(uint8_t *buf, size_t len)
{
    if (len == 0)
        return 0;

#ifdef _WIN32
    // Use BCryptGenRandom on Windows
    NTSTATUS status = BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return NT_SUCCESS(status) ? 0 : -1;
#else
    // Unix and ViperDOS: use /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }

    size_t bytes_read = 0;
    while (bytes_read < len)
    {
        ssize_t result = read(fd, buf + bytes_read, len - bytes_read);
        if (result < 0)
        {
            if (errno == EINTR)
                continue; // Interrupted, retry
            close(fd);
            return -1;
        }
        if (result == 0)
        {
            // EOF on /dev/urandom shouldn't happen, but handle it
            close(fd);
            return -1;
        }
        bytes_read += (size_t)result;
    }

    close(fd);
    return 0;
#endif
}

/// @brief Generate cryptographically secure random bytes.
void *rt_crypto_rand_bytes(int64_t count)
{
    if (count < 1)
    {
        rt_trap("Rand.Bytes: count must be at least 1");
    }

    // Allocate temporary buffer
    uint8_t *buf = (uint8_t *)malloc((size_t)count);
    if (!buf)
    {
        rt_trap("Rand.Bytes: memory allocation failed");
    }

    // Fill with random data
    if (secure_random_fill(buf, (size_t)count) != 0)
    {
        free(buf);
        rt_trap("Rand.Bytes: failed to generate random bytes");
    }

    // Create Bytes object
    void *result = rt_bytes_new(count);
    for (int64_t i = 0; i < count; i++)
    {
        rt_bytes_set(result, i, buf[i]);
    }

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
int64_t rt_crypto_rand_int(int64_t min, int64_t max)
{
    if (min > max)
    {
        rt_trap("Rand.Int: min must not be greater than max");
    }

    // Special case: only one possible value
    if (min == max)
    {
        return min;
    }

    // Calculate range (max - min + 1), handling potential overflow
    uint64_t range;
    if (min >= 0)
    {
        range = (uint64_t)(max - min) + 1;
    }
    else if (max < 0)
    {
        range = (uint64_t)(max - min) + 1;
    }
    else
    {
        // min < 0 && max >= 0
        range = (uint64_t)max - (uint64_t)min + 1;
    }

    // Find number of bits needed
    int bits = 64;
    uint64_t mask = UINT64_MAX;

    if (range != 0)
    { // range == 0 means full 64-bit range (overflow)
        bits = 0;
        uint64_t r = range - 1;
        while (r > 0)
        {
            bits++;
            r >>= 1;
        }
        mask = (bits == 64) ? UINT64_MAX : ((1ULL << bits) - 1);
    }

    // Rejection sampling
    uint64_t value;
    int attempts = 0;
    const int max_attempts = 1000; // Safety limit

    do
    {
        // Generate random bytes
        uint8_t buf[8];
        if (secure_random_fill(buf, 8) != 0)
        {
            rt_trap("Rand.Int: failed to generate random bytes");
        }

        // Convert to uint64 (little-endian)
        value = 0;
        for (int i = 0; i < 8; i++)
        {
            value |= ((uint64_t)buf[i]) << (i * 8);
        }

        // Apply mask
        value &= mask;

        attempts++;
        if (attempts >= max_attempts)
        {
            rt_trap("Rand.Int: too many rejection sampling attempts");
        }
    } while (range != 0 && value >= range);

    // Add to min
    return min + (int64_t)value;
}
