//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_cipher.h
// Purpose: High-level encryption/decryption API using ChaCha20-Poly1305 AEAD with automatic nonce
// generation and PBKDF2-HMAC-SHA256 key derivation from passwords.
//
// Key invariants:
//   - Password-based format: [magic][iterations][16 bytes salt][12 bytes nonce][ciphertext][tag].
//   - Key-based format: [magic][12 bytes nonce][ciphertext][16 bytes tag].
//   - Nonces are generated automatically from a secure random source.
//   - Decryption returns NULL for invalid/corrupt ciphertext (authentication failure).
//
// Ownership/Lifetime:
//   - Returned Bytes objects are newly allocated; caller must release.
//   - Password and key strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_cipher.c (implementation), src/runtime/network/rt_crypto.h,
// src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// Password-Based Encryption
//=========================================================================

/// @brief Encrypt data using a password.
/// @details Derives a 256-bit key from the password using PBKDF2-HMAC-SHA256
///          with a random 16-byte salt and a fixed strong work factor.
///          Generates a random 12-byte nonce.
///          Uses ChaCha20-Poly1305 AEAD for authenticated encryption.
/// @param plaintext Bytes object containing data to encrypt.
/// @param password Password string used for key derivation.
/// @return Bytes object: [magic(4) | iterations(4) | salt(16) | nonce(12) | ciphertext | tag(16)]
/// @note Traps if plaintext is NULL or password is empty.
void *rt_cipher_encrypt(void *plaintext, rt_string password);

/// @brief Encrypt data using a password and caller-supplied authenticated data.
void *rt_cipher_encrypt_aad(void *plaintext, rt_string password, void *aad);

/// @brief Decrypt data that was encrypted with rt_cipher_encrypt.
/// @param ciphertext Bytes object containing encrypted data.
/// @param password Password string used for key derivation.
/// @return Bytes object containing decrypted plaintext.
/// @note Traps if authentication fails or ciphertext is malformed.
void *rt_cipher_decrypt(void *ciphertext, rt_string password);

/// @brief Decrypt password-encrypted data while authenticating caller-supplied AAD.
void *rt_cipher_decrypt_aad(void *ciphertext, rt_string password, void *aad);

//=========================================================================
// Key-Based Encryption (for pre-derived keys)
//=========================================================================

/// @brief Encrypt data using a raw 256-bit key.
/// @details Generates a random 12-byte nonce. Uses ChaCha20-Poly1305 AEAD.
/// @param plaintext Bytes object containing data to encrypt.
/// @param key Bytes object containing exactly 32 bytes (256-bit key).
/// @return Bytes object: [magic(4) | nonce(12) | ciphertext | tag(16)]
/// @note Traps if plaintext is NULL or key is not 32 bytes.
void *rt_cipher_encrypt_with_key(void *plaintext, void *key);

/// @brief Encrypt data using a raw 256-bit key and caller-supplied authenticated data.
void *rt_cipher_encrypt_with_key_aad(void *plaintext, void *key, void *aad);

/// @brief Decrypt data that was encrypted with rt_cipher_encrypt_with_key.
/// @param ciphertext Bytes object containing encrypted data.
/// @param key Bytes object containing exactly 32 bytes (256-bit key).
/// @return Bytes object containing decrypted plaintext, or NULL on authentication failure.
/// @note Traps only if ciphertext is malformed or the key has the wrong size.
void *rt_cipher_decrypt_with_key(void *ciphertext, void *key);

/// @brief Decrypt key-encrypted data while authenticating caller-supplied AAD.
void *rt_cipher_decrypt_with_key_aad(void *ciphertext, void *key, void *aad);

//=========================================================================
// Key Generation
//=========================================================================

/// @brief Generate a random 256-bit encryption key.
/// @return Bytes object containing 32 random bytes suitable for encryption.
void *rt_cipher_generate_key(void);

/// @brief Derive a 256-bit key from a password and salt.
/// @details Uses PBKDF2-HMAC-SHA256 to derive a key from the password.
/// @param password Password string.
/// @param salt Bytes object containing salt (recommended: 16+ bytes).
/// @return Bytes object containing 32-byte derived key.
void *rt_cipher_derive_key(rt_string password, void *salt);

#ifdef __cplusplus
}
#endif
