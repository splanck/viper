//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_aes.h
// Purpose: AES-128/AES-256 CBC compatibility helpers, authenticated GCM byte
// encryption, and PBKDF2-derived AES-128-GCM string encryption, implemented in
// pure C with no external dependencies.
//
// Key invariants:
//   - Supports AES-128 (16-byte key) and AES-256 (32-byte key).
//   - CBC mode requires a 16-byte initialization vector (IV).
//   - PKCS7 padding is applied automatically; output is always a multiple of 16 bytes.
//   - Decryption validates and removes PKCS7 padding; returns NULL on invalid padding.
//   - Authenticated byte frames are [VAK1 magic(4)][nonce(12)][ciphertext][tag(16)].
//   - Authenticated string frames are [VAG1 magic(4)][iterations(4)][salt(16)]
//     [nonce(12)][ciphertext][tag(16)].
//
// Ownership/Lifetime:
//   - Returned strings/Bytes objects are newly allocated; caller must release.
//   - Key and IV buffers are borrowed; callers retain ownership.
//
// Links: src/runtime/text/rt_aes.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Encrypt data using AES-CBC.
/// @param data Bytes object containing plaintext
/// @param key Bytes object containing key (16 bytes for AES-128, 32 for AES-256)
/// @param iv Bytes object containing initialization vector (16 bytes)
/// @return Bytes object containing ciphertext (with PKCS7 padding)
void *rt_aes_encrypt(void *data, void *key, void *iv);

/// @brief Decrypt data using AES-CBC.
/// @param data Bytes object containing ciphertext
/// @param key Bytes object containing key (16 bytes for AES-128, 32 for AES-256)
/// @param iv Bytes object containing initialization vector (16 bytes)
/// @return Bytes object containing plaintext, or NULL when padding is invalid
void *rt_aes_decrypt(void *data, void *key, void *iv);

/// @brief Decrypt AES-CBC data and report failures as a Result.
/// @details Compatibility wrapper for @ref rt_aes_decrypt. Returns
///          `Ok(Bytes)` for valid plaintext and `Err(str)` for invalid padding,
///          malformed input, approved-mode rejection, invalid key/IV sizes, or
///          traps raised by the legacy decryptor.
/// @param data Bytes object containing ciphertext.
/// @param key Bytes object containing a 16-byte or 32-byte AES key.
/// @param iv Bytes object containing the 16-byte initialization vector.
/// @return Opaque Zanna.Result containing plaintext bytes or a diagnostic string.
void *rt_aes_decrypt_result(void *data, void *key, void *iv);

/// @brief Attempt AES-CBC decryption and discard diagnostic details.
/// @details Returns `Some(Bytes)` for valid plaintext and `None` for invalid
///          padding, malformed input, approved-mode rejection, invalid key/IV
///          sizes, or traps raised by the legacy decryptor.
/// @param data Bytes object containing ciphertext.
/// @param key Bytes object containing a 16-byte or 32-byte AES key.
/// @param iv Bytes object containing the 16-byte initialization vector.
/// @return Opaque Zanna.Option containing plaintext bytes, or None.
void *rt_aes_try_decrypt(void *data, void *key, void *iv);

/// @brief Encrypt data using AES-128-GCM or AES-256-GCM with optional authenticated data.
/// @param key Bytes object containing key (16 bytes for AES-128, 32 for AES-256).
/// @return Bytes object: [magic(4)][nonce(12)][ciphertext][tag(16)].
void *rt_aes_encrypt_auth(void *data, void *key, void *aad);

/// @brief Decrypt data produced by rt_aes_encrypt_auth.
void *rt_aes_decrypt_auth(void *data, void *key, void *aad);

/// @brief Decrypt AES-GCM authenticated data and report failures as a Result.
/// @details Compatibility wrapper for @ref rt_aes_decrypt_auth. Returns
///          `Ok(Bytes)` when the ciphertext and optional AAD authenticate, and
///          `Err(str)` for tag mismatch, malformed input, invalid key sizes, or
///          traps raised by the legacy decryptor.
/// @param data Framed ciphertext produced by @ref rt_aes_encrypt_auth.
/// @param key Bytes object containing a 16-byte or 32-byte AES key.
/// @param aad Additional authenticated data; may be NULL.
/// @return Opaque Zanna.Result containing plaintext bytes or a diagnostic string.
void *rt_aes_decrypt_auth_result(void *data, void *key, void *aad);

/// @brief Attempt AES-GCM authenticated decryption and discard diagnostic details.
/// @details Returns `Some(Bytes)` only when ciphertext and AAD authenticate.
///          Any failure is represented as `None`.
/// @param data Framed ciphertext produced by @ref rt_aes_encrypt_auth.
/// @param key Bytes object containing a 16-byte or 32-byte AES key.
/// @param aad Additional authenticated data; may be NULL.
/// @return Opaque Zanna.Option containing plaintext bytes, or None.
void *rt_aes_try_decrypt_auth(void *data, void *key, void *aad);

/// @brief Encrypt string using PBKDF2-derived AES-128-GCM.
/// @param data String to encrypt
/// @param password Password string used to derive a 128-bit key via PBKDF2-HMAC-SHA256
/// @return Bytes object containing:
///         [magic(4) | iterations(4) | salt(16) | nonce(12) | ciphertext | tag(16)]
void *rt_aes_encrypt_str(rt_string data, rt_string password);

/// @brief Decrypt to string using the authenticated string format.
/// @details Accepts the current AES-128-GCM format and the legacy
///          [16-byte IV | AES-256-CBC ciphertext] format for compatibility.
///          Legacy password derivation considers at most the first 256 bytes;
///          a legacy IV beginning with `VAG1` is ambiguous with current framing.
/// @param data Bytes object containing encrypted string payload
/// @param password Password string used for key derivation
/// @return Decrypted string
rt_string rt_aes_decrypt_str(void *data, rt_string password);

/// @brief Decrypt an AES encrypted string and report failures as a Result.
/// @details Compatibility wrapper for @ref rt_aes_decrypt_str. Returns
///          `Ok(str)` for valid plaintext and `Err(str)` for authentication,
///          format, password, or runtime-trap failures.
/// @param data Bytes object containing encrypted string payload.
/// @param password Password string used for key derivation.
/// @return Opaque Zanna.Result containing a plaintext string or diagnostic string.
void *rt_aes_decrypt_str_result(void *data, rt_string password);

/// @brief Attempt AES encrypted string decryption and discard diagnostic details.
/// @details Returns `Some(str)` for valid plaintext and `None` when decryption
///          fails or the legacy decryptor traps.
/// @param data Bytes object containing encrypted string payload.
/// @param password Password string used for key derivation.
/// @return Opaque Zanna.Option containing plaintext string, or None.
void *rt_aes_try_decrypt_str(void *data, rt_string password);

#ifdef __cplusplus
}
#endif
