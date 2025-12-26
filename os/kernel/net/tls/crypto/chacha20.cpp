/**
 * @file chacha20.cpp
 * @brief ChaCha20, Poly1305 and ChaCha20-Poly1305 AEAD implementation.
 *
 * @details
 * Implements the primitives declared in `chacha20.hpp` following RFC 8439.
 * This code is used by the TLS record layer to encrypt and authenticate records
 * when the ChaCha20-Poly1305 cipher suite is selected.
 */

#include "chacha20.hpp"

namespace viper::crypto
{

/**
 * @brief ChaCha20 quarter-round function.
 *
 * @details
 * The quarter-round is the core mixing primitive of ChaCha20. It operates on
 * four 32-bit words and performs a sequence of add/xor/rotate operations.
 *
 * This implementation follows RFC 8439 and updates the words in place.
 *
 * @param a Word a (updated in place).
 * @param b Word b (updated in place).
 * @param c Word c (updated in place).
 * @param d Word d (updated in place).
 */
static inline void quarter_round(u32 &a, u32 &b, u32 &c, u32 &d)
{
    a += b;
    d ^= a;
    d = (d << 16) | (d >> 16);
    c += d;
    b ^= c;
    b = (b << 12) | (b >> 20);
    a += b;
    d ^= a;
    d = (d << 8) | (d >> 24);
    c += d;
    b ^= c;
    b = (b << 7) | (b >> 25);
}

/**
 * @brief Read a 32-bit little-endian value from a byte pointer.
 *
 * @details
 * ChaCha20 and Poly1305 operate on little-endian word encodings. This helper
 * avoids unaligned loads and makes endianness explicit.
 *
 * @param p Pointer to at least 4 bytes.
 * @return Little-endian decoded 32-bit value.
 */
static inline u32 read_le32(const u8 *p)
{
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

/**
 * @brief Write a 32-bit value to memory in little-endian byte order.
 *
 * @param p Destination pointer (must have at least 4 bytes).
 * @param v Value to encode.
 */
static inline void write_le32(u8 *p, u32 v)
{
    p[0] = static_cast<u8>(v);
    p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16);
    p[3] = static_cast<u8>(v >> 24);
}

/** @copydoc viper::crypto::chacha20_init */
void chacha20_init(ChaCha20State *s,
                   const u8 key[CHACHA20_KEY_SIZE],
                   const u8 nonce[CHACHA20_NONCE_SIZE],
                   u32 counter)
{
    // "expand 32-byte k"
    s->state[0] = 0x61707865;
    s->state[1] = 0x3320646e;
    s->state[2] = 0x79622d32;
    s->state[3] = 0x6b206574;

    // Key
    for (int i = 0; i < 8; i++)
    {
        s->state[4 + i] = read_le32(key + i * 4);
    }

    // Counter
    s->state[12] = counter;

    // Nonce
    s->state[13] = read_le32(nonce);
    s->state[14] = read_le32(nonce + 4);
    s->state[15] = read_le32(nonce + 8);
}

/** @copydoc viper::crypto::chacha20_block */
void chacha20_block(ChaCha20State *s, u8 out[CHACHA20_BLOCK_SIZE])
{
    u32 x[16];

    // Copy state
    for (int i = 0; i < 16; i++)
    {
        x[i] = s->state[i];
    }

    // 20 rounds (10 double-rounds)
    for (int i = 0; i < 10; i++)
    {
        // Column rounds
        quarter_round(x[0], x[4], x[8], x[12]);
        quarter_round(x[1], x[5], x[9], x[13]);
        quarter_round(x[2], x[6], x[10], x[14]);
        quarter_round(x[3], x[7], x[11], x[15]);
        // Diagonal rounds
        quarter_round(x[0], x[5], x[10], x[15]);
        quarter_round(x[1], x[6], x[11], x[12]);
        quarter_round(x[2], x[7], x[8], x[13]);
        quarter_round(x[3], x[4], x[9], x[14]);
    }

    // Add original state
    for (int i = 0; i < 16; i++)
    {
        x[i] += s->state[i];
    }

    // Output as little-endian bytes
    for (int i = 0; i < 16; i++)
    {
        write_le32(out + i * 4, x[i]);
    }

    // Increment counter
    s->state[12]++;
}

/** @copydoc viper::crypto::chacha20_crypt */
void chacha20_crypt(const u8 key[CHACHA20_KEY_SIZE],
                    const u8 nonce[CHACHA20_NONCE_SIZE],
                    u32 counter,
                    const u8 *in,
                    u8 *out,
                    usize len)
{
    ChaCha20State state;
    chacha20_init(&state, key, nonce, counter);

    u8 block[64];
    usize offset = 0;

    while (len > 0)
    {
        chacha20_block(&state, block);

        usize chunk = (len > 64) ? 64 : len;
        for (usize i = 0; i < chunk; i++)
        {
            out[offset + i] = in[offset + i] ^ block[i];
        }

        offset += chunk;
        len -= chunk;
    }
}

// Poly1305 implementation

/**
 * @brief Clamp the Poly1305 `r` parameter as required by RFC 8439.
 *
 * @details
 * Poly1305 requires certain high/low bits of the 128-bit `r` value to be
 * cleared to ensure the polynomial evaluation has the desired security
 * properties. This function applies those bit masks in-place.
 *
 * @param r 16-byte `r` value to clamp.
 */
static void clamp(u8 r[16])
{
    r[3] &= 0x0f;
    r[7] &= 0x0f;
    r[11] &= 0x0f;
    r[15] &= 0x0f;
    r[4] &= 0xfc;
    r[8] &= 0xfc;
    r[12] &= 0xfc;
}

/** @copydoc viper::crypto::poly1305_init */
void poly1305_init(Poly1305State *s, const u8 key[POLY1305_KEY_SIZE])
{
    // Clamp and load r
    u8 r[16];
    for (int i = 0; i < 16; i++)
    {
        r[i] = key[i];
    }
    clamp(r);

    // r in 26-bit limbs
    s->r[0] = (read_le32(r + 0)) & 0x03ffffff;
    s->r[1] = (read_le32(r + 3) >> 2) & 0x03ffffff;
    s->r[2] = (read_le32(r + 6) >> 4) & 0x03ffffff;
    s->r[3] = (read_le32(r + 9) >> 6) & 0x03ffffff;
    s->r[4] = (read_le32(r + 12) >> 8) & 0x03ffffff;

    // h = 0
    for (int i = 0; i < 5; i++)
    {
        s->h[i] = 0;
    }

    // Load s (second half of key)
    for (int i = 0; i < 4; i++)
    {
        s->pad[i] = read_le32(key + 16 + i * 4);
    }

    s->buffer_len = 0;
}

/**
 * @brief Process one 16-byte block of message data for Poly1305.
 *
 * @details
 * Poly1305 evaluates a polynomial over message blocks treated as 16-byte
 * little-endian numbers with an added high bit for non-final blocks. This
 * function:
 * - Decodes the block into 26-bit limbs.
 * - Adds it to the accumulator `h`.
 * - Multiplies by `r` modulo `2^130 - 5`.
 *
 * The `final` flag controls whether the implicit high bit is added.
 *
 * @param s Poly1305 state (updated in place).
 * @param block Pointer to 16 message bytes.
 * @param final True if this is the final (possibly padded) block.
 */
static void poly1305_block(Poly1305State *s, const u8 block[16], bool final)
{
    // Load block into n
    u32 n[5];
    n[0] = (read_le32(block + 0)) & 0x03ffffff;
    n[1] = (read_le32(block + 3) >> 2) & 0x03ffffff;
    n[2] = (read_le32(block + 6) >> 4) & 0x03ffffff;
    n[3] = (read_le32(block + 9) >> 6) & 0x03ffffff;
    n[4] = (read_le32(block + 12) >> 8);

    if (!final)
    {
        n[4] |= (1 << 24); // Add high bit for non-final blocks
    }

    // h += n
    u64 h0 = s->h[0] + n[0];
    u64 h1 = s->h[1] + n[1];
    u64 h2 = s->h[2] + n[2];
    u64 h3 = s->h[3] + n[3];
    u64 h4 = s->h[4] + n[4];

    // h *= r (mod 2^130 - 5)
    u32 r0 = s->r[0];
    u32 r1 = s->r[1];
    u32 r2 = s->r[2];
    u32 r3 = s->r[3];
    u32 r4 = s->r[4];

    u32 s1 = r1 * 5;
    u32 s2 = r2 * 5;
    u32 s3 = r3 * 5;
    u32 s4 = r4 * 5;

    u64 d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
    u64 d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
    u64 d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
    u64 d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
    u64 d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

    // Carry propagation
    u64 c;
    c = d0 >> 26;
    d1 += c;
    d0 &= 0x03ffffff;
    c = d1 >> 26;
    d2 += c;
    d1 &= 0x03ffffff;
    c = d2 >> 26;
    d3 += c;
    d2 &= 0x03ffffff;
    c = d3 >> 26;
    d4 += c;
    d3 &= 0x03ffffff;
    c = d4 >> 26;
    d0 += c * 5;
    d4 &= 0x03ffffff;
    c = d0 >> 26;
    d1 += c;
    d0 &= 0x03ffffff;

    s->h[0] = static_cast<u32>(d0);
    s->h[1] = static_cast<u32>(d1);
    s->h[2] = static_cast<u32>(d2);
    s->h[3] = static_cast<u32>(d3);
    s->h[4] = static_cast<u32>(d4);
}

/** @copydoc viper::crypto::poly1305_update */
void poly1305_update(Poly1305State *s, const void *data, usize len)
{
    const u8 *bytes = static_cast<const u8 *>(data);

    // Handle buffered data
    if (s->buffer_len > 0)
    {
        usize space = 16 - s->buffer_len;
        usize copy = (len < space) ? len : space;

        for (usize i = 0; i < copy; i++)
        {
            s->buffer[s->buffer_len + i] = bytes[i];
        }
        s->buffer_len += copy;
        bytes += copy;
        len -= copy;

        if (s->buffer_len == 16)
        {
            poly1305_block(s, s->buffer, false);
            s->buffer_len = 0;
        }
    }

    // Process full blocks
    while (len >= 16)
    {
        poly1305_block(s, bytes, false);
        bytes += 16;
        len -= 16;
    }

    // Buffer remaining
    for (usize i = 0; i < len; i++)
    {
        s->buffer[i] = bytes[i];
    }
    s->buffer_len = len;
}

/** @copydoc viper::crypto::poly1305_final */
void poly1305_final(Poly1305State *s, u8 tag[POLY1305_TAG_SIZE])
{
    // Process final block if any
    if (s->buffer_len > 0)
    {
        // Pad with 0x01 and zeros
        s->buffer[s->buffer_len] = 0x01;
        for (usize i = s->buffer_len + 1; i < 16; i++)
        {
            s->buffer[i] = 0;
        }
        poly1305_block(s, s->buffer, true);
    }

    // Fully reduce h
    u32 h0 = s->h[0];
    u32 h1 = s->h[1];
    u32 h2 = s->h[2];
    u32 h3 = s->h[3];
    u32 h4 = s->h[4];

    u32 c = h1 >> 26;
    h1 &= 0x03ffffff;
    h2 += c;
    c = h2 >> 26;
    h2 &= 0x03ffffff;
    h3 += c;
    c = h3 >> 26;
    h3 &= 0x03ffffff;
    h4 += c;
    c = h4 >> 26;
    h4 &= 0x03ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x03ffffff;
    h1 += c;

    // Compute h - (2^130 - 5)
    u32 g0 = h0 + 5;
    c = g0 >> 26;
    g0 &= 0x03ffffff;
    u32 g1 = h1 + c;
    c = g1 >> 26;
    g1 &= 0x03ffffff;
    u32 g2 = h2 + c;
    c = g2 >> 26;
    g2 &= 0x03ffffff;
    u32 g3 = h3 + c;
    c = g3 >> 26;
    g3 &= 0x03ffffff;
    u32 g4 = h4 + c - (1 << 26);

    // Select h if h < p, else select g
    u32 mask = (g4 >> 31) - 1; // All 1s if g4 >= 0, all 0s if g4 < 0
    h0 = (h0 & ~mask) | (g0 & mask);
    h1 = (h1 & ~mask) | (g1 & mask);
    h2 = (h2 & ~mask) | (g2 & mask);
    h3 = (h3 & ~mask) | (g3 & mask);
    h4 = (h4 & ~mask) | (g4 & mask);

    // h = h + s
    u64 f = static_cast<u64>(h0) + s->pad[0];
    h0 = static_cast<u32>(f);
    f = static_cast<u64>(h1) + (f >> 32) + s->pad[1];
    h1 = static_cast<u32>(f);
    f = static_cast<u64>(h2) + (f >> 32) + s->pad[2];
    h2 = static_cast<u32>(f);
    f = static_cast<u64>(h3) + (f >> 32) + s->pad[3];
    h3 = static_cast<u32>(f);

    // Convert to bytes (but we need to re-pack from limbs)
    u64 t0 = (static_cast<u64>(h0)) | (static_cast<u64>(h1) << 26);
    u64 t1 = (static_cast<u64>(h1) >> 6) | (static_cast<u64>(h2) << 20);
    u64 t2 = (static_cast<u64>(h2) >> 12) | (static_cast<u64>(h3) << 14);
    u64 t3 = (static_cast<u64>(h3) >> 18) | (static_cast<u64>(h4) << 8);

    // Add pad
    t0 += s->pad[0];
    t1 += s->pad[1] + (t0 >> 32);
    t2 += s->pad[2] + (t1 >> 32);
    t3 += s->pad[3] + (t2 >> 32);

    write_le32(tag + 0, static_cast<u32>(t0));
    write_le32(tag + 4, static_cast<u32>(t1));
    write_le32(tag + 8, static_cast<u32>(t2));
    write_le32(tag + 12, static_cast<u32>(t3));
}

/** @copydoc viper::crypto::poly1305 */
void poly1305(const u8 key[POLY1305_KEY_SIZE],
              const void *data,
              usize len,
              u8 tag[POLY1305_TAG_SIZE])
{
    Poly1305State state;
    poly1305_init(&state, key);
    poly1305_update(&state, data, len);
    poly1305_final(&state, tag);
}

/**
 * @brief Write a 64-bit value to memory in little-endian byte order.
 *
 * @details
 * ChaCha20-Poly1305 encodes AAD and ciphertext lengths as 64-bit little-endian
 * integers when computing the Poly1305 tag (RFC 8439).
 *
 * @param p Destination pointer (must have at least 8 bytes).
 * @param v Value to encode.
 */
static void write_le64(u8 *p, u64 v)
{
    write_le32(p, static_cast<u32>(v));
    write_le32(p + 4, static_cast<u32>(v >> 32));
}

/**
 * @brief Pad a Poly1305 message with zeros up to a 16-byte boundary.
 *
 * @details
 * RFC 8439 defines the AEAD tag computation as:
 * `Poly1305( aad || pad16(aad) || ciphertext || pad16(ciphertext) || len(aad) || len(ciphertext) )`
 *
 * This helper updates the Poly1305 state with the required zero padding when
 * `len` is not already a multiple of 16 bytes.
 *
 * @param s Poly1305 state to update.
 * @param len Length of the most recently added data segment in bytes.
 */
static void poly1305_pad16(Poly1305State *s, usize len)
{
    if (len % 16 != 0)
    {
        u8 zeros[16] = {0};
        poly1305_update(s, zeros, 16 - (len % 16));
    }
}

/** @copydoc viper::crypto::chacha20_poly1305_encrypt */
usize chacha20_poly1305_encrypt(const u8 key[CHACHA20_POLY1305_KEY_SIZE],
                                const u8 nonce[CHACHA20_POLY1305_NONCE_SIZE],
                                const void *aad,
                                usize aad_len,
                                const void *plaintext,
                                usize plaintext_len,
                                u8 *ciphertext)
{
    // Generate Poly1305 key (first block with counter = 0)
    u8 poly_key[64] = {0};
    ChaCha20State state;
    chacha20_init(&state, key, nonce, 0);
    chacha20_block(&state, poly_key);

    // Encrypt with counter = 1
    chacha20_crypt(key, nonce, 1, static_cast<const u8 *>(plaintext), ciphertext, plaintext_len);

    // Compute tag: Poly1305(aad || pad || ciphertext || pad || aad_len || ct_len)
    Poly1305State ps;
    poly1305_init(&ps, poly_key);

    poly1305_update(&ps, aad, aad_len);
    poly1305_pad16(&ps, aad_len);

    poly1305_update(&ps, ciphertext, plaintext_len);
    poly1305_pad16(&ps, plaintext_len);

    u8 lens[16];
    write_le64(lens, aad_len);
    write_le64(lens + 8, plaintext_len);
    poly1305_update(&ps, lens, 16);

    poly1305_final(&ps, ciphertext + plaintext_len);

    return plaintext_len + CHACHA20_POLY1305_TAG_SIZE;
}

/**
 * @brief Constant-time byte array comparison.
 *
 * @details
 * Used to compare Poly1305 tags without leaking timing information about the
 * first mismatching byte.
 *
 * @param a First buffer.
 * @param b Second buffer.
 * @param len Number of bytes to compare.
 * @return True if all bytes are equal, false otherwise.
 */
static bool ct_compare(const u8 *a, const u8 *b, usize len)
{
    u8 diff = 0;
    for (usize i = 0; i < len; i++)
    {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

/** @copydoc viper::crypto::chacha20_poly1305_decrypt */
i64 chacha20_poly1305_decrypt(const u8 key[CHACHA20_POLY1305_KEY_SIZE],
                              const u8 nonce[CHACHA20_POLY1305_NONCE_SIZE],
                              const void *aad,
                              usize aad_len,
                              const void *ciphertext,
                              usize ciphertext_len,
                              u8 *plaintext)
{
    if (ciphertext_len < CHACHA20_POLY1305_TAG_SIZE)
    {
        return -1; // Too short
    }

    usize ct_len = ciphertext_len - CHACHA20_POLY1305_TAG_SIZE;
    const u8 *ct = static_cast<const u8 *>(ciphertext);
    const u8 *tag = ct + ct_len;

    // Generate Poly1305 key
    u8 poly_key[64] = {0};
    ChaCha20State state;
    chacha20_init(&state, key, nonce, 0);
    chacha20_block(&state, poly_key);

    // Compute expected tag
    Poly1305State ps;
    poly1305_init(&ps, poly_key);

    poly1305_update(&ps, aad, aad_len);
    poly1305_pad16(&ps, aad_len);

    poly1305_update(&ps, ct, ct_len);
    poly1305_pad16(&ps, ct_len);

    u8 lens[16];
    write_le64(lens, aad_len);
    write_le64(lens + 8, ct_len);
    poly1305_update(&ps, lens, 16);

    u8 expected_tag[16];
    poly1305_final(&ps, expected_tag);

    // Verify tag (constant time)
    if (!ct_compare(tag, expected_tag, 16))
    {
        return -1; // Authentication failed
    }

    // Decrypt
    chacha20_crypt(key, nonce, 1, ct, plaintext, ct_len);

    return static_cast<i64>(ct_len);
}

} // namespace viper::crypto
