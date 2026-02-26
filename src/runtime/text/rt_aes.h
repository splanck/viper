//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_aes.h
// Purpose: AES-128 and AES-256 encryption in CBC mode with PKCS7 padding, implemented in pure C
// with no external dependencies.
//
// Key invariants:
//   - Supports AES-128 (16-byte key) and AES-256 (32-byte key).
//   - CBC mode requires a 16-byte initialization vector (IV).
//   - PKCS7 padding is applied automatically; output is always a multiple of 16 bytes.
//   - Decryption validates and removes PKCS7 padding; returns NULL on invalid padding.
//
// Ownership/Lifetime:
//   - Returned strings/Bytes objects are newly allocated; caller must release.
//   - Key and IV buffers are borrowed; callers retain ownership.
//
// Links: src/runtime/text/rt_aes.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#ifndef RT_AES_H
#define RT_AES_H

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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
    /// @return Bytes object containing plaintext (PKCS7 padding removed)
    void *rt_aes_decrypt(void *data, void *key, void *iv);

    /// @brief Encrypt string using AES-256-CBC with key derivation.
    /// @param data String to encrypt
    /// @param password Password string (will be derived to 32-byte key using SHA-256)
    /// @return Bytes object containing: 16-byte IV + ciphertext
    void *rt_aes_encrypt_str(rt_string data, rt_string password);

    /// @brief Decrypt to string using AES-256-CBC with key derivation.
    /// @param data Bytes object containing: 16-byte IV + ciphertext
    /// @param password Password string (will be derived to 32-byte key using SHA-256)
    /// @return Decrypted string
    rt_string rt_aes_decrypt_str(void *data, rt_string password);

#ifdef __cplusplus
}
#endif

#endif // RT_AES_H
