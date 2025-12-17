//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bits.c
// Purpose: Implement bit manipulation utilities for Viper.Bits.
//
//===----------------------------------------------------------------------===//

#include "rt_bits.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

// ============================================================================
// Basic Bitwise Operations
// ============================================================================

int64_t rt_bits_and(int64_t a, int64_t b)
{
    return a & b;
}

int64_t rt_bits_or(int64_t a, int64_t b)
{
    return a | b;
}

int64_t rt_bits_xor(int64_t a, int64_t b)
{
    return a ^ b;
}

int64_t rt_bits_not(int64_t val)
{
    return ~val;
}

// ============================================================================
// Shift Operations
// ============================================================================

int64_t rt_bits_shl(int64_t val, int64_t count)
{
    // Clamp count to valid range
    if (count < 0 || count >= 64)
        return 0;
    // Cast to unsigned for well-defined behavior, then back to signed
    return (int64_t)((uint64_t)val << count);
}

int64_t rt_bits_shr(int64_t val, int64_t count)
{
    // Arithmetic shift right (sign-extended)
    // C guarantees arithmetic shift for signed types on most compilers
    if (count < 0)
        return val;
    if (count >= 64)
        return (val < 0) ? -1 : 0;
    return val >> count;
}

int64_t rt_bits_ushr(int64_t val, int64_t count)
{
    // Logical shift right (zero-fill)
    if (count < 0 || count >= 64)
        return 0;
    return (int64_t)((uint64_t)val >> count);
}

// ============================================================================
// Rotate Operations
// ============================================================================

int64_t rt_bits_rotl(int64_t val, int64_t count)
{
    uint64_t u = (uint64_t)val;
    // Normalize count to 0-63
    int shift = (int)(count & 63);
    if (shift == 0)
        return val;
    return (int64_t)((u << shift) | (u >> (64 - shift)));
}

int64_t rt_bits_rotr(int64_t val, int64_t count)
{
    uint64_t u = (uint64_t)val;
    // Normalize count to 0-63
    int shift = (int)(count & 63);
    if (shift == 0)
        return val;
    return (int64_t)((u >> shift) | (u << (64 - shift)));
}

// ============================================================================
// Bit Counting Operations
// ============================================================================

int64_t rt_bits_count(int64_t val)
{
    // Population count (number of 1 bits)
#if defined(__GNUC__) || defined(__clang__)
    return (int64_t)__builtin_popcountll((unsigned long long)val);
#elif defined(_MSC_VER)
    return (int64_t)__popcnt64((unsigned __int64)val);
#else
    // Portable fallback using parallel bit counting
    uint64_t u = (uint64_t)val;
    u = u - ((u >> 1) & 0x5555555555555555ULL);
    u = (u & 0x3333333333333333ULL) + ((u >> 2) & 0x3333333333333333ULL);
    u = (u + (u >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int64_t)((u * 0x0101010101010101ULL) >> 56);
#endif
}

int64_t rt_bits_leadz(int64_t val)
{
    // Count leading zeros
    if (val == 0)
        return 64;
#if defined(__GNUC__) || defined(__clang__)
    return (int64_t)__builtin_clzll((unsigned long long)val);
#elif defined(_MSC_VER)
    unsigned long idx;
    if (_BitScanReverse64(&idx, (unsigned __int64)val))
    {
        return (int64_t)(63 - idx);
    }
    return 64;
#else
    // Portable fallback using binary search
    uint64_t u = (uint64_t)val;
    int64_t n = 0;
    if ((u & 0xFFFFFFFF00000000ULL) == 0)
    {
        n += 32;
        u <<= 32;
    }
    if ((u & 0xFFFF000000000000ULL) == 0)
    {
        n += 16;
        u <<= 16;
    }
    if ((u & 0xFF00000000000000ULL) == 0)
    {
        n += 8;
        u <<= 8;
    }
    if ((u & 0xF000000000000000ULL) == 0)
    {
        n += 4;
        u <<= 4;
    }
    if ((u & 0xC000000000000000ULL) == 0)
    {
        n += 2;
        u <<= 2;
    }
    if ((u & 0x8000000000000000ULL) == 0)
    {
        n += 1;
    }
    return n;
#endif
}

int64_t rt_bits_trailz(int64_t val)
{
    // Count trailing zeros
    if (val == 0)
        return 64;
#if defined(__GNUC__) || defined(__clang__)
    return (int64_t)__builtin_ctzll((unsigned long long)val);
#elif defined(_MSC_VER)
    unsigned long idx;
    if (_BitScanForward64(&idx, (unsigned __int64)val))
    {
        return (int64_t)idx;
    }
    return 64;
#else
    // Portable fallback using binary search
    uint64_t u = (uint64_t)val;
    int64_t n = 0;
    if ((u & 0x00000000FFFFFFFFULL) == 0)
    {
        n += 32;
        u >>= 32;
    }
    if ((u & 0x000000000000FFFFULL) == 0)
    {
        n += 16;
        u >>= 16;
    }
    if ((u & 0x00000000000000FFULL) == 0)
    {
        n += 8;
        u >>= 8;
    }
    if ((u & 0x000000000000000FULL) == 0)
    {
        n += 4;
        u >>= 4;
    }
    if ((u & 0x0000000000000003ULL) == 0)
    {
        n += 2;
        u >>= 2;
    }
    if ((u & 0x0000000000000001ULL) == 0)
    {
        n += 1;
    }
    return n;
#endif
}

// ============================================================================
// Bit Manipulation Operations
// ============================================================================

int64_t rt_bits_flip(int64_t val)
{
    // Reverse all 64 bits
    uint64_t u = (uint64_t)val;
    // Swap adjacent bits
    u = ((u >> 1) & 0x5555555555555555ULL) | ((u & 0x5555555555555555ULL) << 1);
    // Swap adjacent 2-bit pairs
    u = ((u >> 2) & 0x3333333333333333ULL) | ((u & 0x3333333333333333ULL) << 2);
    // Swap adjacent 4-bit nibbles
    u = ((u >> 4) & 0x0F0F0F0F0F0F0F0FULL) | ((u & 0x0F0F0F0F0F0F0F0FULL) << 4);
    // Swap adjacent bytes
    u = ((u >> 8) & 0x00FF00FF00FF00FFULL) | ((u & 0x00FF00FF00FF00FFULL) << 8);
    // Swap adjacent 2-byte pairs
    u = ((u >> 16) & 0x0000FFFF0000FFFFULL) | ((u & 0x0000FFFF0000FFFFULL) << 16);
    // Swap adjacent 4-byte pairs
    u = (u >> 32) | (u << 32);
    return (int64_t)u;
}

int64_t rt_bits_swap(int64_t val)
{
    // Byte swap (endian swap)
#if defined(__GNUC__) || defined(__clang__)
    return (int64_t)__builtin_bswap64((uint64_t)val);
#elif defined(_MSC_VER)
    return (int64_t)_byteswap_uint64((unsigned __int64)val);
#else
    uint64_t u = (uint64_t)val;
    u = ((u >> 8) & 0x00FF00FF00FF00FFULL) | ((u & 0x00FF00FF00FF00FFULL) << 8);
    u = ((u >> 16) & 0x0000FFFF0000FFFFULL) | ((u & 0x0000FFFF0000FFFFULL) << 16);
    u = (u >> 32) | (u << 32);
    return (int64_t)u;
#endif
}

// ============================================================================
// Single Bit Operations
// ============================================================================

bool rt_bits_get(int64_t val, int64_t bit)
{
    // Return true if bit is set (bit position 0-63)
    if (bit < 0 || bit >= 64)
        return false;
    return ((uint64_t)val >> bit) & 1;
}

int64_t rt_bits_set(int64_t val, int64_t bit)
{
    // Return val with bit set
    if (bit < 0 || bit >= 64)
        return val;
    return (int64_t)((uint64_t)val | (1ULL << bit));
}

int64_t rt_bits_clear(int64_t val, int64_t bit)
{
    // Return val with bit cleared
    if (bit < 0 || bit >= 64)
        return val;
    return (int64_t)((uint64_t)val & ~(1ULL << bit));
}

int64_t rt_bits_toggle(int64_t val, int64_t bit)
{
    // Return val with bit flipped
    if (bit < 0 || bit >= 64)
        return val;
    return (int64_t)((uint64_t)val ^ (1ULL << bit));
}
