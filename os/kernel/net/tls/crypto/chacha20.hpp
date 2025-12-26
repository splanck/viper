#pragma once

/**
 * @file chacha20.hpp
 * @brief ChaCha20, Poly1305, and the ChaCha20-Poly1305 AEAD construction.
 *
 * @details
 * TLS 1.3 commonly uses the ChaCha20-Poly1305 AEAD cipher (RFC 8439) for record
 * protection. This module provides:
 * - ChaCha20 stream cipher primitives (init, block, XOR keystream).
 * - Poly1305 one-time authenticator primitives.
 * - AEAD encrypt/decrypt helpers implementing ChaCha20-Poly1305 with AAD.
 *
 * The functions are written for a freestanding kernel environment and do not
 * depend on libc.
 */

#include "../../../include/types.hpp"

namespace viper::crypto
{

/** @name ChaCha20 parameter sizes */
///@{
constexpr usize CHACHA20_KEY_SIZE = 32;
constexpr usize CHACHA20_NONCE_SIZE = 12;
constexpr usize CHACHA20_BLOCK_SIZE = 64;
///@}

/** @name Poly1305 parameter sizes */
///@{
constexpr usize POLY1305_KEY_SIZE = 32;
constexpr usize POLY1305_TAG_SIZE = 16;
///@}

/** @name ChaCha20-Poly1305 AEAD parameter sizes */
///@{
constexpr usize CHACHA20_POLY1305_KEY_SIZE = 32;
constexpr usize CHACHA20_POLY1305_NONCE_SIZE = 12;
constexpr usize CHACHA20_POLY1305_TAG_SIZE = 16;

///@}

/**
 * @brief ChaCha20 internal state (16 32-bit words).
 *
 * @details
 * Holds the ChaCha20 state matrix used by the block function.
 */
struct ChaCha20State
{
    u32 state[16];
};

/**
 * @brief Initialize a ChaCha20 state with key, nonce, and counter.
 *
 * @param s State to initialize.
 * @param key 32-byte key.
 * @param nonce 12-byte nonce.
 * @param counter Initial block counter.
 */
void chacha20_init(ChaCha20State *s,
                   const u8 key[CHACHA20_KEY_SIZE],
                   const u8 nonce[CHACHA20_NONCE_SIZE],
                   u32 counter);

/**
 * @brief Generate one 64-byte ChaCha20 keystream block.
 *
 * @details
 * Runs the ChaCha20 block function using the state and writes the keystream
 * block to `out`. The state counter is incremented.
 *
 * @param s State (counter is updated).
 * @param out Output 64-byte keystream block.
 */
void chacha20_block(ChaCha20State *s, u8 out[CHACHA20_BLOCK_SIZE]);

/**
 * @brief Encrypt/decrypt data with ChaCha20 (XOR keystream).
 *
 * @details
 * ChaCha20 is a stream cipher; encryption and decryption are identical and are
 * performed by XORing the keystream with input bytes.
 *
 * @param key 32-byte key.
 * @param nonce 12-byte nonce.
 * @param counter Initial block counter.
 * @param in Input bytes.
 * @param out Output bytes (may overlap with `in`).
 * @param len Number of bytes.
 */
void chacha20_crypt(const u8 key[CHACHA20_KEY_SIZE],
                    const u8 nonce[CHACHA20_NONCE_SIZE],
                    u32 counter,
                    const u8 *in,
                    u8 *out,
                    usize len);

/**
 * @brief Poly1305 incremental MAC state.
 *
 * @details
 * Stores the r and pad values plus the accumulator used by Poly1305.
 */
struct Poly1305State
{
    u32 r[5];      // r (clamped)
    u32 h[5];      // Accumulator
    u32 pad[4];    // s (one-time pad)
    u8 buffer[16]; // Partial block
    usize buffer_len;
};

/**
 * @brief Initialize a Poly1305 state with a 32-byte one-time key.
 *
 * @param s Poly1305 state.
 * @param key 32-byte key (`r || s`).
 */
void poly1305_init(Poly1305State *s, const u8 key[POLY1305_KEY_SIZE]);

/**
 * @brief Update Poly1305 MAC with more data.
 *
 * @param s Poly1305 state.
 * @param data Input bytes.
 * @param len Number of bytes.
 */
void poly1305_update(Poly1305State *s, const void *data, usize len);

/**
 * @brief Finalize Poly1305 and output the 16-byte tag.
 *
 * @param s Poly1305 state.
 * @param tag Output tag (16 bytes).
 */
void poly1305_final(Poly1305State *s, u8 tag[POLY1305_TAG_SIZE]);

/**
 * @brief Compute Poly1305 MAC in one call.
 *
 * @param key 32-byte key.
 * @param data Input bytes.
 * @param len Number of bytes.
 * @param tag Output tag.
 */
void poly1305(const u8 key[POLY1305_KEY_SIZE],
              const void *data,
              usize len,
              u8 tag[POLY1305_TAG_SIZE]);

/**
 * @brief Encrypt using ChaCha20-Poly1305 AEAD.
 *
 * @details
 * Produces `ciphertext || tag` where `tag` is a 16-byte Poly1305 authenticator
 * computed over the AAD and ciphertext per RFC 8439.
 *
 * @param key AEAD key (32 bytes).
 * @param nonce AEAD nonce (12 bytes).
 * @param aad Additional authenticated data (may be null).
 * @param aad_len AAD length.
 * @param plaintext Plaintext bytes.
 * @param plaintext_len Plaintext length.
 * @param ciphertext Output buffer (must have space for plaintext_len + 16).
 * @return Total ciphertext length (plaintext_len + 16).
 */
usize chacha20_poly1305_encrypt(const u8 key[CHACHA20_POLY1305_KEY_SIZE],
                                const u8 nonce[CHACHA20_POLY1305_NONCE_SIZE],
                                const void *aad,
                                usize aad_len,
                                const void *plaintext,
                                usize plaintext_len,
                                u8 *ciphertext); // Must have space for plaintext_len + 16

/**
 * @brief Decrypt using ChaCha20-Poly1305 AEAD.
 *
 * @details
 * Verifies the Poly1305 tag and, if valid, decrypts the ciphertext into
 * `plaintext`.
 *
 * @param key AEAD key.
 * @param nonce AEAD nonce.
 * @param aad Additional authenticated data (may be null).
 * @param aad_len AAD length.
 * @param ciphertext Ciphertext including the trailing 16-byte tag.
 * @param ciphertext_len Total ciphertext length including tag.
 * @param plaintext Output buffer (must have space for ciphertext_len - 16).
 * @return Plaintext length on success, or -1 if authentication fails.
 */
i64 chacha20_poly1305_decrypt(const u8 key[CHACHA20_POLY1305_KEY_SIZE],
                              const u8 nonce[CHACHA20_POLY1305_NONCE_SIZE],
                              const void *aad,
                              usize aad_len,
                              const void *ciphertext,
                              usize ciphertext_len, // Includes tag
                              u8 *plaintext);

} // namespace viper::crypto
