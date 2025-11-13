//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the runtime library's string API, providing reference-counted
// string objects accessible from both C and C++ code. The runtime string type
// (rt_string) is the foundational text representation used by Viper IL programs,
// particularly those compiled from BASIC source code with extensive string
// manipulation requirements.
//
// Viper's runtime strings use automatic memory management through reference counting.
// Each string object maintains a reference count in its heap header; retain operations
// increment the count, release operations decrement it, and the string is freed when
// the count reaches zero. This design provides deterministic memory management without
// garbage collection overhead.
//
// Key Design Features:
// - Reference counting: Automatic memory management with deterministic lifetime
// - Immortal literals: String constants are marked immortal and never freed
// - UTF-8 encoding: All strings store UTF-8 byte sequences
// - Null-termination: Internal strings are null-terminated for C interop
// - Opaque handles: Callers work with opaque rt_string pointers, not raw bytes
//
// Memory Layout:
// Each runtime string consists of a heap header (reference count, capacity, flags)
// followed by the UTF-8 byte payload and a null terminator. The rt_string type
// points to the payload portion, with the header located at a negative offset.
//
// String Operations:
// - Creation: rt_string_from_bytes, rt_str_empty, rt_concat
// - Manipulation: rt_str_mid, rt_str_left, rt_str_right, rt_str_upper/lower
// - Comparison: rt_str_eq, rt_str_lt, rt_str_le, rt_str_gt, rt_str_ge
// - Conversion: rt_str_str (integer to string), rt_val (string to number)
// - Inspection: rt_str_len, rt_str_data
//
// Lifetime Management:
// Functions that create new strings return them with a reference count of 1.
// Callers are responsible for releasing strings when done. Functions that accept
// string arguments do not change ownership unless explicitly documented. This
// follows "create rule" semantics common in C APIs.
//
// Thread Safety:
// String operations are thread-safe for read-only access to immortal strings.
// Mutable operations or reference count modifications require external synchronization.
//
// C/C++ Interoperability:
// The API uses C linkage (extern "C") and plain C types to ensure compatibility
// across language boundaries. C++ code can use the same functions through the
// extern "C" declarations.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct rt_string_impl;

    /// @brief Exposes the runtime string object layout for host interop wrappers.
    typedef struct rt_string_impl ViperString;

    typedef struct rt_string_impl *rt_string;

    /// @brief Increment the reference count of @p s when non-null.
    /// @param s String to retain; may be NULL.
    /// @return The same pointer passed via @p s.
    rt_string rt_string_ref(rt_string s);

    /// @brief Decrement the reference count of @p s and free when zero.
    /// @param s String to release; may be NULL.
    void rt_string_unref(rt_string s);

    /// @brief Release @p s when non-null.
    /// @param s String handle that may be NULL.
    void rt_str_release_maybe(rt_string s);

    /// @brief Retain @p s when non-null.
    /// @param s String handle that may be NULL.
    void rt_str_retain_maybe(rt_string s);

    rt_string rt_str_empty(void);

    /// @brief Allocate a runtime string by copying @p len bytes from @p bytes.
    /// @param bytes Source buffer; may be NULL when @p len is zero.
    /// @param len Number of bytes to copy into the new runtime string.
    /// @return Newly allocated runtime string containing the copied bytes.
    rt_string rt_string_from_bytes(const char *bytes, size_t len);

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

    /// @brief Compare @p a < @p b lexicographically.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 when a < b, 0 otherwise.
    int64_t rt_str_lt(rt_string a, rt_string b);

    /// @brief Compare @p a <= @p b lexicographically.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 when a <= b, 0 otherwise.
    int64_t rt_str_le(rt_string a, rt_string b);

    /// @brief Compare @p a > @p b lexicographically.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 when a > b, 0 otherwise.
    int64_t rt_str_gt(rt_string a, rt_string b);

    /// @brief Compare @p a >= @p b lexicographically.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 when a >= b, 0 otherwise.
    int64_t rt_str_ge(rt_string a, rt_string b);

    /// @brief Parse decimal integer from @p s.
    /// @param s Source string.
    /// @return Parsed integer value or 0 when missing.
    int64_t rt_to_int(rt_string s);

    /// @brief Parse decimal floating-point value from @p s.
    /// @param s Source string.
    /// @return Parsed double value.
    double rt_to_double(rt_string s);

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
