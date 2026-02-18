//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_aes.c
/// @brief AES encryption/decryption implementation (FIPS-197).
///
/// This file implements AES-128 and AES-256 encryption in CBC mode with
/// PKCS7 padding. The implementation is pure C with no external dependencies.
///
/// **Supported Key Sizes:**
/// - AES-128: 16-byte key (128 bits)
/// - AES-256: 32-byte key (256 bits)
///
/// **Mode of Operation:**
/// - CBC (Cipher Block Chaining) with 16-byte IV
/// - PKCS7 padding for non-block-aligned data
///
/// **Security Notes:**
/// - Always use a unique random IV for each encryption
/// - Key should be derived from password using PBKDF2 or similar
/// - This implementation is not hardened against timing attacks
///
//===----------------------------------------------------------------------===//

#include "rt_aes.h"

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// Forward declaration of runtime trap function
extern void rt_trap(const char *msg);

//=============================================================================
// AES Constants (FIPS-197)
//=============================================================================

/// AES block size in bytes (always 16 for AES)
#ifndef AES_BLOCK_SIZE
#define AES_BLOCK_SIZE 16
#endif

/// S-box substitution table
static const uint8_t sbox[256] = {
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

/// Inverse S-box substitution table
static const uint8_t inv_sbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

/// Round constants for key expansion
static const uint8_t rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

//=============================================================================
// AES Helper Functions
//=============================================================================

/// @brief Multiply by 2 in GF(2^8)
static inline uint8_t xtime(uint8_t x)
{
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

/// @brief Multiply two bytes in GF(2^8)
static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    uint8_t hi_bit;
    for (int i = 0; i < 8; i++)
    {
        if (b & 1)
            result ^= a;
        hi_bit = a & 0x80;
        a <<= 1;
        if (hi_bit)
            a ^= 0x1b; // Reduction polynomial
        b >>= 1;
    }
    return result;
}

//=============================================================================
// SHA-256 Implementation (for key derivation)
//=============================================================================

/// SHA-256 round constants
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

/// SHA-256 initial hash values
static const uint32_t sha256_h0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

/// @brief Compute SHA-256 hash of data.
/// @param data Input data
/// @param len Length of input data
/// @param hash Output hash (32 bytes)
static void local_sha256(const uint8_t *data, size_t len, uint8_t hash[32])
{
    uint32_t h[8];
    for (int i = 0; i < 8; i++)
        h[i] = sha256_h0[i];

    // Pre-processing: adding padding bits
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *padded = (uint8_t *)calloc(padded_len, 1);
    if (!padded)
        return;

    memcpy(padded, data, len);
    padded[len] = 0x80;

    // Append length in bits as big-endian
    uint64_t bit_len = len * 8;
    padded[padded_len - 8] = (uint8_t)(bit_len >> 56);
    padded[padded_len - 7] = (uint8_t)(bit_len >> 48);
    padded[padded_len - 6] = (uint8_t)(bit_len >> 40);
    padded[padded_len - 5] = (uint8_t)(bit_len >> 32);
    padded[padded_len - 4] = (uint8_t)(bit_len >> 24);
    padded[padded_len - 3] = (uint8_t)(bit_len >> 16);
    padded[padded_len - 2] = (uint8_t)(bit_len >> 8);
    padded[padded_len - 1] = (uint8_t)(bit_len);

    // Process each 64-byte chunk
    for (size_t chunk = 0; chunk < padded_len; chunk += 64)
    {
        uint32_t w[64];

        // Break chunk into sixteen 32-bit big-endian words
        for (int i = 0; i < 16; i++)
        {
            w[i] = ((uint32_t)padded[chunk + i * 4 + 0] << 24) |
                   ((uint32_t)padded[chunk + i * 4 + 1] << 16) |
                   ((uint32_t)padded[chunk + i * 4 + 2] << 8) |
                   ((uint32_t)padded[chunk + i * 4 + 3]);
        }

        // Extend the sixteen 32-bit words into sixty-four 32-bit words
        for (int i = 16; i < 64; i++)
            w[i] = SHA256_SIG1(w[i - 2]) + w[i - 7] + SHA256_SIG0(w[i - 15]) + w[i - 16];

        // Initialize working variables
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        // Main loop
        for (int i = 0; i < 64; i++)
        {
            uint32_t t1 = hh + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + w[i];
            uint32_t t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        // Add compressed chunk to current hash value
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    free(padded);

    // Produce final hash value (big-endian)
    for (int i = 0; i < 8; i++)
    {
        hash[i * 4 + 0] = (uint8_t)(h[i] >> 24);
        hash[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        hash[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        hash[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

//=============================================================================
// AES Key Expansion
//=============================================================================

/// @brief Expand the cipher key into the key schedule.
/// @param key Original key (16 or 32 bytes)
/// @param w Expanded key schedule (176 or 240 bytes)
/// @param nk Number of 32-bit words in key (4 for AES-128, 8 for AES-256)
/// @param nr Number of rounds (10 for AES-128, 14 for AES-256)
static void aes_key_expansion(const uint8_t *key, uint8_t *w, int nk, int nr)
{
    int nb = 4; // Number of columns (always 4 for AES)
    int i = 0;

    // First nk words are the original key
    while (i < nk)
    {
        w[4 * i + 0] = key[4 * i + 0];
        w[4 * i + 1] = key[4 * i + 1];
        w[4 * i + 2] = key[4 * i + 2];
        w[4 * i + 3] = key[4 * i + 3];
        i++;
    }

    // Generate remaining words
    uint8_t temp[4];
    i = nk;
    while (i < nb * (nr + 1))
    {
        temp[0] = w[4 * (i - 1) + 0];
        temp[1] = w[4 * (i - 1) + 1];
        temp[2] = w[4 * (i - 1) + 2];
        temp[3] = w[4 * (i - 1) + 3];

        if (i % nk == 0)
        {
            // RotWord + SubWord + Rcon
            uint8_t t = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon[i / nk];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t];
        }
        else if (nk > 6 && i % nk == 4)
        {
            // Extra SubWord for AES-256
            temp[0] = sbox[temp[0]];
            temp[1] = sbox[temp[1]];
            temp[2] = sbox[temp[2]];
            temp[3] = sbox[temp[3]];
        }

        w[4 * i + 0] = w[4 * (i - nk) + 0] ^ temp[0];
        w[4 * i + 1] = w[4 * (i - nk) + 1] ^ temp[1];
        w[4 * i + 2] = w[4 * (i - nk) + 2] ^ temp[2];
        w[4 * i + 3] = w[4 * (i - nk) + 3] ^ temp[3];
        i++;
    }
}

//=============================================================================
// AES Cipher Transformations
//=============================================================================

/// @brief Apply S-box substitution to all bytes in state
static void sub_bytes(uint8_t *state)
{
    for (int i = 0; i < 16; i++)
        state[i] = sbox[state[i]];
}

/// @brief Apply inverse S-box substitution to all bytes in state
static void inv_sub_bytes(uint8_t *state)
{
    for (int i = 0; i < 16; i++)
        state[i] = inv_sbox[state[i]];
}

/// @brief Shift rows of the state matrix
/// State is column-major: state[row + 4*col]
static void shift_rows(uint8_t *state)
{
    uint8_t temp;

    // Row 1: shift left by 1
    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;

    // Row 2: shift left by 2
    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    // Row 3: shift left by 3 (= shift right by 1)
    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

/// @brief Inverse shift rows of the state matrix
static void inv_shift_rows(uint8_t *state)
{
    uint8_t temp;

    // Row 1: shift right by 1
    temp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = temp;

    // Row 2: shift right by 2
    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    // Row 3: shift right by 3 (= shift left by 1)
    temp = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = temp;
}

/// @brief Mix columns transformation
static void mix_columns(uint8_t *state)
{
    for (int c = 0; c < 4; c++)
    {
        int i = c * 4;
        uint8_t a0 = state[i + 0];
        uint8_t a1 = state[i + 1];
        uint8_t a2 = state[i + 2];
        uint8_t a3 = state[i + 3];

        state[i + 0] = xtime(a0) ^ xtime(a1) ^ a1 ^ a2 ^ a3;
        state[i + 1] = a0 ^ xtime(a1) ^ xtime(a2) ^ a2 ^ a3;
        state[i + 2] = a0 ^ a1 ^ xtime(a2) ^ xtime(a3) ^ a3;
        state[i + 3] = xtime(a0) ^ a0 ^ a1 ^ a2 ^ xtime(a3);
    }
}

/// @brief Inverse mix columns transformation
static void inv_mix_columns(uint8_t *state)
{
    for (int c = 0; c < 4; c++)
    {
        int i = c * 4;
        uint8_t a0 = state[i + 0];
        uint8_t a1 = state[i + 1];
        uint8_t a2 = state[i + 2];
        uint8_t a3 = state[i + 3];

        state[i + 0] = gf_mul(a0, 0x0e) ^ gf_mul(a1, 0x0b) ^ gf_mul(a2, 0x0d) ^ gf_mul(a3, 0x09);
        state[i + 1] = gf_mul(a0, 0x09) ^ gf_mul(a1, 0x0e) ^ gf_mul(a2, 0x0b) ^ gf_mul(a3, 0x0d);
        state[i + 2] = gf_mul(a0, 0x0d) ^ gf_mul(a1, 0x09) ^ gf_mul(a2, 0x0e) ^ gf_mul(a3, 0x0b);
        state[i + 3] = gf_mul(a0, 0x0b) ^ gf_mul(a1, 0x0d) ^ gf_mul(a2, 0x09) ^ gf_mul(a3, 0x0e);
    }
}

/// @brief XOR state with round key
static void add_round_key(uint8_t *state, const uint8_t *round_key)
{
    for (int i = 0; i < 16; i++)
        state[i] ^= round_key[i];
}

//=============================================================================
// AES Block Cipher
//=============================================================================

/// @brief Encrypt a single 16-byte block
/// @param input 16-byte plaintext block
/// @param output 16-byte ciphertext block
/// @param w Expanded key schedule
/// @param nr Number of rounds (10 or 14)
static void aes_encrypt_block(const uint8_t *input, uint8_t *output, const uint8_t *w, int nr)
{
    uint8_t state[16];
    memcpy(state, input, 16);

    // Initial round key addition
    add_round_key(state, w);

    // Main rounds
    for (int round = 1; round < nr; round++)
    {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, w + round * 16);
    }

    // Final round (no MixColumns)
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, w + nr * 16);

    memcpy(output, state, 16);
}

/// @brief Decrypt a single 16-byte block
/// @param input 16-byte ciphertext block
/// @param output 16-byte plaintext block
/// @param w Expanded key schedule
/// @param nr Number of rounds (10 or 14)
static void aes_decrypt_block(const uint8_t *input, uint8_t *output, const uint8_t *w, int nr)
{
    uint8_t state[16];
    memcpy(state, input, 16);

    // Initial round key addition
    add_round_key(state, w + nr * 16);

    // Main rounds (in reverse)
    for (int round = nr - 1; round > 0; round--)
    {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, w + round * 16);
        inv_mix_columns(state);
    }

    // Final round (no InvMixColumns)
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, w);

    memcpy(output, state, 16);
}

//=============================================================================
// CBC Mode and PKCS7 Padding
//=============================================================================

/// @brief Apply PKCS7 padding to data
/// @param data Input data
/// @param len Length of input data
/// @param out_len Output: length of padded data (always multiple of 16)
/// @return Newly allocated padded data
static uint8_t *pkcs7_pad(const uint8_t *data, size_t len, size_t *out_len)
{
    size_t pad_len = AES_BLOCK_SIZE - (len % AES_BLOCK_SIZE);
    *out_len = len + pad_len;

    uint8_t *padded = (uint8_t *)malloc(*out_len);
    if (!padded)
        rt_trap("AES: memory allocation failed");

    memcpy(padded, data, len);
    memset(padded + len, (uint8_t)pad_len, pad_len);

    return padded;
}

/// @brief Remove PKCS7 padding from data (S-05: constant-time implementation)
/// @param data Padded data
/// @param len Length of padded data
/// @param out_len Output: length of unpadded data
/// @return 0 on success, -1 on invalid padding
static int pkcs7_unpad(const uint8_t *data, size_t len, size_t *out_len)
{
    if (len == 0 || len % AES_BLOCK_SIZE != 0)
        return -1;

    uint8_t pad_byte = data[len - 1];
    if (pad_byte == 0 || pad_byte > AES_BLOCK_SIZE)
        return -1;

    /* S-05: Constant-time padding check — accumulate mismatch bits without
     * branching on individual byte values to prevent timing side-channels. */
    uint8_t mismatch = 0;
    for (size_t i = 0; i < (size_t)AES_BLOCK_SIZE; i++)
    {
        /* Only check bytes that fall within the padding region */
        uint8_t in_range = (uint8_t)(i < (size_t)pad_byte ? 0xFF : 0x00);
        mismatch |= in_range & (data[len - 1 - i] ^ pad_byte);
    }

    if (mismatch != 0)
        return -1;

    *out_len = len - pad_byte;
    return 0;
}

/// @brief Encrypt data using AES-CBC
/// @param plaintext Input data
/// @param len Length of input data
/// @param key Encryption key
/// @param iv Initialization vector (16 bytes)
/// @param nk Key words (4 for AES-128, 8 for AES-256)
/// @param nr Number of rounds (10 for AES-128, 14 for AES-256)
/// @param out_len Output: length of ciphertext
/// @return Newly allocated ciphertext
static uint8_t *aes_cbc_encrypt(const uint8_t *plaintext,
                                size_t len,
                                const uint8_t *key,
                                const uint8_t *iv,
                                int nk,
                                int nr,
                                size_t *out_len)
{
    // Expand key
    size_t w_size = (size_t)(16 * (nr + 1));
    uint8_t *w = (uint8_t *)malloc(w_size);
    if (!w)
        rt_trap("AES: memory allocation failed");
    aes_key_expansion(key, w, nk, nr);

    // Pad plaintext
    size_t padded_len;
    uint8_t *padded = pkcs7_pad(plaintext, len, &padded_len);

    // Allocate ciphertext
    uint8_t *ciphertext = (uint8_t *)malloc(padded_len);
    if (!ciphertext)
    {
        free(w);
        free(padded);
        rt_trap("AES: memory allocation failed");
    }

    // CBC encryption
    uint8_t prev_block[AES_BLOCK_SIZE];
    memcpy(prev_block, iv, AES_BLOCK_SIZE);

    for (size_t i = 0; i < padded_len; i += AES_BLOCK_SIZE)
    {
        uint8_t block[AES_BLOCK_SIZE];

        // XOR with previous ciphertext (or IV for first block)
        for (int j = 0; j < AES_BLOCK_SIZE; j++)
            block[j] = padded[i + j] ^ prev_block[j];

        // Encrypt block
        aes_encrypt_block(block, ciphertext + i, w, nr);

        // Save for next iteration
        memcpy(prev_block, ciphertext + i, AES_BLOCK_SIZE);
    }

    free(w);
    free(padded);
    *out_len = padded_len;
    return ciphertext;
}

/// @brief Decrypt data using AES-CBC
/// @param ciphertext Input data
/// @param len Length of input data (must be multiple of 16)
/// @param key Encryption key
/// @param iv Initialization vector (16 bytes)
/// @param nk Key words (4 for AES-128, 8 for AES-256)
/// @param nr Number of rounds (10 for AES-128, 14 for AES-256)
/// @param out_len Output: length of plaintext
/// @return Newly allocated plaintext, or NULL on error
static uint8_t *aes_cbc_decrypt(const uint8_t *ciphertext,
                                size_t len,
                                const uint8_t *key,
                                const uint8_t *iv,
                                int nk,
                                int nr,
                                size_t *out_len)
{
    if (len == 0 || len % AES_BLOCK_SIZE != 0)
        return NULL;

    // Expand key
    size_t w_size = (size_t)(16 * (nr + 1));
    uint8_t *w = (uint8_t *)malloc(w_size);
    if (!w)
        rt_trap("AES: memory allocation failed");
    aes_key_expansion(key, w, nk, nr);

    // Allocate plaintext buffer
    uint8_t *plaintext = (uint8_t *)malloc(len);
    if (!plaintext)
    {
        free(w);
        rt_trap("AES: memory allocation failed");
    }

    // CBC decryption
    uint8_t prev_block[AES_BLOCK_SIZE];
    memcpy(prev_block, iv, AES_BLOCK_SIZE);

    for (size_t i = 0; i < len; i += AES_BLOCK_SIZE)
    {
        uint8_t decrypted[AES_BLOCK_SIZE];

        // Decrypt block
        aes_decrypt_block(ciphertext + i, decrypted, w, nr);

        // XOR with previous ciphertext (or IV for first block)
        for (int j = 0; j < AES_BLOCK_SIZE; j++)
            plaintext[i + j] = decrypted[j] ^ prev_block[j];

        // Save current ciphertext for next iteration
        memcpy(prev_block, ciphertext + i, AES_BLOCK_SIZE);
    }

    free(w);

    // Remove PKCS7 padding
    size_t unpadded_len;
    if (pkcs7_unpad(plaintext, len, &unpadded_len) != 0)
    {
        free(plaintext);
        return NULL; // Invalid padding
    }

    *out_len = unpadded_len;
    return plaintext;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Encrypt data using AES-CBC.
///
/// Encrypts binary data using AES in CBC mode with PKCS7 padding.
/// Key length determines AES variant: 16 bytes = AES-128, 32 bytes = AES-256.
///
/// @param data Bytes object containing plaintext
/// @param key Bytes object containing key (16 or 32 bytes)
/// @param iv Bytes object containing initialization vector (must be 16 bytes)
/// @return Bytes object containing ciphertext
void *rt_aes_encrypt(void *data, void *key, void *iv)
{
    size_t data_len, key_len, iv_len;
    uint8_t *data_raw = rt_bytes_extract_raw(data, &data_len);
    uint8_t *key_raw = rt_bytes_extract_raw(key, &key_len);
    uint8_t *iv_raw = rt_bytes_extract_raw(iv, &iv_len);

    // Validate key length
    int nk, nr;
    if (key_len == 16)
    {
        nk = 4;
        nr = 10;
    }
    else if (key_len == 32)
    {
        nk = 8;
        nr = 14;
    }
    else
    {
        if (data_raw)
            free(data_raw);
        if (key_raw)
            free(key_raw);
        if (iv_raw)
            free(iv_raw);
        rt_trap("AES: key must be 16 bytes (AES-128) or 32 bytes (AES-256)");
        return NULL;
    }

    // Validate IV length
    if (iv_len != AES_BLOCK_SIZE)
    {
        if (data_raw)
            free(data_raw);
        if (key_raw)
            free(key_raw);
        if (iv_raw)
            free(iv_raw);
        rt_trap("AES: IV must be exactly 16 bytes");
        return NULL;
    }

    // Handle empty data
    if (data_len == 0)
    {
        data_raw = (uint8_t *)malloc(1);
        if (!data_raw)
            rt_trap("AES: memory allocation failed");
        data_len = 0;
    }

    // Encrypt
    size_t cipher_len;
    uint8_t *cipher = aes_cbc_encrypt(data_raw, data_len, key_raw, iv_raw, nk, nr, &cipher_len);

    // Create result
    void *result = rt_bytes_from_raw(cipher, cipher_len);

    free(data_raw);
    free(key_raw);
    free(iv_raw);
    free(cipher);

    return result;
}

/// @brief Decrypt data using AES-CBC.
///
/// Decrypts binary data using AES in CBC mode with PKCS7 padding removal.
/// Key length determines AES variant: 16 bytes = AES-128, 32 bytes = AES-256.
///
/// @param data Bytes object containing ciphertext
/// @param key Bytes object containing key (16 or 32 bytes)
/// @param iv Bytes object containing initialization vector (must be 16 bytes)
/// @return Bytes object containing plaintext, or NULL on decryption error
void *rt_aes_decrypt(void *data, void *key, void *iv)
{
    size_t data_len, key_len, iv_len;
    uint8_t *data_raw = rt_bytes_extract_raw(data, &data_len);
    uint8_t *key_raw = rt_bytes_extract_raw(key, &key_len);
    uint8_t *iv_raw = rt_bytes_extract_raw(iv, &iv_len);

    // Validate key length
    int nk, nr;
    if (key_len == 16)
    {
        nk = 4;
        nr = 10;
    }
    else if (key_len == 32)
    {
        nk = 8;
        nr = 14;
    }
    else
    {
        if (data_raw)
            free(data_raw);
        if (key_raw)
            free(key_raw);
        if (iv_raw)
            free(iv_raw);
        rt_trap("AES: key must be 16 bytes (AES-128) or 32 bytes (AES-256)");
        return NULL;
    }

    // Validate IV length
    if (iv_len != AES_BLOCK_SIZE)
    {
        if (data_raw)
            free(data_raw);
        if (key_raw)
            free(key_raw);
        if (iv_raw)
            free(iv_raw);
        rt_trap("AES: IV must be exactly 16 bytes");
        return NULL;
    }

    // Decrypt
    size_t plain_len;
    uint8_t *plain = aes_cbc_decrypt(data_raw, data_len, key_raw, iv_raw, nk, nr, &plain_len);

    if (data_raw)
        free(data_raw);
    free(key_raw);
    free(iv_raw);

    if (!plain)
    {
        rt_trap("AES: decryption failed (invalid padding or corrupted data)");
        return NULL;
    }

    // Create result
    void *result = rt_bytes_from_raw(plain, plain_len);
    free(plain);

    return result;
}

/// @brief Derive a 32-byte key from password using iterated SHA-256 (S-06).
///
/// Uses 10 000 rounds of SHA-256 with a fixed application salt and a length
/// prefix for domain separation. This is not PBKDF2 (no per-call salt), but
/// significantly harder to brute-force than a single-pass SHA-256.
/// For production-grade security, use PBKDF2-HMAC-SHA256 with a random salt.
#define DERIVE_KEY_ROUNDS 10000

static void derive_key(const char *password, uint8_t key[32])
{
    size_t pass_len = password ? strlen(password) : 0;

    /* Fixed application-level domain separator (S-06) */
    static const uint8_t kSalt[16] = {
        0x56, 0x49, 0x50, 0x45, 0x52, 0x5f, 0x41, 0x45,
        0x53, 0x5f, 0x4b, 0x44, 0x46, 0x5f, 0x76, 0x31};

    /* Build initial block: salt || length_byte || password */
    uint8_t block[16 + 1 + 256];
    size_t capped = pass_len < 256 ? pass_len : 256;
    memcpy(block, kSalt, 16);
    block[16] = (uint8_t)capped;
    memcpy(block + 17, password, capped);

    local_sha256(block, 17 + capped, key);

    /* Iterate to slow down brute-force attacks */
    for (int r = 1; r < DERIVE_KEY_ROUNDS; r++)
        local_sha256(key, 32, key);
}

#undef DERIVE_KEY_ROUNDS

/// @brief Generate random IV using rt_crypto_rand_bytes
static void generate_iv(uint8_t iv[16])
{
    extern void *rt_crypto_rand_bytes(int64_t count);

    void *rand_bytes = rt_crypto_rand_bytes(16);
    for (int i = 0; i < 16; i++)
        iv[i] = (uint8_t)rt_bytes_get(rand_bytes, i);
}

/// @brief Encrypt string using AES-256-CBC with key derivation.
///
/// Encrypts a string using AES-256-CBC. The password is hashed to derive
/// a 32-byte key. A random 16-byte IV is generated and prepended to the
/// ciphertext.
///
/// Output format: [16-byte IV][ciphertext]
///
/// @param data String to encrypt
/// @param password Password string
/// @return Bytes object containing IV + ciphertext
void *rt_aes_encrypt_str(rt_string data, rt_string password)
{
    const char *data_cstr = rt_string_cstr(data);
    const char *pass_cstr = rt_string_cstr(password);

    if (!data_cstr)
        data_cstr = "";
    if (!pass_cstr)
        pass_cstr = "";

    // Derive key from password
    uint8_t key[32];
    derive_key(pass_cstr, key);

    // Generate random IV
    uint8_t iv[16];
    generate_iv(iv);

    // Encrypt
    size_t plain_len = strlen(data_cstr);
    size_t cipher_len;
    uint8_t *cipher =
        aes_cbc_encrypt((const uint8_t *)data_cstr, plain_len, key, iv, 8, 14, &cipher_len);

    // Create output: IV + ciphertext
    size_t total_len = 16 + cipher_len;
    void *result = rt_bytes_new((int64_t)total_len);

    // Write IV
    for (int i = 0; i < 16; i++)
        rt_bytes_set(result, i, iv[i]);

    // Write ciphertext
    for (size_t i = 0; i < cipher_len; i++)
        rt_bytes_set(result, (int64_t)(16 + i), cipher[i]);

    free(cipher);
    return result;
}

/// @brief Decrypt to string using AES-256-CBC with key derivation.
///
/// Decrypts data that was encrypted with rt_aes_encrypt_str. The input
/// should be: [16-byte IV][ciphertext]
///
/// @param data Bytes object containing IV + ciphertext
/// @param password Password string
/// @return Decrypted string
rt_string rt_aes_decrypt_str(void *data, rt_string password)
{
    const char *pass_cstr = rt_string_cstr(password);
    if (!pass_cstr)
        pass_cstr = "";

    // Get data length
    int64_t total_len = rt_bytes_len(data);
    if (total_len < 16)
    {
        rt_trap("AES: encrypted data too short (missing IV)");
        return rt_const_cstr("");
    }

    // Extract IV
    uint8_t iv[16];
    for (int i = 0; i < 16; i++)
        iv[i] = (uint8_t)rt_bytes_get(data, i);

    // Extract ciphertext
    size_t cipher_len = (size_t)(total_len - 16);
    uint8_t *cipher = (uint8_t *)malloc(cipher_len);
    if (!cipher)
        rt_trap("AES: memory allocation failed");

    for (size_t i = 0; i < cipher_len; i++)
        cipher[i] = (uint8_t)rt_bytes_get(data, (int64_t)(16 + i));

    // Derive key from password
    uint8_t key[32];
    derive_key(pass_cstr, key);

    // Decrypt
    size_t plain_len;
    uint8_t *plain = aes_cbc_decrypt(cipher, cipher_len, key, iv, 8, 14, &plain_len);
    free(cipher);

    if (!plain)
    {
        rt_trap("AES: decryption failed (wrong password or corrupted data)");
        return rt_const_cstr("");
    }

    // Create string (ensure null-termination)
    rt_string result = rt_string_from_bytes((const char *)plain, plain_len);
    free(plain);

    return result;
}
