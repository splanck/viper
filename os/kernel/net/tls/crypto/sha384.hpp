#pragma once

/**
 * @file sha384.hpp
 * @brief SHA-384/SHA-512 and HMAC helpers (freestanding).
 *
 * @details
 * Provides SHA-384 and SHA-512 hashing primitives and one-shot HMAC helpers.
 * These algorithms may be used by TLS cipher suites (e.g., AES_256_GCM_SHA384)
 * and by certificate verification code.
 *
 * The implementation is designed for kernel bring-up and provides both one-shot
 * and incremental hashing APIs.
 */

#include "../../../include/types.hpp"

namespace viper::tls::crypto
{

/** @name Digest and block sizes */
///@{
constexpr usize SHA384_DIGEST_SIZE = 48; // 384 bits
constexpr usize SHA512_DIGEST_SIZE = 64; // 512 bits
constexpr usize SHA512_BLOCK_SIZE = 128; // 1024 bits

///@}

/**
 * @brief SHA-384/SHA-512 incremental hashing context.
 *
 * @details
 * SHA-384 and SHA-512 share the same compression function; SHA-384 differs by
 * initial constants and digest truncation. This context structure supports both.
 */
struct Sha384Context
{
    u64 state[8];
    u64 count[2];
    u8 buffer[SHA512_BLOCK_SIZE];
};

// SHA-512 context (same structure)
using Sha512Context = Sha384Context;

/**
 * @brief Compute SHA-384 hash in one call.
 *
 * @param data Input bytes.
 * @param len Number of bytes.
 * @param digest Output digest (48 bytes).
 */
void sha384(const void *data, usize len, u8 digest[SHA384_DIGEST_SIZE]);

/**
 * @brief Compute SHA-512 hash in one call.
 *
 * @param data Input bytes.
 * @param len Number of bytes.
 * @param digest Output digest (64 bytes).
 */
void sha512(const void *data, usize len, u8 digest[SHA512_DIGEST_SIZE]);

/** @brief Initialize an incremental SHA-384 context. */
void sha384_init(Sha384Context *ctx);
/** @brief Update an incremental SHA-384 context with more data. */
void sha384_update(Sha384Context *ctx, const void *data, usize len);
/** @brief Finalize SHA-384 and output the digest. */
void sha384_final(Sha384Context *ctx, u8 digest[SHA384_DIGEST_SIZE]);

/** @brief Initialize an incremental SHA-512 context. */
void sha512_init(Sha512Context *ctx);
/** @brief Update an incremental SHA-512 context with more data. */
void sha512_update(Sha512Context *ctx, const void *data, usize len);
/** @brief Finalize SHA-512 and output the digest. */
void sha512_final(Sha512Context *ctx, u8 digest[SHA512_DIGEST_SIZE]);

/** @brief Size of an HMAC-SHA384 tag. */
constexpr usize HMAC_SHA384_SIZE = SHA384_DIGEST_SIZE;

/**
 * @brief Compute HMAC-SHA384 in one call.
 *
 * @param key Key bytes.
 * @param key_len Key length.
 * @param data Input bytes.
 * @param data_len Input length.
 * @param mac Output MAC (48 bytes).
 */
void hmac_sha384(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA384_SIZE]);

/** @brief Size of an HMAC-SHA512 tag. */
constexpr usize HMAC_SHA512_SIZE = SHA512_DIGEST_SIZE;

/**
 * @brief Compute HMAC-SHA512 in one call.
 *
 * @param key Key bytes.
 * @param key_len Key length.
 * @param data Input bytes.
 * @param data_len Input length.
 * @param mac Output MAC (64 bytes).
 */
void hmac_sha512(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA512_SIZE]);

} // namespace viper::tls::crypto
