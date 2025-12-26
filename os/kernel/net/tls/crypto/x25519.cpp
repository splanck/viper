/**
 * @file x25519.cpp
 * @brief X25519 (Curve25519) scalar multiplication implementation.
 *
 * @details
 * Implements the X25519 functions declared in `x25519.hpp` following RFC 7748.
 * The code performs field arithmetic modulo `2^255 - 19` and uses the
 * Montgomery ladder for scalar multiplication.
 */

#include "x25519.hpp"

namespace viper::crypto
{

// Field element: 256-bit number mod 2^255 - 19
// Represented as 10 limbs of 25.5 bits each (alternating 26 and 25 bits)
using fe = i64[10];

// Field constants
static const fe fe_zero = {0};
static const fe fe_one = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// The base point (generator) - u coordinate = 9
static const u8 basepoint[32] = {9};

/**
 * @brief Decode a 32-byte little-endian field element into the internal limb representation.
 *
 * @details
 * Curve25519 arithmetic is performed modulo `p = 2^255 - 19`. This
 * implementation represents field elements as 10 signed 64-bit limbs that
 * alternate 26 and 25 bits (25.5-bit radix representation), which matches
 * common reference implementations.
 *
 * The input encoding is the standard RFC 7748 little-endian 255-bit value.
 * The decode routine splits the input into limbs and masks them to the
 * expected bit widths.
 *
 * @param h Output field element (10-limb representation).
 * @param s Pointer to 32 input bytes (little-endian).
 */
static void fe_frombytes(fe h, const u8 *s)
{
    // Combine bytes into 10 limbs (alternating 26 and 25 bits)
    h[0] = (static_cast<i64>(s[0]) | (static_cast<i64>(s[1]) << 8) |
            (static_cast<i64>(s[2]) << 16) | ((static_cast<i64>(s[3]) & 0x3) << 24)) &
           0x3ffffff;

    h[1] = ((static_cast<i64>(s[3]) >> 2) | (static_cast<i64>(s[4]) << 6) |
            (static_cast<i64>(s[5]) << 14) | ((static_cast<i64>(s[6]) & 0x7) << 22)) &
           0x1ffffff;

    h[2] = ((static_cast<i64>(s[6]) >> 3) | (static_cast<i64>(s[7]) << 5) |
            (static_cast<i64>(s[8]) << 13) | ((static_cast<i64>(s[9]) & 0x1f) << 21)) &
           0x3ffffff;

    h[3] = ((static_cast<i64>(s[9]) >> 5) | (static_cast<i64>(s[10]) << 3) |
            (static_cast<i64>(s[11]) << 11) | ((static_cast<i64>(s[12]) & 0x3f) << 19)) &
           0x1ffffff;

    h[4] = ((static_cast<i64>(s[12]) >> 6) | (static_cast<i64>(s[13]) << 2) |
            (static_cast<i64>(s[14]) << 10) | (static_cast<i64>(s[15]) << 18)) &
           0x3ffffff;

    h[5] = (static_cast<i64>(s[16]) | (static_cast<i64>(s[17]) << 8) |
            (static_cast<i64>(s[18]) << 16) | ((static_cast<i64>(s[19]) & 0x1) << 24)) &
           0x1ffffff;

    h[6] = ((static_cast<i64>(s[19]) >> 1) | (static_cast<i64>(s[20]) << 7) |
            (static_cast<i64>(s[21]) << 15) | ((static_cast<i64>(s[22]) & 0x7) << 23)) &
           0x3ffffff;

    h[7] = ((static_cast<i64>(s[22]) >> 3) | (static_cast<i64>(s[23]) << 5) |
            (static_cast<i64>(s[24]) << 13) | ((static_cast<i64>(s[25]) & 0xf) << 21)) &
           0x1ffffff;

    h[8] = ((static_cast<i64>(s[25]) >> 4) | (static_cast<i64>(s[26]) << 4) |
            (static_cast<i64>(s[27]) << 12) | ((static_cast<i64>(s[28]) & 0x3f) << 20)) &
           0x3ffffff;

    h[9] = ((static_cast<i64>(s[28]) >> 6) | (static_cast<i64>(s[29]) << 2) |
            (static_cast<i64>(s[30]) << 10) | (static_cast<i64>(s[31]) << 18)) &
           0x1ffffff;
}

/**
 * @brief Encode an internal field element into 32-byte little-endian form.
 *
 * @details
 * Reduces the element modulo `p = 2^255 - 19` and then packs the 10-limb
 * representation back into the standard 32-byte little-endian encoding used by
 * X25519.
 *
 * @param s Output buffer receiving 32 bytes.
 * @param h Input field element (10-limb representation).
 */
static void fe_tobytes(u8 *s, const fe h)
{
    i64 h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
    i64 h5 = h[5], h6 = h[6], h7 = h[7], h8 = h[8], h9 = h[9];

    // Reduce mod p
    i64 q = (19 * h9 + (1 << 24)) >> 25;
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

    // Carry
    i64 c = h0 >> 26;
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

    // Output
    s[0] = static_cast<u8>(h0);
    s[1] = static_cast<u8>(h0 >> 8);
    s[2] = static_cast<u8>(h0 >> 16);
    s[3] = static_cast<u8>((h0 >> 24) | (h1 << 2));
    s[4] = static_cast<u8>(h1 >> 6);
    s[5] = static_cast<u8>(h1 >> 14);
    s[6] = static_cast<u8>((h1 >> 22) | (h2 << 3));
    s[7] = static_cast<u8>(h2 >> 5);
    s[8] = static_cast<u8>(h2 >> 13);
    s[9] = static_cast<u8>((h2 >> 21) | (h3 << 5));
    s[10] = static_cast<u8>(h3 >> 3);
    s[11] = static_cast<u8>(h3 >> 11);
    s[12] = static_cast<u8>((h3 >> 19) | (h4 << 6));
    s[13] = static_cast<u8>(h4 >> 2);
    s[14] = static_cast<u8>(h4 >> 10);
    s[15] = static_cast<u8>(h4 >> 18);
    s[16] = static_cast<u8>(h5);
    s[17] = static_cast<u8>(h5 >> 8);
    s[18] = static_cast<u8>(h5 >> 16);
    s[19] = static_cast<u8>((h5 >> 24) | (h6 << 1));
    s[20] = static_cast<u8>(h6 >> 7);
    s[21] = static_cast<u8>(h6 >> 15);
    s[22] = static_cast<u8>((h6 >> 23) | (h7 << 3));
    s[23] = static_cast<u8>(h7 >> 5);
    s[24] = static_cast<u8>(h7 >> 13);
    s[25] = static_cast<u8>((h7 >> 21) | (h8 << 4));
    s[26] = static_cast<u8>(h8 >> 4);
    s[27] = static_cast<u8>(h8 >> 12);
    s[28] = static_cast<u8>((h8 >> 20) | (h9 << 6));
    s[29] = static_cast<u8>(h9 >> 2);
    s[30] = static_cast<u8>(h9 >> 10);
    s[31] = static_cast<u8>(h9 >> 18);
}

/**
 * @brief Copy one field element to another.
 *
 * @param h Destination field element.
 * @param f Source field element.
 */
static void fe_copy(fe h, const fe f)
{
    for (int i = 0; i < 10; i++)
        h[i] = f[i];
}

/**
 * @brief Add two field elements.
 *
 * @details
 * Computes `h = f + g` in the field representation. Full modular reduction is
 * deferred; subsequent operations typically perform carries/reduction.
 *
 * @param h Output field element.
 * @param f First addend.
 * @param g Second addend.
 */
static void fe_add(fe h, const fe f, const fe g)
{
    for (int i = 0; i < 10; i++)
        h[i] = f[i] + g[i];
}

/**
 * @brief Subtract two field elements.
 *
 * @details
 * Computes `h = f - g` in the field representation. Full modular reduction is
 * deferred; subsequent operations typically perform carries/reduction.
 *
 * @param h Output field element.
 * @param f Minuend.
 * @param g Subtrahend.
 */
static void fe_sub(fe h, const fe f, const fe g)
{
    for (int i = 0; i < 10; i++)
        h[i] = f[i] - g[i];
}

/**
 * @brief Multiply two field elements modulo `p = 2^255 - 19`.
 *
 * @details
 * Computes `h = f * g` using the 10-limb radix representation. The reduction
 * exploits the special form of the modulus (`2^255 ≡ 19 (mod p)`) to fold
 * carries back into the low limbs.
 *
 * This is performance-critical for scalar multiplication but kept relatively
 * straightforward here for clarity.
 *
 * @param h Output field element.
 * @param f First multiplicand.
 * @param g Second multiplicand.
 */
static void fe_mul(fe h, const fe f, const fe g)
{
    i64 f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    i64 f5 = f[5], f6 = f[6], f7 = f[7], f8 = f[8], f9 = f[9];
    i64 g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
    i64 g5 = g[5], g6 = g[6], g7 = g[7], g8 = g[8], g9 = g[9];

    i64 g1_19 = 19 * g1;
    i64 g2_19 = 19 * g2;
    i64 g3_19 = 19 * g3;
    i64 g4_19 = 19 * g4;
    i64 g5_19 = 19 * g5;
    i64 g6_19 = 19 * g6;
    i64 g7_19 = 19 * g7;
    i64 g8_19 = 19 * g8;
    i64 g9_19 = 19 * g9;

    i64 f1_2 = 2 * f1;
    i64 f3_2 = 2 * f3;
    i64 f5_2 = 2 * f5;
    i64 f7_2 = 2 * f7;
    i64 f9_2 = 2 * f9;

    i64 h0 = f0 * g0 + f1_2 * g9_19 + f2 * g8_19 + f3_2 * g7_19 + f4 * g6_19 + f5_2 * g5_19 +
             f6 * g4_19 + f7_2 * g3_19 + f8 * g2_19 + f9_2 * g1_19;
    i64 h1 = f0 * g1 + f1 * g0 + f2 * g9_19 + f3 * g8_19 + f4 * g7_19 + f5 * g6_19 + f6 * g5_19 +
             f7 * g4_19 + f8 * g3_19 + f9 * g2_19;
    i64 h2 = f0 * g2 + f1_2 * g1 + f2 * g0 + f3_2 * g9_19 + f4 * g8_19 + f5_2 * g7_19 + f6 * g6_19 +
             f7_2 * g5_19 + f8 * g4_19 + f9_2 * g3_19;
    i64 h3 = f0 * g3 + f1 * g2 + f2 * g1 + f3 * g0 + f4 * g9_19 + f5 * g8_19 + f6 * g7_19 +
             f7 * g6_19 + f8 * g5_19 + f9 * g4_19;
    i64 h4 = f0 * g4 + f1_2 * g3 + f2 * g2 + f3_2 * g1 + f4 * g0 + f5_2 * g9_19 + f6 * g8_19 +
             f7_2 * g7_19 + f8 * g6_19 + f9_2 * g5_19;
    i64 h5 = f0 * g5 + f1 * g4 + f2 * g3 + f3 * g2 + f4 * g1 + f5 * g0 + f6 * g9_19 + f7 * g8_19 +
             f8 * g7_19 + f9 * g6_19;
    i64 h6 = f0 * g6 + f1_2 * g5 + f2 * g4 + f3_2 * g3 + f4 * g2 + f5_2 * g1 + f6 * g0 +
             f7_2 * g9_19 + f8 * g8_19 + f9_2 * g7_19;
    i64 h7 = f0 * g7 + f1 * g6 + f2 * g5 + f3 * g4 + f4 * g3 + f5 * g2 + f6 * g1 + f7 * g0 +
             f8 * g9_19 + f9 * g8_19;
    i64 h8 = f0 * g8 + f1_2 * g7 + f2 * g6 + f3_2 * g5 + f4 * g4 + f5_2 * g3 + f6 * g2 + f7_2 * g1 +
             f8 * g0 + f9_2 * g9_19;
    i64 h9 = f0 * g9 + f1 * g8 + f2 * g7 + f3 * g6 + f4 * g5 + f5 * g4 + f6 * g3 + f7 * g2 +
             f8 * g1 + f9 * g0;

    // Carry
    i64 c;
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

/**
 * @brief Square a field element modulo `p`.
 *
 * @details
 * Computes `h = f^2` by reusing the generic multiplication routine.
 *
 * @param h Output field element.
 * @param f Input field element.
 */
static void fe_sq(fe h, const fe f)
{
    fe_mul(h, f, f);
}

/**
 * @brief Compute the multiplicative inverse of a field element.
 *
 * @details
 * Computes `out = z^{-1} mod p` using exponentiation by `p-2` (Fermat's little
 * theorem) where `p = 2^255 - 19`.
 *
 * The exponentiation chain is a classic addition-chain for Curve25519 used by
 * reference implementations. It performs a sequence of squarings and
 * multiplications to reach `z^(2^255-21)`.
 *
 * @param out Output field element receiving the inverse.
 * @param z Input field element to invert.
 */
static void fe_invert(fe out, const fe z)
{
    fe t0, t1, t2, t3;

    fe_sq(t0, z);       // t0 = z^2
    fe_sq(t1, t0);      // t1 = z^4
    fe_sq(t1, t1);      // t1 = z^8
    fe_mul(t1, z, t1);  // t1 = z^9
    fe_mul(t0, t0, t1); // t0 = z^11
    fe_sq(t2, t0);      // t2 = z^22
    fe_mul(t1, t1, t2); // t1 = z^31 = z^(2^5-1)
    fe_sq(t2, t1);      // t2 = z^(2^6-2)
    for (int i = 0; i < 4; i++)
        fe_sq(t2, t2);  // t2 = z^(2^10-2^5)
    fe_mul(t1, t2, t1); // t1 = z^(2^10-1)
    fe_sq(t2, t1);      // t2 = z^(2^11-2)
    for (int i = 0; i < 9; i++)
        fe_sq(t2, t2);  // t2 = z^(2^20-2^10)
    fe_mul(t2, t2, t1); // t2 = z^(2^20-1)
    fe_sq(t3, t2);      // t3 = z^(2^21-2)
    for (int i = 0; i < 19; i++)
        fe_sq(t3, t3);  // t3 = z^(2^40-2^20)
    fe_mul(t2, t3, t2); // t2 = z^(2^40-1)
    for (int i = 0; i < 10; i++)
        fe_sq(t2, t2);  // t2 = z^(2^50-2^10)
    fe_mul(t1, t2, t1); // t1 = z^(2^50-1)
    fe_sq(t2, t1);      // t2 = z^(2^51-2)
    for (int i = 0; i < 49; i++)
        fe_sq(t2, t2);  // t2 = z^(2^100-2^50)
    fe_mul(t2, t2, t1); // t2 = z^(2^100-1)
    fe_sq(t3, t2);      // t3 = z^(2^101-2)
    for (int i = 0; i < 99; i++)
        fe_sq(t3, t3);  // t3 = z^(2^200-2^100)
    fe_mul(t2, t3, t2); // t2 = z^(2^200-1)
    for (int i = 0; i < 50; i++)
        fe_sq(t2, t2);  // t2 = z^(2^250-2^50)
    fe_mul(t1, t2, t1); // t1 = z^(2^250-1)
    for (int i = 0; i < 5; i++)
        fe_sq(t1, t1);   // t1 = z^(2^255-32)
    fe_mul(out, t1, t0); // out = z^(2^255-21)
}

/**
 * @brief Conditionally swap two field elements in constant time.
 *
 * @details
 * The Montgomery ladder requires swapping the working points based on the
 * current scalar bit. To avoid leaking scalar bits via timing, the swap is
 * performed using a mask derived from `b` and XOR operations.
 *
 * @param f First field element (may be swapped).
 * @param g Second field element (may be swapped).
 * @param b Swap selector: 1 to swap, 0 to leave unchanged.
 */
static void fe_cswap(fe f, fe g, int b)
{
    i64 mask = -static_cast<i64>(b);
    for (int i = 0; i < 10; i++)
    {
        i64 x = (f[i] ^ g[i]) & mask;
        f[i] ^= x;
        g[i] ^= x;
    }
}

/**
 * @brief Perform X25519 scalar multiplication using the Montgomery ladder.
 *
 * @details
 * Computes `out = scalar * point` on Curve25519 following RFC 7748.
 *
 * High-level steps:
 * - Clamp the scalar (`scalar[0] &= 248`, clear/force high bits) as required.
 * - Decode the input point (u-coordinate) into the internal field format.
 * - Run the Montgomery ladder from bit 254 down to 0, using constant-time
 *   conditional swaps to avoid side-channel leakage.
 * - Convert the resulting u-coordinate back to 32-byte little-endian form.
 *
 * This function is the core of public key derivation and shared-secret
 * computation.
 *
 * @param out Output buffer receiving 32-byte u-coordinate.
 * @param scalar 32-byte scalar (private key).
 * @param point 32-byte input point (peer public key or base point).
 */
static void x25519_scalarmult(u8 out[32], const u8 scalar[32], const u8 point[32])
{
    u8 e[32];
    for (int i = 0; i < 32; i++)
        e[i] = scalar[i];

    // Clamp
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;

    fe x1, x2, z2, x3, z3, tmp0, tmp1;

    fe_frombytes(x1, point);
    fe_copy(x2, fe_one);
    for (int i = 0; i < 10; i++)
        z2[i] = 0;
    fe_copy(x3, x1);
    fe_copy(z3, fe_one);

    int swap = 0;

    for (int pos = 254; pos >= 0; pos--)
    {
        int b = (e[pos / 8] >> (pos & 7)) & 1;
        swap ^= b;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = b;

        // Montgomery ladder step
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
        fe_mul(z3, tmp1, fe_one); // z3 = tmp1 * 121666
        // Actually need to multiply by a121666
        i64 a121666 = 121666;
        for (int i = 0; i < 10; i++)
        {
            z3[i] = tmp1[i] * a121666;
        }
        // Carry
        i64 c;
        for (int i = 0; i < 9; i++)
        {
            if (i & 1)
            {
                c = z3[i] >> 25;
                z3[i + 1] += c;
                z3[i] -= c << 25;
            }
            else
            {
                c = z3[i] >> 26;
                z3[i + 1] += c;
                z3[i] -= c << 26;
            }
        }
        c = z3[9] >> 25;
        z3[0] += c * 19;
        z3[9] -= c << 25;

        fe_sq(x3, x3);
        fe_add(z3, tmp0, z3);
        fe_mul(z3, z3, tmp1);
        fe_mul(z2, z2, x1);
    }

    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
}

/** @copydoc viper::crypto::x25519_clamp */
void x25519_clamp(u8 key[X25519_KEY_SIZE])
{
    key[0] &= 248;
    key[31] &= 127;
    key[31] |= 64;
}

/** @copydoc viper::crypto::x25519_public_key */
void x25519_public_key(const u8 private_key[X25519_KEY_SIZE], u8 public_key[X25519_KEY_SIZE])
{
    x25519_scalarmult(public_key, private_key, basepoint);
}

/** @copydoc viper::crypto::x25519_shared_secret */
bool x25519_shared_secret(const u8 private_key[X25519_KEY_SIZE],
                          const u8 peer_public_key[X25519_KEY_SIZE],
                          u8 shared_secret[X25519_KEY_SIZE])
{
    x25519_scalarmult(shared_secret, private_key, peer_public_key);

    // Check for all-zero output (invalid)
    u8 zero = 0;
    for (int i = 0; i < 32; i++)
    {
        zero |= shared_secret[i];
    }
    return zero != 0;
}

} // namespace viper::crypto
