#pragma once

/**
 * @file mem.hpp
 * @brief Namespaced wrappers around low-level memory primitives.
 *
 * @details
 * The kernel links a small C runtime that provides fundamental memory routines
 * (`memcpy`, `memset`, `memmove`, `memcmp`). This header re-exports those
 * functions into the `lib::` namespace so kernel code can use a consistent
 * naming style without pulling in standard library headers.
 *
 * The wrappers are inline and simply forward to the underlying C symbols,
 * keeping call overhead minimal while centralizing declarations in one place.
 */

#include "../include/types.hpp"

// Forward declare the C runtime functions
extern "C" {
void *memcpy(void *dest, const void *src, usize n);
void *memset(void *dest, int c, usize n);
void *memmove(void *dest, const void *src, usize n);
int memcmp(const void *s1, const void *s2, usize n);
}

namespace lib {

/**
 * @brief Copy `n` bytes from `src` to `dest`.
 *
 * @details
 * The source and destination regions must not overlap; use @ref memmove if they
 * may overlap.
 *
 * @param dest Destination memory region.
 * @param src Source memory region.
 * @param n Number of bytes to copy.
 * @return `dest`.
 */
inline void *memcpy(void *dest, const void *src, usize n) {
    return ::memcpy(dest, src, n);
}

/**
 * @brief Fill `n` bytes of memory at `dest` with the byte value `c`.
 *
 * @param dest Destination memory region.
 * @param c Byte value (converted to unsigned char) to store.
 * @param n Number of bytes to write.
 * @return `dest`.
 */
inline void *memset(void *dest, int c, usize n) {
    return ::memset(dest, c, n);
}

/**
 * @brief Copy `n` bytes from `src` to `dest`, handling overlap safely.
 *
 * @details
 * Unlike @ref memcpy, this function supports overlapping regions by copying in
 * an order that preserves the source bytes.
 *
 * @param dest Destination memory region.
 * @param src Source memory region.
 * @param n Number of bytes to copy.
 * @return `dest`.
 */
inline void *memmove(void *dest, const void *src, usize n) {
    return ::memmove(dest, src, n);
}

/**
 * @brief Compare two memory regions byte-by-byte.
 *
 * @param s1 First memory region.
 * @param s2 Second memory region.
 * @param n Number of bytes to compare.
 * @return Negative/zero/positive depending on lexicographical ordering.
 */
inline int memcmp(const void *s1, const void *s2, usize n) {
    return ::memcmp(s1, s2, n);
}

} // namespace lib
