//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/**
 * @file str.hpp
 * @brief Minimal string utilities for the ViperDOS kernel.
 *
 * @details
 * The kernel is built in a freestanding environment and avoids depending on
 * the full C standard library. This header provides a small set of
 * `lib::`-namespaced string routines that cover the needs of early bring-up
 * and core kernel subsystems.
 *
 * These functions intentionally mirror common libc interfaces (`strlen`,
 * `strcmp`, etc.) so call sites are familiar, while keeping the implementation
 * header-only and dependency-light.
 *
 * Safety notes:
 * - All functions expect valid pointers to NUL-terminated strings where
 *   applicable.
 * - Bounds checking is minimal; callers must ensure destination buffers are
 *   large enough for copy operations.
 */

#include "../include/types.hpp"

namespace lib {

/**
 * @brief Compute the length of a NUL-terminated string.
 *
 * @param s Pointer to a NUL-terminated string.
 * @return Number of characters before the first NUL byte.
 */
inline usize strlen(const char *s) {
    usize len = 0;
    while (s[len])
        len++;
    return len;
}

/**
 * @brief Compare two NUL-terminated strings lexicographically.
 *
 * @details
 * Returns an integer less than, equal to, or greater than zero depending on
 * whether `s1` is found to be less than, to match, or be greater than `s2`.
 * Comparison is performed using unsigned char semantics, matching libc
 * behavior.
 *
 * @param s1 First string.
 * @param s2 Second string.
 * @return Comparison result: negative, zero, or positive.
 */
inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return static_cast<int>(static_cast<unsigned char>(*s1)) -
           static_cast<int>(static_cast<unsigned char>(*s2));
}

/**
 * @brief Compare up to `n` characters of two strings.
 *
 * @details
 * Comparison stops after `n` characters or when either string reaches NUL.
 * If the first `n` characters are equal (or `n` is zero), the result is zero.
 *
 * @param s1 First string.
 * @param s2 Second string.
 * @param n Maximum number of characters to compare.
 * @return Comparison result: negative, zero, or positive.
 */
inline int strncmp(const char *s1, const char *s2, usize n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return static_cast<int>(static_cast<unsigned char>(*s1)) -
           static_cast<int>(static_cast<unsigned char>(*s2));
}

/**
 * @brief Copy a NUL-terminated string into a destination buffer.
 *
 * @details
 * Copies bytes from `src` to `dest` including the terminating NUL byte.
 * The destination buffer must be large enough to hold the entire string.
 *
 * @param dest Destination buffer.
 * @param src Source NUL-terminated string.
 * @return `dest`.
 */
inline char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

/**
 * @brief Copy up to `n` characters from `src` into `dest`.
 *
 * @details
 * Copies characters until either `n` bytes have been written or a NUL byte is
 * encountered in `src`. If `src` is shorter than `n`, the remainder of `dest`
 * is padded with NUL bytes.
 *
 * @param dest Destination buffer.
 * @param src Source string.
 * @param n Maximum number of bytes to write to `dest`.
 * @return `dest`.
 */
inline char *strncpy(char *dest, const char *src, usize n) {
    char *d = dest;
    while (n && (*d++ = *src++))
        n--;
    while (n--)
        *d++ = '\0';
    return dest;
}

/**
 * @brief Safely copy a string with length limit.
 *
 * @details
 * Copies up to `max - 1` characters from `src` to `dest`, always NUL-terminating
 * the result. This is safer than `strncpy` which may not NUL-terminate if `src`
 * is longer than `n`.
 *
 * @param dest Destination buffer (must be at least `max` bytes).
 * @param src Source NUL-terminated string.
 * @param max Maximum size of destination buffer including NUL terminator.
 * @return `dest`.
 */
inline char *strcpy_safe(char *dest, const char *src, usize max) {
    if (max == 0)
        return dest;
    usize i = 0;
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return dest;
}

/**
 * @brief Check if haystack contains needle as a substring.
 *
 * @param haystack String to search within.
 * @param needle Substring to search for.
 * @return True if needle is found in haystack.
 */
inline bool strcontains(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    for (const char *h = haystack; *h; h++) {
        const char *p = h;
        const char *n = needle;
        while (*n && *p == *n) {
            p++;
            n++;
        }
        if (!*n)
            return true;
    }
    return false;
}

} // namespace lib
