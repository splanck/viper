//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_cipher.h
// Purpose: High-level encryption/decryption API using ChaCha20-Poly1305 AEAD in compatibility
// mode or AES-256-GCM in approved mode, with automatic nonce generation and
// PBKDF2-HMAC-SHA256 key derivation from passwords.
//
// Key invariants:
//   - Password-based format: [magic][iterations][16 bytes salt][12 bytes nonce][ciphertext][tag].
//   - Key-based format: [magic][12 bytes nonce][ciphertext][16 bytes tag].
//   - Nonces use a 4-byte CSPRNG prefix plus an 8-byte process-local counter;
//     callers reusing a raw key across processes must account for the 32-bit
//     cross-process collision margin.
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
///          Generates a 12-byte random-prefix/counter nonce.
///          Uses ChaCha20-Poly1305 AEAD in compatibility mode and AES-256-GCM in approved mode.
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
/// @note Returns NULL on authentication failure; malformed arguments may trap.
void *rt_cipher_decrypt(void *ciphertext, rt_string password);

/// @brief Decrypt password-encrypted data and report failures as a Result.
/// @details This is the production-facing companion to @ref rt_cipher_decrypt.
///          It returns `Ok(Bytes)` for valid authenticated plaintext and
///          `Err(str)` for authentication failure, malformed ciphertext, empty
///          passwords, or other runtime traps raised by the legacy decryptor.
///          Existing `Decrypt` behavior is unchanged; this wrapper simply
///          captures its failure paths into an explicit value.
/// @param ciphertext Bytes object containing encrypted data.
/// @param password Password string used for key derivation.
/// @return Opaque Viper.Result containing plaintext bytes or a diagnostic string.
void *rt_cipher_decrypt_result(void *ciphertext, rt_string password);

/// @brief Attempt password-based decryption and discard diagnostic details.
/// @details Returns `Some(Bytes)` for valid authenticated plaintext and `None`
///          for authentication failure or any trap raised by the legacy
///          decryptor. Use @ref rt_cipher_decrypt_result when callers need a
///          failure message.
/// @param ciphertext Bytes object containing encrypted data.
/// @param password Password string used for key derivation.
/// @return Opaque Viper.Option containing plaintext bytes, or None.
void *rt_cipher_try_decrypt(void *ciphertext, rt_string password);

/// @brief Decrypt password-encrypted data while authenticating caller-supplied AAD.
void *rt_cipher_decrypt_aad(void *ciphertext, rt_string password, void *aad);

/// @brief Decrypt password-encrypted data with AAD and report failures as a Result.
/// @details Authenticates the same additional data used at encryption time.
///          Returns `Ok(Bytes)` on success and `Err(str)` for authentication,
///          format, argument, or runtime-trap failures.
/// @param ciphertext Framed ciphertext produced by @ref rt_cipher_encrypt_aad.
/// @param password Password string used for key derivation.
/// @param aad Additional authenticated data; may be NULL when encryption used none.
/// @return Opaque Viper.Result containing plaintext bytes or a diagnostic string.
void *rt_cipher_decrypt_aad_result(void *ciphertext, rt_string password, void *aad);

/// @brief Attempt password-based AAD decryption and discard diagnostic details.
/// @details Returns `Some(Bytes)` only when both ciphertext and AAD authenticate.
///          Authentication failure, malformed input, and legacy traps all
///          produce `None`.
/// @param ciphertext Framed ciphertext produced by @ref rt_cipher_encrypt_aad.
/// @param password Password string used for key derivation.
/// @param aad Additional authenticated data; may be NULL when encryption used none.
/// @return Opaque Viper.Option containing plaintext bytes, or None.
void *rt_cipher_try_decrypt_aad(void *ciphertext, rt_string password, void *aad);

//=========================================================================
// Key-Based Encryption (for pre-derived keys)
//=========================================================================

/// @brief Encrypt data using a raw 256-bit key.
/// @details Generates a 12-byte random-prefix/counter nonce. Uses
///          ChaCha20-Poly1305 in compatibility mode or AES-256-GCM in approved mode.
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

/// @brief Decrypt raw-key encrypted data and report failures as a Result.
/// @details Returns `Ok(Bytes)` for valid authenticated plaintext and `Err(str)`
///          for wrong keys, tampering, malformed ciphertext, invalid key size,
///          or other traps raised by the legacy decryptor.
/// @param ciphertext Bytes object containing encrypted data.
/// @param key Bytes object containing exactly 32 bytes.
/// @return Opaque Viper.Result containing plaintext bytes or a diagnostic string.
void *rt_cipher_decrypt_with_key_result(void *ciphertext, void *key);

/// @brief Attempt raw-key decryption and discard diagnostic details.
/// @details Returns `Some(Bytes)` for valid authenticated plaintext and `None`
///          for authentication, format, key-size, or runtime-trap failures.
/// @param ciphertext Bytes object containing encrypted data.
/// @param key Bytes object containing exactly 32 bytes.
/// @return Opaque Viper.Option containing plaintext bytes, or None.
void *rt_cipher_try_decrypt_with_key(void *ciphertext, void *key);

/// @brief Decrypt key-encrypted data while authenticating caller-supplied AAD.
void *rt_cipher_decrypt_with_key_aad(void *ciphertext, void *key, void *aad);

/// @brief Decrypt raw-key encrypted data with AAD and report failures as a Result.
/// @details Authenticates the same additional data used at encryption time.
///          Returns `Ok(Bytes)` on success and `Err(str)` for authentication,
///          format, key-size, or runtime-trap failures.
/// @param ciphertext Framed ciphertext produced by @ref rt_cipher_encrypt_with_key_aad.
/// @param key Bytes object containing exactly 32 bytes.
/// @param aad Additional authenticated data; may be NULL when encryption used none.
/// @return Opaque Viper.Result containing plaintext bytes or a diagnostic string.
void *rt_cipher_decrypt_with_key_aad_result(void *ciphertext, void *key, void *aad);

/// @brief Attempt raw-key AAD decryption and discard diagnostic details.
/// @details Returns `Some(Bytes)` only when both ciphertext and AAD authenticate.
///          Any failure is represented as `None`.
/// @param ciphertext Framed ciphertext produced by @ref rt_cipher_encrypt_with_key_aad.
/// @param key Bytes object containing exactly 32 bytes.
/// @param aad Additional authenticated data; may be NULL when encryption used none.
/// @return Opaque Viper.Option containing plaintext bytes, or None.
void *rt_cipher_try_decrypt_with_key_aad(void *ciphertext, void *key, void *aad);

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
