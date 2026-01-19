//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
/// @file rt_crypto.c
/// @brief Cryptographic primitives for TLS support.
///
/// Implements SHA-256, HMAC-SHA256, HKDF, ChaCha20-Poly1305 AEAD, and X25519.
/// All implementations are self-contained with no external dependencies.
//
//===----------------------------------------------------------------------===//

#include "rt_crypto.h"

#include <string.h>

//=============================================================================
// SHA-256
//=============================================================================

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTR32(x, 2) ^ ROTR32(x, 13) ^ ROTR32(x, 22))
#define EP1(x) (ROTR32(x, 6) ^ ROTR32(x, 11) ^ ROTR32(x, 25))
#define SIG0(x) (ROTR32(x, 7) ^ ROTR32(x, 18) ^ ((x) >> 3))
#define SIG1(x) (ROTR32(x, 17) ^ ROTR32(x, 19) ^ ((x) >> 10))

typedef struct
{
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx;

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[64])
{
    uint32_t a, b, c, d, e, f, g, h, t1, t2, w[64];

    for (int i = 0; i < 16; i++)
    {
        w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) | data[i * 4 + 3];
    }
    for (int i = 16; i < 64; i++)
    {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; i++)
    {
        t1 = h + EP1(e) + CH(e, f, g) + sha256_k[i] + w[i];
        t2 = EP0(a) + MAJ(a, b, c);
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

static void sha256_init(sha256_ctx *ctx)
{
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update(sha256_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;
    size_t idx = (ctx->count / 8) % 64;

    ctx->count += len * 8;

    while (len > 0)
    {
        size_t copy = 64 - idx;
        if (copy > len)
            copy = len;
        memcpy(ctx->buffer + idx, ptr, copy);
        idx += copy;
        ptr += copy;
        len -= copy;

        if (idx == 64)
        {
            sha256_transform(ctx, ctx->buffer);
            idx = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t digest[32])
{
    uint64_t bits = ctx->count;
    uint8_t pad[64];
    size_t idx = (ctx->count / 8) % 64;
    size_t padlen = (idx < 56) ? (56 - idx) : (120 - idx);

    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    sha256_update(ctx, pad, padlen);

    pad[0] = (bits >> 56) & 0xFF;
    pad[1] = (bits >> 48) & 0xFF;
    pad[2] = (bits >> 40) & 0xFF;
    pad[3] = (bits >> 32) & 0xFF;
    pad[4] = (bits >> 24) & 0xFF;
    pad[5] = (bits >> 16) & 0xFF;
    pad[6] = (bits >> 8) & 0xFF;
    pad[7] = bits & 0xFF;
    sha256_update(ctx, pad, 8);

    for (int i = 0; i < 8; i++)
    {
        digest[i * 4] = (ctx->state[i] >> 24) & 0xFF;
        digest[i * 4 + 1] = (ctx->state[i] >> 16) & 0xFF;
        digest[i * 4 + 2] = (ctx->state[i] >> 8) & 0xFF;
        digest[i * 4 + 3] = ctx->state[i] & 0xFF;
    }
}

void rt_sha256(const void *data, size_t len, uint8_t digest[32])
{
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

//=============================================================================
// HMAC-SHA256
//=============================================================================

void rt_hmac_sha256(
    const uint8_t *key, size_t key_len, const void *data, size_t data_len, uint8_t mac[32])
{
    uint8_t k[64], ipad[64], opad[64];

    if (key_len > 64)
    {
        rt_sha256(key, key_len, k);
        key_len = 32;
    }
    else
    {
        memcpy(k, key, key_len);
    }
    memset(k + key_len, 0, 64 - key_len);

    for (int i = 0; i < 64; i++)
    {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, mac);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64);
    sha256_update(&ctx, mac, 32);
    sha256_final(&ctx, mac);
}

//=============================================================================
// HKDF-SHA256 (RFC 5869)
//=============================================================================

void rt_hkdf_extract(
    const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len, uint8_t prk[32])
{
    if (salt == NULL || salt_len == 0)
    {
        uint8_t zero_salt[32] = {0};
        rt_hmac_sha256(zero_salt, 32, ikm, ikm_len, prk);
    }
    else
    {
        rt_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    }
}

void rt_hkdf_expand(
    const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *okm, size_t okm_len)
{
    uint8_t t[32] = {0};
    size_t t_len = 0;
    uint8_t counter = 1;
    size_t pos = 0;

    while (pos < okm_len)
    {
        sha256_ctx ctx;
        uint8_t temp[32];

        // HMAC(PRK, T(n-1) || info || counter)
        uint8_t k[64], ipad[64], opad[64];
        memcpy(k, prk, 32);
        memset(k + 32, 0, 32);

        for (int i = 0; i < 64; i++)
        {
            ipad[i] = k[i] ^ 0x36;
            opad[i] = k[i] ^ 0x5c;
        }

        sha256_init(&ctx);
        sha256_update(&ctx, ipad, 64);
        if (t_len > 0)
            sha256_update(&ctx, t, t_len);
        if (info_len > 0)
            sha256_update(&ctx, info, info_len);
        sha256_update(&ctx, &counter, 1);
        sha256_final(&ctx, temp);

        sha256_init(&ctx);
        sha256_update(&ctx, opad, 64);
        sha256_update(&ctx, temp, 32);
        sha256_final(&ctx, t);
        t_len = 32;

        size_t copy = okm_len - pos;
        if (copy > 32)
            copy = 32;
        memcpy(okm + pos, t, copy);
        pos += copy;
        counter++;
    }
}

void rt_hkdf_expand_label(const uint8_t secret[32],
                          const char *label,
                          const uint8_t *context,
                          size_t context_len,
                          uint8_t *out,
                          size_t out_len)
{
    // TLS 1.3 HkdfLabel structure
    uint8_t hkdf_label[512];
    size_t pos = 0;

    // Length (2 bytes, big-endian)
    hkdf_label[pos++] = (out_len >> 8) & 0xFF;
    hkdf_label[pos++] = out_len & 0xFF;

    // Label = "tls13 " + label
    const char *prefix = "tls13 ";
    size_t prefix_len = 6;
    size_t label_len = strlen(label);
    hkdf_label[pos++] = (uint8_t)(prefix_len + label_len);
    memcpy(hkdf_label + pos, prefix, prefix_len);
    pos += prefix_len;
    memcpy(hkdf_label + pos, label, label_len);
    pos += label_len;

    // Context
    hkdf_label[pos++] = (uint8_t)context_len;
    if (context_len > 0)
    {
        memcpy(hkdf_label + pos, context, context_len);
        pos += context_len;
    }

    rt_hkdf_expand(secret, hkdf_label, pos, out, out_len);
}

//=============================================================================
// ChaCha20
//=============================================================================

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define QUARTERROUND(a, b, c, d)                                                                   \
    do                                                                                             \
    {                                                                                              \
        a += b;                                                                                    \
        d ^= a;                                                                                    \
        d = ROTL32(d, 16);                                                                         \
        c += d;                                                                                    \
        b ^= c;                                                                                    \
        b = ROTL32(b, 12);                                                                         \
        a += b;                                                                                    \
        d ^= a;                                                                                    \
        d = ROTL32(d, 8);                                                                          \
        c += d;                                                                                    \
        b ^= c;                                                                                    \
        b = ROTL32(b, 7);                                                                          \
    } while (0)

static void chacha20_block(const uint32_t state[16], uint8_t out[64])
{
    uint32_t x[16];
    memcpy(x, state, 64);

    for (int i = 0; i < 10; i++)
    {
        // Column rounds
        QUARTERROUND(x[0], x[4], x[8], x[12]);
        QUARTERROUND(x[1], x[5], x[9], x[13]);
        QUARTERROUND(x[2], x[6], x[10], x[14]);
        QUARTERROUND(x[3], x[7], x[11], x[15]);
        // Diagonal rounds
        QUARTERROUND(x[0], x[5], x[10], x[15]);
        QUARTERROUND(x[1], x[6], x[11], x[12]);
        QUARTERROUND(x[2], x[7], x[8], x[13]);
        QUARTERROUND(x[3], x[4], x[9], x[14]);
    }

    for (int i = 0; i < 16; i++)
        x[i] += state[i];

    // Output little-endian
    for (int i = 0; i < 16; i++)
    {
        out[i * 4 + 0] = x[i] & 0xFF;
        out[i * 4 + 1] = (x[i] >> 8) & 0xFF;
        out[i * 4 + 2] = (x[i] >> 16) & 0xFF;
        out[i * 4 + 3] = (x[i] >> 24) & 0xFF;
    }
}

static void chacha20_init(uint32_t state[16],
                          const uint8_t key[32],
                          const uint8_t nonce[12],
                          uint32_t counter)
{
    // "expand 32-byte k"
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;

    // Key (little-endian)
    for (int i = 0; i < 8; i++)
    {
        state[4 + i] = ((uint32_t)key[i * 4 + 0]) | ((uint32_t)key[i * 4 + 1] << 8) |
                       ((uint32_t)key[i * 4 + 2] << 16) | ((uint32_t)key[i * 4 + 3] << 24);
    }

    // Counter
    state[12] = counter;

    // Nonce (little-endian)
    for (int i = 0; i < 3; i++)
    {
        state[13 + i] = ((uint32_t)nonce[i * 4 + 0]) | ((uint32_t)nonce[i * 4 + 1] << 8) |
                        ((uint32_t)nonce[i * 4 + 2] << 16) | ((uint32_t)nonce[i * 4 + 3] << 24);
    }
}

static void chacha20_crypt(const uint8_t key[32],
                           const uint8_t nonce[12],
                           uint32_t counter,
                           const uint8_t *in,
                           uint8_t *out,
                           size_t len)
{
    uint32_t state[16];
    uint8_t keystream[64];

    chacha20_init(state, key, nonce, counter);

    while (len > 0)
    {
        chacha20_block(state, keystream);
        size_t use = len > 64 ? 64 : len;
        for (size_t i = 0; i < use; i++)
            out[i] = in[i] ^ keystream[i];
        in += use;
        out += use;
        len -= use;
        state[12]++; // Increment counter
    }
}

//=============================================================================
// Poly1305
//=============================================================================

typedef struct
{
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t buffer[16];
    size_t buffer_len;
} poly1305_ctx;

static void poly1305_init(poly1305_ctx *ctx, const uint8_t key[32])
{
    // r (first 16 bytes, clamped)
    ctx->r[0] = ((uint32_t)key[0] | ((uint32_t)key[1] << 8) | ((uint32_t)key[2] << 16) |
                 ((uint32_t)key[3] << 24)) &
                0x0FFFFFFF;
    ctx->r[1] = (((uint32_t)key[3] >> 2) | ((uint32_t)key[4] << 6) | ((uint32_t)key[5] << 14) |
                 ((uint32_t)key[6] << 22)) &
                0x0FFFFFFC;
    ctx->r[2] = (((uint32_t)key[6] >> 4) | ((uint32_t)key[7] << 4) | ((uint32_t)key[8] << 12) |
                 ((uint32_t)key[9] << 20)) &
                0x0FFFFFFC;
    ctx->r[3] = (((uint32_t)key[9] >> 6) | ((uint32_t)key[10] << 2) | ((uint32_t)key[11] << 10) |
                 ((uint32_t)key[12] << 18)) &
                0x0FFFFFFC;
    ctx->r[4] = (((uint32_t)key[12] >> 8) | ((uint32_t)key[13]) | ((uint32_t)key[14] << 8) |
                 ((uint32_t)key[15] << 16)) &
                0x00FFFFFC;

    // h = 0
    ctx->h[0] = ctx->h[1] = ctx->h[2] = ctx->h[3] = ctx->h[4] = 0;

    // pad (last 16 bytes)
    ctx->pad[0] = (uint32_t)key[16] | ((uint32_t)key[17] << 8) | ((uint32_t)key[18] << 16) |
                  ((uint32_t)key[19] << 24);
    ctx->pad[1] = (uint32_t)key[20] | ((uint32_t)key[21] << 8) | ((uint32_t)key[22] << 16) |
                  ((uint32_t)key[23] << 24);
    ctx->pad[2] = (uint32_t)key[24] | ((uint32_t)key[25] << 8) | ((uint32_t)key[26] << 16) |
                  ((uint32_t)key[27] << 24);
    ctx->pad[3] = (uint32_t)key[28] | ((uint32_t)key[29] << 8) | ((uint32_t)key[30] << 16) |
                  ((uint32_t)key[31] << 24);

    ctx->buffer_len = 0;
}

static void poly1305_blocks(poly1305_ctx *ctx, const uint8_t *data, size_t len, int final)
{
    uint32_t r0 = ctx->r[0], r1 = ctx->r[1], r2 = ctx->r[2], r3 = ctx->r[3], r4 = ctx->r[4];
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];
    uint32_t hibit = final ? 0 : (1 << 24);

    while (len >= 16)
    {
        // h += m[i]
        h0 += ((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
               ((uint32_t)data[3] << 24)) &
              0x3ffffff;
        h1 += (((uint32_t)data[3] >> 2) | ((uint32_t)data[4] << 6) | ((uint32_t)data[5] << 14) |
               ((uint32_t)data[6] << 22)) &
              0x3ffffff;
        h2 += (((uint32_t)data[6] >> 4) | ((uint32_t)data[7] << 4) | ((uint32_t)data[8] << 12) |
               ((uint32_t)data[9] << 20)) &
              0x3ffffff;
        h3 += (((uint32_t)data[9] >> 6) | ((uint32_t)data[10] << 2) | ((uint32_t)data[11] << 10) |
               ((uint32_t)data[12] << 18)) &
              0x3ffffff;
        h4 += (((uint32_t)data[12] >> 8) | ((uint32_t)data[13]) | ((uint32_t)data[14] << 8) |
               ((uint32_t)data[15] << 16)) |
              hibit;

        // h *= r
        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 +
                      (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 +
                      (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 +
                      (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 +
                      (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 +
                      (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        // Carry
        uint32_t c;
        c = (uint32_t)(d0 >> 26);
        h0 = (uint32_t)d0 & 0x3ffffff;
        d1 += c;
        c = (uint32_t)(d1 >> 26);
        h1 = (uint32_t)d1 & 0x3ffffff;
        d2 += c;
        c = (uint32_t)(d2 >> 26);
        h2 = (uint32_t)d2 & 0x3ffffff;
        d3 += c;
        c = (uint32_t)(d3 >> 26);
        h3 = (uint32_t)d3 & 0x3ffffff;
        d4 += c;
        c = (uint32_t)(d4 >> 26);
        h4 = (uint32_t)d4 & 0x3ffffff;
        h0 += c * 5;
        c = h0 >> 26;
        h0 &= 0x3ffffff;
        h1 += c;

        data += 16;
        len -= 16;
    }

    ctx->h[0] = h0;
    ctx->h[1] = h1;
    ctx->h[2] = h2;
    ctx->h[3] = h3;
    ctx->h[4] = h4;
}

static void poly1305_update(poly1305_ctx *ctx, const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;

    if (ctx->buffer_len > 0)
    {
        size_t need = 16 - ctx->buffer_len;
        if (len < need)
        {
            memcpy(ctx->buffer + ctx->buffer_len, ptr, len);
            ctx->buffer_len += len;
            return;
        }
        memcpy(ctx->buffer + ctx->buffer_len, ptr, need);
        poly1305_blocks(ctx, ctx->buffer, 16, 0);
        ptr += need;
        len -= need;
        ctx->buffer_len = 0;
    }

    if (len >= 16)
    {
        size_t blocks = len & ~(size_t)15;
        poly1305_blocks(ctx, ptr, blocks, 0);
        ptr += blocks;
        len -= blocks;
    }

    if (len > 0)
    {
        memcpy(ctx->buffer, ptr, len);
        ctx->buffer_len = len;
    }
}

static void poly1305_final(poly1305_ctx *ctx, uint8_t tag[16])
{
    // Process remaining bytes
    if (ctx->buffer_len > 0)
    {
        ctx->buffer[ctx->buffer_len++] = 1;
        while (ctx->buffer_len < 16)
            ctx->buffer[ctx->buffer_len++] = 0;
        poly1305_blocks(ctx, ctx->buffer, 16, 1);
    }

    // Freeze h
    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];

    uint32_t c = h1 >> 26;
    h1 &= 0x3ffffff;
    h2 += c;
    c = h2 >> 26;
    h2 &= 0x3ffffff;
    h3 += c;
    c = h3 >> 26;
    h3 &= 0x3ffffff;
    h4 += c;
    c = h4 >> 26;
    h4 &= 0x3ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += c;

    // Compute h - p
    uint32_t g0 = h0 + 5;
    c = g0 >> 26;
    g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c;
    c = g1 >> 26;
    g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c;
    c = g2 >> 26;
    g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c;
    c = g3 >> 26;
    g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1 << 26);

    // Select h or h-p based on carry
    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask;
    g1 &= mask;
    g2 &= mask;
    g3 &= mask;
    g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    // h = h + pad
    uint64_t f;
    f = (uint64_t)h0 + (h1 << 26) + ctx->pad[0];
    tag[0] = f & 0xFF;
    tag[1] = (f >> 8) & 0xFF;
    tag[2] = (f >> 16) & 0xFF;
    tag[3] = (f >> 24) & 0xFF;
    f = (f >> 32) + (uint64_t)(h1 >> 6) + (h2 << 20) + ctx->pad[1];
    tag[4] = f & 0xFF;
    tag[5] = (f >> 8) & 0xFF;
    tag[6] = (f >> 16) & 0xFF;
    tag[7] = (f >> 24) & 0xFF;
    f = (f >> 32) + (uint64_t)(h2 >> 12) + (h3 << 14) + ctx->pad[2];
    tag[8] = f & 0xFF;
    tag[9] = (f >> 8) & 0xFF;
    tag[10] = (f >> 16) & 0xFF;
    tag[11] = (f >> 24) & 0xFF;
    f = (f >> 32) + (uint64_t)(h3 >> 18) + (h4 << 8) + ctx->pad[3];
    tag[12] = f & 0xFF;
    tag[13] = (f >> 8) & 0xFF;
    tag[14] = (f >> 16) & 0xFF;
    tag[15] = (f >> 24) & 0xFF;
}

//=============================================================================
// ChaCha20-Poly1305 AEAD
//=============================================================================

static void pad16(poly1305_ctx *ctx, size_t len)
{
    size_t pad = (16 - (len & 15)) & 15;
    uint8_t zeros[16] = {0};
    if (pad > 0)
        poly1305_update(ctx, zeros, pad);
}

size_t rt_chacha20_poly1305_encrypt(const uint8_t key[32],
                                    const uint8_t nonce[12],
                                    const void *aad,
                                    size_t aad_len,
                                    const void *plaintext,
                                    size_t plaintext_len,
                                    uint8_t *ciphertext)
{
    // Generate Poly1305 key (block 0)
    uint8_t poly_key[64];
    uint8_t zeros[64] = {0};
    chacha20_crypt(key, nonce, 0, zeros, poly_key, 64);

    // Encrypt plaintext (starting at block 1)
    chacha20_crypt(key, nonce, 1, (const uint8_t *)plaintext, ciphertext, plaintext_len);

    // Compute tag
    poly1305_ctx poly;
    poly1305_init(&poly, poly_key);
    poly1305_update(&poly, aad, aad_len);
    pad16(&poly, aad_len);
    poly1305_update(&poly, ciphertext, plaintext_len);
    pad16(&poly, plaintext_len);

    // Lengths (little-endian)
    uint8_t lens[16];
    lens[0] = aad_len & 0xFF;
    lens[1] = (aad_len >> 8) & 0xFF;
    lens[2] = (aad_len >> 16) & 0xFF;
    lens[3] = (aad_len >> 24) & 0xFF;
    lens[4] = lens[5] = lens[6] = lens[7] = 0;
    lens[8] = plaintext_len & 0xFF;
    lens[9] = (plaintext_len >> 8) & 0xFF;
    lens[10] = (plaintext_len >> 16) & 0xFF;
    lens[11] = (plaintext_len >> 24) & 0xFF;
    lens[12] = lens[13] = lens[14] = lens[15] = 0;
    poly1305_update(&poly, lens, 16);

    poly1305_final(&poly, ciphertext + plaintext_len);

    return plaintext_len + 16;
}

long rt_chacha20_poly1305_decrypt(const uint8_t key[32],
                                  const uint8_t nonce[12],
                                  const void *aad,
                                  size_t aad_len,
                                  const void *ciphertext,
                                  size_t ciphertext_len,
                                  uint8_t *plaintext)
{
    if (ciphertext_len < 16)
        return -1;

    size_t data_len = ciphertext_len - 16;
    const uint8_t *tag = (const uint8_t *)ciphertext + data_len;

    // Generate Poly1305 key
    uint8_t poly_key[64];
    uint8_t zeros[64] = {0};
    chacha20_crypt(key, nonce, 0, zeros, poly_key, 64);

    // Verify tag
    poly1305_ctx poly;
    poly1305_init(&poly, poly_key);
    poly1305_update(&poly, aad, aad_len);
    pad16(&poly, aad_len);
    poly1305_update(&poly, ciphertext, data_len);
    pad16(&poly, data_len);

    uint8_t lens[16];
    lens[0] = aad_len & 0xFF;
    lens[1] = (aad_len >> 8) & 0xFF;
    lens[2] = (aad_len >> 16) & 0xFF;
    lens[3] = (aad_len >> 24) & 0xFF;
    lens[4] = lens[5] = lens[6] = lens[7] = 0;
    lens[8] = data_len & 0xFF;
    lens[9] = (data_len >> 8) & 0xFF;
    lens[10] = (data_len >> 16) & 0xFF;
    lens[11] = (data_len >> 24) & 0xFF;
    lens[12] = lens[13] = lens[14] = lens[15] = 0;
    poly1305_update(&poly, lens, 16);

    uint8_t computed_tag[16];
    poly1305_final(&poly, computed_tag);

    // Constant-time comparison
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= computed_tag[i] ^ tag[i];
    if (diff != 0)
        return -1;

    // Decrypt
    chacha20_crypt(key, nonce, 1, (const uint8_t *)ciphertext, plaintext, data_len);

    return (long)data_len;
}

//=============================================================================
// X25519 Key Exchange
//=============================================================================

typedef int64_t fe[10];

static void fe_copy(fe h, const fe f)
{
    for (int i = 0; i < 10; i++)
        h[i] = f[i];
}

static void fe_0(fe h)
{
    for (int i = 0; i < 10; i++)
        h[i] = 0;
}

static void fe_1(fe h)
{
    h[0] = 1;
    for (int i = 1; i < 10; i++)
        h[i] = 0;
}

static void fe_add(fe h, const fe f, const fe g)
{
    for (int i = 0; i < 10; i++)
        h[i] = f[i] + g[i];
}

static void fe_sub(fe h, const fe f, const fe g)
{
    for (int i = 0; i < 10; i++)
        h[i] = f[i] - g[i];
}

static void fe_mul(fe h, const fe f, const fe g)
{
    int64_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    int64_t f5 = f[5], f6 = f[6], f7 = f[7], f8 = f[8], f9 = f[9];
    int64_t g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
    int64_t g5 = g[5], g6 = g[6], g7 = g[7], g8 = g[8], g9 = g[9];

    int64_t g1_19 = 19 * g1, g2_19 = 19 * g2, g3_19 = 19 * g3, g4_19 = 19 * g4, g5_19 = 19 * g5;
    int64_t g6_19 = 19 * g6, g7_19 = 19 * g7, g8_19 = 19 * g8, g9_19 = 19 * g9;
    int64_t f1_2 = 2 * f1, f3_2 = 2 * f3, f5_2 = 2 * f5, f7_2 = 2 * f7, f9_2 = 2 * f9;

    int64_t h0 = f0 * g0 + f1_2 * g9_19 + f2 * g8_19 + f3_2 * g7_19 + f4 * g6_19 + f5_2 * g5_19 +
                 f6 * g4_19 + f7_2 * g3_19 + f8 * g2_19 + f9_2 * g1_19;
    int64_t h1 = f0 * g1 + f1 * g0 + f2 * g9_19 + f3 * g8_19 + f4 * g7_19 + f5 * g6_19 +
                 f6 * g5_19 + f7 * g4_19 + f8 * g3_19 + f9 * g2_19;
    int64_t h2 = f0 * g2 + f1_2 * g1 + f2 * g0 + f3_2 * g9_19 + f4 * g8_19 + f5_2 * g7_19 +
                 f6 * g6_19 + f7_2 * g5_19 + f8 * g4_19 + f9_2 * g3_19;
    int64_t h3 = f0 * g3 + f1 * g2 + f2 * g1 + f3 * g0 + f4 * g9_19 + f5 * g8_19 + f6 * g7_19 +
                 f7 * g6_19 + f8 * g5_19 + f9 * g4_19;
    int64_t h4 = f0 * g4 + f1_2 * g3 + f2 * g2 + f3_2 * g1 + f4 * g0 + f5_2 * g9_19 + f6 * g8_19 +
                 f7_2 * g7_19 + f8 * g6_19 + f9_2 * g5_19;
    int64_t h5 = f0 * g5 + f1 * g4 + f2 * g3 + f3 * g2 + f4 * g1 + f5 * g0 + f6 * g9_19 +
                 f7 * g8_19 + f8 * g7_19 + f9 * g6_19;
    int64_t h6 = f0 * g6 + f1_2 * g5 + f2 * g4 + f3_2 * g3 + f4 * g2 + f5_2 * g1 + f6 * g0 +
                 f7_2 * g9_19 + f8 * g8_19 + f9_2 * g7_19;
    int64_t h7 = f0 * g7 + f1 * g6 + f2 * g5 + f3 * g4 + f4 * g3 + f5 * g2 + f6 * g1 + f7 * g0 +
                 f8 * g9_19 + f9 * g8_19;
    int64_t h8 = f0 * g8 + f1_2 * g7 + f2 * g6 + f3_2 * g5 + f4 * g4 + f5_2 * g3 + f6 * g2 +
                 f7_2 * g1 + f8 * g0 + f9_2 * g9_19;
    int64_t h9 = f0 * g9 + f1 * g8 + f2 * g7 + f3 * g6 + f4 * g5 + f5 * g4 + f6 * g3 + f7 * g2 +
                 f8 * g1 + f9 * g0;

    // Carry
    int64_t c;
    c = (h0 + (1 << 25)) >> 26;
    h1 += c;
    h0 -= c << 26;
    c = (h4 + (1 << 25)) >> 26;
    h5 += c;
    h4 -= c << 26;
    c = (h1 + (1 << 24)) >> 25;
    h2 += c;
    h1 -= c << 25;
    c = (h5 + (1 << 24)) >> 25;
    h6 += c;
    h5 -= c << 25;
    c = (h2 + (1 << 25)) >> 26;
    h3 += c;
    h2 -= c << 26;
    c = (h6 + (1 << 25)) >> 26;
    h7 += c;
    h6 -= c << 26;
    c = (h3 + (1 << 24)) >> 25;
    h4 += c;
    h3 -= c << 25;
    c = (h7 + (1 << 24)) >> 25;
    h8 += c;
    h7 -= c << 25;
    c = (h4 + (1 << 25)) >> 26;
    h5 += c;
    h4 -= c << 26;
    c = (h8 + (1 << 25)) >> 26;
    h9 += c;
    h8 -= c << 26;
    c = (h9 + (1 << 24)) >> 25;
    h0 += c * 19;
    h9 -= c << 25;
    c = (h0 + (1 << 25)) >> 26;
    h1 += c;
    h0 -= c << 26;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
    h[5] = h5;
    h[6] = h6;
    h[7] = h7;
    h[8] = h8;
    h[9] = h9;
}

static void fe_sq(fe h, const fe f)
{
    fe_mul(h, f, f);
}

static void fe_invert(fe out, const fe z)
{
    fe t0, t1, t2, t3;

    fe_sq(t0, z);
    fe_sq(t1, t0);
    fe_sq(t1, t1);
    fe_mul(t1, z, t1);
    fe_mul(t0, t0, t1);
    fe_sq(t2, t0);
    fe_mul(t1, t1, t2);
    fe_sq(t2, t1);
    for (int i = 1; i < 5; i++)
        fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t2, t1);
    for (int i = 1; i < 10; i++)
        fe_sq(t2, t2);
    fe_mul(t2, t2, t1);
    fe_sq(t3, t2);
    for (int i = 1; i < 20; i++)
        fe_sq(t3, t3);
    fe_mul(t2, t3, t2);
    fe_sq(t2, t2);
    for (int i = 1; i < 10; i++)
        fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t2, t1);
    for (int i = 1; i < 50; i++)
        fe_sq(t2, t2);
    fe_mul(t2, t2, t1);
    fe_sq(t3, t2);
    for (int i = 1; i < 100; i++)
        fe_sq(t3, t3);
    fe_mul(t2, t3, t2);
    fe_sq(t2, t2);
    for (int i = 1; i < 50; i++)
        fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t1, t1);
    for (int i = 1; i < 5; i++)
        fe_sq(t1, t1);
    fe_mul(out, t1, t0);
}

static void fe_from_bytes(fe h, const uint8_t s[32])
{
    h[0] = ((int64_t)s[0] | ((int64_t)s[1] << 8) | ((int64_t)s[2] << 16) |
            (((int64_t)s[3] & 0x03) << 24)) &
           0x3ffffff;
    h[1] = (((int64_t)s[3] >> 2) | ((int64_t)s[4] << 6) | ((int64_t)s[5] << 14) |
            (((int64_t)s[6] & 0x07) << 22)) &
           0x1ffffff;
    h[2] = (((int64_t)s[6] >> 3) | ((int64_t)s[7] << 5) | ((int64_t)s[8] << 13) |
            (((int64_t)s[9] & 0x1f) << 21)) &
           0x3ffffff;
    h[3] = (((int64_t)s[9] >> 5) | ((int64_t)s[10] << 3) | ((int64_t)s[11] << 11) |
            (((int64_t)s[12] & 0x3f) << 19)) &
           0x1ffffff;
    h[4] = (((int64_t)s[12] >> 6) | ((int64_t)s[13] << 2) | ((int64_t)s[14] << 10) |
            ((int64_t)s[15] << 18)) &
           0x3ffffff;
    h[5] = ((int64_t)s[16] | ((int64_t)s[17] << 8) | ((int64_t)s[18] << 16) |
            (((int64_t)s[19] & 0x01) << 24)) &
           0x1ffffff;
    h[6] = (((int64_t)s[19] >> 1) | ((int64_t)s[20] << 7) | ((int64_t)s[21] << 15) |
            (((int64_t)s[22] & 0x07) << 23)) &
           0x3ffffff;
    h[7] = (((int64_t)s[22] >> 3) | ((int64_t)s[23] << 5) | ((int64_t)s[24] << 13) |
            (((int64_t)s[25] & 0x0f) << 21)) &
           0x1ffffff;
    h[8] = (((int64_t)s[25] >> 4) | ((int64_t)s[26] << 4) | ((int64_t)s[27] << 12) |
            (((int64_t)s[28] & 0x3f) << 20)) &
           0x3ffffff;
    h[9] = (((int64_t)s[28] >> 6) | ((int64_t)s[29] << 2) | ((int64_t)s[30] << 10) |
            ((int64_t)s[31] << 18)) &
           0x1ffffff;
}

static void fe_to_bytes(uint8_t s[32], const fe h)
{
    int64_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
    int64_t h5 = h[5], h6 = h[6], h7 = h[7], h8 = h[8], h9 = h[9];

    int64_t q = (19 * h9 + (1LL << 24)) >> 25;
    q = (h0 + q) >> 26;
    q = (h1 + q) >> 25;
    q = (h2 + q) >> 26;
    q = (h3 + q) >> 25;
    q = (h4 + q) >> 26;
    q = (h5 + q) >> 25;
    q = (h6 + q) >> 26;
    q = (h7 + q) >> 25;
    q = (h8 + q) >> 26;
    q = (h9 + q) >> 25;

    h0 += 19 * q;

    int64_t c = h0 >> 26;
    h1 += c;
    h0 -= c << 26;
    c = h1 >> 25;
    h2 += c;
    h1 -= c << 25;
    c = h2 >> 26;
    h3 += c;
    h2 -= c << 26;
    c = h3 >> 25;
    h4 += c;
    h3 -= c << 25;
    c = h4 >> 26;
    h5 += c;
    h4 -= c << 26;
    c = h5 >> 25;
    h6 += c;
    h5 -= c << 25;
    c = h6 >> 26;
    h7 += c;
    h6 -= c << 26;
    c = h7 >> 25;
    h8 += c;
    h7 -= c << 25;
    c = h8 >> 26;
    h9 += c;
    h8 -= c << 26;
    c = h9 >> 25;
    h9 -= c << 25;

    s[0] = (uint8_t)h0;
    s[1] = (uint8_t)(h0 >> 8);
    s[2] = (uint8_t)(h0 >> 16);
    s[3] = (uint8_t)((h0 >> 24) | (h1 << 2));
    s[4] = (uint8_t)(h1 >> 6);
    s[5] = (uint8_t)(h1 >> 14);
    s[6] = (uint8_t)((h1 >> 22) | (h2 << 3));
    s[7] = (uint8_t)(h2 >> 5);
    s[8] = (uint8_t)(h2 >> 13);
    s[9] = (uint8_t)((h2 >> 21) | (h3 << 5));
    s[10] = (uint8_t)(h3 >> 3);
    s[11] = (uint8_t)(h3 >> 11);
    s[12] = (uint8_t)((h3 >> 19) | (h4 << 6));
    s[13] = (uint8_t)(h4 >> 2);
    s[14] = (uint8_t)(h4 >> 10);
    s[15] = (uint8_t)(h4 >> 18);
    s[16] = (uint8_t)h5;
    s[17] = (uint8_t)(h5 >> 8);
    s[18] = (uint8_t)(h5 >> 16);
    s[19] = (uint8_t)((h5 >> 24) | (h6 << 1));
    s[20] = (uint8_t)(h6 >> 7);
    s[21] = (uint8_t)(h6 >> 15);
    s[22] = (uint8_t)((h6 >> 23) | (h7 << 3));
    s[23] = (uint8_t)(h7 >> 5);
    s[24] = (uint8_t)(h7 >> 13);
    s[25] = (uint8_t)((h7 >> 21) | (h8 << 4));
    s[26] = (uint8_t)(h8 >> 4);
    s[27] = (uint8_t)(h8 >> 12);
    s[28] = (uint8_t)((h8 >> 20) | (h9 << 6));
    s[29] = (uint8_t)(h9 >> 2);
    s[30] = (uint8_t)(h9 >> 10);
    s[31] = (uint8_t)(h9 >> 18);
}

static void x25519_scalarmult(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    fe x1, x2, z2, x3, z3, tmp0, tmp1;
    uint8_t e[32];

    memcpy(e, scalar, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;

    fe_from_bytes(x1, point);
    fe_1(x2);
    fe_0(z2);
    fe_copy(x3, x1);
    fe_1(z3);

    int swap = 0;
    for (int pos = 254; pos >= 0; pos--)
    {
        int b = (e[pos / 8] >> (pos & 7)) & 1;
        swap ^= b;
        for (int i = 0; i < 10; i++)
        {
            int64_t dummy = swap * (x2[i] ^ x3[i]);
            x2[i] ^= dummy;
            x3[i] ^= dummy;
            dummy = swap * (z2[i] ^ z3[i]);
            z2[i] ^= dummy;
            z3[i] ^= dummy;
        }
        swap = b;

        fe_sub(tmp0, x3, z3);
        fe_sub(tmp1, x2, z2);
        fe_add(x2, x2, z2);
        fe_add(z2, x3, z3);
        fe_mul(z3, tmp0, x2);
        fe_mul(z2, z2, tmp1);
        fe_sq(tmp0, tmp1);
        fe_sq(tmp1, x2);
        fe_add(x3, z3, z2);
        fe_sub(z2, z3, z2);
        fe_mul(x2, tmp1, tmp0);
        fe_sub(tmp1, tmp1, tmp0);
        fe_sq(z2, z2);
        fe fe_121666 = {121666, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        fe_mul(z3, tmp1, fe_121666);
        fe_sq(x3, x3);
        fe_add(tmp0, tmp0, z3);
        fe_mul(z3, x1, z2);
        fe_mul(z2, tmp1, tmp0);
    }

    for (int i = 0; i < 10; i++)
    {
        int64_t dummy = swap * (x2[i] ^ x3[i]);
        x2[i] ^= dummy;
        x3[i] ^= dummy;
        dummy = swap * (z2[i] ^ z3[i]);
        z2[i] ^= dummy;
        z3[i] ^= dummy;
    }

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_to_bytes(out, x2);
}

static const uint8_t x25519_basepoint[32] = {9};

void rt_x25519_keygen(uint8_t secret[32], uint8_t public_key[32])
{
    rt_crypto_random_bytes(secret, 32);
    x25519_scalarmult(public_key, secret, x25519_basepoint);
}

void rt_x25519(const uint8_t secret[32], const uint8_t peer_public[32], uint8_t shared[32])
{
    x25519_scalarmult(shared, secret, peer_public);
}

//=============================================================================
// Random Number Generation (Platform-specific)
//=============================================================================

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>

void rt_crypto_random_bytes(uint8_t *buf, size_t len)
{
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hProv, (DWORD)len, buf);
        CryptReleaseContext(hProv, 0);
        return;
    }

    // Fallback PRNG (should not happen on Windows)
    static uint64_t state = 0x123456789ABCDEF0ULL;
    if (state == 0x123456789ABCDEF0ULL)
        state ^= (uint64_t)(uintptr_t)buf ^ GetTickCount();

    for (size_t i = 0; i < len; i++)
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (state >> 32) & 0xFF;
    }
}

#else
#include <fcntl.h>
#include <unistd.h>

void rt_crypto_random_bytes(uint8_t *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0)
    {
        ssize_t n = read(fd, buf, len);
        close(fd);
        if ((size_t)n == len)
            return;
    }

    // Fallback PRNG
    static uint64_t state = 0x123456789ABCDEF0ULL;
    if (state == 0x123456789ABCDEF0ULL)
        state ^= (uint64_t)(uintptr_t)buf;

    for (size_t i = 0; i < len; i++)
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        buf[i] = (state >> 32) & 0xFF;
    }
}
#endif
