/**
 * @file overflow.h
 * @brief Integer overflow detection using compiler builtins.
 *
 * Provides portable wrappers around __builtin_{add,sub,mul}_overflow
 * for safe integer arithmetic with overflow checking.
 *
 * Usage:
 *   int a = INT_MAX, b = 1, result;
 *   if (add_overflow(a, b, &result)) {
 *       // overflow occurred
 *   }
 */

#ifndef VIPER_OVERFLOW_H
#define VIPER_OVERFLOW_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check for addition overflow.
 *
 * Computes a + b, storing the result in *res. Returns non-zero if the
 * addition overflowed the result type.
 *
 * Works with any integer type (int, long, unsigned, etc.) via
 * __builtin_add_overflow which is type-generic.
 */
#define add_overflow(a, b, res) __builtin_add_overflow((a), (b), (res))

/**
 * @brief Check for subtraction overflow.
 *
 * Computes a - b, storing the result in *res. Returns non-zero if the
 * subtraction overflowed the result type.
 */
#define sub_overflow(a, b, res) __builtin_sub_overflow((a), (b), (res))

/**
 * @brief Check for multiplication overflow.
 *
 * Computes a * b, storing the result in *res. Returns non-zero if the
 * multiplication overflowed the result type.
 */
#define mul_overflow(a, b, res) __builtin_mul_overflow((a), (b), (res))

/**
 * @brief Safe addition with saturation for signed 32-bit integers.
 *
 * Returns a + b, clamped to [INT32_MIN, INT32_MAX] on overflow.
 */
static inline int add_sat_i32(int a, int b) {
    int result;
    if (__builtin_add_overflow(a, b, &result)) {
        return (a > 0) ? __INT_MAX__ : (-__INT_MAX__ - 1);
    }
    return result;
}

/**
 * @brief Safe addition with saturation for unsigned 32-bit integers.
 *
 * Returns a + b, clamped to UINT32_MAX on overflow.
 */
static inline unsigned int add_sat_u32(unsigned int a, unsigned int b) {
    unsigned int result;
    if (__builtin_add_overflow(a, b, &result)) {
        return (unsigned int)-1;
    }
    return result;
}

#ifdef __cplusplus
}
#endif

#endif /* VIPER_OVERFLOW_H */
