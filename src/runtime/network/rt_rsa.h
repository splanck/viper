//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_rsa.h
// Purpose: Native RSA key management, parsing, and signature verification
//          (PKCS#1 v1.5 and RSASSA-PSS) used by the TLS 1.3 and X.509 layers.
//          Supports PKCS#1 RSAPublicKey, PKCS#1 RSAPrivateKey, and PKCS#8
//          PrivateKeyInfo DER formats.
// Key invariants:
//   - rt_rsa_key_t owns its heap buffers; always call rt_rsa_key_free to release.
//   - Signature verification is public-data-only; not constant-time on sig/key.
//   - Maximum modulus size: RT_RSA_MAX_MOD_BYTES (512 bytes = 4096-bit RSA).
// Ownership/Lifetime:
//   - Parse output parameters must first be initialized with rt_rsa_key_init.
//     On success the previous initialized contents are released and *out holds
//     heap-allocated buffers; caller must call rt_rsa_key_free(out) when done.
// Links: src/runtime/network/rt_rsa.c (implementation),
//        src/runtime/network/rt_tls_verify.c (caller)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Hash function selector for RSA-PSS and PKCS#1 v1.5 signing/verification.
/// @details Maps to the SHA-2 family digest length and the corresponding
///          OID inside the DigestInfo structure used by PKCS#1 v1.5.
typedef enum {
    RT_RSA_HASH_SHA256 = 0, ///< SHA-256 (32-byte digest, OID 2.16.840.1.101.3.4.2.1).
    RT_RSA_HASH_SHA384 = 1, ///< SHA-384 (48-byte digest, OID 2.16.840.1.101.3.4.2.2).
    RT_RSA_HASH_SHA512 = 2, ///< SHA-512 (64-byte digest, OID 2.16.840.1.101.3.4.2.3).
} rt_rsa_hash_t;

/// @brief Parsed RSA key material with separately-owned big-endian components.
/// @details The key fields are big-endian byte arrays sized exactly to their
///          significant byte length (no leading zero padding). All three
///          buffers are heap-allocated by the parse routines and freed by
///          @ref rt_rsa_key_free. Only the public components are populated
///          when parsing an RSAPublicKey; PKCS#1 / PKCS#8 private-key parses
///          additionally populate @c private_exponent.
typedef struct {
    uint8_t *modulus;           ///< Public modulus n (big-endian, no leading zeros).
    size_t modulus_len;         ///< Length of @c modulus in bytes.
    uint8_t *public_exponent;   ///< Public exponent e (big-endian, typically 65537).
    size_t public_exponent_len; ///< Length of @c public_exponent in bytes.
    uint8_t *private_exponent;  ///< Private exponent d (big-endian); NULL for public-only keys.
    size_t private_exponent_len;///< Length of @c private_exponent in bytes (0 when @c d is absent).
} rt_rsa_key_t;

/// @brief Zero-initialise an rt_rsa_key_t so it is safe to pass to rt_rsa_key_free without prior parse.
void rt_rsa_key_init(rt_rsa_key_t *key);

/// @brief Release all heap buffers owned by key and zero the struct.
void rt_rsa_key_free(rt_rsa_key_t *key);

/// @brief Parse a PKCS#1 RSAPublicKey DER blob (SEQUENCE { modulus INTEGER, exponent INTEGER }).
/// @param out Initialized key object to replace on success.
/// @return 1 on success; 0 on malformed input.
int rt_rsa_parse_public_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);

/// @brief Parse a two-prime PKCS#1 RSAPrivateKey DER blob and retain n/e/d.
/// @param out Initialized key object to replace on success.
/// @return 1 on success; 0 on malformed input.
int rt_rsa_parse_private_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);

/// @brief Parse a PKCS#8 PrivateKeyInfo DER blob containing an rsaEncryption key.
/// @param out Initialized key object to replace on success.
/// @return 1 on success; 0 on malformed input or unrecognised algorithm.
int rt_rsa_parse_private_key_pkcs8(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);

/// @brief Test whether two rt_rsa_key_t values have the same public components (modulus + exponent).
/// @return 1 if equal, 0 otherwise.
int rt_rsa_public_equals(const rt_rsa_key_t *lhs, const rt_rsa_key_t *rhs);

/// @brief Produce an RSASSA-PSS signature over a pre-computed digest.
/// @return 1 on success; 0 on error or invalid key.
int rt_rsa_pss_sign(const rt_rsa_key_t *key,
                    rt_rsa_hash_t hash_id,
                    const uint8_t *digest,
                    size_t digest_len,
                    uint8_t *sig_out,
                    size_t *sig_len_out);

/// @brief Verify an RSASSA-PSS signature against a pre-computed digest.
/// @return 1 if the signature is valid, 0 otherwise.
int rt_rsa_pss_verify(const rt_rsa_key_t *key,
                      rt_rsa_hash_t hash_id,
                      const uint8_t *digest,
                      size_t digest_len,
                      const uint8_t *sig,
                      size_t sig_len);

/// @brief Verify a PKCS#1 v1.5 signature against a pre-computed digest.
/// @return 1 if the signature is valid, 0 otherwise.
int rt_rsa_pkcs1_v15_verify(const rt_rsa_key_t *key,
                            rt_rsa_hash_t hash_id,
                            const uint8_t *digest,
                            size_t digest_len,
                            const uint8_t *sig,
                            size_t sig_len);

#ifdef __cplusplus
}
#endif
