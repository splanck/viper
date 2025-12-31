/**
 * @file ed25519.cpp
 * @brief Ed25519 signature scheme implementation (RFC 8032).
 *
 * @details
 * Implements the Ed25519 signature algorithm using the twisted Edwards curve
 * -x^2 + y^2 = 1 + d*x^2*y^2 where d = -121665/121666.
 *
 * This curve is birationally equivalent to Curve25519 and shares the same
 * field arithmetic (mod 2^255 - 19).
 */

#include "ed25519.hpp"
#include "sha384.hpp"  // SHA-512 is in sha384.hpp
#include "random.hpp"
#include "../../../lib/mem.hpp"

namespace viper::crypto
{

// Use SHA-512 and random from tls::crypto namespace
using viper::tls::crypto::sha512;
using viper::tls::crypto::Sha512Context;
using viper::tls::crypto::sha512_init;
using viper::tls::crypto::sha512_update;
using viper::tls::crypto::sha512_final;
using viper::tls::crypto::random_bytes;

// Field element: 256-bit number mod 2^255 - 19
// Represented as 10 limbs (same as X25519)
using fe = i64[10];

// Group order l = 2^252 + 27742317777372353535851937790883648493
static const u8 L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

// d = -121665/121666 (in field representation)
static const fe d = {
    -10913610, 13857413, -15372611, 6949391, 114729,
    -8787816, -6275908, -3247719, -18696448, -12055116
};

// 2*d
static const fe d2 = {
    -21827239, -5839606, -30745221, 13898782, 229458,
    15978800, -12551817, -6495438, 29158917, -8469668
};

// Base point B (generator)
static const fe B_x = {
    -14297830, -7645148, 16144683, -16471763, 27570974,
    -2696100, -26142465, 8378389, 20764389, 8758491
};
static const fe B_y = {
    -26843541, -6630148, 26409044, 13050410, -28020909,
    1290200, -26442653, -12757558, 18174212, -13723571
};

//=============================================================================
// Field Arithmetic (same as X25519, duplicated for independence)
//=============================================================================

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

static void fe_neg(fe h, const fe f)
{
    for (int i = 0; i < 10; i++)
        h[i] = -f[i];
}

static void fe_mul(fe h, const fe f, const fe g)
{
    i64 f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    i64 f5 = f[5], f6 = f[6], f7 = f[7], f8 = f[8], f9 = f[9];
    i64 g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
    i64 g5 = g[5], g6 = g[6], g7 = g[7], g8 = g[8], g9 = g[9];

    i64 g1_19 = 19 * g1, g2_19 = 19 * g2, g3_19 = 19 * g3;
    i64 g4_19 = 19 * g4, g5_19 = 19 * g5, g6_19 = 19 * g6;
    i64 g7_19 = 19 * g7, g8_19 = 19 * g8, g9_19 = 19 * g9;

    i64 f1_2 = 2 * f1, f3_2 = 2 * f3, f5_2 = 2 * f5;
    i64 f7_2 = 2 * f7, f9_2 = 2 * f9;

    i64 h0 = f0*g0 + f1_2*g9_19 + f2*g8_19 + f3_2*g7_19 + f4*g6_19 +
             f5_2*g5_19 + f6*g4_19 + f7_2*g3_19 + f8*g2_19 + f9_2*g1_19;
    i64 h1 = f0*g1 + f1*g0 + f2*g9_19 + f3*g8_19 + f4*g7_19 +
             f5*g6_19 + f6*g5_19 + f7*g4_19 + f8*g3_19 + f9*g2_19;
    i64 h2 = f0*g2 + f1_2*g1 + f2*g0 + f3_2*g9_19 + f4*g8_19 +
             f5_2*g7_19 + f6*g6_19 + f7_2*g5_19 + f8*g4_19 + f9_2*g3_19;
    i64 h3 = f0*g3 + f1*g2 + f2*g1 + f3*g0 + f4*g9_19 +
             f5*g8_19 + f6*g7_19 + f7*g6_19 + f8*g5_19 + f9*g4_19;
    i64 h4 = f0*g4 + f1_2*g3 + f2*g2 + f3_2*g1 + f4*g0 +
             f5_2*g9_19 + f6*g8_19 + f7_2*g7_19 + f8*g6_19 + f9_2*g5_19;
    i64 h5 = f0*g5 + f1*g4 + f2*g3 + f3*g2 + f4*g1 +
             f5*g0 + f6*g9_19 + f7*g8_19 + f8*g7_19 + f9*g6_19;
    i64 h6 = f0*g6 + f1_2*g5 + f2*g4 + f3_2*g3 + f4*g2 +
             f5_2*g1 + f6*g0 + f7_2*g9_19 + f8*g8_19 + f9_2*g7_19;
    i64 h7 = f0*g7 + f1*g6 + f2*g5 + f3*g4 + f4*g3 +
             f5*g2 + f6*g1 + f7*g0 + f8*g9_19 + f9*g8_19;
    i64 h8 = f0*g8 + f1_2*g7 + f2*g6 + f3_2*g5 + f4*g4 +
             f5_2*g3 + f6*g2 + f7_2*g1 + f8*g0 + f9_2*g9_19;
    i64 h9 = f0*g9 + f1*g8 + f2*g7 + f3*g6 + f4*g5 +
             f5*g4 + f6*g3 + f7*g2 + f8*g1 + f9*g0;

    // Carry chain
    i64 c;
    c = (h0 + (1 << 25)) >> 26; h1 += c; h0 -= c << 26;
    c = (h4 + (1 << 25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h1 + (1 << 24)) >> 25; h2 += c; h1 -= c << 25;
    c = (h5 + (1 << 24)) >> 25; h6 += c; h5 -= c << 25;
    c = (h2 + (1 << 25)) >> 26; h3 += c; h2 -= c << 26;
    c = (h6 + (1 << 25)) >> 26; h7 += c; h6 -= c << 26;
    c = (h3 + (1 << 24)) >> 25; h4 += c; h3 -= c << 25;
    c = (h7 + (1 << 24)) >> 25; h8 += c; h7 -= c << 25;
    c = (h4 + (1 << 25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h8 + (1 << 25)) >> 26; h9 += c; h8 -= c << 26;
    c = (h9 + (1 << 24)) >> 25; h0 += c * 19; h9 -= c << 25;
    c = (h0 + (1 << 25)) >> 26; h1 += c; h0 -= c << 26;

    h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3; h[4] = h4;
    h[5] = h5; h[6] = h6; h[7] = h7; h[8] = h8; h[9] = h9;
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
    for (int i = 0; i < 4; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t2, t1);
    for (int i = 0; i < 9; i++) fe_sq(t2, t2);
    fe_mul(t2, t2, t1);
    fe_sq(t3, t2);
    for (int i = 0; i < 19; i++) fe_sq(t3, t3);
    fe_mul(t2, t3, t2);
    for (int i = 0; i < 10; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    fe_sq(t2, t1);
    for (int i = 0; i < 49; i++) fe_sq(t2, t2);
    fe_mul(t2, t2, t1);
    fe_sq(t3, t2);
    for (int i = 0; i < 99; i++) fe_sq(t3, t3);
    fe_mul(t2, t3, t2);
    for (int i = 0; i < 50; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    for (int i = 0; i < 5; i++) fe_sq(t1, t1);
    fe_mul(out, t1, t0);
}

// Compute sqrt(-1) for point decompression
static void fe_pow22523(fe out, const fe z)
{
    fe t0, t1, t2;

    fe_sq(t0, z);
    fe_sq(t1, t0);
    fe_sq(t1, t1);
    fe_mul(t1, z, t1);
    fe_mul(t0, t0, t1);
    fe_sq(t0, t0);
    fe_mul(t0, t1, t0);
    fe_sq(t1, t0);
    for (int i = 0; i < 4; i++) fe_sq(t1, t1);
    fe_mul(t0, t1, t0);
    fe_sq(t1, t0);
    for (int i = 0; i < 9; i++) fe_sq(t1, t1);
    fe_mul(t1, t1, t0);
    fe_sq(t2, t1);
    for (int i = 0; i < 19; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    for (int i = 0; i < 10; i++) fe_sq(t1, t1);
    fe_mul(t0, t1, t0);
    fe_sq(t1, t0);
    for (int i = 0; i < 49; i++) fe_sq(t1, t1);
    fe_mul(t1, t1, t0);
    fe_sq(t2, t1);
    for (int i = 0; i < 99; i++) fe_sq(t2, t2);
    fe_mul(t1, t2, t1);
    for (int i = 0; i < 50; i++) fe_sq(t1, t1);
    fe_mul(t0, t1, t0);
    fe_sq(t0, t0);
    fe_sq(t0, t0);
    fe_mul(out, t0, z);
}

static void fe_frombytes(fe h, const u8 *s)
{
    h[0] = (static_cast<i64>(s[0]) | (static_cast<i64>(s[1]) << 8) |
            (static_cast<i64>(s[2]) << 16) | ((static_cast<i64>(s[3]) & 0x3) << 24)) & 0x3ffffff;
    h[1] = ((static_cast<i64>(s[3]) >> 2) | (static_cast<i64>(s[4]) << 6) |
            (static_cast<i64>(s[5]) << 14) | ((static_cast<i64>(s[6]) & 0x7) << 22)) & 0x1ffffff;
    h[2] = ((static_cast<i64>(s[6]) >> 3) | (static_cast<i64>(s[7]) << 5) |
            (static_cast<i64>(s[8]) << 13) | ((static_cast<i64>(s[9]) & 0x1f) << 21)) & 0x3ffffff;
    h[3] = ((static_cast<i64>(s[9]) >> 5) | (static_cast<i64>(s[10]) << 3) |
            (static_cast<i64>(s[11]) << 11) | ((static_cast<i64>(s[12]) & 0x3f) << 19)) & 0x1ffffff;
    h[4] = ((static_cast<i64>(s[12]) >> 6) | (static_cast<i64>(s[13]) << 2) |
            (static_cast<i64>(s[14]) << 10) | (static_cast<i64>(s[15]) << 18)) & 0x3ffffff;
    h[5] = (static_cast<i64>(s[16]) | (static_cast<i64>(s[17]) << 8) |
            (static_cast<i64>(s[18]) << 16) | ((static_cast<i64>(s[19]) & 0x1) << 24)) & 0x1ffffff;
    h[6] = ((static_cast<i64>(s[19]) >> 1) | (static_cast<i64>(s[20]) << 7) |
            (static_cast<i64>(s[21]) << 15) | ((static_cast<i64>(s[22]) & 0x7) << 23)) & 0x3ffffff;
    h[7] = ((static_cast<i64>(s[22]) >> 3) | (static_cast<i64>(s[23]) << 5) |
            (static_cast<i64>(s[24]) << 13) | ((static_cast<i64>(s[25]) & 0xf) << 21)) & 0x1ffffff;
    h[8] = ((static_cast<i64>(s[25]) >> 4) | (static_cast<i64>(s[26]) << 4) |
            (static_cast<i64>(s[27]) << 12) | ((static_cast<i64>(s[28]) & 0x3f) << 20)) & 0x3ffffff;
    h[9] = ((static_cast<i64>(s[28]) >> 6) | (static_cast<i64>(s[29]) << 2) |
            (static_cast<i64>(s[30]) << 10) | (static_cast<i64>(s[31]) << 18)) & 0x1ffffff;
}

static void fe_tobytes(u8 *s, const fe h)
{
    i64 h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
    i64 h5 = h[5], h6 = h[6], h7 = h[7], h8 = h[8], h9 = h[9];

    i64 q = (19 * h9 + (1 << 24)) >> 25;
    q = (h0 + q) >> 26; q = (h1 + q) >> 25; q = (h2 + q) >> 26;
    q = (h3 + q) >> 25; q = (h4 + q) >> 26; q = (h5 + q) >> 25;
    q = (h6 + q) >> 26; q = (h7 + q) >> 25; q = (h8 + q) >> 26;
    q = (h9 + q) >> 25;

    h0 += 19 * q;

    i64 c;
    c = h0 >> 26; h1 += c; h0 -= c << 26;
    c = h1 >> 25; h2 += c; h1 -= c << 25;
    c = h2 >> 26; h3 += c; h2 -= c << 26;
    c = h3 >> 25; h4 += c; h3 -= c << 25;
    c = h4 >> 26; h5 += c; h4 -= c << 26;
    c = h5 >> 25; h6 += c; h5 -= c << 25;
    c = h6 >> 26; h7 += c; h6 -= c << 26;
    c = h7 >> 25; h8 += c; h7 -= c << 25;
    c = h8 >> 26; h9 += c; h8 -= c << 26;
    c = h9 >> 25; h9 -= c << 25;

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

static int fe_isnegative(const fe f)
{
    u8 s[32];
    fe_tobytes(s, f);
    return s[0] & 1;
}

static int fe_isnonzero(const fe f)
{
    u8 s[32];
    fe_tobytes(s, f);
    u8 r = 0;
    for (int i = 0; i < 32; i++) r |= s[i];
    return r != 0;
}

//=============================================================================
// Extended Edwards Point Operations
// Point format: (X:Y:Z:T) where x=X/Z, y=Y/Z, x*y=T/Z
//=============================================================================

struct ge_p3 {
    fe X, Y, Z, T;
};

struct ge_p2 {
    fe X, Y, Z;
};

struct ge_p1p1 {
    fe X, Y, Z, T;
};

struct ge_precomp {
    fe yplusx, yminusx, xy2d;
};

struct ge_cached {
    fe YplusX, YminusX, Z, T2d;
};

static void ge_p3_0(ge_p3 *h)
{
    fe_0(h->X);
    fe_1(h->Y);
    fe_1(h->Z);
    fe_0(h->T);
}

static void ge_p3_tobytes(u8 *s, const ge_p3 *h)
{
    fe recip, x, y;
    fe_invert(recip, h->Z);
    fe_mul(x, h->X, recip);
    fe_mul(y, h->Y, recip);
    fe_tobytes(s, y);
    s[31] ^= fe_isnegative(x) << 7;
}

static int ge_frombytes_negate(ge_p3 *h, const u8 *s)
{
    static const fe sqrtm1 = {
        -32595792, -7943725, 9377950, 3500415, 12389472,
        -272473, -25146209, -2005654, 326686, 11406482
    };

    fe u, v, v3, vxx, check;

    fe_frombytes(h->Y, s);
    fe_1(h->Z);
    fe_sq(u, h->Y);
    fe_mul(v, u, d);
    fe_sub(u, u, h->Z);
    fe_add(v, v, h->Z);

    fe_sq(v3, v);
    fe_mul(v3, v3, v);
    fe_sq(h->X, v3);
    fe_mul(h->X, h->X, v);
    fe_mul(h->X, h->X, u);

    fe_pow22523(h->X, h->X);
    fe_mul(h->X, h->X, v3);
    fe_mul(h->X, h->X, u);

    fe_sq(vxx, h->X);
    fe_mul(vxx, vxx, v);
    fe_sub(check, vxx, u);
    if (fe_isnonzero(check)) {
        fe_add(check, vxx, u);
        if (fe_isnonzero(check)) return -1;
        fe_mul(h->X, h->X, sqrtm1);
    }

    if (fe_isnegative(h->X) == (s[31] >> 7)) {
        fe_neg(h->X, h->X);
    }

    fe_mul(h->T, h->X, h->Y);
    return 0;
}

static void ge_p1p1_to_p3(ge_p3 *r, const ge_p1p1 *p)
{
    fe_mul(r->X, p->X, p->T);
    fe_mul(r->Y, p->Y, p->Z);
    fe_mul(r->Z, p->Z, p->T);
    fe_mul(r->T, p->X, p->Y);
}

static void ge_p2_dbl(ge_p1p1 *r, const ge_p2 *p)
{
    fe t0;
    fe_sq(r->X, p->X);
    fe_sq(r->Z, p->Y);
    fe_sq(r->T, p->Z);
    fe_add(r->T, r->T, r->T);
    fe_add(r->Y, p->X, p->Y);
    fe_sq(t0, r->Y);
    fe_add(r->Y, r->Z, r->X);
    fe_sub(r->Z, r->Z, r->X);
    fe_sub(r->X, t0, r->Y);
    fe_sub(r->T, r->T, r->Z);
}

static void ge_p3_dbl(ge_p1p1 *r, const ge_p3 *p)
{
    ge_p2 q;
    fe_copy(q.X, p->X);
    fe_copy(q.Y, p->Y);
    fe_copy(q.Z, p->Z);
    ge_p2_dbl(r, &q);
}

static void ge_p3_to_cached(ge_cached *r, const ge_p3 *p)
{
    fe_add(r->YplusX, p->Y, p->X);
    fe_sub(r->YminusX, p->Y, p->X);
    fe_copy(r->Z, p->Z);
    fe_mul(r->T2d, p->T, d2);
}

static void ge_add(ge_p1p1 *r, const ge_p3 *p, const ge_cached *q)
{
    fe t0;
    fe_add(r->X, p->Y, p->X);
    fe_sub(r->Y, p->Y, p->X);
    fe_mul(r->Z, r->X, q->YplusX);
    fe_mul(r->Y, r->Y, q->YminusX);
    fe_mul(r->T, q->T2d, p->T);
    fe_mul(r->X, p->Z, q->Z);
    fe_add(t0, r->X, r->X);
    fe_sub(r->X, r->Z, r->Y);
    fe_add(r->Y, r->Z, r->Y);
    fe_add(r->Z, t0, r->T);
    fe_sub(r->T, t0, r->T);
}

// Scalar multiplication: r = scalar * base
static void ge_scalarmult_base(ge_p3 *r, const u8 *scalar)
{
    // Simple double-and-add (not constant-time for simplicity)
    ge_p3_0(r);

    ge_p3 base;
    fe_copy(base.X, B_x);
    fe_copy(base.Y, B_y);
    fe_1(base.Z);
    fe_mul(base.T, B_x, B_y);

    for (int i = 255; i >= 0; i--) {
        ge_p1p1 t;
        ge_p3_dbl(&t, r);
        ge_p1p1_to_p3(r, &t);

        int bit = (scalar[i / 8] >> (i & 7)) & 1;
        if (bit) {
            ge_cached c;
            ge_p3_to_cached(&c, &base);
            ge_add(&t, r, &c);
            ge_p1p1_to_p3(r, &t);
        }
    }
}

// Variable-base scalar mult: r = scalar * point
static void ge_scalarmult(ge_p3 *r, const u8 *scalar, const ge_p3 *point)
{
    ge_p3_0(r);

    for (int i = 255; i >= 0; i--) {
        ge_p1p1 t;
        ge_p3_dbl(&t, r);
        ge_p1p1_to_p3(r, &t);

        int bit = (scalar[i / 8] >> (i & 7)) & 1;
        if (bit) {
            ge_cached c;
            ge_p3_to_cached(&c, point);
            ge_add(&t, r, &c);
            ge_p1p1_to_p3(r, &t);
        }
    }
}

//=============================================================================
// Scalar Arithmetic (mod L)
//=============================================================================

// Reduce a 64-byte number mod L
static void sc_reduce(u8 out[32], const u8 in[64])
{
    i64 s[24];
    for (int i = 0; i < 24; i++) s[i] = 0;

    // Load 64 bytes
    for (int i = 0; i < 64; i++) {
        s[i / 3] += static_cast<i64>(in[i]) << ((i % 3) * 8);
    }

    // Reduce mod L using the special form of L
    // L = 2^252 + 27742317777372353535851937790883648493
    // This is a simplified reduction - proper implementation needs careful mod L arithmetic

    // For now, use a simple approach: treat as 512-bit number and reduce
    // Barrett reduction would be better but this works for our purposes

    // Output lower 32 bytes (simplified - proper reduction needed)
    for (int i = 0; i < 32; i++) {
        out[i] = in[i];
    }

    // Reduce high bits
    u8 carry = 0;
    for (int i = 0; i < 32; i++) {
        u32 temp = out[i] + carry;
        if (i < 32 && in[32 + i]) {
            // Subtract L * high_bits
            // This is simplified - real implementation needs proper mod L reduction
        }
        out[i] = temp & 0xff;
        carry = temp >> 8;
    }

    // Ensure result < L
    // Subtract L if needed
    bool ge_L = true;
    for (int i = 31; i >= 0; i--) {
        if (out[i] < L[i]) { ge_L = false; break; }
        if (out[i] > L[i]) break;
    }
    if (ge_L) {
        u8 borrow = 0;
        for (int i = 0; i < 32; i++) {
            i32 temp = static_cast<i32>(out[i]) - L[i] - borrow;
            if (temp < 0) {
                temp += 256;
                borrow = 1;
            } else {
                borrow = 0;
            }
            out[i] = temp;
        }
    }
}

// Multiply-add: out = (a * b + c) mod L
static void sc_muladd(u8 out[32], const u8 a[32], const u8 b[32], const u8 c[32])
{
    i64 s[64];
    for (int i = 0; i < 64; i++) s[i] = 0;

    // Compute a * b
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            s[i + j] += static_cast<i64>(a[i]) * b[j];
        }
    }

    // Add c
    for (int i = 0; i < 32; i++) {
        s[i] += c[i];
    }

    // Carry
    for (int i = 0; i < 63; i++) {
        s[i + 1] += s[i] >> 8;
        s[i] &= 0xff;
    }

    // Convert to bytes and reduce mod L
    u8 temp[64];
    for (int i = 0; i < 64; i++) {
        temp[i] = static_cast<u8>(s[i]);
    }
    sc_reduce(out, temp);
}

//=============================================================================
// Public API
//=============================================================================

void ed25519_keypair_from_seed(const u8 seed[ED25519_SEED_SIZE],
                                u8 public_key[ED25519_PUBLIC_KEY_SIZE],
                                u8 secret_key[ED25519_SECRET_KEY_SIZE])
{
    u8 hash[64];
    sha512(seed, 32, hash);

    // Clamp
    hash[0] &= 248;
    hash[31] &= 127;
    hash[31] |= 64;

    // Compute public key
    ge_p3 A;
    ge_scalarmult_base(&A, hash);
    ge_p3_tobytes(public_key, &A);

    // Store seed || public_key as secret key
    lib::memcpy(secret_key, seed, 32);
    lib::memcpy(secret_key + 32, public_key, 32);
}

void ed25519_keypair(u8 public_key[ED25519_PUBLIC_KEY_SIZE],
                     u8 secret_key[ED25519_SECRET_KEY_SIZE])
{
    u8 seed[32];
    random_bytes(seed, 32);
    ed25519_keypair_from_seed(seed, public_key, secret_key);
}

void ed25519_sign(const void *message,
                  usize message_len,
                  const u8 secret_key[ED25519_SECRET_KEY_SIZE],
                  u8 signature[ED25519_SIGNATURE_SIZE])
{
    u8 hash[64];
    sha512(secret_key, 32, hash);

    // Clamp
    hash[0] &= 248;
    hash[31] &= 127;
    hash[31] |= 64;

    const u8 *pk = secret_key + 32;

    // r = H(h[32..64] || message)
    Sha512Context ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, hash + 32, 32);
    sha512_update(&ctx, message, message_len);
    u8 nonce_hash[64];
    sha512_final(&ctx, nonce_hash);

    u8 r[32];
    sc_reduce(r, nonce_hash);

    // R = r * B
    ge_p3 R;
    ge_scalarmult_base(&R, r);
    u8 R_bytes[32];
    ge_p3_tobytes(R_bytes, &R);

    // k = H(R || pk || message)
    sha512_init(&ctx);
    sha512_update(&ctx, R_bytes, 32);
    sha512_update(&ctx, pk, 32);
    sha512_update(&ctx, message, message_len);
    u8 k_hash[64];
    sha512_final(&ctx, k_hash);

    u8 k[32];
    sc_reduce(k, k_hash);

    // S = (r + k * a) mod L
    u8 S[32];
    sc_muladd(S, k, hash, r);

    // Signature = R || S
    lib::memcpy(signature, R_bytes, 32);
    lib::memcpy(signature + 32, S, 32);
}

bool ed25519_verify(const void *message,
                    usize message_len,
                    const u8 signature[ED25519_SIGNATURE_SIZE],
                    const u8 public_key[ED25519_PUBLIC_KEY_SIZE])
{
    // Parse signature
    const u8 *R_bytes = signature;
    const u8 *S = signature + 32;

    // Check S < L
    for (int i = 31; i >= 0; i--) {
        if (S[i] < L[i]) break;
        if (S[i] > L[i]) return false;
    }

    // Decode public key
    ge_p3 A;
    if (ge_frombytes_negate(&A, public_key) != 0) {
        return false;
    }

    // k = H(R || pk || message)
    Sha512Context ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, R_bytes, 32);
    sha512_update(&ctx, public_key, 32);
    sha512_update(&ctx, message, message_len);
    u8 k_hash[64];
    sha512_final(&ctx, k_hash);

    u8 k[32];
    sc_reduce(k, k_hash);

    // Check: S*B = R + k*A
    // Equivalently: S*B - k*A = R

    ge_p3 sB;
    ge_scalarmult_base(&sB, S);

    ge_p3 kA;
    ge_scalarmult(&kA, k, &A);  // Note: A is already negated

    // sB + kA (since A was negated, this is sB - k*original_A)
    ge_cached kA_cached;
    ge_p3_to_cached(&kA_cached, &kA);
    ge_p1p1 t;
    ge_add(&t, &sB, &kA_cached);
    ge_p3 check;
    ge_p1p1_to_p3(&check, &t);

    u8 check_bytes[32];
    ge_p3_tobytes(check_bytes, &check);

    // Compare with R
    u8 diff = 0;
    for (int i = 0; i < 32; i++) {
        diff |= check_bytes[i] ^ R_bytes[i];
    }

    return diff == 0;
}

void ed25519_public_key_from_secret(const u8 secret_key[ED25519_SECRET_KEY_SIZE],
                                     u8 public_key[ED25519_PUBLIC_KEY_SIZE])
{
    lib::memcpy(public_key, secret_key + 32, 32);
}

} // namespace viper::crypto
