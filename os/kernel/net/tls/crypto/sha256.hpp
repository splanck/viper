#pragma once

/**
 * @file sha256.hpp
 * @brief SHA-256 and HMAC-SHA256 primitives used by TLS.
 *
 * @details
 * Provides a small, freestanding implementation of:
 * - SHA-256 (FIPS 180-4) for hashing and transcript computation.
 * - HMAC-SHA256 (RFC 2104) used by HKDF and TLS Finished computation.
 *
 * The API supports both incremental and one-shot hashing to accommodate the
 * needs of the TLS key schedule and certificate verification code.
 */

#include "../../../include/types.hpp"

namespace viper::crypto
{

/** @brief SHA-256 digest size in bytes. */
constexpr usize SHA256_DIGEST_SIZE = 32;
/** @brief SHA-256 compression block size in bytes. */
constexpr usize SHA256_BLOCK_SIZE = 64;

/**
 * @brief SHA-256 incremental hashing context.
 *
 * @details
 * Stores the current hash state, total bit count, and a partial block buffer
 * used to accumulate input until a full 64-byte block is available.
 */
struct Sha256Context
{
    u32 state[8];     // Hash state (H0-H7)
    u64 bit_count;    // Total bits processed
    u8 buffer[64];    // Partial block buffer
    usize buffer_len; // Bytes in buffer
};

/**
 * @brief Initialize a SHA-256 context.
 *
 * @details
 * Sets the initial hash constants and clears counters/buffers.
 *
 * @param ctx Context to initialize.
 */
void sha256_init(Sha256Context *ctx);

/**
 * @brief Update a SHA-256 context with more data.
 *
 * @details
 * Feeds `len` bytes into the hash, processing full blocks as they become
 * available and buffering any remainder.
 *
 * @param ctx Context to update.
 * @param data Input bytes.
 * @param len Number of bytes.
 */
void sha256_update(Sha256Context *ctx, const void *data, usize len);

/**
 * @brief Finalize a SHA-256 context and output the digest.
 *
 * @details
 * Pads the message per SHA-256 specification, processes final blocks, and
 * writes the 32-byte digest to `digest`. The context contents are left in a
 * finalized state and should be reinitialized before reuse.
 *
 * @param ctx Context to finalize.
 * @param digest Output digest buffer (32 bytes).
 */
void sha256_final(Sha256Context *ctx, u8 digest[SHA256_DIGEST_SIZE]);

/**
 * @brief Compute SHA-256 hash of a buffer in one call.
 *
 * @param data Input bytes.
 * @param len Number of bytes.
 * @param digest Output digest buffer (32 bytes).
 */
void sha256(const void *data, usize len, u8 digest[SHA256_DIGEST_SIZE]);

/** @brief Size of an HMAC-SHA256 tag in bytes. */
constexpr usize HMAC_SHA256_SIZE = SHA256_DIGEST_SIZE;

/**
 * @brief HMAC-SHA256 incremental context.
 *
 * @details
 * Holds inner and outer SHA-256 contexts plus the outer key pad. The HMAC API
 * mirrors the SHA-256 incremental interface.
 */
struct HmacSha256Context
{
    Sha256Context inner;
    Sha256Context outer;
    u8 key_pad[SHA256_BLOCK_SIZE];
};

/**
 * @brief Initialize an HMAC-SHA256 context with a key.
 *
 * @details
 * Normalizes the key to a block-sized value (hashing long keys), then prepares
 * inner/outer padded contexts.
 *
 * @param ctx HMAC context.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 */
void hmac_sha256_init(HmacSha256Context *ctx, const void *key, usize key_len);

/**
 * @brief Update an HMAC-SHA256 context with more data.
 *
 * @param ctx HMAC context.
 * @param data Input bytes.
 * @param len Number of bytes.
 */
void hmac_sha256_update(HmacSha256Context *ctx, const void *data, usize len);

/**
 * @brief Finalize an HMAC-SHA256 context and output the MAC.
 *
 * @param ctx HMAC context.
 * @param mac Output buffer (32 bytes).
 */
void hmac_sha256_final(HmacSha256Context *ctx, u8 mac[HMAC_SHA256_SIZE]);

/**
 * @brief Compute HMAC-SHA256 in one call.
 *
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param data Input bytes.
 * @param data_len Input length in bytes.
 * @param mac Output MAC buffer (32 bytes).
 */
void hmac_sha256(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA256_SIZE]);

} // namespace viper::crypto
