#pragma once

/**
 * @file aes_ctr.hpp
 * @brief AES-CTR (Counter Mode) encryption for SSH transport.
 *
 * @details
 * SSH-2 commonly uses AES in CTR mode (aes128-ctr, aes256-ctr).
 * This module provides:
 * - AES-CTR encryption/decryption (symmetric - same function for both).
 * - Incremental state management for SSH packet processing.
 *
 * CTR mode turns AES into a stream cipher by encrypting a counter and
 * XORing with plaintext. This avoids padding issues and is parallelizable.
 */

#include "../../../include/types.hpp"
#include "aes_gcm.hpp" // Reuse AesKey structure and key expansion

namespace viper::tls::crypto
{

/**
 * @brief AES-CTR stream cipher state.
 *
 * @details
 * Maintains the counter block and keystream position for incremental
 * encryption/decryption across multiple calls.
 */
struct AesCtrState
{
    AesKey key;          ///< Expanded AES key
    u8 counter[16];      ///< Current counter block (big-endian)
    u8 keystream[16];    ///< Current keystream block
    usize keystream_pos; ///< Position within keystream (0-15)
};

/**
 * @brief Initialize AES-128-CTR state.
 *
 * @param state CTR state to initialize.
 * @param key 16-byte AES key.
 * @param iv 16-byte initial counter value (typically from SSH IV).
 */
void aes_128_ctr_init(AesCtrState *state, const u8 key[AES_128_KEY_SIZE], const u8 iv[16]);

/**
 * @brief Initialize AES-256-CTR state.
 *
 * @param state CTR state to initialize.
 * @param key 32-byte AES key.
 * @param iv 16-byte initial counter value.
 */
void aes_256_ctr_init(AesCtrState *state, const u8 key[AES_256_KEY_SIZE], const u8 iv[16]);

/**
 * @brief Encrypt or decrypt data using AES-CTR.
 *
 * @details
 * CTR mode is symmetric - the same function encrypts and decrypts.
 * This function processes data incrementally, maintaining state across calls.
 *
 * @param state Initialized CTR state.
 * @param in Input data (plaintext for encryption, ciphertext for decryption).
 * @param out Output buffer (same size as input).
 * @param len Number of bytes to process.
 */
void aes_ctr_process(AesCtrState *state, const u8 *in, u8 *out, usize len);

/**
 * @brief One-shot AES-128-CTR encryption/decryption.
 *
 * @param key 16-byte AES key.
 * @param iv 16-byte initial counter value.
 * @param in Input data.
 * @param out Output buffer (same size as input).
 * @param len Number of bytes to process.
 */
void aes_128_ctr_crypt(
    const u8 key[AES_128_KEY_SIZE], const u8 iv[16], const u8 *in, u8 *out, usize len);

/**
 * @brief One-shot AES-256-CTR encryption/decryption.
 *
 * @param key 32-byte AES key.
 * @param iv 16-byte initial counter value.
 * @param in Input data.
 * @param out Output buffer (same size as input).
 * @param len Number of bytes to process.
 */
void aes_256_ctr_crypt(
    const u8 key[AES_256_KEY_SIZE], const u8 iv[16], const u8 *in, u8 *out, usize len);

/**
 * @brief Increment a 128-bit counter in big-endian format.
 *
 * @details
 * SSH CTR mode uses big-endian counter increment across the full 16 bytes.
 * This differs from GCM which only increments the low 32 bits.
 *
 * @param counter 16-byte counter to increment in place.
 */
void aes_ctr_increment(u8 counter[16]);

} // namespace viper::tls::crypto
