/**
 * @file sha384.cpp
 * @brief SHA-384/SHA-512 and HMAC implementation.
 *
 * @details
 * Implements the hashing primitives declared in `sha384.hpp`, including the
 * shared SHA-512 compression function and SHA-384 digest truncation.
 */

#include "sha384.hpp"
#include "../../../lib/mem.hpp"

namespace viper::tls::crypto
{

// SHA-512 round constants (first 80 primes)
static const u64 K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

// SHA-512 initial values
static const u64 SHA512_INIT[8] = {0x6a09e667f3bcc908ULL,
                                   0xbb67ae8584caa73bULL,
                                   0x3c6ef372fe94f82bULL,
                                   0xa54ff53a5f1d36f1ULL,
                                   0x510e527fade682d1ULL,
                                   0x9b05688c2b3e6c1fULL,
                                   0x1f83d9abfb41bd6bULL,
                                   0x5be0cd19137e2179ULL};

// SHA-384 initial values (different from SHA-512)
static const u64 SHA384_INIT[8] = {0xcbbb9d5dc1059ed8ULL,
                                   0x629a292a367cd507ULL,
                                   0x9159015a3070dd17ULL,
                                   0x152fecd8f70e5939ULL,
                                   0x67332667ffc00b31ULL,
                                   0x8eb44a8768581511ULL,
                                   0xdb0c2e0d64f98fa7ULL,
                                   0x47b5481dbefa4fa4ULL};

/**
 * @brief Rotate a 64-bit word right by `n` bits.
 *
 * @details
 * SHA-512 defines several rotation-based functions over 64-bit words. This
 * helper implements the core rotate-right primitive.
 *
 * @param x Input word.
 * @param n Rotation amount in bits (0–63).
 * @return Rotated value.
 */
static inline u64 rotr64(u64 x, int n)
{
    return (x >> n) | (x << (64 - n));
}

/**
 * @brief SHA-512 choice function.
 *
 * @details
 * Defined as: `Ch(x,y,z) = (x & y) ^ (~x & z)`.
 */
static inline u64 Ch(u64 x, u64 y, u64 z)
{
    return (x & y) ^ (~x & z);
}

/**
 * @brief SHA-512 majority function.
 *
 * @details
 * Defined as: `Maj(x,y,z) = (x & y) ^ (x & z) ^ (y & z)`.
 */
static inline u64 Maj(u64 x, u64 y, u64 z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

/** @brief SHA-512 Σ0 (big sigma 0) function. */
static inline u64 Sigma0(u64 x)
{
    return rotr64(x, 28) ^ rotr64(x, 34) ^ rotr64(x, 39);
}

/** @brief SHA-512 Σ1 (big sigma 1) function. */
static inline u64 Sigma1(u64 x)
{
    return rotr64(x, 14) ^ rotr64(x, 18) ^ rotr64(x, 41);
}

/** @brief SHA-512 σ0 (small sigma 0) function. */
static inline u64 sigma0(u64 x)
{
    return rotr64(x, 1) ^ rotr64(x, 8) ^ (x >> 7);
}

/** @brief SHA-512 σ1 (small sigma 1) function. */
static inline u64 sigma1(u64 x)
{
    return rotr64(x, 19) ^ rotr64(x, 61) ^ (x >> 6);
}

/**
 * @brief SHA-512 compression function for one 1024-bit block.
 *
 * @details
 * Processes one 128-byte message block:
 * - Expands the 16 message words into an 80-word message schedule.
 * - Runs the 80-round compression loop.
 * - Adds the working variables back into the hash state.
 *
 * SHA-384 uses the same compression function; it differs only in initial state
 * and digest truncation.
 *
 * @param state Current hash state (8 words), updated in place.
 * @param block 128-byte message block.
 */
static void sha512_transform(u64 state[8], const u8 block[128])
{
    u64 W[80];
    u64 a, b, c, d, e, f, g, h;

    // Prepare message schedule
    for (int i = 0; i < 16; i++)
    {
        W[i] = (static_cast<u64>(block[i * 8 + 0]) << 56) |
               (static_cast<u64>(block[i * 8 + 1]) << 48) |
               (static_cast<u64>(block[i * 8 + 2]) << 40) |
               (static_cast<u64>(block[i * 8 + 3]) << 32) |
               (static_cast<u64>(block[i * 8 + 4]) << 24) |
               (static_cast<u64>(block[i * 8 + 5]) << 16) |
               (static_cast<u64>(block[i * 8 + 6]) << 8) | (static_cast<u64>(block[i * 8 + 7]));
    }

    for (int i = 16; i < 80; i++)
    {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    // Initialize working variables
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    // 80 rounds
    for (int i = 0; i < 80; i++)
    {
        u64 T1 = h + Sigma1(e) + Ch(e, f, g) + K512[i] + W[i];
        u64 T2 = Sigma0(a) + Maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    // Add to state
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/** @copydoc viper::tls::crypto::sha512_init */
void sha512_init(Sha512Context *ctx)
{
    lib::memcpy(ctx->state, SHA512_INIT, sizeof(SHA512_INIT));
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

/** @copydoc viper::tls::crypto::sha384_init */
void sha384_init(Sha384Context *ctx)
{
    lib::memcpy(ctx->state, SHA384_INIT, sizeof(SHA384_INIT));
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

/** @copydoc viper::tls::crypto::sha512_update */
void sha512_update(Sha512Context *ctx, const void *data, usize len)
{
    const u8 *bytes = static_cast<const u8 *>(data);
    usize buffer_used = static_cast<usize>(ctx->count[0] & 127);

    // Update bit count
    u64 old_count = ctx->count[0];
    ctx->count[0] += len;
    if (ctx->count[0] < old_count)
    {
        ctx->count[1]++;
    }

    // Process buffered data first
    if (buffer_used > 0)
    {
        usize buffer_space = 128 - buffer_used;
        if (len < buffer_space)
        {
            lib::memcpy(ctx->buffer + buffer_used, bytes, len);
            return;
        }
        lib::memcpy(ctx->buffer + buffer_used, bytes, buffer_space);
        sha512_transform(ctx->state, ctx->buffer);
        bytes += buffer_space;
        len -= buffer_space;
    }

    // Process full blocks
    while (len >= 128)
    {
        sha512_transform(ctx->state, bytes);
        bytes += 128;
        len -= 128;
    }

    // Buffer remaining
    if (len > 0)
    {
        lib::memcpy(ctx->buffer, bytes, len);
    }
}

/** @copydoc viper::tls::crypto::sha384_update */
void sha384_update(Sha384Context *ctx, const void *data, usize len)
{
    sha512_update(ctx, data, len);
}

/** @copydoc viper::tls::crypto::sha512_final */
void sha512_final(Sha512Context *ctx, u8 digest[SHA512_DIGEST_SIZE])
{
    usize buffer_used = static_cast<usize>(ctx->count[0] & 127);

    // Pad message
    ctx->buffer[buffer_used++] = 0x80;

    if (buffer_used > 112)
    {
        // Need another block
        lib::memset(ctx->buffer + buffer_used, 0, 128 - buffer_used);
        sha512_transform(ctx->state, ctx->buffer);
        buffer_used = 0;
    }

    lib::memset(ctx->buffer + buffer_used, 0, 112 - buffer_used);

    // Append length in bits (big-endian, 128-bit)
    u64 bit_count_hi = (ctx->count[1] << 3) | (ctx->count[0] >> 61);
    u64 bit_count_lo = ctx->count[0] << 3;

    ctx->buffer[112] = (bit_count_hi >> 56) & 0xff;
    ctx->buffer[113] = (bit_count_hi >> 48) & 0xff;
    ctx->buffer[114] = (bit_count_hi >> 40) & 0xff;
    ctx->buffer[115] = (bit_count_hi >> 32) & 0xff;
    ctx->buffer[116] = (bit_count_hi >> 24) & 0xff;
    ctx->buffer[117] = (bit_count_hi >> 16) & 0xff;
    ctx->buffer[118] = (bit_count_hi >> 8) & 0xff;
    ctx->buffer[119] = (bit_count_hi) & 0xff;
    ctx->buffer[120] = (bit_count_lo >> 56) & 0xff;
    ctx->buffer[121] = (bit_count_lo >> 48) & 0xff;
    ctx->buffer[122] = (bit_count_lo >> 40) & 0xff;
    ctx->buffer[123] = (bit_count_lo >> 32) & 0xff;
    ctx->buffer[124] = (bit_count_lo >> 24) & 0xff;
    ctx->buffer[125] = (bit_count_lo >> 16) & 0xff;
    ctx->buffer[126] = (bit_count_lo >> 8) & 0xff;
    ctx->buffer[127] = (bit_count_lo) & 0xff;

    sha512_transform(ctx->state, ctx->buffer);

    // Output digest (big-endian)
    for (int i = 0; i < 8; i++)
    {
        digest[i * 8 + 0] = (ctx->state[i] >> 56) & 0xff;
        digest[i * 8 + 1] = (ctx->state[i] >> 48) & 0xff;
        digest[i * 8 + 2] = (ctx->state[i] >> 40) & 0xff;
        digest[i * 8 + 3] = (ctx->state[i] >> 32) & 0xff;
        digest[i * 8 + 4] = (ctx->state[i] >> 24) & 0xff;
        digest[i * 8 + 5] = (ctx->state[i] >> 16) & 0xff;
        digest[i * 8 + 6] = (ctx->state[i] >> 8) & 0xff;
        digest[i * 8 + 7] = (ctx->state[i]) & 0xff;
    }
}

/** @copydoc viper::tls::crypto::sha384_final */
void sha384_final(Sha384Context *ctx, u8 digest[SHA384_DIGEST_SIZE])
{
    u8 full_digest[SHA512_DIGEST_SIZE];
    sha512_final(ctx, full_digest);
    // SHA-384 is truncated SHA-512
    lib::memcpy(digest, full_digest, SHA384_DIGEST_SIZE);
}

/** @copydoc viper::tls::crypto::sha512 */
void sha512(const void *data, usize len, u8 digest[SHA512_DIGEST_SIZE])
{
    Sha512Context ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, digest);
}

/** @copydoc viper::tls::crypto::sha384 */
void sha384(const void *data, usize len, u8 digest[SHA384_DIGEST_SIZE])
{
    Sha384Context ctx;
    sha384_init(&ctx);
    sha384_update(&ctx, data, len);
    sha384_final(&ctx, digest);
}

/** @copydoc viper::tls::crypto::hmac_sha384 */
void hmac_sha384(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA384_SIZE])
{
    u8 k_ipad[SHA512_BLOCK_SIZE];
    u8 k_opad[SHA512_BLOCK_SIZE];
    u8 tk[SHA384_DIGEST_SIZE];

    // If key is longer than block size, hash it
    if (key_len > SHA512_BLOCK_SIZE)
    {
        sha384(key, key_len, tk);
        key = tk;
        key_len = SHA384_DIGEST_SIZE;
    }

    // Prepare padded keys
    lib::memset(k_ipad, 0x36, SHA512_BLOCK_SIZE);
    lib::memset(k_opad, 0x5c, SHA512_BLOCK_SIZE);

    const u8 *key_bytes = static_cast<const u8 *>(key);
    for (usize i = 0; i < key_len; i++)
    {
        k_ipad[i] ^= key_bytes[i];
        k_opad[i] ^= key_bytes[i];
    }

    // Inner hash: H(K XOR ipad || data)
    Sha384Context ctx;
    sha384_init(&ctx);
    sha384_update(&ctx, k_ipad, SHA512_BLOCK_SIZE);
    sha384_update(&ctx, data, data_len);
    u8 inner[SHA384_DIGEST_SIZE];
    sha384_final(&ctx, inner);

    // Outer hash: H(K XOR opad || inner)
    sha384_init(&ctx);
    sha384_update(&ctx, k_opad, SHA512_BLOCK_SIZE);
    sha384_update(&ctx, inner, SHA384_DIGEST_SIZE);
    sha384_final(&ctx, mac);
}

/** @copydoc viper::tls::crypto::hmac_sha512 */
void hmac_sha512(
    const void *key, usize key_len, const void *data, usize data_len, u8 mac[HMAC_SHA512_SIZE])
{
    u8 k_ipad[SHA512_BLOCK_SIZE];
    u8 k_opad[SHA512_BLOCK_SIZE];
    u8 tk[SHA512_DIGEST_SIZE];

    // If key is longer than block size, hash it
    if (key_len > SHA512_BLOCK_SIZE)
    {
        sha512(key, key_len, tk);
        key = tk;
        key_len = SHA512_DIGEST_SIZE;
    }

    // Prepare padded keys
    lib::memset(k_ipad, 0x36, SHA512_BLOCK_SIZE);
    lib::memset(k_opad, 0x5c, SHA512_BLOCK_SIZE);

    const u8 *key_bytes = static_cast<const u8 *>(key);
    for (usize i = 0; i < key_len; i++)
    {
        k_ipad[i] ^= key_bytes[i];
        k_opad[i] ^= key_bytes[i];
    }

    // Inner hash: H(K XOR ipad || data)
    Sha512Context ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, k_ipad, SHA512_BLOCK_SIZE);
    sha512_update(&ctx, data, data_len);
    u8 inner[SHA512_DIGEST_SIZE];
    sha512_final(&ctx, inner);

    // Outer hash: H(K XOR opad || inner)
    sha512_init(&ctx);
    sha512_update(&ctx, k_opad, SHA512_BLOCK_SIZE);
    sha512_update(&ctx, inner, SHA512_DIGEST_SIZE);
    sha512_final(&ctx, mac);
}

} // namespace viper::tls::crypto
