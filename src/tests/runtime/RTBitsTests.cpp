//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTBitsTests.cpp
// Purpose: Tests for Viper.Bits bit manipulation utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_bits.h"
#include "rt_internal.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Basic Bitwise Operations
// ============================================================================

static void test_and()
{
    assert(rt_bits_and(0xFF, 0x0F) == 0x0F);
    assert(rt_bits_and(0xFF00, 0x00FF) == 0);
    assert(rt_bits_and(-1, 0xFFFF) == 0xFFFF);
    assert(rt_bits_and(0x12345678, 0xF0F0F0F0) == 0x10305070);
    printf("test_and: PASSED\n");
}

static void test_or()
{
    assert(rt_bits_or(0xF0, 0x0F) == 0xFF);
    assert(rt_bits_or(0xFF00, 0x00FF) == 0xFFFF);
    assert(rt_bits_or(0, 0) == 0);
    assert(rt_bits_or(0x1234, 0x5678) == 0x567C);
    printf("test_or: PASSED\n");
}

static void test_xor()
{
    assert(rt_bits_xor(0xFF, 0xFF) == 0);
    assert(rt_bits_xor(0xFF, 0x00) == 0xFF);
    assert(rt_bits_xor(0xAAAA, 0x5555) == 0xFFFF);
    assert(rt_bits_xor(0x12345678, 0x12345678) == 0);
    printf("test_xor: PASSED\n");
}

static void test_not()
{
    assert(rt_bits_not(0) == -1);
    assert(rt_bits_not(-1) == 0);
    assert((rt_bits_not(0xFF) & 0xFF) == 0);
    printf("test_not: PASSED\n");
}

// ============================================================================
// Shift Operations
// ============================================================================

static void test_shl()
{
    assert(rt_bits_shl(1, 0) == 1);
    assert(rt_bits_shl(1, 1) == 2);
    assert(rt_bits_shl(1, 4) == 16);
    assert(rt_bits_shl(1, 63) == (int64_t)(1ULL << 63));
    // Out of range
    assert(rt_bits_shl(1, 64) == 0);
    assert(rt_bits_shl(1, -1) == 0);
    printf("test_shl: PASSED\n");
}

static void test_shr()
{
    // Arithmetic shift right (sign-extended)
    assert(rt_bits_shr(16, 2) == 4);
    assert(rt_bits_shr(256, 4) == 16);
    // Negative values should sign-extend
    assert(rt_bits_shr(-16, 2) == -4);
    assert(rt_bits_shr(-1, 10) == -1);
    // Edge cases
    assert(rt_bits_shr(1, 64) == 0);
    assert(rt_bits_shr(-1, 64) == -1);
    printf("test_shr: PASSED\n");
}

static void test_ushr()
{
    // Logical shift right (zero-fill)
    assert(rt_bits_ushr(16, 2) == 4);
    assert(rt_bits_ushr(256, 4) == 16);
    // Negative values should zero-fill
    int64_t neg = -1;
    int64_t result = rt_bits_ushr(neg, 1);
    assert(result > 0); // Should be positive after zero-fill
    assert(result == (int64_t)0x7FFFFFFFFFFFFFFFLL);
    // Edge cases
    assert(rt_bits_ushr(1, 64) == 0);
    assert(rt_bits_ushr(-1, -1) == 0);
    printf("test_ushr: PASSED\n");
}

// ============================================================================
// Rotate Operations
// ============================================================================

static void test_rotl()
{
    assert(rt_bits_rotl(1, 0) == 1);
    assert(rt_bits_rotl(1, 1) == 2);
    assert(rt_bits_rotl(1, 63) == (int64_t)(1ULL << 63));
    assert(rt_bits_rotl(1, 64) == 1);                    // Full rotation
    assert(rt_bits_rotl((int64_t)(1ULL << 63), 1) == 1); // Rotate high bit to low
    printf("test_rotl: PASSED\n");
}

static void test_rotr()
{
    assert(rt_bits_rotr(1, 0) == 1);
    assert(rt_bits_rotr(2, 1) == 1);
    assert(rt_bits_rotr(1, 1) == (int64_t)(1ULL << 63)); // Rotate low bit to high
    assert(rt_bits_rotr(1, 64) == 1);                    // Full rotation
    printf("test_rotr: PASSED\n");
}

// ============================================================================
// Bit Counting Operations
// ============================================================================

static void test_count()
{
    assert(rt_bits_count(0) == 0);
    assert(rt_bits_count(1) == 1);
    assert(rt_bits_count(3) == 2);
    assert(rt_bits_count(7) == 3);
    assert(rt_bits_count(0xFF) == 8);
    assert(rt_bits_count(0xFFFF) == 16);
    assert(rt_bits_count(-1) == 64);                   // All bits set
    assert(rt_bits_count(0x5555555555555555LL) == 32); // Alternating bits
    printf("test_count: PASSED\n");
}

static void test_leadz()
{
    assert(rt_bits_leadz(0) == 64);
    assert(rt_bits_leadz(1) == 63);
    assert(rt_bits_leadz(2) == 62);
    assert(rt_bits_leadz(0xFF) == 56);
    assert(rt_bits_leadz(-1) == 0);                    // All bits set
    assert(rt_bits_leadz((int64_t)(1ULL << 63)) == 0); // High bit set
    printf("test_leadz: PASSED\n");
}

static void test_trailz()
{
    assert(rt_bits_trailz(0) == 64);
    assert(rt_bits_trailz(1) == 0);
    assert(rt_bits_trailz(2) == 1);
    assert(rt_bits_trailz(4) == 2);
    assert(rt_bits_trailz(8) == 3);
    assert(rt_bits_trailz(0x100) == 8);
    assert(rt_bits_trailz(-1) == 0);
    assert(rt_bits_trailz((int64_t)(1ULL << 63)) == 63);
    printf("test_trailz: PASSED\n");
}

// ============================================================================
// Bit Manipulation Operations
// ============================================================================

static void test_flip()
{
    // Reversing 0 should give 0
    assert(rt_bits_flip(0) == 0);
    // Reversing all 1s should give all 1s
    assert(rt_bits_flip(-1) == -1);
    // Reversing 1 should give high bit set
    assert(rt_bits_flip(1) == (int64_t)(1ULL << 63));
    // Reversing high bit should give 1
    assert(rt_bits_flip((int64_t)(1ULL << 63)) == 1);
    // Double flip should restore original
    int64_t val = 0x123456789ABCDEF0LL;
    assert(rt_bits_flip(rt_bits_flip(val)) == val);
    printf("test_flip: PASSED\n");
}

static void test_swap()
{
    // Byte swap
    assert(rt_bits_swap(0) == 0);
    assert(rt_bits_swap(0x0102030405060708LL) == 0x0807060504030201LL);
    // Double swap should restore original
    int64_t val = 0x123456789ABCDEF0LL;
    assert(rt_bits_swap(rt_bits_swap(val)) == val);
    printf("test_swap: PASSED\n");
}

// ============================================================================
// Single Bit Operations
// ============================================================================

static void test_get()
{
    assert(rt_bits_get(1, 0) == true);
    assert(rt_bits_get(1, 1) == false);
    assert(rt_bits_get(2, 0) == false);
    assert(rt_bits_get(2, 1) == true);
    assert(rt_bits_get(0xFF, 7) == true);
    assert(rt_bits_get(0xFF, 8) == false);
    assert(rt_bits_get(-1, 63) == true);
    // Out of range
    assert(rt_bits_get(1, 64) == false);
    assert(rt_bits_get(1, -1) == false);
    printf("test_get: PASSED\n");
}

static void test_set()
{
    assert(rt_bits_set(0, 0) == 1);
    assert(rt_bits_set(0, 1) == 2);
    assert(rt_bits_set(0, 3) == 8);
    assert(rt_bits_set(1, 0) == 1); // Already set
    assert(rt_bits_set(0, 63) == (int64_t)(1ULL << 63));
    // Out of range should return unchanged
    assert(rt_bits_set(0, 64) == 0);
    assert(rt_bits_set(0, -1) == 0);
    printf("test_set: PASSED\n");
}

static void test_clear()
{
    assert(rt_bits_clear(1, 0) == 0);
    assert(rt_bits_clear(3, 0) == 2);
    assert(rt_bits_clear(3, 1) == 1);
    assert(rt_bits_clear(0xFF, 0) == 0xFE);
    assert(rt_bits_clear(0, 0) == 0); // Already clear
    // Out of range should return unchanged
    assert(rt_bits_clear(1, 64) == 1);
    assert(rt_bits_clear(1, -1) == 1);
    printf("test_clear: PASSED\n");
}

static void test_toggle()
{
    assert(rt_bits_toggle(0, 0) == 1);
    assert(rt_bits_toggle(1, 0) == 0);
    assert(rt_bits_toggle(0, 3) == 8);
    assert(rt_bits_toggle(8, 3) == 0);
    assert(rt_bits_toggle(0xFF, 4) == 0xEF);
    // Out of range should return unchanged
    assert(rt_bits_toggle(0, 64) == 0);
    assert(rt_bits_toggle(1, -1) == 1);
    printf("test_toggle: PASSED\n");
}

// ============================================================================
// Combined/Edge Case Tests
// ============================================================================

static void test_combined_operations()
{
    // Test that set then clear restores original
    int64_t val = 0x1234;
    assert(rt_bits_clear(rt_bits_set(val, 20), 20) == val);

    // Test that toggle twice restores original
    assert(rt_bits_toggle(rt_bits_toggle(val, 5), 5) == val);

    // Test count after setting bits
    int64_t zero = 0;
    zero = rt_bits_set(zero, 0);
    zero = rt_bits_set(zero, 10);
    zero = rt_bits_set(zero, 20);
    assert(rt_bits_count(zero) == 3);

    printf("test_combined_operations: PASSED\n");
}

static void test_edge_cases()
{
    // Maximum positive value
    int64_t max_pos = 0x7FFFFFFFFFFFFFFFLL;
    assert(rt_bits_count(max_pos) == 63);
    assert(rt_bits_leadz(max_pos) == 1);

    // Minimum negative value
    int64_t min_neg = (int64_t)(1ULL << 63); // -9223372036854775808
    assert(rt_bits_count(min_neg) == 1);
    assert(rt_bits_leadz(min_neg) == 0);
    assert(rt_bits_trailz(min_neg) == 63);

    printf("test_edge_cases: PASSED\n");
}

int main()
{
    printf("=== Viper.Bits Tests ===\n\n");

    // Basic operations
    test_and();
    test_or();
    test_xor();
    test_not();

    // Shift operations
    test_shl();
    test_shr();
    test_ushr();

    // Rotate operations
    test_rotl();
    test_rotr();

    // Counting operations
    test_count();
    test_leadz();
    test_trailz();

    // Manipulation operations
    test_flip();
    test_swap();

    // Single bit operations
    test_get();
    test_set();
    test_clear();
    test_toggle();

    // Combined tests
    test_combined_operations();
    test_edge_cases();

    printf("\nAll tests passed!\n");
    return 0;
}
