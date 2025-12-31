#pragma once

/**
 * @file sha1.hpp
 * @brief SHA-1 and HMAC-SHA1 primitives for SSH compatibility.
 *
 * @details
 * Provides a freestanding implementation of:
 * - SHA-1 (FIPS 180-4) for hashing.
 * - HMAC-SHA1 (RFC 2104) used by SSH-2 protocol.
 *
 * Note: SHA-1 is cryptographically weak and should only be used for
 * legacy protocol compatibility (SSH key exchange, etc.).
 */

#include "../../../include/types.hpp"

namespace viper::crypto
{

/** @brief SHA-1 digest size in bytes. */
constexpr usize SHA1_DIGEST_SIZE = 20;
/** @brief SHA-1 compression block size in bytes. */
constexpr usize SHA1_BLOCK_SIZE = 64;

/**
 * @brief SHA-1 incremental hashing context.
 *
 * @details
 * Stores the current hash state (5 words), total bit count, and a partial
 * block buffer used to accumulate input until a full 64-byte block is available.
 */
struct Sha1Context
{
    u32 state[5];     // Hash state (H0-H4)
    u64 bit_count;    // Total bits processed
    u8 buffer[64];    // Partial block buffer
    usize buffer_len; // Bytes in buffer
};

/**
 * @brief Initialize a SHA-1 context.
 *
 * @details
 * Sets the initial hash constants and clears counters/buffers.
 *
 * @param ctx Context to initialize.
 */
void sha1_init(Sha1Context *ctx);

/**
 * @brief Update a SHA-1 context with more data.
 *
 * @details
 * Feeds `len` bytes into the hash, processing full blocks as they become
 * available and buffering any remainder.
 *
 * @param ctx Context to update.
 * @param data Input bytes.
 * @param len Number of bytes.
 */
void sha1_update(Sha1Context *ctx, const void *data, usize len);

/**
 * @brief Finalize a SHA-1 context and output the digest.
 *
 * @details
 * Pads the message per SHA-1 specification, processes final blocks, and
 * writes the 20-byte digest to `digest`.
 *
 * @param ctx Context to finalize.
 * @param digest Output digest buffer (20 bytes).
 */
void sha1_final(Sha1Context *ctx, u8 digest[SHA1_DIGEST_SIZE]);

/**
 * @brief Compute SHA-1 hash of a buffer in one call.
 *
 * @param data Input bytes.
 * @param len Number of bytes.
 * @param digest Output digest buffer (20 bytes).
 */
void sha1(const void *data, usize len, u8 digest[SHA1_DIGEST_SIZE]);

/** @brief Size of an HMAC-SHA1 tag in bytes. */
constexpr usize HMAC_SHA1_SIZE = SHA1_DIGEST_SIZE;

/**
 * @brief HMAC-SHA1 incremental context.
 *
 * @details
 * Holds inner and outer SHA-1 contexts plus the outer key pad.
 */
struct HmacSha1Context
{
    Sha1Context inner;
    Sha1Context outer;
    u8 key_pad[SHA1_BLOCK_SIZE];
};

/**
 * @brief Initialize an HMAC-SHA1 context with a key.
 *
 * @param ctx HMAC context.
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 */
void hmac_sha1_init(HmacSha1Context *ctx, const void *key, usize key_len);

/**
 * @brief Update an HMAC-SHA1 context with more data.
 *
 * @param ctx HMAC context.
 * @param data Input bytes.
 * @param len Number of bytes.
 */
void hmac_sha1_update(HmacSha1Context *ctx, const void *data, usize len);

/**
 * @brief Finalize an HMAC-SHA1 context and output the MAC.
 *
 * @param ctx HMAC context.
 * @param mac Output buffer (20 bytes).
 */
void hmac_sha1_final(HmacSha1Context *ctx, u8 mac[HMAC_SHA1_SIZE]);

/**
 * @brief Compute HMAC-SHA1 in one call.
 *
 * @param key Key bytes.
 * @param key_len Key length in bytes.
 * @param data Input bytes.
 * @param data_len Input length in bytes.
 * @param mac Output MAC buffer (20 bytes).
 */
void hmac_sha1(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA1_SIZE]);

} // namespace viper::crypto
