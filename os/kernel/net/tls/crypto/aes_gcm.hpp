#pragma once

/**
 * @file aes_gcm.hpp
 * @brief AES-GCM (Galois/Counter Mode) primitives for TLS cipher suites.
 *
 * @details
 * TLS defines AES-GCM cipher suites (AES-128-GCM and AES-256-GCM). This module
 * provides a freestanding implementation of:
 * - AES key expansion for 128- and 256-bit keys.
 * - AES-GCM authenticated encryption and decryption with AAD.
 *
 * The implementation is designed for kernel bring-up and is not optimized for
 * performance or constant-time behavior.
 */

#include "../../../include/types.hpp"

namespace viper::tls::crypto
{

/** @brief AES block size in bytes. */
constexpr usize AES_BLOCK_SIZE = 16;

/** @name AES key sizes */
///@{
constexpr usize AES_128_KEY_SIZE = 16;
constexpr usize AES_256_KEY_SIZE = 32;
///@}

/** @name GCM parameter sizes */
///@{
constexpr usize GCM_IV_SIZE = 12;  // 96-bit IV (recommended)
constexpr usize GCM_TAG_SIZE = 16; // 128-bit authentication tag

///@}

/**
 * @brief AES expanded key schedule.
 *
 * @details
 * Stores round keys for AES encryption. The maximum size supports AES-256.
 */
struct AesKey
{
    u32 round_keys[60]; // Max for AES-256 (15 rounds * 4 words)
    int rounds;         // 10 for AES-128, 14 for AES-256
};

/**
 * @brief Expand an AES-128 key into a round key schedule.
 *
 * @param key 16-byte key.
 * @param expanded Output expanded key schedule.
 */
void aes_key_expand_128(const u8 key[AES_128_KEY_SIZE], AesKey *expanded);

/**
 * @brief Expand an AES-256 key into a round key schedule.
 *
 * @param key 32-byte key.
 * @param expanded Output expanded key schedule.
 */
void aes_key_expand_256(const u8 key[AES_256_KEY_SIZE], AesKey *expanded);

/**
 * @brief Encrypt using AES-128-GCM.
 *
 * @details
 * Produces `ciphertext || tag`, where `tag` is a 16-byte authentication tag.
 *
 * @param key 16-byte AES key.
 * @param nonce 12-byte IV/nonce.
 * @param aad Additional authenticated data (may be null).
 * @param aad_len AAD length.
 * @param plaintext Plaintext bytes.
 * @param plaintext_len Plaintext length.
 * @param ciphertext Output buffer (must have room for plaintext_len + 16).
 * @return Total output size (plaintext_len + 16).
 */
usize aes_128_gcm_encrypt(const u8 key[AES_128_KEY_SIZE],
                          const u8 nonce[GCM_IV_SIZE],
                          const void *aad,
                          usize aad_len,
                          const void *plaintext,
                          usize plaintext_len,
                          u8 *ciphertext // Must have room for plaintext_len + GCM_TAG_SIZE
);

/**
 * @brief Decrypt using AES-128-GCM.
 *
 * @details
 * Verifies the authentication tag and, if valid, writes the plaintext to
 * `plaintext`.
 *
 * @param key 16-byte AES key.
 * @param nonce 12-byte IV/nonce.
 * @param aad Additional authenticated data (may be null).
 * @param aad_len AAD length.
 * @param ciphertext Ciphertext including 16-byte tag.
 * @param ciphertext_len Total ciphertext length including tag.
 * @param plaintext Output buffer (must have room for ciphertext_len - 16).
 * @return Plaintext length on success, or -1 on authentication failure.
 */
i64 aes_128_gcm_decrypt(const u8 key[AES_128_KEY_SIZE],
                        const u8 nonce[GCM_IV_SIZE],
                        const void *aad,
                        usize aad_len,
                        const void *ciphertext,
                        usize ciphertext_len, // Includes tag
                        u8 *plaintext);

/**
 * @brief Encrypt using AES-256-GCM.
 *
 * @param key 32-byte AES key.
 * @param nonce 12-byte IV/nonce.
 * @param aad Additional authenticated data (may be null).
 * @param aad_len AAD length.
 * @param plaintext Plaintext bytes.
 * @param plaintext_len Plaintext length.
 * @param ciphertext Output buffer (must have room for plaintext_len + 16).
 * @return Total output size (plaintext_len + 16).
 */
usize aes_256_gcm_encrypt(const u8 key[AES_256_KEY_SIZE],
                          const u8 nonce[GCM_IV_SIZE],
                          const void *aad,
                          usize aad_len,
                          const void *plaintext,
                          usize plaintext_len,
                          u8 *ciphertext);

/**
 * @brief Decrypt using AES-256-GCM.
 *
 * @param key 32-byte AES key.
 * @param nonce 12-byte IV/nonce.
 * @param aad Additional authenticated data (may be null).
 * @param aad_len AAD length.
 * @param ciphertext Ciphertext including 16-byte tag.
 * @param ciphertext_len Total ciphertext length including tag.
 * @param plaintext Output buffer.
 * @return Plaintext length on success, or -1 on authentication failure.
 */
i64 aes_256_gcm_decrypt(const u8 key[AES_256_KEY_SIZE],
                        const u8 nonce[GCM_IV_SIZE],
                        const void *aad,
                        usize aad_len,
                        const void *ciphertext,
                        usize ciphertext_len,
                        u8 *plaintext);

} // namespace viper::tls::crypto
