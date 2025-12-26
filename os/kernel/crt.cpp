/**
 * @file crt.cpp
 * @brief Minimal C runtime routines for the freestanding kernel.
 *
 * @details
 * In a freestanding kernel environment there is no libc to provide common C
 * runtime functions. Compilers may still emit calls to routines such as
 * `memcpy`/`memset` for struct copies, initialization, and other low-level
 * operations.
 *
 * This file provides small, byte-wise implementations of the most essential
 * memory routines. They are intentionally simple and prioritize correctness and
 * portability over performance. More optimized implementations can be added
 * later once profiling and architecture-specific tuning are in scope.
 */

#include "include/types.hpp"

extern "C"
{
    /**
     * @brief Copy bytes from one memory region to another.
     *
     * @details
     * Copies exactly `n` bytes from `src` to `dest` and returns `dest`.
     *
     * This routine has the same contract as the standard C `memcpy`:
     * - The source and destination regions must not overlap.
     * - If the regions may overlap, callers must use @ref memmove instead.
     *
     * @param dest Destination buffer.
     * @param src Source buffer.
     * @param n Number of bytes to copy.
     * @return `dest`.
     */
    void *memcpy(void *dest, const void *src, usize n)
    {
        u8 *d = static_cast<u8 *>(dest);
        const u8 *s = static_cast<const u8 *>(src);
        while (n--)
        {
            *d++ = *s++;
        }
        return dest;
    }

    /**
     * @brief Fill a memory region with a byte value.
     *
     * @details
     * Writes `n` bytes of the value `c` (converted to `u8`) into the region
     * starting at `dest`, then returns `dest`.
     *
     * This routine is commonly used by the compiler and kernel code to zero or
     * initialize buffers without relying on libc.
     *
     * @param dest Destination buffer.
     * @param c Byte value to store (lower 8 bits are used).
     * @param n Number of bytes to write.
     * @return `dest`.
     */
    void *memset(void *dest, int c, usize n)
    {
        u8 *d = static_cast<u8 *>(dest);
        while (n--)
        {
            *d++ = static_cast<u8>(c);
        }
        return dest;
    }

    /**
     * @brief Copy bytes between potentially overlapping memory regions.
     *
     * @details
     * Copies `n` bytes from `src` to `dest` and returns `dest`. Unlike @ref memcpy,
     * this function is safe when the regions overlap; it chooses forward or
     * backward copying depending on the relative addresses.
     *
     * @param dest Destination buffer.
     * @param src Source buffer.
     * @param n Number of bytes to move.
     * @return `dest`.
     */
    void *memmove(void *dest, const void *src, usize n)
    {
        u8 *d = static_cast<u8 *>(dest);
        const u8 *s = static_cast<const u8 *>(src);
        if (d < s)
        {
            while (n--)
            {
                *d++ = *s++;
            }
        }
        else
        {
            d += n;
            s += n;
            while (n--)
            {
                *--d = *--s;
            }
        }
        return dest;
    }

    /**
     * @brief Compare two memory regions lexicographically.
     *
     * @details
     * Compares `n` bytes of `s1` and `s2` and returns an integer indicating their
     * ordering:
     * - 0 if all bytes are equal
     * - < 0 if the first differing byte in `s1` is less than the corresponding
     *   byte in `s2`
     * - > 0 if the first differing byte in `s1` is greater than the corresponding
     *   byte in `s2`
     *
     * This routine matches the standard C `memcmp` contract.
     *
     * @param s1 First memory region.
     * @param s2 Second memory region.
     * @param n Number of bytes to compare.
     * @return Comparison result as described above.
     */
    int memcmp(const void *s1, const void *s2, usize n)
    {
        const u8 *p1 = static_cast<const u8 *>(s1);
        const u8 *p2 = static_cast<const u8 *>(s2);
        while (n--)
        {
            if (*p1 != *p2)
            {
                return *p1 - *p2;
            }
            p1++;
            p2++;
        }
        return 0;
    }

} // extern "C"
