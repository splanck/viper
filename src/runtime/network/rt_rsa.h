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
//   - After a successful parse call, *out holds heap-allocated buffers;
//     caller must call rt_rsa_key_free(out) when done.
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

typedef enum {
    RT_RSA_HASH_SHA256 = 0,
    RT_RSA_HASH_SHA384 = 1,
    RT_RSA_HASH_SHA512 = 2,
} rt_rsa_hash_t;

typedef struct {
    uint8_t *modulus;
    size_t modulus_len;
    uint8_t *public_exponent;
    size_t public_exponent_len;
    uint8_t *private_exponent;
    size_t private_exponent_len;
} rt_rsa_key_t;

/// @brief Zero-initialise an rt_rsa_key_t so it is safe to pass to rt_rsa_key_free without prior parse.
void rt_rsa_key_init(rt_rsa_key_t *key);

/// @brief Release all heap buffers owned by key and zero the struct.
void rt_rsa_key_free(rt_rsa_key_t *key);

/// @brief Parse a PKCS#1 RSAPublicKey DER blob (SEQUENCE { modulus INTEGER, exponent INTEGER }).
/// @return 1 on success; 0 on malformed input.
int rt_rsa_parse_public_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);

/// @brief Parse a PKCS#1 RSAPrivateKey DER blob (version, modulus, public and private exponents).
/// @return 1 on success; 0 on malformed input.
int rt_rsa_parse_private_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);

/// @brief Parse a PKCS#8 PrivateKeyInfo DER blob containing an rsaEncryption key.
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
