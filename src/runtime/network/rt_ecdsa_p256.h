//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_ecdsa_p256.h
// Purpose: Native ECDSA P-256 (secp256r1) signature verification in pure C.
//          No external dependencies — replaces dlopen(libcrypto) on Linux.
// Key invariants:
//   - Verification-only: no signing or key generation.
//   - All inputs are big-endian byte arrays; no heap allocation.
//   - Constant-time field arithmetic is NOT guaranteed (verify-only, public data).
// Ownership/Lifetime:
//   - Pure functions operating on caller-owned buffers; no ownership transfer.
// Links: src/runtime/network/rt_ecdsa_p256.c (implementation),
//        src/runtime/network/rt_tls.c (integration)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Verify an ECDSA P-256 (secp256r1) signature over a SHA-256 digest.
    ///
    /// Implements the ECDSA verification algorithm per FIPS 186-4 / SEC1 §4.1.4:
    ///   1. Check 1 ≤ r, s < n
    ///   2. w = s⁻¹ mod n
    ///   3. u₁ = (digest · w) mod n, u₂ = (r · w) mod n
    ///   4. R = u₁·G + u₂·Q
    ///   5. Valid if R.x ≡ r (mod n)
    ///
    /// @param pubkey_x  32-byte big-endian X coordinate of the public key point Q.
    /// @param pubkey_y  32-byte big-endian Y coordinate of the public key point Q.
    /// @param digest    32-byte SHA-256 digest of the signed message.
    /// @param sig_r     32-byte big-endian R component of the ECDSA signature.
    /// @param sig_s     32-byte big-endian S component of the ECDSA signature.
    /// @return 1 if the signature is valid, 0 if invalid or on any error.
    int ecdsa_p256_verify(const uint8_t pubkey_x[32],
                          const uint8_t pubkey_y[32],
                          const uint8_t digest[32],
                          const uint8_t sig_r[32],
                          const uint8_t sig_s[32]);

#ifdef __cplusplus
}
#endif
