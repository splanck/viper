// File: src/runtime/rt_string.h
// Purpose: Declares string runtime APIs shared between C and C++ callers.
// Key invariants: Strings use reference-counted heap headers with immortal literals.
// Ownership/Lifetime: Callers manage retains/releases; returned strings follow transfer rules.
// Links: docs/codemap.md
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    struct rt_string_impl;
    typedef struct rt_string_impl *rt_string;

    /// @brief Increment the reference count of @p s when non-null.
    /// @param s String to retain; may be NULL.
    /// @return The same pointer passed via @p s.
    rt_string rt_string_ref(rt_string s);

    /// @brief Decrement the reference count of @p s and free when zero.
    /// @param s String to release; may be NULL.
    void rt_string_unref(rt_string s);

    /// @brief Return the number of bytes stored in @p s (excluding the terminator).
    /// @param s String to measure; NULL returns 0.
    /// @return Length in bytes.
    int64_t rt_len(rt_string s);

    /// @brief Concatenate @p a and @p b, consuming both operands.
    /// @param a Left operand; released on success.
    /// @param b Right operand; released on success.
    /// @return Newly allocated concatenation result.
    rt_string rt_concat(rt_string a, rt_string b);

    /// @brief Slice substring of @p s starting at @p start with length @p len.
    /// @param s Source string; consumed when returning a new allocation.
    /// @param start Zero-based starting offset.
    /// @param len Number of bytes to copy.
    /// @return Substring or shared empty string when @p len == 0.
    rt_string rt_substr(rt_string s, int64_t start, int64_t len);

    /// @brief Return leftmost @p n characters of @p s.
    /// @param s Source string; traps if NULL.
    /// @param n Count of characters to copy.
    /// @return Prefix view or shared empty string if @p n == 0.
    rt_string rt_left(rt_string s, int64_t n);

    /// @brief Return rightmost @p n characters of @p s.
    /// @param s Source string; traps if NULL.
    /// @param n Count of characters to copy.
    /// @return Suffix view or shared empty string if @p n == 0.
    rt_string rt_right(rt_string s, int64_t n);

    /// @brief Return substring starting at @p start to the end of @p s.
    /// @param s Source string; traps if NULL.
    /// @param start Zero-based offset where slicing begins.
    /// @return Trailing slice or shared empty string if @p start >= len.
    rt_string rt_mid2(rt_string s, int64_t start);

    /// @brief Return substring starting at @p start with length @p len.
    /// @param s Source string; traps if NULL.
    /// @param start Zero-based offset where slicing begins.
    /// @param len Number of characters to copy.
    /// @return Substring respecting bounds; may share storage.
    rt_string rt_mid3(rt_string s, int64_t start, int64_t len);

    /// @brief Find @p needle within @p hay starting at 1-based index @p start.
    /// @param start 1-based starting position (clamped to valid range).
    /// @param hay Haystack string.
    /// @param needle Needle string.
    /// @return 1-based match index or 0 when not found.
    int64_t rt_instr3(int64_t start, rt_string hay, rt_string needle);

    /// @brief Find @p needle within @p hay starting at index 1.
    /// @param hay Haystack string.
    /// @param needle Needle string.
    /// @return 1-based match index or 0 when not found.
    int64_t rt_instr2(rt_string hay, rt_string needle);

    /// @brief Remove leading spaces and tabs.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated trimmed string.
    rt_string rt_ltrim(rt_string s);

    /// @brief Remove trailing spaces and tabs.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated trimmed string.
    rt_string rt_rtrim(rt_string s);

    /// @brief Remove leading and trailing spaces and tabs.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated trimmed string.
    rt_string rt_trim(rt_string s);

    /// @brief Convert ASCII characters to uppercase.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated uppercase string.
    rt_string rt_ucase(rt_string s);

    /// @brief Convert ASCII characters to lowercase.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated lowercase string.
    rt_string rt_lcase(rt_string s);

    /// @brief Create single-character string from ASCII code @p code.
    /// @param code Value 0-255 specifying the character.
    /// @return Newly allocated string of length one.
    rt_string rt_chr(int64_t code);

    /// @brief Return ASCII code of the first character in @p s.
    /// @param s Source string; traps if NULL.
    /// @return Value 0-255 or 0 if empty.
    int64_t rt_asc(rt_string s);

    /// @brief Compare @p a and @p b for equality.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 when equal, 0 otherwise.
    int64_t rt_str_eq(rt_string a, rt_string b);

    /// @brief Parse decimal integer from @p s.
    /// @param s Source string.
    /// @return Parsed integer value or 0 when missing.
    int64_t rt_to_int(rt_string s);

    /// @brief Convert integer @p v to decimal string.
    /// @param v Value to format.
    /// @return Newly allocated decimal representation.
    rt_string rt_int_to_str(int64_t v);

    /// @brief Convert floating-point @p v to decimal string.
    /// @param v Value to format.
    /// @return Newly allocated decimal representation.
    rt_string rt_f64_to_str(double v);

    /// @brief Allocate a runtime string from a double value using standard formatting.
    /// @param v Double-precision value to convert.
    /// @return Newly allocated runtime string.
    rt_string rt_str_d_alloc(double v);

    /// @brief Allocate a runtime string from a float value using standard formatting.
    /// @param v Single-precision value to convert.
    /// @return Newly allocated runtime string.
    rt_string rt_str_f_alloc(float v);

    /// @brief Allocate a runtime string from a 32-bit integer using standard formatting.
    /// @param v 32-bit integer value to convert.
    /// @return Newly allocated runtime string.
    rt_string rt_str_i32_alloc(int32_t v);

    /// @brief Allocate a runtime string from a 16-bit integer using standard formatting.
    /// @param v 16-bit integer value to convert.
    /// @return Newly allocated runtime string.
    rt_string rt_str_i16_alloc(int16_t v);

    /// @brief Parse leading numeric prefix from @p s as double.
    /// @param s Source string.
    /// @return Parsed numeric value or 0 when absent.
    double rt_val(rt_string s);

    /// @brief Convert numeric @p v to decimal string.
    /// @param v Value to format.
    /// @return Newly allocated decimal representation.
    rt_string rt_str(double v);

    /// @brief Obtain a null-terminated C string view of @p s.
    /// @param s Source string; must not be NULL.
    /// @return Pointer to the string's internal UTF-8 buffer.
    const char *rt_string_cstr(rt_string s);

    /// @brief Wrap constant C string @p str without copying.
    /// @param str Null-terminated literal pointer.
    /// @return Non-owning runtime string view.
    rt_string rt_const_cstr(const char *str);

#ifdef __cplusplus
}
#endif

