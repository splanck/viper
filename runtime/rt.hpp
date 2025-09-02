// File: runtime/rt.hpp
// Purpose: Declares C runtime utilities for strings and I/O.
// Key invariants: Reference counts remain non-negative.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/class-catalog.md
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct rt_str_impl
    {
        int64_t refcnt;
        int64_t size;
        int64_t capacity;
        char *data;
    } *rt_str;

    /// @brief Abort execution with message @p msg.
    /// @param msg Null-terminated message string.
    void rt_trap(const char *msg);

    /// @brief Print trap message and terminate process.
    /// @param msg Null-terminated message string.
    void rt_abort(const char *msg);

    /// @brief Print string @p s to stdout.
    /// @param s Reference-counted string.
    void rt_print_str(rt_str s);

    /// @brief Print signed 64-bit integer @p v to stdout.
    /// @param v Value to print.
    void rt_print_i64(int64_t v);

    /// @brief Print 64-bit float @p v to stdout.
    /// @param v Value to print.
    void rt_print_f64(double v);

    /// @brief Read a line from stdin.
    /// @return Newly allocated string without trailing newline.
    rt_str rt_input_line(void);

    /// @brief Get length of string @p s in bytes.
    /// @param s String to measure.
    /// @return Number of bytes excluding terminator.
    int64_t rt_len(rt_str s);

    /// @brief Concatenate strings @p a and @p b.
    /// @param a Left operand; consumed.
    /// @param b Right operand; consumed.
    /// @return New string containing a followed by b.
    rt_str rt_concat(rt_str a, rt_str b);

    /// @brief Slice substring of @p s.
    /// @param s Source string.
    /// @param start Starting index (0-based).
    /// @param len Number of bytes to copy.
    /// @return Newly allocated substring.
    rt_str rt_substr(rt_str s, int64_t start, int64_t len);

    /// @brief Compare strings for equality.
    /// @param a First string.
    /// @param b Second string.
    /// @return 1 if equal, 0 otherwise.
    int64_t rt_str_eq(rt_str a, rt_str b);

    /// @brief Convert decimal string @p s to signed 64-bit integer.
    /// @param s Input decimal string.
    /// @return Parsed integer value.
    int64_t rt_to_int(rt_str s);

    /// @brief Allocate @p bytes of zeroed memory.
    /// @param bytes Number of bytes to allocate.
    /// @return Pointer to zeroed block or trap on failure.
    void *rt_alloc(int64_t bytes);

    /// @brief Wrap constant C string @p str as rt_str without copying.
    /// @param str Null-terminated constant string.
    /// @return Non-owning rt_str view.
    rt_str rt_const_cstr(const char *str);

#ifdef __cplusplus
}
#endif
