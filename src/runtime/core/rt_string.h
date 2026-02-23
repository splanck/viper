//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_string.h
// Purpose: Reference-counted UTF-8 string API providing creation, manipulation, comparison, search, and conversion operations for the primary Viper runtime string type.
//
// Key invariants:
//   - Strings are null-terminated UTF-8; the null terminator is not counted in length.
//   - Reference counting uses atomic increments/decrements for thread safety.
//   - Immortal string literals created by rt_string_literal are never freed.
//   - Empty string is represented as NULL or a zero-length allocated string; both are valid.
//
// Ownership/Lifetime:
//   - New strings start with refcount 1; callers own the initial reference.
//   - rt_string_ref increments refcount; rt_string_unref decrements and frees at zero.
//   - Callers must balance every retain with exactly one release.
//
// Links: src/runtime/core/rt_string.c (implementation), src/runtime/core/rt_heap.h
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

    /// @brief Return non-zero when @p p points at a runtime string handle.
    /// @details Used by generic object helpers to distinguish string handles from
    ///          heap-managed objects without forcing callers to inspect internals.
    /// @param p Pointer to test; may be NULL.
    int rt_string_is_handle(void *p);

    rt_string rt_str_empty(void);

    /// @brief Allocate a runtime string by copying @p len bytes from @p bytes.
    /// @param bytes Source buffer; may be NULL when @p len is zero.
    /// @param len Number of bytes to copy into the new runtime string.
    /// @return Newly allocated runtime string containing the copied bytes.
    rt_string rt_string_from_bytes(const char *bytes, size_t len);

    /// @brief Create a runtime string from a string literal.
    /// @param bytes Pointer to the literal data.
    /// @param len Number of bytes in the literal.
    /// @return Runtime string containing the literal data.
    rt_string rt_str_from_lit(const char *bytes, size_t len);

    /// @brief Return the number of bytes stored in @p s (excluding the terminator).
    /// @param s String to measure; NULL returns 0.
    /// @return Length in bytes.
    int64_t rt_str_len(rt_string s);

    /// @brief Return 1 when @p s is empty or NULL; 0 otherwise.
    int64_t rt_str_is_empty(rt_string s);

    /// @brief Concatenate @p a and @p b, consuming both operands.
    /// @param a Left operand; released on success.
    /// @param b Right operand; released on success.
    /// @return Newly allocated concatenation result.
    rt_string rt_str_concat(rt_string a, rt_string b);

    /// @brief Slice substring of @p s starting at @p start with length @p len.
    /// @param s Source string; consumed when returning a new allocation.
    /// @param start Zero-based starting offset.
    /// @param len Number of bytes to copy.
    /// @return Substring or shared empty string when @p len == 0.
    rt_string rt_str_substr(rt_string s, int64_t start, int64_t len);

    /// @brief Identity constructor for strings; used by Viper.Strings.FromStr.
    rt_string rt_str_clone(rt_string s);

    /// @brief Return leftmost @p n characters of @p s.
    /// @param s Source string; traps if NULL.
    /// @param n Count of characters to copy.
    /// @return Prefix view or shared empty string if @p n == 0.
    rt_string rt_str_left(rt_string s, int64_t n);

    /// @brief Return rightmost @p n characters of @p s.
    /// @param s Source string; traps if NULL.
    /// @param n Count of characters to copy.
    /// @return Suffix view or shared empty string if @p n == 0.
    rt_string rt_str_right(rt_string s, int64_t n);

    /// @brief Return substring starting at @p start to the end of @p s.
    /// @param s Source string; traps if NULL.
    /// @param start Zero-based offset where slicing begins.
    /// @return Trailing slice or shared empty string if @p start >= len.
    rt_string rt_str_mid(rt_string s, int64_t start);

    /// @brief Return substring starting at @p start with length @p len.
    /// @param s Source string; traps if NULL.
    /// @param start Zero-based offset where slicing begins.
    /// @param len Number of characters to copy.
    /// @return Substring respecting bounds; may share storage.
    rt_string rt_str_mid_len(rt_string s, int64_t start, int64_t len);

    /// @brief Find @p needle within @p hay starting at 1-based index @p start.
    /// @param start 1-based starting position (clamped to valid range).
    /// @param hay Haystack string.
    /// @param needle Needle string.
    /// @return 1-based match index or 0 when not found.
    int64_t rt_instr3(int64_t start, rt_string hay, rt_string needle);

    /// @brief Viper.String.IndexOfFrom wrapper with receiver-first argument order.
    /// @param hay Haystack string.
    /// @param start 1-based starting position.
    /// @param needle Needle string.
    /// @return 1-based match index or 0 when not found.
    int64_t rt_str_index_of_from(rt_string hay, int64_t start, rt_string needle);

    /// @brief Find @p needle within @p hay starting at index 1.
    /// @param hay Haystack string.
    /// @param needle Needle string.
    /// @return 1-based match index or 0 when not found.
    int64_t rt_str_index_of(rt_string hay, rt_string needle);

    /// @brief Remove leading spaces and tabs.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated trimmed string.
    rt_string rt_str_ltrim(rt_string s);

    /// @brief Remove trailing spaces and tabs.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated trimmed string.
    rt_string rt_str_rtrim(rt_string s);

    /// @brief Remove leading and trailing spaces and tabs.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated trimmed string.
    rt_string rt_str_trim(rt_string s);

    /// @brief Convert ASCII characters to uppercase.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated uppercase string.
    rt_string rt_str_ucase(rt_string s);

    /// @brief Convert ASCII characters to lowercase.
    /// @param s Source string; traps if NULL.
    /// @return Newly allocated lowercase string.
    rt_string rt_str_lcase(rt_string s);

    /// @brief Create single-character string from ASCII code @p code.
    /// @param code Value 0-255 specifying the character.
    /// @return Newly allocated string of length one.
    rt_string rt_str_chr(int64_t code);

    /// @brief Return ASCII code of the first character in @p s.
    /// @param s Source string; traps if NULL.
    /// @return Value 0-255 or 0 if empty.
    int64_t rt_str_asc(rt_string s);

    /// @brief Compare @p a and @p b for equality.
    /// @param a First operand.
    /// @param b Second operand.
    /// @return 1 when equal, 0 otherwise.
    int8_t rt_str_eq(rt_string a, rt_string b);

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

    //===----------------------------------------------------------------------===//
    // Extended String Functions (Viper.String expansion)
    //===----------------------------------------------------------------------===//

    /// @brief Replace all occurrences of needle with replacement.
    /// @param haystack Source string.
    /// @param needle String to find.
    /// @param replacement String to substitute.
    /// @return Newly allocated string with replacements.
    rt_string rt_str_replace(rt_string haystack, rt_string needle, rt_string replacement);

    /// @brief Check if string starts with prefix.
    /// @param str Source string.
    /// @param prefix Prefix to check.
    /// @return 1 if str starts with prefix, 0 otherwise.
    int64_t rt_str_starts_with(rt_string str, rt_string prefix);

    /// @brief Check if string ends with suffix.
    /// @param str Source string.
    /// @param suffix Suffix to check.
    /// @return 1 if str ends with suffix, 0 otherwise.
    int64_t rt_str_ends_with(rt_string str, rt_string suffix);

    /// @brief Check if string contains needle.
    /// @param str Source string.
    /// @param needle Substring to find.
    /// @return 1 if str contains needle, 0 otherwise.
    int64_t rt_str_has(rt_string str, rt_string needle);

    /// @brief Count non-overlapping occurrences of needle in str.
    /// @param str Source string.
    /// @param needle Substring to count.
    /// @return Number of non-overlapping occurrences.
    int64_t rt_str_count(rt_string str, rt_string needle);

    /// @brief Pad string on the left to reach specified width.
    /// @param str Source string.
    /// @param width Target width.
    /// @param pad_str Padding character (first char used).
    /// @return Newly allocated padded string.
    rt_string rt_str_pad_left(rt_string str, int64_t width, rt_string pad_str);

    /// @brief Pad string on the right to reach specified width.
    /// @param str Source string.
    /// @param width Target width.
    /// @param pad_str Padding character (first char used).
    /// @return Newly allocated padded string.
    rt_string rt_str_pad_right(rt_string str, int64_t width, rt_string pad_str);

    /// @brief Split string by delimiter into a sequence.
    /// @param str Source string.
    /// @param delim Delimiter string.
    /// @return Seq containing string parts.
    void *rt_str_split(rt_string str, rt_string delim);

    /// @brief Join sequence of strings with separator.
    /// @param sep Separator string.
    /// @param seq Sequence of strings to join.
    /// @return Newly allocated joined string.
    rt_string rt_str_join(rt_string sep, void *seq);

    /// @brief Repeat string count times.
    /// @param str Source string.
    /// @param count Number of repetitions.
    /// @return Newly allocated repeated string.
    rt_string rt_str_repeat(rt_string str, int64_t count);

    /// @brief Reverse string bytes (ASCII-safe).
    /// @param str Source string.
    /// @return Newly allocated reversed string.
    rt_string rt_str_flip(rt_string str);

    /// @brief Compare two strings, returning -1, 0, or 1.
    /// @param a First string.
    /// @param b Second string.
    /// @return -1 if a < b, 0 if a == b, 1 if a > b.
    int64_t rt_str_cmp(rt_string a, rt_string b);

    /// @brief Case-insensitive string comparison, returning -1, 0, or 1.
    /// @param a First string.
    /// @param b Second string.
    /// @return -1 if a < b, 0 if a == b, 1 if a > b (case-insensitive).
    int64_t rt_str_cmp_nocase(rt_string a, rt_string b);

    /// @brief Capitalize the first character of a string.
    /// @param str Source string.
    /// @return Newly allocated string with first character uppercased.
    rt_string rt_str_capitalize(rt_string str);

    /// @brief Title-case a string (capitalize first letter of each word).
    /// @param str Source string.
    /// @return Newly allocated title-cased string.
    rt_string rt_str_title(rt_string str);

    /// @brief Remove a prefix from a string if present.
    /// @param str Source string.
    /// @param prefix Prefix to remove.
    /// @return Newly allocated string without prefix, or copy if not present.
    rt_string rt_str_remove_prefix(rt_string str, rt_string prefix);

    /// @brief Remove a suffix from a string if present.
    /// @param str Source string.
    /// @param suffix Suffix to remove.
    /// @return Newly allocated string without suffix, or copy if not present.
    rt_string rt_str_remove_suffix(rt_string str, rt_string suffix);

    /// @brief Find the last occurrence of a needle in a string.
    /// @param haystack Source string.
    /// @param needle Substring to find.
    /// @return 1-based index of last occurrence, or 0 if not found.
    int64_t rt_str_last_index_of(rt_string haystack, rt_string needle);

    /// @brief Trim a specific character from both ends of a string.
    /// @param str Source string.
    /// @param ch Character to trim (first char of string used).
    /// @return Newly allocated trimmed string.
    rt_string rt_str_trim_char(rt_string str, rt_string ch);

    /// @brief Convert string to a URL-safe slug.
    /// @param str Source string.
    /// @return Newly allocated slug (lowercase, alphanumeric + hyphens).
    rt_string rt_str_slug(rt_string str);

    /// @brief Compute Levenshtein edit distance between two strings.
    /// @param a First string.
    /// @param b Second string.
    /// @return Edit distance (insertions + deletions + substitutions).
    int64_t rt_str_levenshtein(rt_string a, rt_string b);

    /// @brief Compute Jaro similarity score between two strings.
    /// @param a First string.
    /// @param b Second string.
    /// @return Similarity score between 0.0 (no match) and 1.0 (identical).
    double rt_str_jaro(rt_string a, rt_string b);

    /// @brief Compute Jaro-Winkler similarity score between two strings.
    /// @param a First string.
    /// @param b Second string.
    /// @return Similarity score between 0.0 (no match) and 1.0 (identical).
    double rt_str_jaro_winkler(rt_string a, rt_string b);

    /// @brief Compute Hamming distance between two equal-length strings.
    /// @param a First string.
    /// @param b Second string.
    /// @return Number of positions where characters differ, or -1 if lengths differ.
    int64_t rt_str_hamming(rt_string a, rt_string b);

    /// @brief Convert string to camelCase.
    /// @param str Source string (may contain spaces, hyphens, underscores).
    /// @return camelCase version (e.g., "hello world" -> "helloWorld").
    rt_string rt_str_camel_case(rt_string str);

    /// @brief Convert string to PascalCase.
    /// @param str Source string.
    /// @return PascalCase version (e.g., "hello world" -> "HelloWorld").
    rt_string rt_str_pascal_case(rt_string str);

    /// @brief Convert string to snake_case.
    /// @param str Source string.
    /// @return snake_case version (e.g., "helloWorld" -> "hello_world").
    rt_string rt_str_snake_case(rt_string str);

    /// @brief Convert string to kebab-case.
    /// @param str Source string.
    /// @return kebab-case version (e.g., "helloWorld" -> "hello-world").
    rt_string rt_str_kebab_case(rt_string str);

    /// @brief Convert string to SCREAMING_SNAKE_CASE.
    /// @param str Source string.
    /// @return UPPER_SNAKE version (e.g., "helloWorld" -> "HELLO_WORLD").
    rt_string rt_str_screaming_snake(rt_string str);

    /// @brief SQL LIKE pattern matching (case-sensitive).
    /// @param text Text to match.
    /// @param pattern Pattern (% = any chars, _ = one char, \ = escape).
    /// @return 1 if matched, 0 otherwise.
    int8_t rt_string_like(rt_string text, rt_string pattern);

    /// @brief SQL ILIKE pattern matching (case-insensitive).
    /// @param text Text to match.
    /// @param pattern Pattern (% = any chars, _ = one char, \ = escape).
    /// @return 1 if matched, 0 otherwise.
    int8_t rt_string_like_ci(rt_string text, rt_string pattern);

#ifdef __cplusplus
}
#endif
