/**
 * @file sha256.cpp
 * @brief SHA-256 and HMAC-SHA256 implementation.
 *
 * @details
 * Implements the primitives declared in `sha256.hpp`. The SHA-256 algorithm is
 * based on FIPS 180-4. The HMAC implementation follows the standard
 * HMAC construction using SHA-256 as the underlying hash.
 */

#include "sha256.hpp"

namespace viper::crypto
{

// SHA-256 constants (first 32 bits of cube roots of first 64 primes)
static const u32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Initial hash values (first 32 bits of square roots of first 8 primes)
static const u32 H_INIT[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

/**
 * @brief Rotate a 32-bit word right by `n` bits.
 *
 * @details
 * SHA-256 defines several rotation-based functions over 32-bit words. This
 * helper implements the core rotate-right primitive.
 *
 * @param x Input word.
 * @param n Rotation amount in bits (0–31).
 * @return Rotated value.
 */
static inline u32 rotr(u32 x, int n)
{
    return (x >> n) | (x << (32 - n));
}

/**
 * @brief SHA-256 choice function.
 *
 * @details
 * Defined as: `Ch(x,y,z) = (x & y) ^ (~x & z)`.
 * Interpreted bitwise, it selects bits from `y` or `z` based on `x`.
 */
static inline u32 ch(u32 x, u32 y, u32 z)
{
    return (x & y) ^ (~x & z);
}

/**
 * @brief SHA-256 majority function.
 *
 * @details
 * Defined as: `Maj(x,y,z) = (x & y) ^ (x & z) ^ (y & z)`.
 * For each bit position, the output is the majority value of the three inputs.
 */
static inline u32 maj(u32 x, u32 y, u32 z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

/**
 * @brief SHA-256 Σ0 (big sigma 0) function.
 *
 * @details
 * Defined as: `Σ0(x) = ROTR^2(x) ^ ROTR^13(x) ^ ROTR^22(x)`.
 */
static inline u32 sigma0(u32 x)
{
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

/**
 * @brief SHA-256 Σ1 (big sigma 1) function.
 *
 * @details
 * Defined as: `Σ1(x) = ROTR^6(x) ^ ROTR^11(x) ^ ROTR^25(x)`.
 */
static inline u32 sigma1(u32 x)
{
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

/**
 * @brief SHA-256 σ0 (small sigma 0) function.
 *
 * @details
 * Defined as: `σ0(x) = ROTR^7(x) ^ ROTR^18(x) ^ SHR^3(x)`.
 */
static inline u32 gamma0(u32 x)
{
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

/**
 * @brief SHA-256 σ1 (small sigma 1) function.
 *
 * @details
 * Defined as: `σ1(x) = ROTR^17(x) ^ ROTR^19(x) ^ SHR^10(x)`.
 */
static inline u32 gamma1(u32 x)
{
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

/**
 * @brief SHA-256 compression function for one 512-bit block.
 *
 * @details
 * Processes one 64-byte message block:
 * - Expands the 16 message words into a 64-word message schedule.
 * - Runs the 64-round compression loop.
 * - Adds the working variables back into the hash state.
 *
 * This is the core of SHA-256 and is called by @ref sha256_update as input data
 * accumulates in 64-byte chunks.
 *
 * @param state Current hash state (8 words), updated in place.
 * @param block 64-byte message block.
 */
static void sha256_transform(u32 state[8], const u8 block[64])
{
    u32 W[64];

    // Prepare message schedule
    for (int i = 0; i < 16; i++)
    {
        W[i] = (static_cast<u32>(block[i * 4]) << 24) | (static_cast<u32>(block[i * 4 + 1]) << 16) |
               (static_cast<u32>(block[i * 4 + 2]) << 8) | static_cast<u32>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++)
    {
        W[i] = gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16];
    }

    // Initialize working variables
    u32 a = state[0];
    u32 b = state[1];
    u32 c = state[2];
    u32 d = state[3];
    u32 e = state[4];
    u32 f = state[5];
    u32 g = state[6];
    u32 h = state[7];

    // 64 rounds
    for (int i = 0; i < 64; i++)
    {
        u32 t1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        u32 t2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    // Update state
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/** @copydoc viper::crypto::sha256_init */
void sha256_init(Sha256Context *ctx)
{
    for (int i = 0; i < 8; i++)
    {
        ctx->state[i] = H_INIT[i];
    }
    ctx->bit_count = 0;
    ctx->buffer_len = 0;
}

/** @copydoc viper::crypto::sha256_update */
void sha256_update(Sha256Context *ctx, const void *data, usize len)
{
    const u8 *bytes = static_cast<const u8 *>(data);

    ctx->bit_count += len * 8;

    // Handle any data in buffer
    if (ctx->buffer_len > 0)
    {
        usize space = 64 - ctx->buffer_len;
        usize copy = (len < space) ? len : space;

        for (usize i = 0; i < copy; i++)
        {
            ctx->buffer[ctx->buffer_len + i] = bytes[i];
        }
        ctx->buffer_len += copy;
        bytes += copy;
        len -= copy;

        if (ctx->buffer_len == 64)
        {
            sha256_transform(ctx->state, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }

    // Process full blocks
    while (len >= 64)
    {
        sha256_transform(ctx->state, bytes);
        bytes += 64;
        len -= 64;
    }

    // Buffer remaining bytes
    for (usize i = 0; i < len; i++)
    {
        ctx->buffer[i] = bytes[i];
    }
    ctx->buffer_len = len;
}

/** @copydoc viper::crypto::sha256_final */
void sha256_final(Sha256Context *ctx, u8 digest[SHA256_DIGEST_SIZE])
{
    // Pad message
    u8 pad[64];
    usize pad_len = 64 - ctx->buffer_len;

    if (pad_len < 9)
    {
        pad_len += 64; // Need extra block for length
    }

    // Copy buffer to pad
    for (usize i = 0; i < ctx->buffer_len; i++)
    {
        pad[i] = ctx->buffer[i];
    }

    // Add 0x80 and zeros
    pad[ctx->buffer_len] = 0x80;
    for (usize i = ctx->buffer_len + 1; i < pad_len - 8; i++)
    {
        pad[i % 64] = 0;
    }

    // Process first block if we need two
    if (pad_len > 64)
    {
        sha256_transform(ctx->state, pad);
        for (int i = 0; i < 56; i++)
        {
            pad[i] = 0;
        }
    }

    // Add length in bits (big-endian)
    u64 bits = ctx->bit_count;
    pad[56] = static_cast<u8>(bits >> 56);
    pad[57] = static_cast<u8>(bits >> 48);
    pad[58] = static_cast<u8>(bits >> 40);
    pad[59] = static_cast<u8>(bits >> 32);
    pad[60] = static_cast<u8>(bits >> 24);
    pad[61] = static_cast<u8>(bits >> 16);
    pad[62] = static_cast<u8>(bits >> 8);
    pad[63] = static_cast<u8>(bits);

    sha256_transform(ctx->state, pad);

    // Output digest (big-endian)
    for (int i = 0; i < 8; i++)
    {
        digest[i * 4] = static_cast<u8>(ctx->state[i] >> 24);
        digest[i * 4 + 1] = static_cast<u8>(ctx->state[i] >> 16);
        digest[i * 4 + 2] = static_cast<u8>(ctx->state[i] >> 8);
        digest[i * 4 + 3] = static_cast<u8>(ctx->state[i]);
    }
}

/** @copydoc viper::crypto::sha256 */
void sha256(const void *data, usize len, u8 digest[SHA256_DIGEST_SIZE])
{
    Sha256Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

// HMAC-SHA256 implementation
/** @copydoc viper::crypto::hmac_sha256_init */
void hmac_sha256_init(HmacSha256Context *ctx, const void *key, usize key_len)
{
    u8 key_block[SHA256_BLOCK_SIZE];

    // If key is longer than block size, hash it first
    if (key_len > SHA256_BLOCK_SIZE)
    {
        sha256(key, key_len, key_block);
        key_len = SHA256_DIGEST_SIZE;
    }
    else
    {
        const u8 *k = static_cast<const u8 *>(key);
        for (usize i = 0; i < key_len; i++)
        {
            key_block[i] = k[i];
        }
    }

    // Pad key with zeros
    for (usize i = key_len; i < SHA256_BLOCK_SIZE; i++)
    {
        key_block[i] = 0;
    }

    // Create inner and outer pads
    u8 inner_pad[SHA256_BLOCK_SIZE];
    u8 outer_pad[SHA256_BLOCK_SIZE];

    for (usize i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
        inner_pad[i] = key_block[i] ^ 0x36;
        outer_pad[i] = key_block[i] ^ 0x5c;
    }

    // Save outer pad for final
    for (usize i = 0; i < SHA256_BLOCK_SIZE; i++)
    {
        ctx->key_pad[i] = outer_pad[i];
    }

    // Start inner hash
    sha256_init(&ctx->inner);
    sha256_update(&ctx->inner, inner_pad, SHA256_BLOCK_SIZE);

    // Prepare outer context
    sha256_init(&ctx->outer);
    sha256_update(&ctx->outer, outer_pad, SHA256_BLOCK_SIZE);
}

/** @copydoc viper::crypto::hmac_sha256_update */
void hmac_sha256_update(HmacSha256Context *ctx, const void *data, usize len)
{
    sha256_update(&ctx->inner, data, len);
}

/** @copydoc viper::crypto::hmac_sha256_final */
void hmac_sha256_final(HmacSha256Context *ctx, u8 mac[HMAC_SHA256_SIZE])
{
    u8 inner_hash[SHA256_DIGEST_SIZE];
    sha256_final(&ctx->inner, inner_hash);

    // Outer hash = SHA256(outer_pad || inner_hash)
    sha256_update(&ctx->outer, inner_hash, SHA256_DIGEST_SIZE);
    sha256_final(&ctx->outer, mac);
}

/** @copydoc viper::crypto::hmac_sha256 */
void hmac_sha256(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA256_SIZE])
{
    HmacSha256Context ctx;
    hmac_sha256_init(&ctx, key, key_len);
    hmac_sha256_update(&ctx, data, data_len);
    hmac_sha256_final(&ctx, mac);
}

} // namespace viper::crypto
