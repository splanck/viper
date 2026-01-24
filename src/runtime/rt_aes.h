//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_aes.h
/// @brief AES encryption/decryption functions for the Viper runtime.
///
/// Provides AES-128 and AES-256 encryption in CBC mode with PKCS7 padding.
/// All functions are implemented in pure C with no external dependencies.
///
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
