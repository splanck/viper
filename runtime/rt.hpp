// File: runtime/rt.hpp
// Purpose: Declares C runtime utilities for memory, strings, and I/O.
// Key invariants: Reference counts remain non-negative.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/codemap.md
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct rt_string_impl;
    typedef struct rt_string_impl *rt_string;

    /// @brief Abort execution with message @p msg.
    /// @param msg Null-terminated message string.
    void rt_trap(const char *msg);

    /// @brief Print trap message and terminate process.
    /// @param msg Null-terminated message string.
    void rt_abort(const char *msg);

    /// @brief Print string @p s to stdout.
    /// @param s Reference-counted string.
    void rt_print_str(rt_string s);

    /// @brief Print signed 64-bit integer @p v to stdout.
    /// @param v Value to print.
    void rt_print_i64(int64_t v);

    /// @brief Print 64-bit float @p v to stdout.
    /// @param v Value to print.
    void rt_print_f64(double v);

    /// @brief Read a line from stdin.
    /// @return Newly allocated string without trailing newline.
    rt_string rt_input_line(void);

    /// @brief Increment reference count of @p s.
    /// @param s String to reference; null allowed.
    /// @return Input string pointer.
    rt_string rt_string_ref(rt_string s);

    /// @brief Decrement reference count of @p s and free on zero.
    /// @param s String to release; null allowed.
    void rt_string_unref(rt_string s);

    /// @brief Get length of string @p s in bytes.
    /// @param s String to measure.
    /// @return Number of bytes excluding terminator.
    int64_t rt_len(rt_string s);

    /// @brief Concatenate strings @p a and @p b.
    /// @param a Left operand; consumed.
    /// @param b Right operand; consumed.
    /// @return New string containing a followed by b.
    rt_string rt_concat(rt_string a, rt_string b);

    /// @brief Slice substring of @p s.
    /// Complexity: O(len) with one allocation and copy. Returns @p s unchanged when
    /// the full range is requested and a shared empty string when @p len is 0.
    /// @param s Source string.
    /// @param start Starting index (0-based).
    /// @param len Number of bytes to copy.
    /// @return Substring view.
    rt_string rt_substr(rt_string s, int64_t start, int64_t len);

    /// @brief Return leftmost @p n characters of @p s.
    /// @param s Source string; traps if null.
    /// @param n Number of characters to copy; traps if negative.
    /// Complexity: O(n) copy. Returns @p s unchanged if @p n >= rt_len(@p s) and a
    /// shared empty string when @p n == 0.
    /// @return Substring of length @c n or less.
    rt_string rt_left(rt_string s, int64_t n);

    /// @brief Return rightmost @p n characters of @p s.
    /// @param s Source string; traps if null.
    /// @param n Number of characters to copy; traps if negative.
    /// Complexity: O(n) copy. Returns @p s unchanged if @p n >= rt_len(@p s) and a
    /// shared empty string when @p n == 0.
    /// @return Substring of length @c n or less.
    rt_string rt_right(rt_string s, int64_t n);

    /// @brief Return substring starting at @p start to end.
    /// @param s Source string; traps if null.
    /// @param start Starting offset (0-based); traps if negative.
    /// Complexity: O(n) copy of trailing slice. Returns @p s unchanged if
    /// @p start <= 0 and a shared empty string when @p start >= rt_len(@p s).
    /// @return Substring from @p start to end.
    rt_string rt_mid2(rt_string s, int64_t start);

    /// @brief Return substring of length @p len starting at @p start.
    /// @param s Source string; traps if null.
    /// @param start Starting offset (0-based); traps if negative.
    /// @param len Number of characters to copy; traps if negative.
    /// Complexity: O(len) copy. Returns @p s unchanged when requesting the full
    /// string and a shared empty string if @p len == 0 or @p start >= rt_len(@p s).
    /// @return Substring view.
    rt_string rt_mid3(rt_string s, int64_t start, int64_t len);

    /// @brief Find @p needle within @p hay starting at @p start.
    /// @param start 1-based index after which search begins; clamped to [1, rt_len(hay) + 1].
    /// @param hay Haystack string; null returns 0.
    /// @param needle Needle string; null returns 0.
    /// @return 1-based index of match or 0 if not found. Empty @p needle returns @p start.
    int64_t rt_instr3(int64_t start, rt_string hay, rt_string needle);

    /// @brief Find @p needle within @p hay starting at index 1.
    /// @param hay Haystack string; null returns 0.
    /// @param needle Needle string; null returns 0.
    /// @return 1-based index of match or 0 if not found. Empty @p needle returns 1.
    int64_t rt_instr2(rt_string hay, rt_string needle);

    /// @brief Remove leading spaces and tabs.
    /// Whitespace is ASCII space (0x20) or tab (0x09).
    /// @param s Source string; traps if null.
    /// @return Newly allocated trimmed string.
    rt_string rt_ltrim(rt_string s);

    /// @brief Remove trailing spaces and tabs.
    /// Whitespace is ASCII space (0x20) or tab (0x09).
    /// @param s Source string; traps if null.
    /// @return Newly allocated trimmed string.
    rt_string rt_rtrim(rt_string s);

    /// @brief Remove leading and trailing spaces and tabs.
    /// Whitespace is ASCII space (0x20) or tab (0x09).
    /// @param s Source string; traps if null.
    /// @return Newly allocated trimmed string.
    rt_string rt_trim(rt_string s);

    /// @brief Convert string to uppercase (ASCII a-z).
    /// Non-ASCII bytes are left unchanged.
    /// @param s Source string; traps if null.
    /// @return Newly allocated uppercase string.
    rt_string rt_ucase(rt_string s);

    /// @brief Convert string to lowercase (ASCII A-Z).
    /// Non-ASCII bytes are left unchanged.
    /// @param s Source string; traps if null.
    /// @return Newly allocated lowercase string.
    rt_string rt_lcase(rt_string s);

    /// @brief Return single-character string from @p code.
    /// @param code ASCII code 0-255; traps if out of range.
    /// @return Newly allocated one-character string.
    rt_string rt_chr(int64_t code);

    /// @brief Return ASCII code of first character of @p s.
    /// @param s Source string; traps if null.
    /// @return Code 0-255 of first character, or 0 if empty.
    int64_t rt_asc(rt_string s);

    /// @brief Compare strings for equality.
    /// @param a First string.
    /// @param b Second string.
    /// @return 1 if equal, 0 otherwise.
    int64_t rt_str_eq(rt_string a, rt_string b);

    /// @brief Convert decimal string @p s to signed 64-bit integer.
    /// @param s Input decimal string.
    /// @return Parsed integer value.
    int64_t rt_to_int(rt_string s);

    /// @brief Convert signed 64-bit integer @p v to decimal string.
    /// @param v Integer to convert.
    /// @return Newly allocated string representation.
    rt_string rt_int_to_str(int64_t v);

    /// @brief Convert 64-bit float @p v to decimal string.
    /// @param v Float to convert.
    /// @return Newly allocated string representation.
    rt_string rt_f64_to_str(double v);

    /// @brief Parse leading decimal numeric prefix of @p s.
    /// @param s Input string; leading spaces allowed.
    /// @return Parsed value or 0 if no digits found.
    double rt_val(rt_string s);

    /// @brief Convert numeric @p v to decimal string.
    /// @param v Value to convert.
    /// @return Newly allocated string representation.
    rt_string rt_str(double v);

    /// @brief Allocate @p bytes of zeroed memory.
    /// @param bytes Number of bytes to allocate.
    /// @return Pointer to zeroed block or trap on failure.
    void *rt_alloc(int64_t bytes);

    /// @brief Wrap constant C string @p str as rt_string without copying.
    /// @param str Null-terminated constant string.
    /// @return Non-owning, read-only rt_string view; capacity is 0.
    rt_string rt_const_cstr(const char *str);

#ifdef __cplusplus
}
#endif
