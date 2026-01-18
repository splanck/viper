//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_hash.c
/// @brief Cryptographic hash functions (MD5, SHA1, SHA256) and checksums (CRC32).
///
/// This file implements various hashing algorithms for data integrity checking,
/// content addressing, and cryptographic applications. All hash functions
/// return lowercase hexadecimal strings for easy display and comparison.
///
/// **Available Hash Functions:**
///
/// | Algorithm | Output Size | Security | Speed    | Use Case              |
/// |-----------|-------------|----------|----------|-----------------------|
/// | MD5       | 128 bits    | Broken   | Fast     | Legacy, checksums     |
/// | SHA-1     | 160 bits    | Broken   | Medium   | Legacy, git (moving)  |
/// | SHA-256   | 256 bits    | Strong   | Medium   | Security, blockchain  |
/// | CRC32     | 32 bits     | None     | V. Fast  | Error detection       |
///
/// **Security Warnings:**
/// - **MD5**: Cryptographically broken. Collisions can be generated in seconds.
///   Do NOT use for security. Acceptable for checksums and content addressing.
/// - **SHA-1**: Cryptographically broken. Chosen-prefix collisions demonstrated.
///   Do NOT use for new security applications.
/// - **SHA-256**: Currently secure. Recommended for all security applications.
/// - **CRC32**: NOT a cryptographic hash. Only suitable for error detection.
///
/// **Output Format:**
/// ```
/// Hash.MD5("Hello")    → "8b1a9953c4611296a827abf8c47804d7"  (32 chars)
/// Hash.SHA1("Hello")   → "f7ff9e8b7bb2e09b70935a5d785e0cc5d9d0abf0" (40 chars)
/// Hash.SHA256("Hello") → "185f8db32271fe25f561a6fc938b2e26..." (64 chars)
/// Hash.CRC32("Hello")  → 4157704578 (integer)
/// ```
///
/// **Common Use Cases:**
/// - File integrity verification (download checksums)
/// - Content-addressable storage (deduplication)
/// - Password hashing (with proper salting and key stretching)
/// - Digital signatures (as part of a larger scheme)
/// - Cache keys and ETags
///
/// **Thread Safety:** All functions are thread-safe (no global mutable state
/// except CRC32 table which is initialized once).
///
/// @see rt_codec.c For hex encoding/decoding utilities
///
//===----------------------------------------------------------------------===//

#include "rt_hash.h"

#include "rt_bytes.h"
#include "rt_codec.h"
#include "rt_crc32.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// MD5 Implementation (RFC 1321)
//=============================================================================

typedef struct
{
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD5_CTX;

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define MD5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac);                                        \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }
#define MD5_GG(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac);                                        \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }
#define MD5_HH(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac);                                        \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }
#define MD5_II(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac);                                        \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];

    for (int i = 0; i < 16; i++)
    {
        x[i] = ((uint32_t)block[i * 4]) | ((uint32_t)block[i * 4 + 1] << 8) |
               ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);
    }

    // Round 1
    MD5_FF(a, b, c, d, x[0], 7, 0xd76aa478);
    MD5_FF(d, a, b, c, x[1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[2], 17, 0x242070db);
    MD5_FF(b, c, d, a, x[3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[4], 7, 0xf57c0faf);
    MD5_FF(d, a, b, c, x[5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[6], 17, 0xa8304613);
    MD5_FF(b, c, d, a, x[7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, x[8], 7, 0x698098d8);
    MD5_FF(d, a, b, c, x[9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1);
    MD5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12], 7, 0x6b901122);
    MD5_FF(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], 17, 0xa679438e);
    MD5_FF(b, c, d, a, x[15], 22, 0x49b40821);

    // Round 2
    MD5_GG(a, b, c, d, x[1], 5, 0xf61e2562);
    MD5_GG(d, a, b, c, x[6], 9, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51);
    MD5_GG(b, c, d, a, x[0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[5], 5, 0xd62f105d);
    MD5_GG(d, a, b, c, x[10], 9, 0x02441453);
    MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681);
    MD5_GG(b, c, d, a, x[4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[9], 5, 0x21e1cde6);
    MD5_GG(d, a, b, c, x[14], 9, 0xc33707d6);
    MD5_GG(c, d, a, b, x[3], 14, 0xf4d50d87);
    MD5_GG(b, c, d, a, x[8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13], 5, 0xa9e3e905);
    MD5_GG(d, a, b, c, x[2], 9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[7], 14, 0x676f02d9);
    MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    // Round 3
    MD5_HH(a, b, c, d, x[5], 4, 0xfffa3942);
    MD5_HH(d, a, b, c, x[8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122);
    MD5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, x[1], 4, 0xa4beea44);
    MD5_HH(d, a, b, c, x[4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[7], 16, 0xf6bb4b60);
    MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13], 4, 0x289b7ec6);
    MD5_HH(d, a, b, c, x[0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[3], 16, 0xd4ef3085);
    MD5_HH(b, c, d, a, x[6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, x[9], 4, 0xd9d4d039);
    MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8);
    MD5_HH(b, c, d, a, x[2], 23, 0xc4ac5665);

    // Round 4
    MD5_II(a, b, c, d, x[0], 6, 0xf4292244);
    MD5_II(d, a, b, c, x[7], 10, 0x432aff97);
    MD5_II(c, d, a, b, x[14], 15, 0xab9423a7);
    MD5_II(b, c, d, a, x[5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, x[12], 6, 0x655b59c3);
    MD5_II(d, a, b, c, x[3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], 15, 0xffeff47d);
    MD5_II(b, c, d, a, x[1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, x[8], 6, 0x6fa87e4f);
    MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[6], 15, 0xa3014314);
    MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, x[4], 6, 0xf7537e82);
    MD5_II(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, x[2], 15, 0x2ad7d2bb);
    MD5_II(b, c, d, a, x[9], 21, 0xeb86d391);

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(MD5_CTX *ctx)
{
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void md5_update(MD5_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t i, index, partLen;

    index = (size_t)((ctx->count[0] >> 3) & 0x3F);

    if ((ctx->count[0] += ((uint32_t)len << 3)) < ((uint32_t)len << 3))
        ctx->count[1]++;
    ctx->count[1] += ((uint32_t)len >> 29);

    partLen = 64 - index;

    if (len >= partLen)
    {
        memcpy(&ctx->buffer[index], data, partLen);
        md5_transform(ctx->state, ctx->buffer);

        for (i = partLen; i + 63 < len; i += 64)
        {
            md5_transform(ctx->state, &data[i]);
        }
        index = 0;
    }
    else
    {
        i = 0;
    }

    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void md5_final(uint8_t digest[16], MD5_CTX *ctx)
{
    static const uint8_t padding[64] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                        0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t bits[8];
    size_t index, padLen;

    for (int i = 0; i < 4; i++)
    {
        bits[i] = (uint8_t)(ctx->count[0] >> (i * 8));
        bits[i + 4] = (uint8_t)(ctx->count[1] >> (i * 8));
    }

    index = (size_t)((ctx->count[0] >> 3) & 0x3f);
    padLen = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, padding, padLen);
    md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++)
    {
        digest[i] = (uint8_t)(ctx->state[0] >> (i * 8));
        digest[i + 4] = (uint8_t)(ctx->state[1] >> (i * 8));
        digest[i + 8] = (uint8_t)(ctx->state[2] >> (i * 8));
        digest[i + 12] = (uint8_t)(ctx->state[3] >> (i * 8));
    }
}

static void compute_md5(const uint8_t *data, size_t len, uint8_t digest[16])
{
    MD5_CTX ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(digest, &ctx);
}

//=============================================================================
// SHA1 Implementation (RFC 3174 / FIPS 180-1)
//=============================================================================

typedef struct
{
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

#define SHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64])
{
    uint32_t a, b, c, d, e;
    uint32_t w[80];

    for (int i = 0; i < 16; i++)
    {
        w[i] = ((uint32_t)buffer[i * 4] << 24) | ((uint32_t)buffer[i * 4 + 1] << 16) |
               ((uint32_t)buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++)
    {
        w[i] = SHA1_ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    for (int i = 0; i < 20; i++)
    {
        uint32_t temp = SHA1_ROL(a, 5) + ((b & c) | ((~b) & d)) + e + w[i] + 0x5A827999;
        e = d;
        d = c;
        c = SHA1_ROL(b, 30);
        b = a;
        a = temp;
    }
    for (int i = 20; i < 40; i++)
    {
        uint32_t temp = SHA1_ROL(a, 5) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1;
        e = d;
        d = c;
        c = SHA1_ROL(b, 30);
        b = a;
        a = temp;
    }
    for (int i = 40; i < 60; i++)
    {
        uint32_t temp = SHA1_ROL(a, 5) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDC;
        e = d;
        d = c;
        c = SHA1_ROL(b, 30);
        b = a;
        a = temp;
    }
    for (int i = 60; i < 80; i++)
    {
        uint32_t temp = SHA1_ROL(a, 5) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6;
        e = d;
        d = c;
        c = SHA1_ROL(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_init(SHA1_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t i, j;

    j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    if ((j + len) > 63)
    {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64)
        {
            sha1_transform(ctx->state, &data[i]);
        }
        j = 0;
    }
    else
    {
        i = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void sha1_final(uint8_t digest[20], SHA1_CTX *ctx)
{
    uint8_t finalcount[8];
    uint8_t c;

    for (int i = 0; i < 8; i++)
    {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }

    c = 0x80;
    sha1_update(ctx, &c, 1);
    while ((ctx->count[0] & 504) != 448)
    {
        c = 0x00;
        sha1_update(ctx, &c, 1);
    }
    sha1_update(ctx, finalcount, 8);

    for (int i = 0; i < 20; i++)
    {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

static void compute_sha1(const uint8_t *data, size_t len, uint8_t digest[20])
{
    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(digest, &ctx);
}

//=============================================================================
// SHA256 Implementation (RFC 6234 / FIPS 180-4)
//=============================================================================

typedef struct
{
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t buffer[64];
} SHA256_CTX;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ ((~(x)) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, m[64];

    for (int i = 0, j = 0; i < 16; ++i, j += 4)
    {
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    }
    for (int i = 16; i < 64; ++i)
    {
        m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; ++i)
    {
        t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + m[i];
        t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->bitcount = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        ctx->buffer[ctx->bitcount / 8 % 64] = data[i];
        ctx->bitcount += 8;
        if ((ctx->bitcount / 8 % 64) == 0)
        {
            sha256_transform(ctx, ctx->buffer);
        }
    }
}

static void sha256_final(uint8_t hash[32], SHA256_CTX *ctx)
{
    size_t i = ctx->bitcount / 8 % 64;

    ctx->buffer[i++] = 0x80;

    if (i > 56)
    {
        while (i < 64)
            ctx->buffer[i++] = 0x00;
        sha256_transform(ctx, ctx->buffer);
        i = 0;
    }

    while (i < 56)
        ctx->buffer[i++] = 0x00;

    for (int j = 7; j >= 0; --j)
    {
        ctx->buffer[i++] = (uint8_t)(ctx->bitcount >> (j * 8));
    }
    sha256_transform(ctx, ctx->buffer);

    for (int j = 0; j < 8; ++j)
    {
        hash[j * 4] = (ctx->state[j] >> 24) & 0xff;
        hash[j * 4 + 1] = (ctx->state[j] >> 16) & 0xff;
        hash[j * 4 + 2] = (ctx->state[j] >> 8) & 0xff;
        hash[j * 4 + 3] = ctx->state[j] & 0xff;
    }
}

static void compute_sha256(const uint8_t *data, size_t len, uint8_t hash[32])
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(hash, &ctx);
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Computes the MD5 hash of a string.
///
/// Calculates the 128-bit MD5 message digest (RFC 1321) and returns it as
/// a 32-character lowercase hexadecimal string.
///
/// **Security Warning:** MD5 is cryptographically broken. Collisions can be
/// generated in seconds on modern hardware. Do NOT use for:
/// - Password hashing
/// - Digital signatures
/// - Certificate verification
/// - Any security-critical application
///
/// **Acceptable uses:**
/// - File checksums (non-security)
/// - Content deduplication
/// - Cache key generation
/// - Legacy system compatibility
///
/// **Examples:**
/// ```
/// Hash.MD5("")        → "d41d8cd98f00b204e9800998ecf8427e"
/// Hash.MD5("Hello")   → "8b1a9953c4611296a827abf8c47804d7"
/// Hash.MD5("a")       → "0cc175b9c0f1b6a831c399e269772661"
/// ```
///
/// **Usage example:**
/// ```
/// Dim content = ReadFile("document.txt")
/// Dim checksum = Hash.MD5(content)
/// Print "MD5: " & checksum
/// ```
///
/// @param str The string to hash. NULL is treated as empty string.
///
/// @return A 32-character lowercase hex string representing the MD5 hash.
///
/// @note O(n) time complexity where n is input length.
/// @note Always returns exactly 32 characters.
///
/// @see rt_hash_sha256 For a secure alternative
/// @see rt_hash_md5_bytes For hashing Bytes objects
rt_string rt_hash_md5(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        cstr = "";

    uint8_t digest[16];
    compute_md5((const uint8_t *)cstr, strlen(cstr), digest);
    return rt_codec_hex_enc_bytes(digest, 16);
}

/// @brief Computes the MD5 hash of a Bytes object.
///
/// Calculates the 128-bit MD5 message digest of binary data and returns it
/// as a 32-character lowercase hexadecimal string. This function is useful
/// for hashing binary data that may contain null bytes.
///
/// **Security Warning:** MD5 is cryptographically broken. See rt_hash_md5
/// for details on appropriate use cases.
///
/// **Usage example:**
/// ```
/// Dim data = Bytes.New(256)
/// ' ... fill with binary data ...
/// Dim checksum = Hash.MD5Bytes(data)
/// Print "Binary MD5: " & checksum
/// ```
///
/// @param bytes A Bytes object containing the data to hash.
///              NULL is treated as empty input.
///
/// @return A 32-character lowercase hex string representing the MD5 hash.
///
/// @note O(n) time complexity where n is the byte array length.
/// @note Creates a temporary copy of the data for hashing.
///
/// @see rt_hash_md5 For hashing strings
/// @see rt_hash_sha256_bytes For a secure alternative
rt_string rt_hash_md5_bytes(void *bytes)
{
    if (!bytes)
    {
        uint8_t digest[16];
        compute_md5(NULL, 0, digest);
        return rt_codec_hex_enc_bytes(digest, 16);
    }

    int64_t len = rt_bytes_len(bytes);
    uint8_t digest[16];

    // Get raw data pointer from bytes object
    uint8_t *data = (uint8_t *)malloc((size_t)len);
    if (!data && len > 0)
        rt_trap("Hash: memory allocation failed");

    for (int64_t i = 0; i < len; i++)
    {
        data[i] = (uint8_t)rt_bytes_get(bytes, i);
    }

    compute_md5(data, (size_t)len, digest);
    free(data);
    return rt_codec_hex_enc_bytes(digest, 16);
}

/// @brief Computes the SHA-1 hash of a string.
///
/// Calculates the 160-bit SHA-1 message digest (RFC 3174 / FIPS 180-1) and
/// returns it as a 40-character lowercase hexadecimal string.
///
/// **Security Warning:** SHA-1 is cryptographically broken. Chosen-prefix
/// collisions have been demonstrated (SHAttered attack, 2017). Do NOT use for:
/// - Password hashing
/// - Digital signatures
/// - Certificate verification
/// - Any security-critical application
///
/// **Acceptable uses:**
/// - Git object identifiers (though Git is migrating to SHA-256)
/// - Legacy system compatibility
/// - Non-security checksums
///
/// **Examples:**
/// ```
/// Hash.SHA1("")        → "da39a3ee5e6b4b0d3255bfef95601890afd80709"
/// Hash.SHA1("Hello")   → "f7ff9e8b7bb2e09b70935a5d785e0cc5d9d0abf0"
/// Hash.SHA1("abc")     → "a9993e364706816aba3e25717850c26c9cd0d89d"
/// ```
///
/// **Usage example:**
/// ```
/// Dim content = ReadFile("source.c")
/// Dim hash = Hash.SHA1(content)
/// Print "SHA1: " & hash
/// ```
///
/// @param str The string to hash. NULL is treated as empty string.
///
/// @return A 40-character lowercase hex string representing the SHA-1 hash.
///
/// @note O(n) time complexity where n is input length.
/// @note Always returns exactly 40 characters.
///
/// @see rt_hash_sha256 For a secure alternative
/// @see rt_hash_sha1_bytes For hashing Bytes objects
rt_string rt_hash_sha1(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        cstr = "";

    uint8_t digest[20];
    compute_sha1((const uint8_t *)cstr, strlen(cstr), digest);
    return rt_codec_hex_enc_bytes(digest, 20);
}

/// @brief Computes the SHA-1 hash of a Bytes object.
///
/// Calculates the 160-bit SHA-1 message digest of binary data and returns it
/// as a 40-character lowercase hexadecimal string. This function is useful
/// for hashing binary data that may contain null bytes.
///
/// **Security Warning:** SHA-1 is cryptographically broken. See rt_hash_sha1
/// for details on appropriate use cases.
///
/// **Usage example:**
/// ```
/// Dim fileData = File.ReadAllBytes("image.png")
/// Dim hash = Hash.SHA1Bytes(fileData)
/// Print "Image SHA1: " & hash
/// ```
///
/// @param bytes A Bytes object containing the data to hash.
///              NULL is treated as empty input.
///
/// @return A 40-character lowercase hex string representing the SHA-1 hash.
///
/// @note O(n) time complexity where n is the byte array length.
/// @note Creates a temporary copy of the data for hashing.
///
/// @see rt_hash_sha1 For hashing strings
/// @see rt_hash_sha256_bytes For a secure alternative
rt_string rt_hash_sha1_bytes(void *bytes)
{
    if (!bytes)
    {
        uint8_t digest[20];
        compute_sha1(NULL, 0, digest);
        return rt_codec_hex_enc_bytes(digest, 20);
    }

    int64_t len = rt_bytes_len(bytes);
    uint8_t digest[20];

    uint8_t *data = (uint8_t *)malloc((size_t)len);
    if (!data && len > 0)
        rt_trap("Hash: memory allocation failed");

    for (int64_t i = 0; i < len; i++)
    {
        data[i] = (uint8_t)rt_bytes_get(bytes, i);
    }

    compute_sha1(data, (size_t)len, digest);
    free(data);
    return rt_codec_hex_enc_bytes(digest, 20);
}

/// @brief Computes the SHA-256 hash of a string.
///
/// Calculates the 256-bit SHA-256 message digest (RFC 6234 / FIPS 180-4) and
/// returns it as a 64-character lowercase hexadecimal string. SHA-256 is part
/// of the SHA-2 family and is currently considered cryptographically secure.
///
/// **This is the recommended hash function for security applications.**
///
/// **Use cases:**
/// - Password hashing (with proper salting and key stretching)
/// - Digital signatures
/// - Certificate verification
/// - Blockchain and cryptocurrency
/// - HMAC constructions
/// - Content integrity verification
/// - Secure token generation
///
/// **Examples:**
/// ```
/// Hash.SHA256("")      → "e3b0c44298fc1c149afbf4c8996fb924..."
/// Hash.SHA256("Hello") → "185f8db32271fe25f561a6fc938b2e26..."
/// Hash.SHA256("abc")   → "ba7816bf8f01cfea414140de5dae2223..."
/// ```
///
/// **Usage example:**
/// ```
/// ' Verify file integrity
/// Dim content = ReadFile("document.txt")
/// Dim computed = Hash.SHA256(content)
/// If computed = expectedHash Then
///     Print "File integrity verified"
/// Else
///     Print "WARNING: File has been modified!"
/// End If
/// ```
///
/// **Password hashing (simplified):**
/// ```
/// ' NOTE: For production, use proper key derivation (PBKDF2, bcrypt, etc.)
/// Dim salt = GenerateRandomBytes(16)
/// Dim saltedPassword = salt & password
/// Dim hash = Hash.SHA256(saltedPassword)
/// ```
///
/// @param str The string to hash. NULL is treated as empty string.
///
/// @return A 64-character lowercase hex string representing the SHA-256 hash.
///
/// @note O(n) time complexity where n is input length.
/// @note Always returns exactly 64 characters.
/// @note Suitable for all security applications.
///
/// @see rt_hash_sha256_bytes For hashing Bytes objects
/// @see rt_hash_md5 For legacy/checksum uses (NOT secure)
rt_string rt_hash_sha256(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        cstr = "";

    uint8_t hash[32];
    compute_sha256((const uint8_t *)cstr, strlen(cstr), hash);
    return rt_codec_hex_enc_bytes(hash, 32);
}

/// @brief Computes the SHA-256 hash of a Bytes object.
///
/// Calculates the 256-bit SHA-256 message digest of binary data and returns it
/// as a 64-character lowercase hexadecimal string. This is the recommended
/// function for securely hashing binary data.
///
/// **This is the recommended hash function for security applications
/// involving binary data.**
///
/// **Usage example:**
/// ```
/// ' Hash a binary file
/// Dim fileData = File.ReadAllBytes("document.pdf")
/// Dim hash = Hash.SHA256Bytes(fileData)
/// Print "SHA256: " & hash
///
/// ' Verify download integrity
/// Dim downloadedData = DownloadFile(url)
/// Dim computed = Hash.SHA256Bytes(downloadedData)
/// If computed = expectedChecksum Then
///     Print "Download verified successfully"
/// End If
/// ```
///
/// @param bytes A Bytes object containing the data to hash.
///              NULL is treated as empty input.
///
/// @return A 64-character lowercase hex string representing the SHA-256 hash.
///
/// @note O(n) time complexity where n is the byte array length.
/// @note Creates a temporary copy of the data for hashing.
/// @note Suitable for all security applications.
///
/// @see rt_hash_sha256 For hashing strings
/// @see rt_hash_md5_bytes For legacy/checksum uses (NOT secure)
rt_string rt_hash_sha256_bytes(void *bytes)
{
    if (!bytes)
    {
        uint8_t hash[32];
        compute_sha256(NULL, 0, hash);
        return rt_codec_hex_enc_bytes(hash, 32);
    }

    int64_t len = rt_bytes_len(bytes);
    uint8_t hash[32];

    uint8_t *data = (uint8_t *)malloc((size_t)len);
    if (!data && len > 0)
        rt_trap("Hash: memory allocation failed");

    for (int64_t i = 0; i < len; i++)
    {
        data[i] = (uint8_t)rt_bytes_get(bytes, i);
    }

    compute_sha256(data, (size_t)len, hash);
    free(data);
    return rt_codec_hex_enc_bytes(hash, 32);
}

/// @brief Computes the CRC32 checksum of a string.
///
/// Calculates the 32-bit CRC (Cyclic Redundancy Check) using the IEEE 802.3
/// polynomial (0xEDB88320, bit-reversed). Returns the checksum as an integer.
///
/// **Important:** CRC32 is NOT a cryptographic hash. It is designed for error
/// detection in data transmission, not for security. The same input always
/// produces the same output, but it is trivial to craft inputs that produce
/// a desired CRC32 value.
///
/// **Use cases:**
/// - File integrity checking (detect accidental corruption)
/// - Network packet error detection
/// - ZIP/GZIP file checksums
/// - Quick data comparison (fingerprinting)
/// - Hash table distribution (non-security)
///
/// **NOT suitable for:**
/// - Password hashing
/// - Digital signatures
/// - Any security application
/// - Collision resistance requirements
///
/// **Examples:**
/// ```
/// Hash.CRC32("")        → 0
/// Hash.CRC32("Hello")   → 4157704578
/// Hash.CRC32("123456789") → 3421780262 (standard test vector)
/// ```
///
/// **Usage example:**
/// ```
/// ' Quick file comparison
/// Dim content = ReadFile("data.bin")
/// Dim checksum = Hash.CRC32(content)
/// Print "CRC32: " & checksum
///
/// ' Verify integrity
/// If Hash.CRC32(receivedData) = expectedCRC Then
///     Print "Data received correctly"
/// Else
///     Print "Data corrupted during transfer"
/// End If
/// ```
///
/// @param str The string to compute CRC32 for. NULL is treated as empty string.
///
/// @return The 32-bit CRC32 checksum as an integer (0 to 4294967295).
///
/// @note O(n) time complexity where n is input length.
/// @note Very fast compared to cryptographic hashes.
/// @note Uses IEEE 802.3 polynomial (same as Ethernet, ZIP, PNG, etc.).
///
/// @see rt_hash_crc32_bytes For computing CRC32 of Bytes objects
/// @see rt_hash_sha256 For security-sensitive applications
int64_t rt_hash_crc32(rt_string str)
{
    const char *cstr = rt_string_cstr(str);
    if (!cstr)
        return (int64_t)rt_crc32_compute(NULL, 0);

    return (int64_t)rt_crc32_compute((const uint8_t *)cstr, strlen(cstr));
}

/// @brief Computes the CRC32 checksum of a Bytes object.
///
/// Calculates the 32-bit CRC (Cyclic Redundancy Check) of binary data using
/// the IEEE 802.3 polynomial. This is useful for binary data that may contain
/// null bytes.
///
/// **Important:** CRC32 is NOT a cryptographic hash. See rt_hash_crc32 for
/// details on appropriate use cases.
///
/// **Usage example:**
/// ```
/// ' Checksum binary file
/// Dim fileData = File.ReadAllBytes("archive.zip")
/// Dim crc = Hash.CRC32Bytes(fileData)
/// Print "File CRC32: " & crc
///
/// ' Validate network packet
/// Dim packet = ReceivePacket()
/// Dim payloadCRC = Hash.CRC32Bytes(packet.Payload)
/// If payloadCRC = packet.ExpectedCRC Then
///     ProcessPacket(packet)
/// Else
///     RequestRetransmit()
/// End If
/// ```
///
/// @param bytes A Bytes object containing the data to checksum.
///              NULL is treated as empty input.
///
/// @return The 32-bit CRC32 checksum as an integer (0 to 4294967295).
///
/// @note O(n) time complexity where n is the byte array length.
/// @note Very fast compared to cryptographic hashes.
/// @note Creates a temporary copy of the data for processing.
///
/// @see rt_hash_crc32 For computing CRC32 of strings
/// @see rt_hash_sha256_bytes For security-sensitive applications
int64_t rt_hash_crc32_bytes(void *bytes)
{
    if (!bytes)
        return (int64_t)rt_crc32_compute(NULL, 0);

    int64_t len = rt_bytes_len(bytes);

    uint8_t *data = (uint8_t *)malloc((size_t)len);
    if (!data && len > 0)
        rt_trap("Hash: memory allocation failed");

    for (int64_t i = 0; i < len; i++)
    {
        data[i] = (uint8_t)rt_bytes_get(bytes, i);
    }

    uint32_t result = rt_crc32_compute(data, (size_t)len);
    free(data);
    return (int64_t)result;
}

//=============================================================================
// HMAC Implementation (RFC 2104)
//=============================================================================

#define HMAC_BLOCK_SIZE 64 // Block size for MD5, SHA1, SHA256

/// @brief Helper to extract bytes from a Bytes object.
static uint8_t *extract_bytes_data(void *bytes, size_t *out_len)
{
    if (!bytes)
    {
        *out_len = 0;
        return NULL;
    }

    int64_t len = rt_bytes_len(bytes);
    *out_len = (size_t)len;

    if (len == 0)
        return NULL;

    uint8_t *data = (uint8_t *)malloc((size_t)len);
    if (!data)
        rt_trap("HMAC: memory allocation failed");

    for (int64_t i = 0; i < len; i++)
    {
        data[i] = (uint8_t)rt_bytes_get(bytes, i);
    }

    return data;
}

/// @brief Compute HMAC-MD5 with raw bytes.
static void hmac_md5_raw(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[16])
{
    uint8_t k_padded[HMAC_BLOCK_SIZE];
    uint8_t k_ipad[HMAC_BLOCK_SIZE];
    uint8_t k_opad[HMAC_BLOCK_SIZE];

    // If key is longer than block size, hash it first
    if (key_len > HMAC_BLOCK_SIZE)
    {
        compute_md5(key, key_len, k_padded);
        memset(k_padded + 16, 0, HMAC_BLOCK_SIZE - 16);
    }
    else
    {
        memcpy(k_padded, key, key_len);
        memset(k_padded + key_len, 0, HMAC_BLOCK_SIZE - key_len);
    }

    // XOR key with ipad and opad
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++)
    {
        k_ipad[i] = k_padded[i] ^ 0x36;
        k_opad[i] = k_padded[i] ^ 0x5c;
    }

    // Inner hash: H(K xor ipad || data)
    uint8_t inner_hash[16];
    size_t inner_len = HMAC_BLOCK_SIZE + data_len;
    uint8_t *inner_data = (uint8_t *)malloc(inner_len);
    if (!inner_data)
        rt_trap("HMAC: memory allocation failed");

    memcpy(inner_data, k_ipad, HMAC_BLOCK_SIZE);
    if (data_len > 0)
        memcpy(inner_data + HMAC_BLOCK_SIZE, data, data_len);
    compute_md5(inner_data, inner_len, inner_hash);
    free(inner_data);

    // Outer hash: H(K xor opad || inner_hash)
    uint8_t outer_data[HMAC_BLOCK_SIZE + 16];
    memcpy(outer_data, k_opad, HMAC_BLOCK_SIZE);
    memcpy(outer_data + HMAC_BLOCK_SIZE, inner_hash, 16);
    compute_md5(outer_data, HMAC_BLOCK_SIZE + 16, out);
}

/// @brief Compute HMAC-SHA1 with raw bytes.
static void hmac_sha1_raw(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[20])
{
    uint8_t k_padded[HMAC_BLOCK_SIZE];
    uint8_t k_ipad[HMAC_BLOCK_SIZE];
    uint8_t k_opad[HMAC_BLOCK_SIZE];

    // If key is longer than block size, hash it first
    if (key_len > HMAC_BLOCK_SIZE)
    {
        compute_sha1(key, key_len, k_padded);
        memset(k_padded + 20, 0, HMAC_BLOCK_SIZE - 20);
    }
    else
    {
        memcpy(k_padded, key, key_len);
        memset(k_padded + key_len, 0, HMAC_BLOCK_SIZE - key_len);
    }

    // XOR key with ipad and opad
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++)
    {
        k_ipad[i] = k_padded[i] ^ 0x36;
        k_opad[i] = k_padded[i] ^ 0x5c;
    }

    // Inner hash: H(K xor ipad || data)
    uint8_t inner_hash[20];
    size_t inner_len = HMAC_BLOCK_SIZE + data_len;
    uint8_t *inner_data = (uint8_t *)malloc(inner_len);
    if (!inner_data)
        rt_trap("HMAC: memory allocation failed");

    memcpy(inner_data, k_ipad, HMAC_BLOCK_SIZE);
    if (data_len > 0)
        memcpy(inner_data + HMAC_BLOCK_SIZE, data, data_len);
    compute_sha1(inner_data, inner_len, inner_hash);
    free(inner_data);

    // Outer hash: H(K xor opad || inner_hash)
    uint8_t outer_data[HMAC_BLOCK_SIZE + 20];
    memcpy(outer_data, k_opad, HMAC_BLOCK_SIZE);
    memcpy(outer_data + HMAC_BLOCK_SIZE, inner_hash, 20);
    compute_sha1(outer_data, HMAC_BLOCK_SIZE + 20, out);
}

/// @brief Compute HMAC-SHA256 with raw bytes (exported for PBKDF2).
void rt_hash_hmac_sha256_raw(
    const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32])
{
    uint8_t k_padded[HMAC_BLOCK_SIZE];
    uint8_t k_ipad[HMAC_BLOCK_SIZE];
    uint8_t k_opad[HMAC_BLOCK_SIZE];

    // If key is longer than block size, hash it first
    if (key_len > HMAC_BLOCK_SIZE)
    {
        compute_sha256(key, key_len, k_padded);
        memset(k_padded + 32, 0, HMAC_BLOCK_SIZE - 32);
    }
    else
    {
        memcpy(k_padded, key, key_len);
        memset(k_padded + key_len, 0, HMAC_BLOCK_SIZE - key_len);
    }

    // XOR key with ipad and opad
    for (int i = 0; i < HMAC_BLOCK_SIZE; i++)
    {
        k_ipad[i] = k_padded[i] ^ 0x36;
        k_opad[i] = k_padded[i] ^ 0x5c;
    }

    // Inner hash: H(K xor ipad || data)
    uint8_t inner_hash[32];
    size_t inner_len = HMAC_BLOCK_SIZE + data_len;
    uint8_t *inner_data = (uint8_t *)malloc(inner_len);
    if (!inner_data)
        rt_trap("HMAC: memory allocation failed");

    memcpy(inner_data, k_ipad, HMAC_BLOCK_SIZE);
    if (data_len > 0)
        memcpy(inner_data + HMAC_BLOCK_SIZE, data, data_len);
    compute_sha256(inner_data, inner_len, inner_hash);
    free(inner_data);

    // Outer hash: H(K xor opad || inner_hash)
    uint8_t outer_data[HMAC_BLOCK_SIZE + 32];
    memcpy(outer_data, k_opad, HMAC_BLOCK_SIZE);
    memcpy(outer_data + HMAC_BLOCK_SIZE, inner_hash, 32);
    compute_sha256(outer_data, HMAC_BLOCK_SIZE + 32, out);
}

//=============================================================================
// HMAC Public API
//=============================================================================

/// @brief Compute HMAC-MD5 of string data with string key.
rt_string rt_hash_hmac_md5(rt_string key, rt_string data)
{
    const char *key_cstr = rt_string_cstr(key);
    const char *data_cstr = rt_string_cstr(data);
    if (!key_cstr)
        key_cstr = "";
    if (!data_cstr)
        data_cstr = "";

    uint8_t digest[16];
    hmac_md5_raw((const uint8_t *)key_cstr,
                 strlen(key_cstr),
                 (const uint8_t *)data_cstr,
                 strlen(data_cstr),
                 digest);
    return rt_codec_hex_enc_bytes(digest, 16);
}

/// @brief Compute HMAC-MD5 of Bytes data with Bytes key.
rt_string rt_hash_hmac_md5_bytes(void *key, void *data)
{
    size_t key_len, data_len;
    uint8_t *key_data = extract_bytes_data(key, &key_len);
    uint8_t *msg_data = extract_bytes_data(data, &data_len);

    uint8_t digest[16];
    hmac_md5_raw(key_data ? key_data : (const uint8_t *)"",
                 key_len,
                 msg_data ? msg_data : (const uint8_t *)"",
                 data_len,
                 digest);

    if (key_data)
        free(key_data);
    if (msg_data)
        free(msg_data);

    return rt_codec_hex_enc_bytes(digest, 16);
}

/// @brief Compute HMAC-SHA1 of string data with string key.
rt_string rt_hash_hmac_sha1(rt_string key, rt_string data)
{
    const char *key_cstr = rt_string_cstr(key);
    const char *data_cstr = rt_string_cstr(data);
    if (!key_cstr)
        key_cstr = "";
    if (!data_cstr)
        data_cstr = "";

    uint8_t digest[20];
    hmac_sha1_raw((const uint8_t *)key_cstr,
                  strlen(key_cstr),
                  (const uint8_t *)data_cstr,
                  strlen(data_cstr),
                  digest);
    return rt_codec_hex_enc_bytes(digest, 20);
}

/// @brief Compute HMAC-SHA1 of Bytes data with Bytes key.
rt_string rt_hash_hmac_sha1_bytes(void *key, void *data)
{
    size_t key_len, data_len;
    uint8_t *key_data = extract_bytes_data(key, &key_len);
    uint8_t *msg_data = extract_bytes_data(data, &data_len);

    uint8_t digest[20];
    hmac_sha1_raw(key_data ? key_data : (const uint8_t *)"",
                  key_len,
                  msg_data ? msg_data : (const uint8_t *)"",
                  data_len,
                  digest);

    if (key_data)
        free(key_data);
    if (msg_data)
        free(msg_data);

    return rt_codec_hex_enc_bytes(digest, 20);
}

/// @brief Compute HMAC-SHA256 of string data with string key.
rt_string rt_hash_hmac_sha256(rt_string key, rt_string data)
{
    const char *key_cstr = rt_string_cstr(key);
    const char *data_cstr = rt_string_cstr(data);
    if (!key_cstr)
        key_cstr = "";
    if (!data_cstr)
        data_cstr = "";

    uint8_t digest[32];
    rt_hash_hmac_sha256_raw((const uint8_t *)key_cstr,
                            strlen(key_cstr),
                            (const uint8_t *)data_cstr,
                            strlen(data_cstr),
                            digest);
    return rt_codec_hex_enc_bytes(digest, 32);
}

/// @brief Compute HMAC-SHA256 of Bytes data with Bytes key.
rt_string rt_hash_hmac_sha256_bytes(void *key, void *data)
{
    size_t key_len, data_len;
    uint8_t *key_data = extract_bytes_data(key, &key_len);
    uint8_t *msg_data = extract_bytes_data(data, &data_len);

    uint8_t digest[32];
    rt_hash_hmac_sha256_raw(key_data ? key_data : (const uint8_t *)"",
                            key_len,
                            msg_data ? msg_data : (const uint8_t *)"",
                            data_len,
                            digest);

    if (key_data)
        free(key_data);
    if (msg_data)
        free(msg_data);

    return rt_codec_hex_enc_bytes(digest, 32);
}
