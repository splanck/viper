//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_crypto_module.h
// Purpose: Zero-dependency cryptographic module boundary, approved-mode state,
//          self-tests, policy checks, and DRBG-backed random generation.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rt_crypto_module_mode {
    RT_CRYPTO_MODULE_MODE_COMPAT = 0,
    RT_CRYPTO_MODULE_MODE_APPROVED = 1,
} rt_crypto_module_mode_t;

typedef enum rt_crypto_module_state {
    RT_CRYPTO_MODULE_STATE_UNINITIALIZED = 0,
    RT_CRYPTO_MODULE_STATE_SELF_TESTING = 1,
    RT_CRYPTO_MODULE_STATE_READY = 2,
    RT_CRYPTO_MODULE_STATE_ERROR = 3,
} rt_crypto_module_state_t;

typedef enum rt_crypto_module_service {
    RT_CRYPTO_SERVICE_AES_GCM = 0,
    RT_CRYPTO_SERVICE_SHA2 = 1,
    RT_CRYPTO_SERVICE_HMAC_SHA2 = 2,
    RT_CRYPTO_SERVICE_HKDF_SHA2 = 3,
    RT_CRYPTO_SERVICE_PBKDF2_SHA256 = 4,
    RT_CRYPTO_SERVICE_DRBG = 5,
    RT_CRYPTO_SERVICE_ECDSA_P256 = 6,
    RT_CRYPTO_SERVICE_RSA_PSS = 7,
    RT_CRYPTO_SERVICE_TLS13_APPROVED_PROFILE = 8,
    RT_CRYPTO_SERVICE_CHACHA20_POLY1305 = 9,
    RT_CRYPTO_SERVICE_X25519 = 10,
    RT_CRYPTO_SERVICE_SCRYPT = 11,
    RT_CRYPTO_SERVICE_MD5 = 12,
    RT_CRYPTO_SERVICE_SHA1 = 13,
    RT_CRYPTO_SERVICE_CRC32 = 14,
    RT_CRYPTO_SERVICE_SIPHASH = 15,
} rt_crypto_module_service_t;

int rt_crypto_module_init(void);
int rt_crypto_module_self_test(void);
int rt_crypto_module_set_mode(rt_crypto_module_mode_t mode);
rt_crypto_module_mode_t rt_crypto_module_get_mode(void);
rt_crypto_module_state_t rt_crypto_module_get_state(void);
int rt_crypto_module_is_approved_mode(void);
int rt_crypto_module_service_allowed(rt_crypto_module_service_t service);
const char *rt_crypto_module_status_cstr(void);
void rt_crypto_module_random_bytes(uint8_t *buf, size_t len);

int8_t rt_crypto_module_enable_approved_mode(void);
void rt_crypto_module_disable_approved_mode(void);
int8_t rt_crypto_module_is_approved_mode_viper(void);
rt_string rt_crypto_module_status_text(void);

#ifdef __cplusplus
}
#endif
