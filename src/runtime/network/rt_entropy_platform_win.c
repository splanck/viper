//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_entropy_platform_win.c
// Purpose: Windows OS entropy adapter for runtime cryptography.
//
// Key invariants:
//   - Entropy comes from the system-preferred BCryptGenRandom provider.
//   - Large requests are chunked to the ULONG size accepted by BCrypt.
//
// Ownership/Lifetime:
//   - The adapter opens no persistent handles and owns no global state.
//
// Links: src/runtime/network/rt_entropy_platform.h
//
//===----------------------------------------------------------------------===//

#include "rt_entropy_platform.h"

#include <bcrypt.h>
#include <stdint.h>

#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

/// @brief Fill a buffer from the Windows system CSPRNG.
/// @details Calls BCryptGenRandom() with BCRYPT_USE_SYSTEM_PREFERRED_RNG,
///          chunking requests larger than ULONG_MAX. The function reports
///          source failure instead of producing fallback bytes.
/// @param buf Destination buffer. May be NULL only for zero-length requests.
/// @param len Number of bytes to produce.
/// @return 0 on success, -1 on invalid arguments or BCrypt failure.
int rt_entropy_platform_random_bytes(uint8_t *buf, size_t len) {
    if (!buf && len > 0)
        return -1;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > UINT32_MAX)
            chunk = UINT32_MAX;
        NTSTATUS status =
            BCryptGenRandom(NULL, buf + off, (ULONG)chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(status))
            return -1;
        off += chunk;
    }
    return 0;
}
