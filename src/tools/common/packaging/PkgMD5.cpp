//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgMD5.cpp
// Purpose: Self-contained MD5 digest (RFC 1321) for the packaging library.
//          Ported from src/runtime/text/rt_hash.c with all GC deps removed.
//
// Key invariants:
//   - No runtime (viper_rt_*) dependencies.
//   - Little-endian byte ordering for block processing.
//
// Ownership/Lifetime:
//   - No allocations beyond stack; digest written to caller buffer.
//
// Links: PkgMD5.hpp, src/runtime/text/rt_hash.c
//
//===----------------------------------------------------------------------===//

#include "PkgMD5.hpp"

#include <cstring>

namespace viper::pkg
{
namespace
{

struct MD5Context
{
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
};

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))
#define MD5_ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_F((b), (c), (d)) + (x) + static_cast<uint32_t>(ac);                             \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }
#define MD5_GG(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_G((b), (c), (d)) + (x) + static_cast<uint32_t>(ac);                             \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }
#define MD5_HH(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_H((b), (c), (d)) + (x) + static_cast<uint32_t>(ac);                             \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }
#define MD5_II(a, b, c, d, x, s, ac)                                                               \
    {                                                                                              \
        (a) += MD5_I((b), (c), (d)) + (x) + static_cast<uint32_t>(ac);                             \
        (a) = MD5_ROTATE_LEFT((a), (s));                                                           \
        (a) += (b);                                                                                \
    }

void md5Transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];

    for (int i = 0; i < 16; i++)
    {
        x[i] = static_cast<uint32_t>(block[i * 4]) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
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

void md5Init(MD5Context &ctx)
{
    ctx.count[0] = ctx.count[1] = 0;
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xefcdab89;
    ctx.state[2] = 0x98badcfe;
    ctx.state[3] = 0x10325476;
}

void md5Update(MD5Context &ctx, const uint8_t *data, size_t len)
{
    size_t index = static_cast<size_t>((ctx.count[0] >> 3) & 0x3F);

    if ((ctx.count[0] += (static_cast<uint32_t>(len) << 3)) < (static_cast<uint32_t>(len) << 3))
        ctx.count[1]++;
    ctx.count[1] += static_cast<uint32_t>(len >> 29);

    size_t partLen = 64 - index;
    size_t i;

    if (len >= partLen)
    {
        std::memcpy(&ctx.buffer[index], data, partLen);
        md5Transform(ctx.state, ctx.buffer);

        for (i = partLen; i + 63 < len; i += 64)
            md5Transform(ctx.state, &data[i]);
        index = 0;
    }
    else
    {
        i = 0;
    }

    std::memcpy(&ctx.buffer[index], &data[i], len - i);
}

void md5Final(uint8_t digest[16], MD5Context &ctx)
{
    static const uint8_t padding[64] = {0x80};
    uint8_t bits[8];

    for (int i = 0; i < 4; i++)
    {
        bits[i] = static_cast<uint8_t>(ctx.count[0] >> (i * 8));
        bits[i + 4] = static_cast<uint8_t>(ctx.count[1] >> (i * 8));
    }

    size_t index = static_cast<size_t>((ctx.count[0] >> 3) & 0x3f);
    size_t padLen = (index < 56) ? (56 - index) : (120 - index);
    md5Update(ctx, padding, padLen);
    md5Update(ctx, bits, 8);

    for (int i = 0; i < 4; i++)
    {
        digest[i] = static_cast<uint8_t>(ctx.state[0] >> (i * 8));
        digest[i + 4] = static_cast<uint8_t>(ctx.state[1] >> (i * 8));
        digest[i + 8] = static_cast<uint8_t>(ctx.state[2] >> (i * 8));
        digest[i + 12] = static_cast<uint8_t>(ctx.state[3] >> (i * 8));
    }
}

} // namespace

void md5(const uint8_t *data, size_t len, uint8_t digest[16])
{
    MD5Context ctx{};
    md5Init(ctx);
    md5Update(ctx, data, len);
    md5Final(digest, ctx);
}

std::string md5hex(const uint8_t *data, size_t len)
{
    uint8_t digest[16];
    md5(data, len, digest);

    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (int i = 0; i < 16; i++)
    {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 0x0f]);
    }
    return result;
}

} // namespace viper::pkg
