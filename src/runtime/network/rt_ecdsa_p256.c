//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_ecdsa_p256.c
// Purpose: Native ECDSA P-256 (secp256r1) signature verification and
//          signing in pure C.
//          Implements 256-bit modular field arithmetic, Jacobian projective
//          elliptic curve point operations, and the ECDSA verify algorithm.
// Key invariants:
//   - Field elements are uint64_t[4] in big-endian limb order (limb[0] = MSW).
//   - Point-at-infinity represented by Z == 0 in Jacobian coordinates.
//   - No heap allocation; all temporaries are stack-local.
//   - Verification-only: constant-time not required (all inputs are public).
// Ownership/Lifetime:
//   - Pure functions; no state, no side effects beyond output parameters.
// Links: rt_ecdsa_p256.h (API), rt_tls.c (caller)
//
//===----------------------------------------------------------------------===//

#include "rt_ecdsa_p256.h"
#include "rt_crypto.h"
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define ECDSA_P256_MAYBE_UNUSED __attribute__((unused))
#else
#define ECDSA_P256_MAYBE_UNUSED
#endif

//=============================================================================
// 256-bit unsigned integer type (big-endian limb order: [0]=MSW, [3]=LSW)
//=============================================================================

typedef uint64_t u256[4];

// 512-bit for multiplication intermediate
typedef uint64_t u512[8];

//=============================================================================
// P-256 curve constants (NIST FIPS 186-4 / SEC 2)
//=============================================================================

// Field prime p = 2^256 - 2^224 + 2^192 + 2^96 - 1
static const u256 P256_P = {
    0xFFFFFFFF00000001ULL, 0x0000000000000000ULL, 0x00000000FFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};

// Curve order n
static const u256 P256_N = {
    0xFFFFFFFF00000000ULL, 0xFFFFFFFFFFFFFFFFULL, 0xBCE6FAADA7179E84ULL, 0xF3B9CAC2FC632551ULL};

// Curve parameter a = -3 mod p = p - 3
static const u256 P256_A = {
    0xFFFFFFFF00000001ULL, 0x0000000000000000ULL, 0x00000000FFFFFFFFULL, 0xFFFFFFFFFFFFFFFCULL};

// Curve parameter b
static const u256 P256_B = {
    0x5AC635D8AA3A93E7ULL, 0xB3EBBD55769886BCULL, 0x651D06B0CC53B0F6ULL, 0x3BCE3C3E27D2604BULL};

// Generator point G (affine x coordinate)
static const u256 P256_GX = {
    0x6B17D1F2E12C4247ULL, 0xF8BCE6E563A440F2ULL, 0x77037D812DEB33A0ULL, 0xF4A13945D898C296ULL};

// Generator point G (affine y coordinate)
static const u256 P256_GY = {
    0x4FE342E2FE1A7F9BULL, 0x8EE7EB4A7C0F9E16ULL, 0x2BCE33576B315ECEULL, 0xCBB6406837BF51F5ULL};

//=============================================================================
// 256-bit arithmetic helpers
//=============================================================================

static void u256_zero(u256 r) {
    r[0] = r[1] = r[2] = r[3] = 0;
}

static void u256_copy(u256 r, const u256 a) {
    r[0] = a[0];
    r[1] = a[1];
    r[2] = a[2];
    r[3] = a[3];
}

static int u256_is_zero(const u256 a) {
    return (a[0] | a[1] | a[2] | a[3]) == 0;
}

// Compare: returns -1 if a < b, 0 if equal, 1 if a > b
static int u256_cmp(const u256 a, const u256 b) {
    for (int i = 0; i < 4; i++) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

// Load from 32-byte big-endian
static void u256_from_bytes(u256 r, const uint8_t b[32]) {
    for (int i = 0; i < 4; i++) {
        r[i] = 0;
        for (int j = 0; j < 8; j++)
            r[i] = (r[i] << 8) | b[i * 8 + j];
    }
}

static void u256_to_bytes(uint8_t out[32], const u256 a) {
    for (int i = 0; i < 4; i++) {
        uint64_t limb = a[i];
        for (int j = 7; j >= 0; j--) {
            out[i * 8 + j] = (uint8_t)(limb & 0xFF);
            limb >>= 8;
        }
    }
}

// r = a + b, returns carry (0 or 1)
#ifdef _MSC_VER
static uint64_t u256_add(u256 r, const u256 a, const u256 b) {
    unsigned char carry = 0;
    for (int i = 3; i >= 0; i--)
        carry = _addcarry_u64(carry, a[i], b[i], &r[i]);
    return carry;
}
#else
static uint64_t u256_add(u256 r, const u256 a, const u256 b) {
    uint64_t carry = 0;
    for (int i = 3; i >= 0; i--) {
        __uint128_t sum = (__uint128_t)a[i] + b[i] + carry;
        r[i] = (uint64_t)sum;
        carry = (uint64_t)(sum >> 64);
    }
    return carry;
}
#endif

// r = a - b, returns borrow (0 or 1)
#ifdef _MSC_VER
static uint64_t u256_sub(u256 r, const u256 a, const u256 b) {
    unsigned char borrow = 0;
    for (int i = 3; i >= 0; i--)
        borrow = _subborrow_u64(borrow, a[i], b[i], &r[i]);
    return borrow;
}
#else
static uint64_t u256_sub(u256 r, const u256 a, const u256 b) {
    uint64_t borrow = 0;
    for (int i = 3; i >= 0; i--) {
        __uint128_t diff = (__uint128_t)a[i] - b[i] - borrow;
        r[i] = (uint64_t)diff;
        borrow = (diff >> 127) & 1; // top bit indicates borrow
    }
    return borrow;
}
#endif

// 256x256 → 512 schoolbook multiplication.
//
// The field / scalar code stores 256-bit integers as four 64-bit limbs in
// big-endian limb order. To keep the wide multiply simple and portable, we
// expand both operands into little-endian 32-bit digits, do base-2^32
// schoolbook multiplication, normalize carries, then pack back into eight
// 64-bit big-endian limbs.
static ECDSA_P256_MAYBE_UNUSED void u256_mul_wide(u512 r, const u256 a, const u256 b) {
    uint32_t aw[8], bw[8];
    uint64_t acc[16];

    memset(acc, 0, sizeof(acc));

    for (int i = 0; i < 4; i++) {
        uint64_t limb_a = a[3 - i];
        uint64_t limb_b = b[3 - i];
        aw[2 * i] = (uint32_t)(limb_a & 0xFFFFFFFFu);
        aw[2 * i + 1] = (uint32_t)(limb_a >> 32);
        bw[2 * i] = (uint32_t)(limb_b & 0xFFFFFFFFu);
        bw[2 * i + 1] = (uint32_t)(limb_b >> 32);
    }

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++)
            acc[i + j] += (uint64_t)aw[i] * (uint64_t)bw[j];
    }

    for (int i = 0; i < 15; i++) {
        acc[i + 1] += acc[i] >> 32;
        acc[i] &= 0xFFFFFFFFu;
    }
    acc[15] &= 0xFFFFFFFFu;

    for (int i = 0; i < 8; i++)
        r[7 - i] = (acc[2 * i + 1] << 32) | acc[2 * i];
}

static ECDSA_P256_MAYBE_UNUSED int u512_bit_length_le(const uint64_t limbs[8]) {
    for (int i = 7; i >= 0; i--) {
        if (limbs[i] != 0)
            return i * 64 + (64 - __builtin_clzll(limbs[i]));
    }
    return 0;
}

static ECDSA_P256_MAYBE_UNUSED uint64_t u256_shifted_limb_le(const uint64_t mod_le[4],
                                                             int shift,
                                                             int limb_index) {
    const int word_shift = shift / 64;
    const int bit_shift = shift % 64;
    const int base = limb_index - word_shift;
    uint64_t limb = 0;

    if (base >= 0 && base < 4)
        limb |= mod_le[base] << bit_shift;
    if (bit_shift != 0 && base - 1 >= 0 && base - 1 < 4)
        limb |= mod_le[base - 1] >> (64 - bit_shift);
    return limb;
}

static ECDSA_P256_MAYBE_UNUSED int u512_cmp_shifted_u256_le(const uint64_t value_le[8],
                                                            const uint64_t mod_le[4],
                                                            int shift) {
    for (int i = 7; i >= 0; i--) {
        const uint64_t shifted = u256_shifted_limb_le(mod_le, shift, i);
        if (value_le[i] < shifted)
            return -1;
        if (value_le[i] > shifted)
            return 1;
    }
    return 0;
}

static ECDSA_P256_MAYBE_UNUSED void u512_sub_shifted_u256_le(uint64_t value_le[8],
                                                             const uint64_t mod_le[4],
                                                             int shift) {
    uint64_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        const uint64_t shifted = u256_shifted_limb_le(mod_le, shift, i);
#ifdef _MSC_VER
        borrow = _subborrow_u64((unsigned char)borrow, value_le[i], shifted, &value_le[i]);
#else
        __uint128_t diff = (__uint128_t)value_le[i] - shifted - borrow;
        value_le[i] = (uint64_t)diff;
        borrow = (uint64_t)((diff >> 127) & 1);
#endif
    }
}

static ECDSA_P256_MAYBE_UNUSED void u512_mod_u256(u256 out, const u512 wide, const u256 mod) {
    uint64_t value_le[8];
    uint64_t mod_le[4];
    int shift;

    for (int i = 0; i < 8; i++)
        value_le[i] = wide[7 - i];
    for (int i = 0; i < 4; i++)
        mod_le[i] = mod[3 - i];

    shift = u512_bit_length_le(value_le) - 256;
    if (shift < 0)
        shift = 0;

    for (; shift >= 0; shift--) {
        if (u512_cmp_shifted_u256_le(value_le, mod_le, shift) >= 0)
            u512_sub_shifted_u256_le(value_le, mod_le, shift);
    }

    for (int i = 0; i < 4; i++)
        out[3 - i] = value_le[i];
}

//=============================================================================
// Field arithmetic mod p (NIST P-256 prime)
//=============================================================================

// r = a mod p (assumes a < 2p)
static void fp_reduce_once(u256 r, const u256 a) {
    u256 tmp;
    uint64_t borrow = u256_sub(tmp, a, P256_P);
    // If borrow, a < p, so keep a; otherwise use a - p
    if (borrow)
        u256_copy(r, a);
    else
        u256_copy(r, tmp);
}

// r = a + b mod p
static void fp_add(u256 r, const u256 a, const u256 b) {
    uint64_t carry = u256_add(r, a, b);
    if (carry) {
        // Subtract p to bring back in range
        u256_sub(r, r, P256_P);
    } else {
        fp_reduce_once(r, r);
    }
}

// r = a - b mod p
static void fp_sub(u256 r, const u256 a, const u256 b) {
    uint64_t borrow = u256_sub(r, a, b);
    if (borrow) {
        // Add p to make positive
        u256_add(r, r, P256_P);
    }
}

static void u256_mod_add_generic(u256 r, const u256 a, const u256 b, const u256 mod) {
    uint64_t carry = u256_add(r, a, b);
    if (carry || u256_cmp(r, mod) >= 0)
        u256_sub(r, r, mod);
}

static void u256_mod_double_generic(u256 r, const u256 a, const u256 mod) {
    u256_mod_add_generic(r, a, a, mod);
}

static void u256_mod_mul_generic(u256 r, const u256 a, const u256 b, const u256 mod) {
    u256 result, base;
    u256_zero(result);
    u256_copy(base, a);
    while (u256_cmp(base, mod) >= 0)
        u256_sub(base, base, mod);

    for (int limb = 3; limb >= 0; limb--) {
        uint64_t word = b[limb];
        for (int bit = 0; bit < 64; bit++) {
            if (word & 1ULL)
                u256_mod_add_generic(result, result, base, mod);
            u256_mod_double_generic(base, base, mod);
            word >>= 1;
        }
    }
    u256_copy(r, result);
}

// P-256 fast reduction (Solinas reduction)
// p = 2^256 - 2^224 + 2^192 + 2^96 - 1
// Input: 512-bit product t[0..7], output: 256-bit result r mod p
#if defined(__GNUC__) || defined(__clang__)
static ECDSA_P256_MAYBE_UNUSED void fp_reduce_512(u256 r, const u512 t) {
#else
static void fp_reduce_512(u256 r, const u512 t) {
#endif
    // Extract 32-bit words from the 512-bit value (c0=MSW, c15=LSW)
    // t[0] is the most significant 64 bits, t[7] is least significant
    uint64_t c[16];
    for (int i = 0; i < 8; i++) {
        c[2 * i] = t[i] >> 32;
        c[2 * i + 1] = t[i] & 0xFFFFFFFFULL;
    }

    // NIST reduction formulas for P-256 (FIPS 186-4, D.2.3)
    // s1 = (c7, c6, c5, c4, c3, c2, c1, c0) — the low 256 bits
    // s2 = (c15, c14, c13, c12, c11, 0, 0, 0)
    // s3 = (0, c15, c14, c13, c12, 0, 0, 0)
    // s4 = (c15, c14, 0, 0, 0, c10, c9, c8)
    // s5 = (c8, c13, c15, c14, c13, c11, c10, c9)
    // s6 = (c10, c8, 0, 0, 0, c13, c12, c11)
    // s7 = (c11, c9, 0, 0, c15, c14, c13, c12)
    // s8 = (c12, 0, c10, c9, c8, c15, c14, c13)
    // s9 = (c13, 0, c11, c10, c9, 0, c15, c14)
    // result = s1 + 2*s2 + 2*s3 + s4 + s5 − s6 − s7 − s8 − s9 (mod p)

    // We compute using int128 accumulators per 32-bit column (columns c0..c7)
    // Column layout: result = (r7, r6, r5, r4, r3, r2, r1, r0)
    // where r0 is the least significant 32-bit word.
    // In our c[] array, c[15] is the LSW and c[0] is the MSW of the 512-bit input.
    // Rename for clarity: let w[i] = c[15-i], so w[0]=c15(LSW), w[15]=c0(MSW)
    // The NIST formulas use the convention where index 0 = LSW.
    // So s1 = (w[7], w[6], w[5], w[4], w[3], w[2], w[1], w[0])
    //    where w[i] = c[15-i]

    // Let's use w[0..15] where w[0] is the least significant 32-bit word
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)c[15 - i];

    // Accumulate in int64_t to handle carries and borrows
    int64_t acc[8];
    memset(acc, 0, sizeof(acc));

    // s1 = (w7, w6, w5, w4, w3, w2, w1, w0)
    for (int i = 0; i < 8; i++)
        acc[i] += w[i];

    // 2 * s2 = 2 * (w11, 0, 0, 0, 0, w15, w14, w13) -- WRONG, let me redo
    // Actually the NIST formulas in FIPS 186-4 D.2.3 are:
    // (using 0-indexed from LSW, an 8-element tuple)
    // s1 = (c0, c1, c2, c3, c4, c5, c6, c7) [low 256 bits]
    // But our c[] has c0=MSW. Let me just use w[] = LSW-first.
    // w[0..7] = lower 256 bits, w[8..15] = upper 256 bits
    //
    // NIST P-256 reduction (from HAC / FIPS 186-4):
    // s1 = (w[7], w[6], w[5], w[4], w[3], w[2], w[1], w[0])
    // s2 = (w[15], w[14], w[13], w[12], w[11], 0, 0, 0)
    // s3 = (0, w[15], w[14], w[13], w[12], 0, 0, 0)
    // s4 = (w[15], w[14], 0, 0, 0, w[10], w[9], w[8])
    // s5 = (w[8], w[13], w[15], w[14], w[13], w[11], w[10], w[9])
    // s6 = (w[10], w[8], 0, 0, 0, w[13], w[12], w[11])
    // s7 = (w[11], w[9], 0, 0, w[15], w[14], w[13], w[12])
    // s8 = (w[12], 0, w[10], w[9], w[8], w[15], w[14], w[13])
    // s9 = (w[13], 0, w[11], w[10], w[9], 0, w[15], w[14])
    //
    // result = s1 + 2*s2 + 2*s3 + s4 + s5 - s6 - s7 - s8 - s9 (mod p)

    // Reset and recompute properly
    memset(acc, 0, sizeof(acc));

    // s1
    for (int i = 0; i < 8; i++)
        acc[i] += (int64_t)w[i];

    // 2 * s2: (w[15], w[14], w[13], w[12], w[11], 0, 0, 0)
    acc[3] += 2 * (int64_t)w[11];
    acc[4] += 2 * (int64_t)w[12];
    acc[5] += 2 * (int64_t)w[13];
    acc[6] += 2 * (int64_t)w[14];
    acc[7] += 2 * (int64_t)w[15];

    // 2 * s3: (0, w[15], w[14], w[13], w[12], 0, 0, 0)
    acc[3] += 2 * (int64_t)w[12];
    acc[4] += 2 * (int64_t)w[13];
    acc[5] += 2 * (int64_t)w[14];
    acc[6] += 2 * (int64_t)w[15];

    // s4: (w[15], w[14], 0, 0, 0, w[10], w[9], w[8])
    acc[0] += (int64_t)w[8];
    acc[1] += (int64_t)w[9];
    acc[2] += (int64_t)w[10];
    acc[6] += (int64_t)w[14];
    acc[7] += (int64_t)w[15];

    // s5: (w[8], w[13], w[15], w[14], w[13], w[11], w[10], w[9])
    acc[0] += (int64_t)w[9];
    acc[1] += (int64_t)w[10];
    acc[2] += (int64_t)w[11];
    acc[3] += (int64_t)w[13];
    acc[4] += (int64_t)w[14];
    acc[5] += (int64_t)w[15];
    acc[6] += (int64_t)w[13];
    acc[7] += (int64_t)w[8];

    // -s6: -(w[10], w[8], 0, 0, 0, w[13], w[12], w[11])
    acc[0] -= (int64_t)w[11];
    acc[1] -= (int64_t)w[12];
    acc[2] -= (int64_t)w[13];
    acc[6] -= (int64_t)w[8];
    acc[7] -= (int64_t)w[10];

    // -s7: -(w[11], w[9], 0, 0, w[15], w[14], w[13], w[12])
    acc[0] -= (int64_t)w[12];
    acc[1] -= (int64_t)w[13];
    acc[2] -= (int64_t)w[14];
    acc[3] -= (int64_t)w[15];
    acc[6] -= (int64_t)w[9];
    acc[7] -= (int64_t)w[11];

    // -s8: -(w[12], 0, w[10], w[9], w[8], w[15], w[14], w[13])
    acc[0] -= (int64_t)w[13];
    acc[1] -= (int64_t)w[14];
    acc[2] -= (int64_t)w[15];
    acc[3] -= (int64_t)w[8];
    acc[4] -= (int64_t)w[9];
    acc[5] -= (int64_t)w[10];
    acc[7] -= (int64_t)w[12];

    // -s9: -(w[13], 0, w[11], w[10], w[9], 0, w[15], w[14])
    acc[0] -= (int64_t)w[14];
    acc[1] -= (int64_t)w[15];
    acc[3] -= (int64_t)w[9];
    acc[4] -= (int64_t)w[10];
    acc[5] -= (int64_t)w[11];
    acc[7] -= (int64_t)w[13];

    // Propagate carries through the 32-bit columns
    for (int i = 0; i < 7; i++) {
        acc[i + 1] += acc[i] >> 32;
        acc[i] &= 0xFFFFFFFF;
    }

    // Convert back to u256
    // Handle final carries/borrows by adding/subtracting multiples of p
    // Pack 32-bit words into 64-bit limbs (big-endian limb order)
    r[3] = ((uint64_t)(uint32_t)acc[1] << 32) | (uint64_t)(uint32_t)acc[0];
    r[2] = ((uint64_t)(uint32_t)acc[3] << 32) | (uint64_t)(uint32_t)acc[2];
    r[1] = ((uint64_t)(uint32_t)acc[5] << 32) | (uint64_t)(uint32_t)acc[4];
    r[0] = ((uint64_t)(uint32_t)acc[7] << 32) | (uint64_t)(uint32_t)acc[6];

    // Reduce: the result may be slightly negative or > p, so add/sub p as needed
    // acc[7] carries tell us how many times p to add/subtract
    int64_t top = acc[7] >> 32;
    if (top > 0) {
        // Subtract p up to a few times
        for (int64_t i = 0; i < top + 1; i++) {
            u256 tmp;
            if (u256_cmp(r, P256_P) >= 0)
                u256_sub(r, r, P256_P);
            else
                break;
            (void)tmp;
        }
    } else if (top < 0) {
        // Add p to make positive
        for (int64_t i = 0; i > top - 1; i--) {
            u256_add(r, r, P256_P);
            if (u256_cmp(r, P256_P) < 0 || (r[0] >> 63) == 0)
                break;
        }
    }

    // Final conditional subtraction
    fp_reduce_once(r, r);
}

static ECDSA_P256_MAYBE_UNUSED void fp_reduce_512_generic(u256 r, const u512 t) {
    u512_mod_u256(r, t, P256_P);
}

// r = a * b mod p
static void fp_mul(u256 r, const u256 a, const u256 b) {
    u256_mod_mul_generic(r, a, b, P256_P);
}

// r = a^2 mod p
static void fp_sqr(u256 r, const u256 a) {
    fp_mul(r, a, a);
}

// r = a^exp mod p (binary square-and-multiply, exp is u256)
static void fp_pow(u256 r, const u256 a, const u256 exp) {
    u256 base, result;
    u256_copy(base, a);
    result[0] = 0;
    result[1] = 0;
    result[2] = 0;
    result[3] = 1; // result = 1

    for (int i = 0; i < 4; i++) {
        for (int bit = 63; bit >= 0; bit--) {
            fp_sqr(result, result);
            if ((exp[i] >> bit) & 1)
                fp_mul(result, result, base);
        }
    }
    u256_copy(r, result);
}

// r = a^(-1) mod p using Fermat's little theorem: a^(p-2) mod p
static void fp_inv(u256 r, const u256 a) {
    u256 exp;
    u256_copy(exp, P256_P);
    // p - 2
#ifdef _MSC_VER
    unsigned char borrow_c = _subborrow_u64(0, exp[3], 2, &exp[3]);
    for (int i = 2; i >= 0 && borrow_c; i--)
        borrow_c = _subborrow_u64(borrow_c, exp[i], 0, &exp[i]);
#else
    uint64_t borrow = 0;
    __uint128_t diff = (__uint128_t)exp[3] - 2 - borrow;
    exp[3] = (uint64_t)diff;
    borrow = (diff >> 127) & 1;
    for (int i = 2; i >= 0 && borrow; i--) {
        diff = (__uint128_t)exp[i] - borrow;
        exp[i] = (uint64_t)diff;
        borrow = (diff >> 127) & 1;
    }
#endif

    fp_pow(r, a, exp);
}

//=============================================================================
// Scalar arithmetic mod n (curve order)
//=============================================================================

// r = a mod n (assumes a < 2n)
static void sn_reduce_once(u256 r, const u256 a) {
    u256 tmp;
    uint64_t borrow = u256_sub(tmp, a, P256_N);
    if (borrow)
        u256_copy(r, a);
    else
        u256_copy(r, tmp);
}

// r = a + b mod n
static void sn_add(u256 r, const u256 a, const u256 b) {
    uint64_t carry = u256_add(r, a, b);
    if (carry || u256_cmp(r, P256_N) >= 0)
        u256_sub(r, r, P256_N);
}

// r = a * b mod n
static void sn_mul(u256 r, const u256 a, const u256 b) {
    u256_mod_mul_generic(r, a, b, P256_N);
}

// r = a^(-1) mod n using Fermat: a^(n-2) mod n
static void sn_inv(u256 r, const u256 a) {
    u256 exp;
    u256_copy(exp, P256_N);
    // n - 2
#ifdef _MSC_VER
    unsigned char borrow_c = _subborrow_u64(0, exp[3], 2, &exp[3]);
    for (int i = 2; i >= 0 && borrow_c; i--)
        borrow_c = _subborrow_u64(borrow_c, exp[i], 0, &exp[i]);
#else
    uint64_t borrow = 0;
    __uint128_t diff = (__uint128_t)exp[3] - 2 - borrow;
    exp[3] = (uint64_t)diff;
    borrow = (diff >> 127) & 1;
    for (int i = 2; i >= 0 && borrow; i--) {
        diff = (__uint128_t)exp[i] - borrow;
        exp[i] = (uint64_t)diff;
        borrow = (diff >> 127) & 1;
    }
#endif

    // Square-and-multiply: a^exp mod n
    u256 base, result;
    u256_copy(base, a);
    u256_zero(result);
    result[3] = 1; // result = 1

    for (int i = 0; i < 4; i++) {
        for (int bit = 63; bit >= 0; bit--) {
            sn_mul(result, result, result);
            if ((exp[i] >> bit) & 1)
                sn_mul(result, result, base);
        }
    }
    u256_copy(r, result);
}

//=============================================================================
// Jacobian projective point operations on P-256
// Affine (x, y) = (X/Z^2, Y/Z^3), point at infinity has Z = 0
//=============================================================================

typedef struct {
    u256 X, Y, Z;
} jpoint;

static void jpoint_set_infinity(jpoint *P) {
    u256_zero(P->X);
    u256_zero(P->Y);
    u256_zero(P->Z);
    P->Y[3] = 1; // Convention: Y=1 for point at infinity
}

static int jpoint_is_infinity(const jpoint *P) {
    return u256_is_zero(P->Z);
}

static void jpoint_from_affine(jpoint *P, const u256 x, const u256 y) {
    u256_copy(P->X, x);
    u256_copy(P->Y, y);
    u256_zero(P->Z);
    P->Z[3] = 1; // Z = 1
}

static void jpoint_to_affine(u256 x, u256 y, const jpoint *P);

// Point doubling using affine formulas.
//
// This deliberately trades speed for simpler, easier-to-audit arithmetic.
// The server/client TLS stack only needs a small number of scalar multiplies
// per handshake, so the extra inversion cost is acceptable.
static void jpoint_double(jpoint *R, const jpoint *P) {
    static const u256 THREE = {0, 0, 0, 3};

    if (jpoint_is_infinity(P)) {
        jpoint_set_infinity(R);
        return;
    }

    u256 x1, y1, lambda, numerator, denominator, tmp;

    jpoint_to_affine(x1, y1, P);
    if (u256_is_zero(y1)) {
        jpoint_set_infinity(R);
        return;
    }

    fp_sqr(numerator, x1);
    fp_add(tmp, numerator, numerator);
    fp_add(numerator, tmp, numerator); // 3*x^2
    fp_sub(numerator, numerator, THREE); // 3*x^2 - 3

    fp_add(denominator, y1, y1); // 2*y
    fp_inv(denominator, denominator);
    fp_mul(lambda, numerator, denominator);

    fp_sqr(R->X, lambda);
    fp_sub(R->X, R->X, x1);
    fp_sub(R->X, R->X, x1);

    fp_sub(tmp, x1, R->X);
    fp_mul(R->Y, lambda, tmp);
    fp_sub(R->Y, R->Y, y1);

    u256_zero(R->Z);
    R->Z[3] = 1;
}

// Point addition using affine formulas.
static void jpoint_add(jpoint *Res, const jpoint *P, const jpoint *Q) {
    if (jpoint_is_infinity(P)) {
        memcpy(Res, Q, sizeof(jpoint));
        return;
    }
    if (jpoint_is_infinity(Q)) {
        memcpy(Res, P, sizeof(jpoint));
        return;
    }

    u256 x1, y1, x2, y2, lambda, numerator, denominator, tmp;

    jpoint_to_affine(x1, y1, P);
    jpoint_to_affine(x2, y2, Q);

    if (u256_cmp(x1, x2) == 0) {
        if (u256_cmp(y1, y2) != 0) {
            jpoint_set_infinity(Res);
            return;
        }
        jpoint_double(Res, P);
        return;
    }

    fp_sub(numerator, y2, y1);
    fp_sub(denominator, x2, x1);
    fp_inv(denominator, denominator);
    fp_mul(lambda, numerator, denominator);

    fp_sqr(Res->X, lambda);
    fp_sub(Res->X, Res->X, x1);
    fp_sub(Res->X, Res->X, x2);

    fp_sub(tmp, x1, Res->X);
    fp_mul(Res->Y, lambda, tmp);
    fp_sub(Res->Y, Res->Y, y1);

    u256_zero(Res->Z);
    Res->Z[3] = 1;
}

// Scalar multiplication: R = k * P (double-and-add, MSB first)
static void jpoint_scalar_mul(jpoint *R, const u256 k, const jpoint *P) {
    jpoint_set_infinity(R);

    // Find the highest set bit
    int started = 0;
    for (int i = 0; i < 4; i++) {
        for (int bit = 63; bit >= 0; bit--) {
            if (started) {
                jpoint tmp;
                jpoint_double(&tmp, R);
                memcpy(R, &tmp, sizeof(jpoint));
            }

            if ((k[i] >> bit) & 1) {
                started = 1;
                jpoint tmp;
                jpoint_add(&tmp, R, P);
                memcpy(R, &tmp, sizeof(jpoint));
            }
        }
    }
}

// Convert Jacobian → affine: x = X/Z^2, y = Y/Z^3
static void jpoint_to_affine(u256 x, u256 y, const jpoint *P) {
    if (jpoint_is_infinity(P)) {
        u256_zero(x);
        u256_zero(y);
        return;
    }

    u256 z_inv, z_inv2, z_inv3;
    fp_inv(z_inv, P->Z);
    fp_sqr(z_inv2, z_inv);
    fp_mul(z_inv3, z_inv2, z_inv);

    fp_mul(x, P->X, z_inv2);
    fp_mul(y, P->Y, z_inv3);
}

//=============================================================================
// ECDSA P-256 Verification (FIPS 186-4 / SEC1 §4.1.4)
//=============================================================================

/// @brief Verify an ECDSA-P256 signature against a public key and pre-computed message digest,
/// per FIPS 186-4 / SEC1 §4.1.4. Performs full input validation (1 ≤ r, s < n; public key on
/// curve), then computes R = s^-1·(e·G + r·Q) and checks that R.x ≡ r (mod n). All internal
/// scalar math runs against the built-in u256/fp/sn helpers — no external crypto deps.
/// Returns 1 on a valid signature, 0 on any failure (bad input, point at infinity, mismatch).
int ecdsa_p256_verify(const uint8_t pubkey_x[32],
                      const uint8_t pubkey_y[32],
                      const uint8_t digest[32],
                      const uint8_t sig_r[32],
                      const uint8_t sig_s[32]) {
    u256 r, s, e, qx, qy;

    u256_from_bytes(r, sig_r);
    u256_from_bytes(s, sig_s);
    u256_from_bytes(e, digest);
    u256_from_bytes(qx, pubkey_x);
    u256_from_bytes(qy, pubkey_y);

    // Step 1: Check 1 ≤ r, s < n
    if (u256_is_zero(r) || u256_cmp(r, P256_N) >= 0)
        return 0;
    if (u256_is_zero(s) || u256_cmp(s, P256_N) >= 0)
        return 0;

    // Step 2: Validate public key is on the curve
    // Check y^2 = x^3 + ax + b (mod p)
    {
        u256 lhs, rhs, x2, x3, ax, tmp;
        fp_sqr(lhs, qy);          // y^2
        fp_sqr(x2, qx);           // x^2
        fp_mul(x3, x2, qx);       // x^3
        fp_mul(ax, P256_A, qx);   // a*x
        fp_add(rhs, x3, ax);      // x^3 + a*x
        fp_add(rhs, rhs, P256_B); // x^3 + a*x + b
        (void)tmp;
        if (u256_cmp(lhs, rhs) != 0)
            return 0;
    }

    // Step 3: w = s^(-1) mod n
    u256 w;
    sn_inv(w, s);

    // Step 4: u1 = e*w mod n, u2 = r*w mod n
    u256 u1, u2;
    sn_mul(u1, e, w);
    sn_mul(u2, r, w);

    // Step 5: R = u1*G + u2*Q
    jpoint G, Q, R1, R2, R;
    jpoint_from_affine(&G, P256_GX, P256_GY);
    jpoint_from_affine(&Q, qx, qy);

    jpoint_scalar_mul(&R1, u1, &G);
    jpoint_scalar_mul(&R2, u2, &Q);
    jpoint_add(&R, &R1, &R2);

    if (jpoint_is_infinity(&R))
        return 0;

    // Step 6: Convert to affine and check x ≡ r (mod n)
    u256 rx, ry;
    jpoint_to_affine(rx, ry, &R);

    // Reduce rx mod n (in case rx >= n, though unlikely for P-256)
    sn_reduce_once(rx, rx);

    return u256_cmp(rx, r) == 0 ? 1 : 0;
}

int ecdsa_p256_public_from_private(const uint8_t privkey[32],
                                   uint8_t pubkey_x[32],
                                   uint8_t pubkey_y[32]) {
    u256 d;
    u256_from_bytes(d, privkey);
    if (u256_is_zero(d) || u256_cmp(d, P256_N) >= 0)
        return 0;

    jpoint G;
    jpoint Q;
    u256 qx, qy;
    jpoint_from_affine(&G, P256_GX, P256_GY);
    jpoint_scalar_mul(&Q, d, &G);
    if (jpoint_is_infinity(&Q))
        return 0;
    jpoint_to_affine(qx, qy, &Q);
    u256_to_bytes(pubkey_x, qx);
    u256_to_bytes(pubkey_y, qy);
    return 1;
}

int ecdsa_p256_sign(const uint8_t privkey[32],
                    const uint8_t digest[32],
                    uint8_t sig_r[32],
                    uint8_t sig_s[32]) {
    u256 d, e;
    u256_from_bytes(d, privkey);
    u256_from_bytes(e, digest);
    if (u256_is_zero(d) || u256_cmp(d, P256_N) >= 0)
        return 0;

    jpoint G;
    jpoint_from_affine(&G, P256_GX, P256_GY);

    for (int attempt = 0; attempt < 32; attempt++) {
        uint8_t nonce_bytes[32];
        u256 k;
        u256 r, s, tmp, kinv;
        jpoint R;
        u256 rx, ry;

        rt_crypto_random_bytes(nonce_bytes, sizeof(nonce_bytes));
        u256_from_bytes(k, nonce_bytes);
        sn_reduce_once(k, k);
        if (u256_is_zero(k) || u256_cmp(k, P256_N) >= 0)
            continue;

        jpoint_scalar_mul(&R, k, &G);
        if (jpoint_is_infinity(&R))
            continue;

        jpoint_to_affine(rx, ry, &R);
        sn_reduce_once(r, rx);
        if (u256_is_zero(r))
            continue;

        sn_mul(tmp, r, d);
        sn_add(tmp, tmp, e);
        if (u256_is_zero(tmp))
            continue;

        sn_inv(kinv, k);
        sn_mul(s, kinv, tmp);
        if (u256_is_zero(s))
            continue;

        // Canonicalize to low-S form for broader interoperability.
        u256 half_n = {
            0x7FFFFFFF80000000ULL, 0x7FFFFFFFFFFFFFFFULL, 0xDE737D56D38BCF42ULL, 0x79DCE5617E3192A8ULL};
        if (u256_cmp(s, half_n) > 0)
            u256_sub(s, P256_N, s);

        u256_to_bytes(sig_r, r);
        u256_to_bytes(sig_s, s);
        return 1;
    }

    return 0;
}
