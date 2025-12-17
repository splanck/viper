//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_bits.h
// Purpose: Bit manipulation utilities for Viper.Bits.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Bitwise AND of two values.
    int64_t rt_bits_and(int64_t a, int64_t b);

    /// @brief Bitwise OR of two values.
    int64_t rt_bits_or(int64_t a, int64_t b);

    /// @brief Bitwise XOR of two values.
    int64_t rt_bits_xor(int64_t a, int64_t b);

    /// @brief Bitwise NOT of a value.
    int64_t rt_bits_not(int64_t val);

    /// @brief Logical shift left.
    int64_t rt_bits_shl(int64_t val, int64_t count);

    /// @brief Arithmetic shift right (sign-extended).
    int64_t rt_bits_shr(int64_t val, int64_t count);

    /// @brief Logical shift right (zero-fill).
    int64_t rt_bits_ushr(int64_t val, int64_t count);

    /// @brief Rotate left.
    int64_t rt_bits_rotl(int64_t val, int64_t count);

    /// @brief Rotate right.
    int64_t rt_bits_rotr(int64_t val, int64_t count);

    /// @brief Population count (number of 1 bits).
    int64_t rt_bits_count(int64_t val);

    /// @brief Count leading zeros.
    int64_t rt_bits_leadz(int64_t val);

    /// @brief Count trailing zeros.
    int64_t rt_bits_trailz(int64_t val);

    /// @brief Reverse all 64 bits.
    int64_t rt_bits_flip(int64_t val);

    /// @brief Byte swap (endian swap).
    int64_t rt_bits_swap(int64_t val);

    /// @brief Get bit at position (0-63).
    bool rt_bits_get(int64_t val, int64_t bit);

    /// @brief Set bit at position (0-63).
    int64_t rt_bits_set(int64_t val, int64_t bit);

    /// @brief Clear bit at position (0-63).
    int64_t rt_bits_clear(int64_t val, int64_t bit);

    /// @brief Toggle bit at position (0-63).
    int64_t rt_bits_toggle(int64_t val, int64_t bit);

#ifdef __cplusplus
}
#endif
