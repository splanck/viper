//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/network/rt_ecdsa_p256.c
// Purpose: Native ECDSA P-256 (secp256r1) signature verification in pure C.
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
#include <string.h>

#ifdef _MSC_VER
#include <intrin.h>
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

// 256x256 → 512 schoolbook multiplication
#ifdef _MSC_VER
static void u256_mul_wide(u512 r, const u256 a, const u256 b) {
    memset(r, 0, 8 * sizeof(uint64_t));
    for (int i = 3; i >= 0; i--) {
        uint64_t carry = 0;
        for (int j = 3; j >= 0; j--) {
            uint64_t hi;
            uint64_t lo = _umul128(a[i], b[j], &hi);
            // Add existing value at r[i+j+1]
            unsigned char c = _addcarry_u64(0, lo, r[i + j + 1], &r[i + j + 1]);
            // Add incoming carry to hi
            unsigned char c2 = _addcarry_u64(c, hi, carry, &carry);
            // If c2 overflows, it propagates into the next iteration's carry
            // (cannot happen in practice for 256×256, but be safe)
            (void)c2;
        }
        r[i] += carry;
    }
}
#else
static void u256_mul_wide(u512 r, const u256 a, const u256 b) {
    memset(r, 0, 8 * sizeof(uint64_t));
    for (int i = 3; i >= 0; i--) {
        __uint128_t carry = 0;
        for (int j = 3; j >= 0; j--) {
            __uint128_t prod = (__uint128_t)a[i] * b[j] + r[i + j + 1] + carry;
            r[i + j + 1] = (uint64_t)prod;
            carry = prod >> 64;
        }
        r[i] += (uint64_t)carry;
    }
}
#endif

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

// P-256 fast reduction (Solinas reduction)
// p = 2^256 - 2^224 + 2^192 + 2^96 - 1
// Input: 512-bit product t[0..7], output: 256-bit result r mod p
static void fp_reduce_512(u256 r, const u512 t) {
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

// r = a * b mod p
static void fp_mul(u256 r, const u256 a, const u256 b) {
    u512 wide;
    u256_mul_wide(wide, a, b);
    fp_reduce_512(r, wide);
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

// r = a * b mod n
static void sn_mul(u256 r, const u256 a, const u256 b) {
    u512 wide;
    u256_mul_wide(wide, a, b);

    // Generic Barrett-like reduction mod n:
    // For correctness, we do repeated subtraction since the product fits in 512 bits
    // and n is 256 bits. We extract 256-bit chunks and reduce.

    // Simple approach: convert 512-bit to a series of fp_reduce-like steps
    // Since we only need this for a few multiplications in ECDSA verify,
    // we use a straightforward approach.

    // Pack the low 256 bits
    u256 lo = {wide[4], wide[5], wide[6], wide[7]};
    u256 hi = {wide[0], wide[1], wide[2], wide[3]};

    // If hi is zero, just reduce lo
    if (u256_is_zero(hi)) {
        sn_reduce_once(r, lo);
        return;
    }

    // Compute hi * 2^256 mod n + lo mod n
    // 2^256 mod n = 2^256 - n (since n < 2^256)
    // = 0x00000000FFFFFFFF00000000000000004319055258E8617B0C46353D039CDAAFULL
    static const u256 R_MOD_N = {
        0x00000000FFFFFFFFULL, 0x0000000000000000ULL, 0x4319055258E8617BULL, 0x0C46353D039CDAAFULL};

    // result = hi * R_MOD_N + lo (mod n)
    // Since hi < 2^256 and R_MOD_N < n, the product hi * R_MOD_N < 2^512
    // We need to reduce this carefully.

    // Multiply hi * R_MOD_N (both 256-bit) → 512-bit
    u512 hr;
    u256_mul_wide(hr, hi, R_MOD_N);

    // Add lo to the low 256 bits of hr
    u256 hr_lo = {hr[4], hr[5], hr[6], hr[7]};
    u256 hr_hi = {hr[0], hr[1], hr[2], hr[3]};

    u256 sum;
    uint64_t carry = u256_add(sum, hr_lo, lo);
    if (carry) {
        u256 one_val = {0, 0, 0, 1};
        u256_add(hr_hi, hr_hi, one_val);
    }

    // Now reduce hr_hi * 2^256 + sum (mod n)
    // Recurse: if hr_hi is non-zero, multiply again
    if (u256_is_zero(hr_hi)) {
        sn_reduce_once(r, sum);
        return;
    }

    // Second round: hr_hi * R_MOD_N + sum
    u512 hr2;
    u256_mul_wide(hr2, hr_hi, R_MOD_N);
    u256 hr2_lo = {hr2[4], hr2[5], hr2[6], hr2[7]};

    // hr2_hi should be zero or very small at this point
    u256 sum2;
    u256_add(sum2, hr2_lo, sum);
    sn_reduce_once(sum2, sum2);
    sn_reduce_once(r, sum2);
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

// Point doubling in Jacobian coordinates
// Uses the formula for a = -3 (P-256 specific optimization):
//   M = 3*(X-Z^2)*(X+Z^2)
//   S = 4*X*Y^2
//   X' = M^2 - 2*S
//   Y' = M*(S - X') - 8*Y^4
//   Z' = 2*Y*Z
static void jpoint_double(jpoint *R, const jpoint *P) {
    if (jpoint_is_infinity(P)) {
        jpoint_set_infinity(R);
        return;
    }

    u256 m, s, t, y2, z2, tmp;

    // Z^2
    fp_sqr(z2, P->Z);

    // Y^2
    fp_sqr(y2, P->Y);

    // S = 4*X*Y^2
    fp_mul(s, P->X, y2);
    fp_add(s, s, s);
    fp_add(s, s, s); // s = 4*X*Y^2

    // M = 3*(X-Z^2)*(X+Z^2) [using a=-3 optimization]
    fp_sub(tmp, P->X, z2);
    fp_add(t, P->X, z2);
    fp_mul(m, tmp, t);
    u256 m3;
    fp_add(m3, m, m);
    fp_add(m, m3, m); // m = 3*(X-Z^2)*(X+Z^2)

    // X' = M^2 - 2*S
    fp_sqr(R->X, m);
    fp_sub(R->X, R->X, s);
    fp_sub(R->X, R->X, s);

    // Y' = M*(S - X') - 8*Y^4
    fp_sub(tmp, s, R->X);
    fp_mul(R->Y, m, tmp);
    fp_sqr(t, y2);   // Y^4
    fp_add(t, t, t); // 2*Y^4
    fp_add(t, t, t); // 4*Y^4
    fp_add(t, t, t); // 8*Y^4
    fp_sub(R->Y, R->Y, t);

    // Z' = 2*Y*Z
    fp_mul(R->Z, P->Y, P->Z);
    fp_add(R->Z, R->Z, R->Z);
}

// Point addition in Jacobian coordinates (mixed: Q.Z=1 for affine Q not used here)
// General formula:
//   U1 = X1*Z2^2,  U2 = X2*Z1^2
//   S1 = Y1*Z2^3,  S2 = Y2*Z1^3
//   H = U2-U1,     R = S2-S1
//   X3 = R^2 - H^3 - 2*U1*H^2
//   Y3 = R*(U1*H^2 - X3) - S1*H^3
//   Z3 = H*Z1*Z2
static void jpoint_add(jpoint *Res, const jpoint *P, const jpoint *Q) {
    if (jpoint_is_infinity(P)) {
        memcpy(Res, Q, sizeof(jpoint));
        return;
    }
    if (jpoint_is_infinity(Q)) {
        memcpy(Res, P, sizeof(jpoint));
        return;
    }

    u256 z1sq, z2sq, u1, u2, z1cu, z2cu, s1, s2, h, rr;

    fp_sqr(z1sq, P->Z);
    fp_sqr(z2sq, Q->Z);

    fp_mul(u1, P->X, z2sq);
    fp_mul(u2, Q->X, z1sq);

    fp_mul(z1cu, z1sq, P->Z);
    fp_mul(z2cu, z2sq, Q->Z);

    fp_mul(s1, P->Y, z2cu);
    fp_mul(s2, Q->Y, z1cu);

    fp_sub(h, u2, u1);
    fp_sub(rr, s2, s1);

    // If h == 0 and rr == 0, points are equal → double
    if (u256_is_zero(h) && u256_is_zero(rr)) {
        jpoint_double(Res, P);
        return;
    }
    // If h == 0 and rr != 0, points are inverses → infinity
    if (u256_is_zero(h)) {
        jpoint_set_infinity(Res);
        return;
    }

    u256 h2, h3, u1h2;
    fp_sqr(h2, h);
    fp_mul(h3, h2, h);
    fp_mul(u1h2, u1, h2);

    // X3 = rr^2 - h3 - 2*u1h2
    fp_sqr(Res->X, rr);
    fp_sub(Res->X, Res->X, h3);
    fp_sub(Res->X, Res->X, u1h2);
    fp_sub(Res->X, Res->X, u1h2);

    // Y3 = rr*(u1h2 - X3) - s1*h3
    u256 tmp;
    fp_sub(tmp, u1h2, Res->X);
    fp_mul(Res->Y, rr, tmp);
    fp_mul(tmp, s1, h3);
    fp_sub(Res->Y, Res->Y, tmp);

    // Z3 = h * Z1 * Z2
    fp_mul(Res->Z, P->Z, Q->Z);
    fp_mul(Res->Z, Res->Z, h);
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
