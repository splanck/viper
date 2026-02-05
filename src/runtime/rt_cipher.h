//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_cipher.h
// Purpose: High-level encryption/decryption API using ChaCha20-Poly1305 AEAD.
// Key invariants: Automatic nonce generation, key derivation from passwords.
// Ownership/Lifetime: Returned Bytes objects are newly allocated.
// Links: docs/viperlib/crypto.md
//
// Ciphertext format (for password-based encryption):
//   [16 bytes salt] [12 bytes nonce] [ciphertext] [16 bytes tag]
//
// For key-based encryption (EncryptWithKey/DecryptWithKey):
//   [12 bytes nonce] [ciphertext] [16 bytes tag]
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Password-Based Encryption
    //=========================================================================

    /// @brief Encrypt data using a password.
    /// @details Derives a 256-bit key from the password using HKDF-SHA256 with
    ///          a random 16-byte salt. Generates a random 12-byte nonce.
    ///          Uses ChaCha20-Poly1305 AEAD for authenticated encryption.
    /// @param plaintext Bytes object containing data to encrypt.
    /// @param password Password string used for key derivation.
    /// @return Bytes object: [salt(16) | nonce(12) | ciphertext | tag(16)]
    /// @note Traps if plaintext is NULL or password is empty.
    void *rt_cipher_encrypt(void *plaintext, rt_string password);

    /// @brief Decrypt data that was encrypted with rt_cipher_encrypt.
    /// @param ciphertext Bytes object containing encrypted data.
    /// @param password Password string used for key derivation.
    /// @return Bytes object containing decrypted plaintext.
    /// @note Traps if authentication fails or ciphertext is malformed.
    void *rt_cipher_decrypt(void *ciphertext, rt_string password);

    //=========================================================================
    // Key-Based Encryption (for pre-derived keys)
    //=========================================================================

    /// @brief Encrypt data using a raw 256-bit key.
    /// @details Generates a random 12-byte nonce. Uses ChaCha20-Poly1305 AEAD.
    /// @param plaintext Bytes object containing data to encrypt.
    /// @param key Bytes object containing exactly 32 bytes (256-bit key).
    /// @return Bytes object: [nonce(12) | ciphertext | tag(16)]
    /// @note Traps if plaintext is NULL or key is not 32 bytes.
    void *rt_cipher_encrypt_with_key(void *plaintext, void *key);

    /// @brief Decrypt data that was encrypted with rt_cipher_encrypt_with_key.
    /// @param ciphertext Bytes object containing encrypted data.
    /// @param key Bytes object containing exactly 32 bytes (256-bit key).
    /// @return Bytes object containing decrypted plaintext.
    /// @note Traps if authentication fails, ciphertext is malformed, or key wrong size.
    void *rt_cipher_decrypt_with_key(void *ciphertext, void *key);

    //=========================================================================
    // Key Generation
    //=========================================================================

    /// @brief Generate a random 256-bit encryption key.
    /// @return Bytes object containing 32 random bytes suitable for encryption.
    void *rt_cipher_generate_key(void);

    /// @brief Derive a 256-bit key from a password and salt.
    /// @details Uses HKDF-SHA256 to derive a key from the password.
    /// @param password Password string.
    /// @param salt Bytes object containing salt (recommended: 16+ bytes).
    /// @return Bytes object containing 32-byte derived key.
    void *rt_cipher_derive_key(rt_string password, void *salt);

#ifdef __cplusplus
}
#endif
