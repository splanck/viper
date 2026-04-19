//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_rsa.h
// Purpose: Native RSA parsing and signature helpers used by TLS/X.509.
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

void rt_rsa_key_init(rt_rsa_key_t *key);
void rt_rsa_key_free(rt_rsa_key_t *key);

int rt_rsa_parse_public_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);
int rt_rsa_parse_private_key_pkcs1(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);
int rt_rsa_parse_private_key_pkcs8(const uint8_t *der, size_t der_len, rt_rsa_key_t *out);
int rt_rsa_public_equals(const rt_rsa_key_t *lhs, const rt_rsa_key_t *rhs);

int rt_rsa_pss_sign(const rt_rsa_key_t *key,
                    rt_rsa_hash_t hash_id,
                    const uint8_t *digest,
                    size_t digest_len,
                    uint8_t *sig_out,
                    size_t *sig_len_out);

int rt_rsa_pss_verify(const rt_rsa_key_t *key,
                      rt_rsa_hash_t hash_id,
                      const uint8_t *digest,
                      size_t digest_len,
                      const uint8_t *sig,
                      size_t sig_len);

int rt_rsa_pkcs1_v15_verify(const rt_rsa_key_t *key,
                            rt_rsa_hash_t hash_id,
                            const uint8_t *digest,
                            size_t digest_len,
                            const uint8_t *sig,
                            size_t sig_len);

#ifdef __cplusplus
}
#endif
