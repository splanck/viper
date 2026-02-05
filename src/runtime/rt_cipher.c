//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_cipher.c
// Purpose: High-level encryption/decryption using ChaCha20-Poly1305.
//
//===----------------------------------------------------------------------===//

#include "rt_cipher.h"
#include "rt_bytes.h"
#include "rt_crypto.h"

#include <stdint.h>
#include <string.h>

// External trap function (defined in rt_io.c)
extern void rt_trap(const char *msg);

//=============================================================================
// Internal Constants
//=============================================================================

#define CIPHER_SALT_SIZE 16
#define CIPHER_NONCE_SIZE 12
#define CIPHER_KEY_SIZE 32
#define CIPHER_TAG_SIZE 16

// HKDF info string for key derivation
static const char *HKDF_INFO = "viper-cipher-v1";

//=============================================================================
// Internal Bytes Access
//=============================================================================

typedef struct
{
    int64_t len;
    uint8_t *data;
} bytes_impl;

static inline uint8_t *bytes_data(void *obj)
{
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

static inline int64_t bytes_len(void *obj)
{
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// Key Derivation
//=============================================================================

/// Derive a 32-byte key from password and salt using HKDF-SHA256.
static void derive_key(const char *password, size_t password_len,
                       const uint8_t *salt, size_t salt_len,
                       uint8_t key[CIPHER_KEY_SIZE])
{
    uint8_t prk[32];

    // HKDF-Extract: PRK = HMAC-SHA256(salt, password)
    rt_hkdf_extract(salt, salt_len, (const uint8_t *)password, password_len, prk);

    // HKDF-Expand: key = HKDF-Expand(PRK, info, 32)
    rt_hkdf_expand(prk, (const uint8_t *)HKDF_INFO, strlen(HKDF_INFO), key, CIPHER_KEY_SIZE);

    // Clear PRK from stack
    memset(prk, 0, sizeof(prk));
}

//=============================================================================
// Password-Based Encryption
//=============================================================================

void *rt_cipher_encrypt(void *plaintext, rt_string password)
{
    if (!plaintext)
    {
        rt_trap("Cipher.Encrypt: plaintext is null");
        return NULL;
    }

    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_len(password);
    if (pwd_len == 0)
    {
        rt_trap("Cipher.Encrypt: password is empty");
        return NULL;
    }

    uint8_t *plain_data = bytes_data(plaintext);
    int64_t plain_len = bytes_len(plaintext);

    // Generate random salt and nonce
    uint8_t salt[CIPHER_SALT_SIZE];
    uint8_t nonce[CIPHER_NONCE_SIZE];
    rt_crypto_random_bytes(salt, CIPHER_SALT_SIZE);
    rt_crypto_random_bytes(nonce, CIPHER_NONCE_SIZE);

    // Derive key from password
    uint8_t key[CIPHER_KEY_SIZE];
    derive_key(pwd, pwd_len, salt, CIPHER_SALT_SIZE, key);

    // Output: salt + nonce + ciphertext + tag
    int64_t out_len = CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE + plain_len + CIPHER_TAG_SIZE;
    void *result = rt_bytes_new(out_len);
    uint8_t *out_data = bytes_data(result);

    // Copy salt and nonce to output
    memcpy(out_data, salt, CIPHER_SALT_SIZE);
    memcpy(out_data + CIPHER_SALT_SIZE, nonce, CIPHER_NONCE_SIZE);

    // Encrypt: ciphertext goes after salt + nonce
    rt_chacha20_poly1305_encrypt(key, nonce,
                                  NULL, 0, // No AAD
                                  plain_data, (size_t)plain_len,
                                  out_data + CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE);

    // Clear key from stack
    memset(key, 0, sizeof(key));

    return result;
}

void *rt_cipher_decrypt(void *ciphertext, rt_string password)
{
    if (!ciphertext)
    {
        rt_trap("Cipher.Decrypt: ciphertext is null");
        return NULL;
    }

    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_len(password);
    if (pwd_len == 0)
    {
        rt_trap("Cipher.Decrypt: password is empty");
        return NULL;
    }

    uint8_t *ct_data = bytes_data(ciphertext);
    int64_t ct_len = bytes_len(ciphertext);

    // Minimum: salt + nonce + tag (no plaintext)
    int64_t min_len = CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE + CIPHER_TAG_SIZE;
    if (ct_len < min_len)
    {
        rt_trap("Cipher.Decrypt: ciphertext too short");
        return NULL;
    }

    // Extract salt and nonce
    const uint8_t *salt = ct_data;
    const uint8_t *nonce = ct_data + CIPHER_SALT_SIZE;
    const uint8_t *encrypted = ct_data + CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE;
    int64_t encrypted_len = ct_len - CIPHER_SALT_SIZE - CIPHER_NONCE_SIZE;

    // Derive key from password
    uint8_t key[CIPHER_KEY_SIZE];
    derive_key(pwd, pwd_len, salt, CIPHER_SALT_SIZE, key);

    // Plaintext length = encrypted_len - tag
    int64_t plain_len = encrypted_len - CIPHER_TAG_SIZE;
    void *result = rt_bytes_new(plain_len);
    uint8_t *plain_data = bytes_data(result);

    // Decrypt and verify tag
    long decrypt_result = rt_chacha20_poly1305_decrypt(
        key, nonce,
        NULL, 0, // No AAD
        encrypted, (size_t)encrypted_len,
        plain_data);

    // Clear key from stack
    memset(key, 0, sizeof(key));

    if (decrypt_result < 0)
    {
        rt_trap("Cipher.Decrypt: authentication failed (wrong password or corrupted data)");
        return NULL;
    }

    return result;
}

//=============================================================================
// Key-Based Encryption
//=============================================================================

void *rt_cipher_encrypt_with_key(void *plaintext, void *key_bytes)
{
    if (!plaintext)
    {
        rt_trap("Cipher.EncryptWithKey: plaintext is null");
        return NULL;
    }

    if (!key_bytes || bytes_len(key_bytes) != CIPHER_KEY_SIZE)
    {
        rt_trap("Cipher.EncryptWithKey: key must be exactly 32 bytes");
        return NULL;
    }

    uint8_t *plain_data = bytes_data(plaintext);
    int64_t plain_len = bytes_len(plaintext);
    const uint8_t *key = bytes_data(key_bytes);

    // Generate random nonce
    uint8_t nonce[CIPHER_NONCE_SIZE];
    rt_crypto_random_bytes(nonce, CIPHER_NONCE_SIZE);

    // Output: nonce + ciphertext + tag
    int64_t out_len = CIPHER_NONCE_SIZE + plain_len + CIPHER_TAG_SIZE;
    void *result = rt_bytes_new(out_len);
    uint8_t *out_data = bytes_data(result);

    // Copy nonce to output
    memcpy(out_data, nonce, CIPHER_NONCE_SIZE);

    // Encrypt: ciphertext goes after nonce
    rt_chacha20_poly1305_encrypt(key, nonce,
                                  NULL, 0, // No AAD
                                  plain_data, (size_t)plain_len,
                                  out_data + CIPHER_NONCE_SIZE);

    return result;
}

void *rt_cipher_decrypt_with_key(void *ciphertext, void *key_bytes)
{
    if (!ciphertext)
    {
        rt_trap("Cipher.DecryptWithKey: ciphertext is null");
        return NULL;
    }

    if (!key_bytes || bytes_len(key_bytes) != CIPHER_KEY_SIZE)
    {
        rt_trap("Cipher.DecryptWithKey: key must be exactly 32 bytes");
        return NULL;
    }

    uint8_t *ct_data = bytes_data(ciphertext);
    int64_t ct_len = bytes_len(ciphertext);
    const uint8_t *key = bytes_data(key_bytes);

    // Minimum: nonce + tag (no plaintext)
    int64_t min_len = CIPHER_NONCE_SIZE + CIPHER_TAG_SIZE;
    if (ct_len < min_len)
    {
        rt_trap("Cipher.DecryptWithKey: ciphertext too short");
        return NULL;
    }

    // Extract nonce
    const uint8_t *nonce = ct_data;
    const uint8_t *encrypted = ct_data + CIPHER_NONCE_SIZE;
    int64_t encrypted_len = ct_len - CIPHER_NONCE_SIZE;

    // Plaintext length = encrypted_len - tag
    int64_t plain_len = encrypted_len - CIPHER_TAG_SIZE;
    void *result = rt_bytes_new(plain_len);
    uint8_t *plain_data = bytes_data(result);

    // Decrypt and verify tag
    long decrypt_result = rt_chacha20_poly1305_decrypt(
        key, nonce,
        NULL, 0, // No AAD
        encrypted, (size_t)encrypted_len,
        plain_data);

    if (decrypt_result < 0)
    {
        rt_trap("Cipher.DecryptWithKey: authentication failed (corrupted data or wrong key)");
        return NULL;
    }

    return result;
}

//=============================================================================
// Key Generation
//=============================================================================

void *rt_cipher_generate_key(void)
{
    void *key = rt_bytes_new(CIPHER_KEY_SIZE);
    rt_crypto_random_bytes(bytes_data(key), CIPHER_KEY_SIZE);
    return key;
}

void *rt_cipher_derive_key(rt_string password, void *salt_bytes)
{
    if (!salt_bytes)
    {
        rt_trap("Cipher.DeriveKey: salt is null");
        return NULL;
    }

    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_len(password);
    if (pwd_len == 0)
    {
        rt_trap("Cipher.DeriveKey: password is empty");
        return NULL;
    }

    const uint8_t *salt = bytes_data(salt_bytes);
    int64_t salt_len = bytes_len(salt_bytes);

    void *key = rt_bytes_new(CIPHER_KEY_SIZE);
    derive_key(pwd, pwd_len, salt, (size_t)salt_len, bytes_data(key));

    return key;
}
