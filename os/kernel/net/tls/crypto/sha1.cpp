/**
 * @file sha1.cpp
 * @brief SHA-1 and HMAC-SHA1 implementation.
 *
 * @details
 * Implements the primitives declared in `sha1.hpp`. The SHA-1 algorithm is
 * based on FIPS 180-4. This implementation is for legacy SSH compatibility.
 *
 * Note: SHA-1 is cryptographically deprecated for signatures but still
 * required for SSH-2 key exchange compatibility with older servers.
 */

#include "sha1.hpp"

namespace viper::crypto
{

// SHA-1 initial hash values
static const u32 H_INIT[5] = {
    0x67452301,
    0xefcdab89,
    0x98badcfe,
    0x10325476,
    0xc3d2e1f0
};

// SHA-1 round constants
static const u32 K[4] = {
    0x5a827999,  // rounds 0-19
    0x6ed9eba1,  // rounds 20-39
    0x8f1bbcdc,  // rounds 40-59
    0xca62c1d6   // rounds 60-79
};

/**
 * @brief Rotate a 32-bit word left by `n` bits.
 */
static inline u32 rotl(u32 x, int n)
{
    return (x << n) | (x >> (32 - n));
}

/**
 * @brief SHA-1 f function for rounds 0-19.
 * Ch(x,y,z) = (x & y) ^ (~x & z)
 */
static inline u32 f_ch(u32 x, u32 y, u32 z)
{
    return (x & y) ^ (~x & z);
}

/**
 * @brief SHA-1 f function for rounds 20-39 and 60-79.
 * Parity(x,y,z) = x ^ y ^ z
 */
static inline u32 f_parity(u32 x, u32 y, u32 z)
{
    return x ^ y ^ z;
}

/**
 * @brief SHA-1 f function for rounds 40-59.
 * Maj(x,y,z) = (x & y) ^ (x & z) ^ (y & z)
 */
static inline u32 f_maj(u32 x, u32 y, u32 z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

/**
 * @brief SHA-1 compression function for one 512-bit block.
 *
 * @details
 * Processes one 64-byte message block:
 * - Expands the 16 message words into an 80-word message schedule.
 * - Runs the 80-round compression loop.
 * - Adds the working variables back into the hash state.
 *
 * @param state Current hash state (5 words), updated in place.
 * @param block 64-byte message block.
 */
static void sha1_transform(u32 state[5], const u8 block[64])
{
    u32 W[80];

    // Prepare message schedule (first 16 words from block)
    for (int i = 0; i < 16; i++)
    {
        W[i] = (static_cast<u32>(block[i * 4]) << 24) |
               (static_cast<u32>(block[i * 4 + 1]) << 16) |
               (static_cast<u32>(block[i * 4 + 2]) << 8) |
               static_cast<u32>(block[i * 4 + 3]);
    }

    // Extend message schedule (words 16-79)
    for (int i = 16; i < 80; i++)
    {
        W[i] = rotl(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);
    }

    // Initialize working variables
    u32 a = state[0];
    u32 b = state[1];
    u32 c = state[2];
    u32 d = state[3];
    u32 e = state[4];

    // 80 rounds
    for (int i = 0; i < 80; i++)
    {
        u32 f, k;

        if (i < 20)
        {
            f = f_ch(b, c, d);
            k = K[0];
        }
        else if (i < 40)
        {
            f = f_parity(b, c, d);
            k = K[1];
        }
        else if (i < 60)
        {
            f = f_maj(b, c, d);
            k = K[2];
        }
        else
        {
            f = f_parity(b, c, d);
            k = K[3];
        }

        u32 temp = rotl(a, 5) + f + e + k + W[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = temp;
    }

    // Update state
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(Sha1Context *ctx)
{
    for (int i = 0; i < 5; i++)
    {
        ctx->state[i] = H_INIT[i];
    }
    ctx->bit_count = 0;
    ctx->buffer_len = 0;
}

void sha1_update(Sha1Context *ctx, const void *data, usize len)
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
            sha1_transform(ctx->state, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }

    // Process full blocks
    while (len >= 64)
    {
        sha1_transform(ctx->state, bytes);
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

void sha1_final(Sha1Context *ctx, u8 digest[SHA1_DIGEST_SIZE])
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
        sha1_transform(ctx->state, pad);
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

    sha1_transform(ctx->state, pad);

    // Output digest (big-endian, 5 words = 20 bytes)
    for (int i = 0; i < 5; i++)
    {
        digest[i * 4] = static_cast<u8>(ctx->state[i] >> 24);
        digest[i * 4 + 1] = static_cast<u8>(ctx->state[i] >> 16);
        digest[i * 4 + 2] = static_cast<u8>(ctx->state[i] >> 8);
        digest[i * 4 + 3] = static_cast<u8>(ctx->state[i]);
    }
}

void sha1(const void *data, usize len, u8 digest[SHA1_DIGEST_SIZE])
{
    Sha1Context ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}

// HMAC-SHA1 implementation
void hmac_sha1_init(HmacSha1Context *ctx, const void *key, usize key_len)
{
    u8 key_block[SHA1_BLOCK_SIZE];

    // If key is longer than block size, hash it first
    if (key_len > SHA1_BLOCK_SIZE)
    {
        sha1(key, key_len, key_block);
        key_len = SHA1_DIGEST_SIZE;
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
    for (usize i = key_len; i < SHA1_BLOCK_SIZE; i++)
    {
        key_block[i] = 0;
    }

    // Create inner and outer pads
    u8 inner_pad[SHA1_BLOCK_SIZE];
    u8 outer_pad[SHA1_BLOCK_SIZE];

    for (usize i = 0; i < SHA1_BLOCK_SIZE; i++)
    {
        inner_pad[i] = key_block[i] ^ 0x36;
        outer_pad[i] = key_block[i] ^ 0x5c;
    }

    // Save outer pad for final
    for (usize i = 0; i < SHA1_BLOCK_SIZE; i++)
    {
        ctx->key_pad[i] = outer_pad[i];
    }

    // Start inner hash
    sha1_init(&ctx->inner);
    sha1_update(&ctx->inner, inner_pad, SHA1_BLOCK_SIZE);

    // Prepare outer context
    sha1_init(&ctx->outer);
    sha1_update(&ctx->outer, outer_pad, SHA1_BLOCK_SIZE);
}

void hmac_sha1_update(HmacSha1Context *ctx, const void *data, usize len)
{
    sha1_update(&ctx->inner, data, len);
}

void hmac_sha1_final(HmacSha1Context *ctx, u8 mac[HMAC_SHA1_SIZE])
{
    u8 inner_hash[SHA1_DIGEST_SIZE];
    sha1_final(&ctx->inner, inner_hash);

    // Outer hash = SHA1(outer_pad || inner_hash)
    sha1_update(&ctx->outer, inner_hash, SHA1_DIGEST_SIZE);
    sha1_final(&ctx->outer, mac);
}

void hmac_sha1(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA1_SIZE])
{
    HmacSha1Context ctx;
    hmac_sha1_init(&ctx, key, key_len);
    hmac_sha1_update(&ctx, data, data_len);
    hmac_sha1_final(&ctx, mac);
}

} // namespace viper::crypto
