//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bigint.h
// Purpose: Arbitrary precision integer arithmetic for Viper.Math.BigInt.
// Key invariants: All operations produce normalized results (no leading zeros).
// Ownership/Lifetime: Returned values are newly allocated, ref-counted objects.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // BigInt Creation
    //=========================================================================

    /// @brief Create a BigInt from a 64-bit integer.
    /// @param val The integer value.
    /// @return New BigInt object (refcount = 1).
    void *rt_bigint_from_i64(int64_t val);

    /// @brief Create a BigInt from a string.
    /// @param str String representation (decimal, or hex with 0x prefix).
    /// @return New BigInt object, or NULL if invalid string.
    void *rt_bigint_from_str(rt_string str);

    /// @brief Create a BigInt from a byte array (big-endian two's complement).
    /// @param bytes Byte sequence.
    /// @return New BigInt object.
    void *rt_bigint_from_bytes(void *bytes);

    /// @brief Create a BigInt representing zero.
    /// @return New BigInt object.
    void *rt_bigint_zero(void);

    /// @brief Create a BigInt representing one.
    /// @return New BigInt object.
    void *rt_bigint_one(void);

    //=========================================================================
    // Conversion
    //=========================================================================

    /// @brief Convert BigInt to 64-bit integer.
    /// @param a BigInt to convert.
    /// @return Integer value (truncated if too large).
    int64_t rt_bigint_to_i64(void *a);

    /// @brief Convert BigInt to decimal string.
    /// @param a BigInt to convert.
    /// @return Decimal string representation.
    rt_string rt_bigint_to_str(void *a);

    /// @brief Convert BigInt to string in given base.
    /// @param a BigInt to convert.
    /// @param base Base (2-36).
    /// @return String representation in specified base.
    rt_string rt_bigint_to_str_base(void *a, int64_t base);

    /// @brief Convert BigInt to big-endian byte array.
    /// @param a BigInt to convert.
    /// @return Byte array (two's complement).
    void *rt_bigint_to_bytes(void *a);

    /// @brief Check if BigInt fits in 64-bit signed integer.
    /// @param a BigInt to check.
    /// @return 1 if fits, 0 otherwise.
    int8_t rt_bigint_fits_i64(void *a);

    //=========================================================================
    // Basic Arithmetic
    //=========================================================================

    /// @brief Add two BigInts.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with result.
    void *rt_bigint_add(void *a, void *b);

    /// @brief Subtract two BigInts (a - b).
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with result.
    void *rt_bigint_sub(void *a, void *b);

    /// @brief Multiply two BigInts.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with result.
    void *rt_bigint_mul(void *a, void *b);

    /// @brief Divide two BigInts (a / b), truncated toward zero.
    /// @param a Dividend.
    /// @param b Divisor.
    /// @return New BigInt with quotient. Traps on division by zero.
    void *rt_bigint_div(void *a, void *b);

    /// @brief Remainder of division (a % b).
    /// @param a Dividend.
    /// @param b Divisor.
    /// @return New BigInt with remainder. Traps on division by zero.
    void *rt_bigint_mod(void *a, void *b);

    /// @brief Divide and get both quotient and remainder.
    /// @param a Dividend.
    /// @param b Divisor.
    /// @param[out] remainder Pointer to store remainder (may be NULL).
    /// @return New BigInt with quotient. Traps on division by zero.
    void *rt_bigint_divmod(void *a, void *b, void **remainder);

    /// @brief Negate a BigInt.
    /// @param a Operand.
    /// @return New BigInt with negated value.
    void *rt_bigint_neg(void *a);

    /// @brief Absolute value.
    /// @param a Operand.
    /// @return New BigInt with absolute value.
    void *rt_bigint_abs(void *a);

    //=========================================================================
    // Comparison
    //=========================================================================

    /// @brief Compare two BigInts.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return -1 if a < b, 0 if a == b, 1 if a > b.
    int64_t rt_bigint_cmp(void *a, void *b);

    /// @brief Check if two BigInts are equal.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 if equal, 0 otherwise.
    int8_t rt_bigint_eq(void *a, void *b);

    /// @brief Check if BigInt is zero.
    /// @param a Operand.
    /// @return 1 if zero, 0 otherwise.
    int8_t rt_bigint_is_zero(void *a);

    /// @brief Check if BigInt is negative.
    /// @param a Operand.
    /// @return 1 if negative, 0 otherwise.
    int8_t rt_bigint_is_negative(void *a);

    /// @brief Get the sign of a BigInt.
    /// @param a Operand.
    /// @return -1 if negative, 0 if zero, 1 if positive.
    int64_t rt_bigint_sign(void *a);

    //=========================================================================
    // Bitwise Operations
    //=========================================================================

    /// @brief Bitwise AND.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with result.
    void *rt_bigint_and(void *a, void *b);

    /// @brief Bitwise OR.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with result.
    void *rt_bigint_or(void *a, void *b);

    /// @brief Bitwise XOR.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with result.
    void *rt_bigint_xor(void *a, void *b);

    /// @brief Bitwise NOT (one's complement).
    /// @param a Operand.
    /// @return New BigInt with result.
    void *rt_bigint_not(void *a);

    /// @brief Left shift.
    /// @param a Value to shift.
    /// @param n Number of bits to shift.
    /// @return New BigInt with result.
    void *rt_bigint_shl(void *a, int64_t n);

    /// @brief Right shift (arithmetic).
    /// @param a Value to shift.
    /// @param n Number of bits to shift.
    /// @return New BigInt with result.
    void *rt_bigint_shr(void *a, int64_t n);

    //=========================================================================
    // Advanced Operations
    //=========================================================================

    /// @brief Compute power (a^n).
    /// @param a Base.
    /// @param n Exponent (must be non-negative).
    /// @return New BigInt with result. Traps on negative exponent.
    void *rt_bigint_pow(void *a, int64_t n);

    /// @brief Compute modular exponentiation (a^n mod m).
    /// @param a Base.
    /// @param n Exponent.
    /// @param m Modulus.
    /// @return New BigInt with result.
    void *rt_bigint_pow_mod(void *a, void *n, void *m);

    /// @brief Compute greatest common divisor.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with GCD.
    void *rt_bigint_gcd(void *a, void *b);

    /// @brief Compute least common multiple.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return New BigInt with LCM.
    void *rt_bigint_lcm(void *a, void *b);

    /// @brief Get the number of bits needed to represent the value.
    /// @param a Operand.
    /// @return Number of bits (excluding sign).
    int64_t rt_bigint_bit_length(void *a);

    /// @brief Test a specific bit.
    /// @param a Value to test.
    /// @param n Bit index (0 = LSB).
    /// @return 1 if bit is set, 0 otherwise.
    int8_t rt_bigint_test_bit(void *a, int64_t n);

    /// @brief Set a specific bit.
    /// @param a Value.
    /// @param n Bit index (0 = LSB).
    /// @return New BigInt with bit set.
    void *rt_bigint_set_bit(void *a, int64_t n);

    /// @brief Clear a specific bit.
    /// @param a Value.
    /// @param n Bit index (0 = LSB).
    /// @return New BigInt with bit cleared.
    void *rt_bigint_clear_bit(void *a, int64_t n);

    /// @brief Integer square root (floor).
    /// @param a Value (must be non-negative).
    /// @return New BigInt with floor(sqrt(a)). Traps on negative input.
    void *rt_bigint_sqrt(void *a);

#ifdef __cplusplus
}
#endif
