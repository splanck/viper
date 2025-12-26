/**
 * @file aes_gcm.cpp
 * @brief AES-GCM implementation for TLS cipher suites.
 *
 * @details
 * Implements AES key expansion and the GCM authenticated-encryption mode used
 * by TLS AES-GCM cipher suites. The implementation is intended for kernel
 * bring-up and favors clarity and correctness over optimization.
 */

#include "aes_gcm.hpp"
#include "../../../lib/mem.hpp"

namespace viper::tls::crypto
{

// AES S-box (SubBytes substitution table)
static const u8 sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

// Round constants for key expansion
static const u8 rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

/**
 * @brief Multiply two bytes in the AES finite field GF(2^8).
 *
 * @details
 * AES operates on bytes interpreted as elements of GF(2^8) with the
 * irreducible polynomial:
 *
 * `x^8 + x^4 + x^3 + x + 1` (0x11B).
 *
 * This helper performs multiplication using the classic shift-and-conditional-
 * xor method with reduction by 0x1B when the high bit overflows.
 *
 * @param a First operand.
 * @param b Second operand.
 * @return Product `a * b` in GF(2^8).
 */
static inline u8 gf_mul(u8 a, u8 b)
{
    u8 result = 0;
    u8 hi_bit;
    for (int i = 0; i < 8; i++)
    {
        if (b & 1)
        {
            result ^= a;
        }
        hi_bit = a & 0x80;
        a <<= 1;
        if (hi_bit)
        {
            a ^= 0x1b; // Reduction polynomial
        }
        b >>= 1;
    }
    return result;
}

/**
 * @brief Rotate a 32-bit word left by 8 bits.
 *
 * @details
 * AES key expansion operates on 32-bit words. Rotating by one byte is the
 * `RotWord` transform used when generating round keys.
 *
 * @param w Input word.
 * @return `w` rotated left by 8 bits.
 */
static inline u32 rot_word(u32 w)
{
    return (w << 8) | (w >> 24);
}

/**
 * @brief Apply the AES S-box to each byte of a 32-bit word.
 *
 * @details
 * This is the `SubWord` transform used by AES key expansion. The most
 * significant byte is substituted into the most significant position, and so
 * on.
 *
 * @param w Input word.
 * @return Word with S-box applied per byte.
 */
static inline u32 sub_word(u32 w)
{
    return (static_cast<u32>(sbox[(w >> 24) & 0xff]) << 24) |
           (static_cast<u32>(sbox[(w >> 16) & 0xff]) << 16) |
           (static_cast<u32>(sbox[(w >> 8) & 0xff]) << 8) | (static_cast<u32>(sbox[(w) & 0xff]));
}

// Key expansion for AES-128
/** @copydoc viper::tls::crypto::aes_key_expand_128 */
void aes_key_expand_128(const u8 key[AES_128_KEY_SIZE], AesKey *expanded)
{
    expanded->rounds = 10;

    // First 4 words are the key itself
    for (int i = 0; i < 4; i++)
    {
        expanded->round_keys[i] =
            (static_cast<u32>(key[4 * i]) << 24) | (static_cast<u32>(key[4 * i + 1]) << 16) |
            (static_cast<u32>(key[4 * i + 2]) << 8) | (static_cast<u32>(key[4 * i + 3]));
    }

    // Expand to 44 words
    for (int i = 4; i < 44; i++)
    {
        u32 temp = expanded->round_keys[i - 1];
        if (i % 4 == 0)
        {
            temp = sub_word(rot_word(temp)) ^ (static_cast<u32>(rcon[i / 4]) << 24);
        }
        expanded->round_keys[i] = expanded->round_keys[i - 4] ^ temp;
    }
}

// Key expansion for AES-256
/** @copydoc viper::tls::crypto::aes_key_expand_256 */
void aes_key_expand_256(const u8 key[AES_256_KEY_SIZE], AesKey *expanded)
{
    expanded->rounds = 14;

    // First 8 words are the key itself
    for (int i = 0; i < 8; i++)
    {
        expanded->round_keys[i] =
            (static_cast<u32>(key[4 * i]) << 24) | (static_cast<u32>(key[4 * i + 1]) << 16) |
            (static_cast<u32>(key[4 * i + 2]) << 8) | (static_cast<u32>(key[4 * i + 3]));
    }

    // Expand to 60 words
    for (int i = 8; i < 60; i++)
    {
        u32 temp = expanded->round_keys[i - 1];
        if (i % 8 == 0)
        {
            temp = sub_word(rot_word(temp)) ^ (static_cast<u32>(rcon[i / 8]) << 24);
        }
        else if (i % 8 == 4)
        {
            temp = sub_word(temp);
        }
        expanded->round_keys[i] = expanded->round_keys[i - 8] ^ temp;
    }
}

/**
 * @brief Encrypt a single 16-byte block with AES.
 *
 * @details
 * Implements AES block encryption using the expanded round keys in @ref AesKey.
 * This routine is used both for:
 * - GCM counter-mode keystream generation.
 * - Computing `H = E(K, 0^128)` and `E(K, J0)` for GHASH/tag construction.
 *
 * The function performs the standard AES round structure:
 * - Initial AddRoundKey.
 * - `rounds - 1` iterations of SubBytes, ShiftRows, MixColumns, AddRoundKey.
 * - Final round without MixColumns.
 *
 * @param key Expanded AES key schedule.
 * @param in 16-byte plaintext block.
 * @param out 16-byte ciphertext block.
 */
static void aes_encrypt_block(const AesKey *key, const u8 in[16], u8 out[16])
{
    u8 state[16];
    lib::memcpy(state, in, 16);

    // AddRoundKey for round 0
    for (int i = 0; i < 4; i++)
    {
        u32 rk = key->round_keys[i];
        state[4 * i] ^= (rk >> 24) & 0xff;
        state[4 * i + 1] ^= (rk >> 16) & 0xff;
        state[4 * i + 2] ^= (rk >> 8) & 0xff;
        state[4 * i + 3] ^= (rk) & 0xff;
    }

    // Main rounds
    for (int round = 1; round <= key->rounds; round++)
    {
        u8 temp[16];

        // SubBytes
        for (int i = 0; i < 16; i++)
        {
            temp[i] = sbox[state[i]];
        }

        // ShiftRows
        // Row 0: no shift
        state[0] = temp[0];
        state[4] = temp[4];
        state[8] = temp[8];
        state[12] = temp[12];
        // Row 1: shift left by 1
        state[1] = temp[5];
        state[5] = temp[9];
        state[9] = temp[13];
        state[13] = temp[1];
        // Row 2: shift left by 2
        state[2] = temp[10];
        state[6] = temp[14];
        state[10] = temp[2];
        state[14] = temp[6];
        // Row 3: shift left by 3
        state[3] = temp[15];
        state[7] = temp[3];
        state[11] = temp[7];
        state[15] = temp[11];

        // MixColumns (skip in final round)
        if (round < key->rounds)
        {
            for (int col = 0; col < 4; col++)
            {
                u8 a = state[4 * col];
                u8 b = state[4 * col + 1];
                u8 c = state[4 * col + 2];
                u8 d = state[4 * col + 3];

                temp[4 * col] = gf_mul(0x02, a) ^ gf_mul(0x03, b) ^ c ^ d;
                temp[4 * col + 1] = a ^ gf_mul(0x02, b) ^ gf_mul(0x03, c) ^ d;
                temp[4 * col + 2] = a ^ b ^ gf_mul(0x02, c) ^ gf_mul(0x03, d);
                temp[4 * col + 3] = gf_mul(0x03, a) ^ b ^ c ^ gf_mul(0x02, d);
            }
            lib::memcpy(state, temp, 16);
        }

        // AddRoundKey
        for (int i = 0; i < 4; i++)
        {
            u32 rk = key->round_keys[round * 4 + i];
            state[4 * i] ^= (rk >> 24) & 0xff;
            state[4 * i + 1] ^= (rk >> 16) & 0xff;
            state[4 * i + 2] ^= (rk >> 8) & 0xff;
            state[4 * i + 3] ^= (rk) & 0xff;
        }
    }

    lib::memcpy(out, state, 16);
}

/**
 * @brief Multiply two 128-bit values for GHASH in GF(2^128).
 *
 * @details
 * GHASH treats 128-bit values as elements of GF(2^128) under the reduction
 * polynomial used by GCM. This implementation uses a straightforward
 * bit-by-bit multiply-and-reduce method.
 *
 * Security note:
 * The bit-by-bit algorithm is simple and easy to review, but it is not
 * constant-time. For production use, consider replacing it with a constant-
 * time implementation or a hardware-accelerated path.
 *
 * @param x 16-byte input value (multiplicand).
 * @param h 16-byte hash subkey `H`.
 * @param out 16-byte output receiving `x * h` in GF(2^128).
 */
static void ghash_mult(const u8 x[16], const u8 h[16], u8 out[16])
{
    u8 v[16];
    u8 z[16] = {0};

    lib::memcpy(v, h, 16);

    for (int i = 0; i < 16; i++)
    {
        for (int j = 7; j >= 0; j--)
        {
            if ((x[i] >> j) & 1)
            {
                // Z ^= V
                for (int k = 0; k < 16; k++)
                {
                    z[k] ^= v[k];
                }
            }

            // V = V * x (multiply by x in GF(2^128))
            u8 carry = v[15] & 1;
            for (int k = 15; k > 0; k--)
            {
                v[k] = (v[k] >> 1) | ((v[k - 1] & 1) << 7);
            }
            v[0] >>= 1;

            // Reduce by R = x^128 + x^7 + x^2 + x + 1
            if (carry)
            {
                v[0] ^= 0xe1;
            }
        }
    }

    lib::memcpy(out, z, 16);
}

/**
 * @brief Compute the GHASH authentication value for GCM.
 *
 * @details
 * GHASH is defined over the concatenation of:
 * - Additional authenticated data (AAD), padded to 16 bytes.
 * - Ciphertext, padded to 16 bytes.
 * - 128-bit length block containing bit lengths of AAD and ciphertext.
 *
 * This implementation processes full 16-byte blocks first and pads any final
 * partial block with zeros.
 *
 * @param h 16-byte GHASH subkey `H = E(K, 0^128)`.
 * @param aad Additional authenticated data pointer (may be null if `aad_len` is 0).
 * @param aad_len Length of AAD in bytes.
 * @param ciphertext Ciphertext pointer (may be null if `ct_len` is 0).
 * @param ct_len Length of ciphertext in bytes.
 * @param out 16-byte output buffer receiving GHASH result.
 */
static void ghash(const u8 h[16],
                  const void *aad,
                  usize aad_len,
                  const void *ciphertext,
                  usize ct_len,
                  u8 out[16])
{
    u8 y[16] = {0};
    u8 temp[16];

    const u8 *aad_ptr = static_cast<const u8 *>(aad);
    const u8 *ct_ptr = static_cast<const u8 *>(ciphertext);

    // Process AAD
    usize i = 0;
    while (i + 16 <= aad_len)
    {
        for (int j = 0; j < 16; j++)
        {
            y[j] ^= aad_ptr[i + j];
        }
        ghash_mult(y, h, temp);
        lib::memcpy(y, temp, 16);
        i += 16;
    }

    // Pad final AAD block
    if (i < aad_len)
    {
        u8 block[16] = {0};
        for (usize j = 0; i + j < aad_len; j++)
        {
            block[j] = aad_ptr[i + j];
        }
        for (int j = 0; j < 16; j++)
        {
            y[j] ^= block[j];
        }
        ghash_mult(y, h, temp);
        lib::memcpy(y, temp, 16);
    }

    // Process ciphertext
    i = 0;
    while (i + 16 <= ct_len)
    {
        for (int j = 0; j < 16; j++)
        {
            y[j] ^= ct_ptr[i + j];
        }
        ghash_mult(y, h, temp);
        lib::memcpy(y, temp, 16);
        i += 16;
    }

    // Pad final ciphertext block
    if (i < ct_len)
    {
        u8 block[16] = {0};
        for (usize j = 0; i + j < ct_len; j++)
        {
            block[j] = ct_ptr[i + j];
        }
        for (int j = 0; j < 16; j++)
        {
            y[j] ^= block[j];
        }
        ghash_mult(y, h, temp);
        lib::memcpy(y, temp, 16);
    }

    // Append lengths (in bits, big-endian)
    u8 len_block[16] = {0};
    u64 aad_bits = aad_len * 8;
    u64 ct_bits = ct_len * 8;

    len_block[0] = (aad_bits >> 56) & 0xff;
    len_block[1] = (aad_bits >> 48) & 0xff;
    len_block[2] = (aad_bits >> 40) & 0xff;
    len_block[3] = (aad_bits >> 32) & 0xff;
    len_block[4] = (aad_bits >> 24) & 0xff;
    len_block[5] = (aad_bits >> 16) & 0xff;
    len_block[6] = (aad_bits >> 8) & 0xff;
    len_block[7] = (aad_bits) & 0xff;

    len_block[8] = (ct_bits >> 56) & 0xff;
    len_block[9] = (ct_bits >> 48) & 0xff;
    len_block[10] = (ct_bits >> 40) & 0xff;
    len_block[11] = (ct_bits >> 32) & 0xff;
    len_block[12] = (ct_bits >> 24) & 0xff;
    len_block[13] = (ct_bits >> 16) & 0xff;
    len_block[14] = (ct_bits >> 8) & 0xff;
    len_block[15] = (ct_bits) & 0xff;

    for (int j = 0; j < 16; j++)
    {
        y[j] ^= len_block[j];
    }
    ghash_mult(y, h, out);
}

/**
 * @brief Increment the 32-bit counter portion of a 16-byte GCM counter block.
 *
 * @details
 * For the 96-bit IV construction used by TLS, GCM defines `J0` as:
 * `IV || 0x00000001` and then increments the last 32 bits for each block.
 *
 * The increment is performed in big-endian order over bytes 12..15.
 *
 * @param counter 16-byte counter block updated in place.
 */
static void inc_counter(u8 counter[16])
{
    for (int i = 15; i >= 12; i--)
    {
        if (++counter[i] != 0)
            break;
    }
}

/**
 * @brief Encrypt plaintext using AES-GCM and append the authentication tag.
 *
 * @details
 * Implements the standard GCM construction for a 96-bit IV:
 * - Compute `H = E(K, 0^128)`.
 * - Construct `J0 = IV || 0x00000001`.
 * - Encrypt plaintext with AES-CTR starting from `inc32(J0)`.
 * - Compute GHASH over AAD and ciphertext.
 * - Compute tag as `GHASH ^ E(K, J0)` and append it.
 *
 * The output buffer must be large enough to hold `plaintext_len + GCM_TAG_SIZE`
 * bytes.
 *
 * @param key Expanded AES key schedule.
 * @param nonce 96-bit IV/nonce.
 * @param aad Additional authenticated data pointer.
 * @param aad_len Length of AAD in bytes.
 * @param plaintext Plaintext pointer.
 * @param plaintext_len Plaintext length in bytes.
 * @param ciphertext Output buffer receiving ciphertext followed by tag.
 * @return Total bytes written (`plaintext_len + GCM_TAG_SIZE`).
 */
static usize gcm_encrypt(const AesKey *key,
                         const u8 nonce[GCM_IV_SIZE],
                         const void *aad,
                         usize aad_len,
                         const void *plaintext,
                         usize plaintext_len,
                         u8 *ciphertext)
{
    u8 h[16] = {0};
    u8 j0[16];
    u8 counter[16];
    u8 keystream[16];
    u8 tag[16];

    // Compute H = E(K, 0^128)
    aes_encrypt_block(key, h, h);

    // J0 = IV || 0^31 || 1 (for 96-bit IV)
    lib::memcpy(j0, nonce, GCM_IV_SIZE);
    j0[12] = 0;
    j0[13] = 0;
    j0[14] = 0;
    j0[15] = 1;

    // Start counter at J0 + 1
    lib::memcpy(counter, j0, 16);
    inc_counter(counter);

    const u8 *pt = static_cast<const u8 *>(plaintext);
    u8 *ct = ciphertext;

    // Encrypt plaintext with counter mode
    usize i = 0;
    while (i + 16 <= plaintext_len)
    {
        aes_encrypt_block(key, counter, keystream);
        for (int j = 0; j < 16; j++)
        {
            ct[i + j] = pt[i + j] ^ keystream[j];
        }
        inc_counter(counter);
        i += 16;
    }

    // Encrypt final partial block
    if (i < plaintext_len)
    {
        aes_encrypt_block(key, counter, keystream);
        for (usize j = 0; i + j < plaintext_len; j++)
        {
            ct[i + j] = pt[i + j] ^ keystream[j];
        }
    }

    // Compute GHASH
    u8 ghash_out[16];
    ghash(h, aad, aad_len, ciphertext, plaintext_len, ghash_out);

    // Compute tag = GHASH ^ E(K, J0)
    aes_encrypt_block(key, j0, keystream);
    for (int j = 0; j < 16; j++)
    {
        tag[j] = ghash_out[j] ^ keystream[j];
    }

    // Append tag to ciphertext
    lib::memcpy(ciphertext + plaintext_len, tag, GCM_TAG_SIZE);

    return plaintext_len + GCM_TAG_SIZE;
}

/**
 * @brief Decrypt ciphertext using AES-GCM after verifying the authentication tag.
 *
 * @details
 * Validates the last 16 bytes of the input as the GCM authentication tag.
 * Verification is performed using a constant-time byte comparison.
 *
 * Only after the tag verifies does the function decrypt the ciphertext with
 * AES-CTR. This avoids releasing unauthenticated plaintext to the caller.
 *
 * @param key Expanded AES key schedule.
 * @param nonce 96-bit IV/nonce.
 * @param aad Additional authenticated data pointer.
 * @param aad_len Length of AAD in bytes.
 * @param ciphertext Ciphertext pointer (ciphertext || tag).
 * @param ciphertext_len Total ciphertext length including tag.
 * @param plaintext Output buffer receiving decrypted plaintext.
 * @return Plaintext length on success, or -1 on authentication/format failure.
 */
static i64 gcm_decrypt(const AesKey *key,
                       const u8 nonce[GCM_IV_SIZE],
                       const void *aad,
                       usize aad_len,
                       const void *ciphertext,
                       usize ciphertext_len,
                       u8 *plaintext)
{
    if (ciphertext_len < GCM_TAG_SIZE)
    {
        return -1;
    }

    usize ct_len = ciphertext_len - GCM_TAG_SIZE;
    const u8 *ct = static_cast<const u8 *>(ciphertext);
    const u8 *received_tag = ct + ct_len;

    u8 h[16] = {0};
    u8 j0[16];
    u8 counter[16];
    u8 keystream[16];
    u8 computed_tag[16];

    // Compute H = E(K, 0^128)
    aes_encrypt_block(key, h, h);

    // J0 = IV || 0^31 || 1
    lib::memcpy(j0, nonce, GCM_IV_SIZE);
    j0[12] = 0;
    j0[13] = 0;
    j0[14] = 0;
    j0[15] = 1;

    // Compute GHASH over ciphertext (before decryption)
    u8 ghash_out[16];
    ghash(h, aad, aad_len, ct, ct_len, ghash_out);

    // Compute expected tag
    aes_encrypt_block(key, j0, keystream);
    for (int j = 0; j < 16; j++)
    {
        computed_tag[j] = ghash_out[j] ^ keystream[j];
    }

    // Verify tag (constant-time comparison)
    u8 diff = 0;
    for (int j = 0; j < 16; j++)
    {
        diff |= computed_tag[j] ^ received_tag[j];
    }
    if (diff != 0)
    {
        return -1; // Authentication failed
    }

    // Decrypt ciphertext
    lib::memcpy(counter, j0, 16);
    inc_counter(counter);

    usize i = 0;
    while (i + 16 <= ct_len)
    {
        aes_encrypt_block(key, counter, keystream);
        for (int j = 0; j < 16; j++)
        {
            plaintext[i + j] = ct[i + j] ^ keystream[j];
        }
        inc_counter(counter);
        i += 16;
    }

    // Decrypt final partial block
    if (i < ct_len)
    {
        aes_encrypt_block(key, counter, keystream);
        for (usize j = 0; i + j < ct_len; j++)
        {
            plaintext[i + j] = ct[i + j] ^ keystream[j];
        }
    }

    return static_cast<i64>(ct_len);
}

// Public API - AES-128-GCM
/** @copydoc viper::tls::crypto::aes_128_gcm_encrypt */
usize aes_128_gcm_encrypt(const u8 key[AES_128_KEY_SIZE],
                          const u8 nonce[GCM_IV_SIZE],
                          const void *aad,
                          usize aad_len,
                          const void *plaintext,
                          usize plaintext_len,
                          u8 *ciphertext)
{
    AesKey expanded;
    aes_key_expand_128(key, &expanded);
    return gcm_encrypt(&expanded, nonce, aad, aad_len, plaintext, plaintext_len, ciphertext);
}

/** @copydoc viper::tls::crypto::aes_128_gcm_decrypt */
i64 aes_128_gcm_decrypt(const u8 key[AES_128_KEY_SIZE],
                        const u8 nonce[GCM_IV_SIZE],
                        const void *aad,
                        usize aad_len,
                        const void *ciphertext,
                        usize ciphertext_len,
                        u8 *plaintext)
{
    AesKey expanded;
    aes_key_expand_128(key, &expanded);
    return gcm_decrypt(&expanded, nonce, aad, aad_len, ciphertext, ciphertext_len, plaintext);
}

// Public API - AES-256-GCM
/** @copydoc viper::tls::crypto::aes_256_gcm_encrypt */
usize aes_256_gcm_encrypt(const u8 key[AES_256_KEY_SIZE],
                          const u8 nonce[GCM_IV_SIZE],
                          const void *aad,
                          usize aad_len,
                          const void *plaintext,
                          usize plaintext_len,
                          u8 *ciphertext)
{
    AesKey expanded;
    aes_key_expand_256(key, &expanded);
    return gcm_encrypt(&expanded, nonce, aad, aad_len, plaintext, plaintext_len, ciphertext);
}

/** @copydoc viper::tls::crypto::aes_256_gcm_decrypt */
i64 aes_256_gcm_decrypt(const u8 key[AES_256_KEY_SIZE],
                        const u8 nonce[GCM_IV_SIZE],
                        const void *aad,
                        usize aad_len,
                        const void *ciphertext,
                        usize ciphertext_len,
                        u8 *plaintext)
{
    AesKey expanded;
    aes_key_expand_256(key, &expanded);
    return gcm_decrypt(&expanded, nonce, aad, aad_len, ciphertext, ciphertext_len, plaintext);
}

} // namespace viper::tls::crypto
