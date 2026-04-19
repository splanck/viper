//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_cipher.c
// Purpose: Implements high-level symmetric encryption/decryption for the
//          Viper.Text.Cipher class using ChaCha20-Poly1305 AEAD. Provides
//          Encrypt and Decrypt operations that handle nonce generation,
//          authentication tag verification, and key derivation.
//
// Key invariants:
//   - Nonces are 12 bytes (96-bit), randomly generated per call.
//   - Authenticated encryption: any tampering with ciphertext or nonce is
//     detected and Decrypt returns NULL (not a trap).
//   - Keys must be exactly 32 bytes (256-bit); wrong size traps.
//   - Output format: [12-byte nonce][16-byte tag][ciphertext bytes].
//   - Decryption verifies the Poly1305 MAC before returning plaintext.
//   - All functions are thread-safe; no global mutable cipher state.
//
// Ownership/Lifetime:
//   - Returned ciphertext/plaintext rt_bytes buffers are fresh allocations
//     owned by the caller.
//   - Input key, plaintext, and ciphertext buffers are borrowed read-only.
//
// Links: src/runtime/text/rt_cipher.h (public API),
//        src/runtime/text/rt_aes.h (AES-CBC cipher, alternative algorithm),
//        src/runtime/text/rt_rand.h (nonce generation)
//
//===----------------------------------------------------------------------===//

#include "rt_cipher.h"
#include "rt_bytes.h"
#include "rt_crypto.h"
#include "rt_keyderive_internal.h"
#include "rt_object.h"

#include <stdint.h>
#include <string.h>

// External trap function (defined in rt_io.c)
#include "rt_trap.h"

//=============================================================================
// Internal Constants
//=============================================================================

#define CIPHER_SALT_SIZE 16
#define CIPHER_NONCE_SIZE 12
#define CIPHER_KEY_SIZE 32
#define CIPHER_TAG_SIZE 16
#define CIPHER_PBKDF2_ITERATIONS 300000

// HKDF info string for key derivation
static const char *HKDF_INFO = "viper-cipher-v1";

//=============================================================================
// Internal Bytes Access
//=============================================================================

typedef struct {
    int64_t len;
    uint8_t *data;
} bytes_impl;

/// @brief Read the raw byte buffer pointer from a Bytes object handle (NULL-safe).
static inline uint8_t *bytes_data(void *obj) {
    if (!obj)
        return NULL;
    return ((bytes_impl *)obj)->data;
}

/// @brief Read the byte length from a Bytes object handle (NULL → 0).
static inline int64_t bytes_len(void *obj) {
    if (!obj)
        return 0;
    return ((bytes_impl *)obj)->len;
}

//=============================================================================
// Key Derivation
//=============================================================================

/// @brief Optimization-resistant zero-fill for sensitive key/nonce buffers.
/// @details Writes through a `volatile` pointer so the compiler can't
///          elide the stores via dead-store elimination. Used after
///          every key-derivation and AEAD operation so transient
///          secrets don't linger in stack frames where a later memory
///          dump (core file, swap, leaked mapping) could expose them.
static void cipher_secure_zero(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len-- > 0)
        *p++ = 0;
}

/// @brief Derive a 32-byte key using the legacy HKDF-SHA256 scheme.
/// @details HKDF (RFC 5869) is fast and unsuitable for password-based key
///          derivation in isolation — it has no cost parameter, so an
///          attacker brute-forcing the password gets one hash evaluation
///          per guess. This path is **legacy-only**: kept for decrypting
///          ciphertext produced by older versions of the runtime that used
///          HKDF before PBKDF2 was adopted. New encryption goes through
///          `derive_key_pbkdf2` instead.
///          Two-step HKDF: Extract collapses (salt, password) into a 256-bit
///          PRK; Expand stretches the PRK into the 32-byte cipher key,
///          domain-separated by the `HKDF_INFO` string. PRK is zeroed before
///          return so it can't linger in heap memory.
static void derive_key_legacy(const char *password,
                              size_t password_len,
                              const uint8_t *salt,
                              size_t salt_len,
                              uint8_t key[CIPHER_KEY_SIZE]) {
    uint8_t prk[32];

    // HKDF-Extract: PRK = HMAC-SHA256(salt, password)
    rt_hkdf_extract(salt, salt_len, (const uint8_t *)password, password_len, prk);

    // HKDF-Expand: key = HKDF-Expand(PRK, info, 32)
    if (rt_hkdf_expand(prk, (const uint8_t *)HKDF_INFO, strlen(HKDF_INFO), key, CIPHER_KEY_SIZE) !=
        0) {
        rt_trap("Cipher: legacy HKDF key derivation failed");
    }

    cipher_secure_zero(prk, sizeof(prk));
}

/// @brief Derive a 32-byte key using PBKDF2-HMAC-SHA256 (modern default).
/// @details Iteration count is fixed at `CIPHER_PBKDF2_ITERATIONS` (300,000
///          at the time of writing), chosen to make per-guess brute force
///          expensive on modern hardware while keeping the per-call latency
///          tolerable for an interactive password-based encrypt operation
///          (~50–100 ms on a typical desktop CPU). This is the path all new
///          encryption uses; `derive_key_legacy` is only invoked when
///          decrypting older ciphertexts that carry a legacy version tag.
static void derive_key_pbkdf2(const char *password,
                              size_t password_len,
                              const uint8_t *salt,
                              size_t salt_len,
                              uint8_t key[CIPHER_KEY_SIZE]) {
    rt_keyderive_pbkdf2_sha256_raw((const uint8_t *)password,
                                   password_len,
                                   salt,
                                   salt_len,
                                   CIPHER_PBKDF2_ITERATIONS,
                                   key,
                                   CIPHER_KEY_SIZE);
}

//=============================================================================
// Password-Based Encryption
//=============================================================================

/// @brief Encrypt data using a password with ChaCha20-Poly1305 AEAD.
/// @details Derives a 256-bit key from the password using PBKDF2-HMAC-SHA256
///          (`CIPHER_PBKDF2_ITERATIONS`, currently 300,000 iterations) with a
///          fresh random 16-byte salt. Generates
///          a random 12-byte nonce. Encrypts and authenticates the plaintext
///          using ChaCha20-Poly1305 (RFC 8439), producing a 16-byte Poly1305
///          authentication tag that detects any tampering.
///
///          Wire format: [salt(16) | nonce(12) | ciphertext | tag(16)]
///
///          The salt is unique per call, so encrypting the same plaintext with
///          the same password always produces different ciphertext.
/// @param plaintext Bytes object containing data to encrypt (traps if NULL).
/// @param password  Password string for key derivation (traps if empty).
/// @return Bytes object containing the encrypted payload.
void *rt_cipher_encrypt(void *plaintext, rt_string password) {
    if (!plaintext) {
        rt_trap("Cipher.Encrypt: plaintext is null");
        return NULL;
    }

    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);
    if (pwd_len == 0) {
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
    derive_key_pbkdf2(pwd, pwd_len, salt, CIPHER_SALT_SIZE, key);

    // Output: salt + nonce + ciphertext + tag
    int64_t out_len = CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE + plain_len + CIPHER_TAG_SIZE;
    void *result = rt_bytes_new(out_len);
    uint8_t *out_data = bytes_data(result);

    // Copy salt and nonce to output
    memcpy(out_data, salt, CIPHER_SALT_SIZE);
    memcpy(out_data + CIPHER_SALT_SIZE, nonce, CIPHER_NONCE_SIZE);

    // Encrypt: ciphertext goes after salt + nonce
    size_t encrypted_len =
        rt_chacha20_poly1305_encrypt(key,
                                     nonce,
                                     NULL,
                                     0, // No AAD
                                     plain_data,
                                     (size_t)plain_len,
                                     out_data + CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE);
    if (encrypted_len == 0 && plain_len != 0) {
        cipher_secure_zero(key, sizeof(key));
        rt_trap("Cipher.Encrypt: encryption failed");
        return NULL;
    }

    cipher_secure_zero(key, sizeof(key));

    return result;
}

/// @brief Decrypt data that was encrypted with rt_cipher_encrypt.
/// @details Extracts the salt and nonce from the ciphertext header, re-derives
///          the key using PBKDF2-HMAC-SHA256, then decrypts and verifies the
///          Poly1305 authentication tag. If verification fails (wrong password
///          or corrupted data), falls back to the legacy HKDF-based key
///          derivation scheme for backward compatibility. Returns NULL if both
///          derivation schemes fail authentication.
///
///          Expected wire format: [salt(16) | nonce(12) | ciphertext | tag(16)]
/// @param ciphertext Bytes object containing the encrypted payload.
/// @param password   Password string for key derivation.
/// @return Bytes object containing the decrypted plaintext, or NULL on auth failure.
void *rt_cipher_decrypt(void *ciphertext, rt_string password) {
    if (!ciphertext) {
        rt_trap("Cipher.Decrypt: ciphertext is null");
        return NULL;
    }

    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);
    if (pwd_len == 0) {
        rt_trap("Cipher.Decrypt: password is empty");
        return NULL;
    }

    uint8_t *ct_data = bytes_data(ciphertext);
    int64_t ct_len = bytes_len(ciphertext);

    // Minimum: salt + nonce + tag (no plaintext)
    int64_t min_len = CIPHER_SALT_SIZE + CIPHER_NONCE_SIZE + CIPHER_TAG_SIZE;
    if (ct_len < min_len) {
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
    derive_key_pbkdf2(pwd, pwd_len, salt, CIPHER_SALT_SIZE, key);

    // Plaintext length = encrypted_len - tag
    int64_t plain_len = encrypted_len - CIPHER_TAG_SIZE;
    void *result = rt_bytes_new(plain_len);
    uint8_t *plain_data = bytes_data(result);

    // Decrypt and verify tag
    long decrypt_result = rt_chacha20_poly1305_decrypt(key,
                                                       nonce,
                                                       NULL,
                                                       0, // No AAD
                                                       encrypted,
                                                       (size_t)encrypted_len,
                                                       plain_data);

    if (decrypt_result < 0) {
        derive_key_legacy(pwd, pwd_len, salt, CIPHER_SALT_SIZE, key);
        decrypt_result = rt_chacha20_poly1305_decrypt(
            key, nonce, NULL, 0, encrypted, (size_t)encrypted_len, plain_data);
        if (decrypt_result < 0) {
            cipher_secure_zero(key, sizeof(key));
            if (result && rt_obj_release_check0(result))
                rt_obj_free(result);
            return NULL;
        }
    }
    cipher_secure_zero(key, sizeof(key));

    return result;
}

//=============================================================================
// Key-Based Encryption
//=============================================================================

/// @brief Encrypt data using a raw 256-bit key with ChaCha20-Poly1305 AEAD.
/// @details Like rt_cipher_encrypt but skips key derivation — the caller
///          provides a pre-derived 32-byte key (e.g., from rt_cipher_generate_key
///          or rt_cipher_derive_key). A random 12-byte nonce is generated per call.
///
///          Wire format: [nonce(12) | ciphertext | tag(16)]
///
///          Note: no salt is stored because no password derivation occurs.
/// @param plaintext  Bytes object containing data to encrypt (traps if NULL).
/// @param key_bytes  Bytes object containing exactly 32 bytes (traps if wrong size).
/// @return Bytes object containing the encrypted payload.
void *rt_cipher_encrypt_with_key(void *plaintext, void *key_bytes) {
    if (!plaintext) {
        rt_trap("Cipher.EncryptWithKey: plaintext is null");
        return NULL;
    }

    if (!key_bytes || bytes_len(key_bytes) != CIPHER_KEY_SIZE) {
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
    size_t encrypted_len = rt_chacha20_poly1305_encrypt(key,
                                                        nonce,
                                                        NULL,
                                                        0, // No AAD
                                                        plain_data,
                                                        (size_t)plain_len,
                                                        out_data + CIPHER_NONCE_SIZE);
    if (encrypted_len == 0 && plain_len != 0) {
        rt_trap("Cipher.EncryptWithKey: encryption failed");
        return NULL;
    }

    return result;
}

/// @brief Decrypt data that was encrypted with rt_cipher_encrypt_with_key.
/// @details Extracts the 12-byte nonce from the ciphertext header, then
///          decrypts and verifies the Poly1305 authentication tag using the
///          provided 32-byte key. Traps if authentication fails.
///
///          Expected wire format: [nonce(12) | ciphertext | tag(16)]
/// @param ciphertext Bytes object containing the encrypted payload.
/// @param key_bytes  Bytes object containing exactly 32 bytes.
/// @return Bytes object containing the decrypted plaintext.
void *rt_cipher_decrypt_with_key(void *ciphertext, void *key_bytes) {
    if (!ciphertext) {
        rt_trap("Cipher.DecryptWithKey: ciphertext is null");
        return NULL;
    }

    if (!key_bytes || bytes_len(key_bytes) != CIPHER_KEY_SIZE) {
        rt_trap("Cipher.DecryptWithKey: key must be exactly 32 bytes");
        return NULL;
    }

    uint8_t *ct_data = bytes_data(ciphertext);
    int64_t ct_len = bytes_len(ciphertext);
    const uint8_t *key = bytes_data(key_bytes);

    // Minimum: nonce + tag (no plaintext)
    int64_t min_len = CIPHER_NONCE_SIZE + CIPHER_TAG_SIZE;
    if (ct_len < min_len) {
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
    long decrypt_result = rt_chacha20_poly1305_decrypt(key,
                                                       nonce,
                                                       NULL,
                                                       0, // No AAD
                                                       encrypted,
                                                       (size_t)encrypted_len,
                                                       plain_data);

    if (decrypt_result < 0) {
        rt_trap("Cipher.DecryptWithKey: authentication failed (corrupted data or wrong key)");
        return NULL;
    }

    return result;
}

//=============================================================================
// Key Generation
//=============================================================================

/// @brief Generate a random 256-bit (32-byte) encryption key from the OS CSPRNG.
/// @details The key is suitable for use with rt_cipher_encrypt_with_key and
///          rt_cipher_decrypt_with_key. Uses the same CSPRNG as rt_crypto_rand_bytes
///          (arc4random on macOS, BCryptGenRandom on Windows, /dev/urandom on Linux).
/// @return Bytes object containing 32 cryptographically random bytes.
void *rt_cipher_generate_key(void) {
    void *key = rt_bytes_new(CIPHER_KEY_SIZE);
    rt_crypto_random_bytes(bytes_data(key), CIPHER_KEY_SIZE);
    return key;
}

/// @brief Derive a deterministic 256-bit key from a password and salt.
/// @details Uses PBKDF2-HMAC-SHA256 with `CIPHER_PBKDF2_ITERATIONS`
///          iterations (currently 300,000). The same password and salt always
///          produce the same key, which is the point — this enables key
///          agreement between parties who share a password. The salt should be
///          at least 16 bytes of random data to prevent rainbow-table attacks.
///          For one-time encryption, prefer rt_cipher_encrypt (which generates
///          salt automatically).
/// @param password   Password string (traps if empty).
/// @param salt_bytes Bytes object containing the salt (traps if NULL or empty).
/// @return Bytes object containing the 32-byte derived key.
void *rt_cipher_derive_key(rt_string password, void *salt_bytes) {
    if (!salt_bytes) {
        rt_trap("Cipher.DeriveKey: salt is null");
        return NULL;
    }

    const char *pwd = rt_string_cstr(password);
    size_t pwd_len = (size_t)rt_str_len(password);
    if (pwd_len == 0) {
        rt_trap("Cipher.DeriveKey: password is empty");
        return NULL;
    }

    const uint8_t *salt = bytes_data(salt_bytes);
    int64_t salt_len = bytes_len(salt_bytes);
    if (salt_len <= 0) {
        rt_trap("Cipher.DeriveKey: salt must not be empty");
        return NULL;
    }

    void *key = rt_bytes_new(CIPHER_KEY_SIZE);
    derive_key_pbkdf2(pwd, pwd_len, salt, (size_t)salt_len, bytes_data(key));

    return key;
}
