//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_scanner.h
// Purpose: String scanner for lexing and parsing strings.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Scanner Creation
    //=========================================================================

    /// @brief Create a new scanner for the given string.
    /// @param source The string to scan.
    /// @return Opaque Scanner object pointer.
    void *rt_scanner_new(rt_string source);

    //=========================================================================
    // Position and State
    //=========================================================================

    /// @brief Get the current position in the string.
    /// @param obj Opaque Scanner object pointer.
    /// @return Current byte position (0-indexed).
    int64_t rt_scanner_pos(void *obj);

    /// @brief Set the current position.
    /// @param obj Opaque Scanner object pointer.
    /// @param pos New position (clamped to valid range).
    void rt_scanner_set_pos(void *obj, int64_t pos);

    /// @brief Check if at end of string.
    /// @param obj Opaque Scanner object pointer.
    /// @return 1 if at end, 0 otherwise.
    int8_t rt_scanner_is_end(void *obj);

    /// @brief Get the remaining length to scan.
    /// @param obj Opaque Scanner object pointer.
    /// @return Number of characters remaining.
    int64_t rt_scanner_remaining(void *obj);

    /// @brief Get the total length of the source string.
    /// @param obj Opaque Scanner object pointer.
    /// @return Total length.
    int64_t rt_scanner_len(void *obj);

    /// @brief Reset scanner to beginning.
    /// @param obj Opaque Scanner object pointer.
    void rt_scanner_reset(void *obj);

    //=========================================================================
    // Peeking (without advancing)
    //=========================================================================

    /// @brief Peek at the current character.
    /// @param obj Opaque Scanner object pointer.
    /// @return Current character, or -1 if at end.
    int64_t rt_scanner_peek(void *obj);

    /// @brief Peek at character at offset from current position.
    /// @param obj Opaque Scanner object pointer.
    /// @param offset Offset from current position.
    /// @return Character at position, or -1 if out of bounds.
    int64_t rt_scanner_peek_at(void *obj, int64_t offset);

    /// @brief Peek at the next n characters as a string.
    /// @param obj Opaque Scanner object pointer.
    /// @param n Number of characters to peek.
    /// @return String of peeked characters (may be shorter at end).
    rt_string rt_scanner_peek_str(void *obj, int64_t n);

    //=========================================================================
    // Reading (with advancing)
    //=========================================================================

    /// @brief Read and advance past the current character.
    /// @param obj Opaque Scanner object pointer.
    /// @return Current character, or -1 if at end.
    int64_t rt_scanner_read(void *obj);

    /// @brief Read the next n characters and advance.
    /// @param obj Opaque Scanner object pointer.
    /// @param n Number of characters to read.
    /// @return String of read characters (may be shorter at end).
    rt_string rt_scanner_read_str(void *obj, int64_t n);

    /// @brief Read until a delimiter character is found.
    /// @param obj Opaque Scanner object pointer.
    /// @param delim Delimiter character to stop at.
    /// @return String up to (not including) delimiter.
    rt_string rt_scanner_read_until(void *obj, int64_t delim);

    /// @brief Read until any of the delimiter characters is found.
    /// @param obj Opaque Scanner object pointer.
    /// @param delims String of delimiter characters.
    /// @return String up to (not including) delimiter.
    rt_string rt_scanner_read_until_any(void *obj, rt_string delims);

    /// @brief Read while predicate is true.
    /// @param obj Opaque Scanner object pointer.
    /// @param pred Predicate function taking a character.
    /// @return String of characters where predicate was true.
    rt_string rt_scanner_read_while(void *obj, int8_t (*pred)(int64_t));

    //=========================================================================
    // Matching
    //=========================================================================

    /// @brief Check if current position matches the given character.
    /// @param obj Opaque Scanner object pointer.
    /// @param c Character to match.
    /// @return 1 if matches, 0 otherwise.
    int8_t rt_scanner_match(void *obj, int64_t c);

    /// @brief Check if current position matches the given string.
    /// @param obj Opaque Scanner object pointer.
    /// @param s String to match.
    /// @return 1 if matches, 0 otherwise.
    int8_t rt_scanner_match_str(void *obj, rt_string s);

    /// @brief Match and consume a character if it matches.
    /// @param obj Opaque Scanner object pointer.
    /// @param c Character to match.
    /// @return 1 if matched and consumed, 0 if not matched.
    int8_t rt_scanner_accept(void *obj, int64_t c);

    /// @brief Match and consume a string if it matches.
    /// @param obj Opaque Scanner object pointer.
    /// @param s String to match.
    /// @return 1 if matched and consumed, 0 if not matched.
    int8_t rt_scanner_accept_str(void *obj, rt_string s);

    /// @brief Match and consume any of the given characters.
    /// @param obj Opaque Scanner object pointer.
    /// @param chars String of characters to try matching.
    /// @return 1 if any character matched and consumed, 0 otherwise.
    int8_t rt_scanner_accept_any(void *obj, rt_string chars);

    //=========================================================================
    // Skipping
    //=========================================================================

    /// @brief Skip n characters.
    /// @param obj Opaque Scanner object pointer.
    /// @param n Number of characters to skip.
    void rt_scanner_skip(void *obj, int64_t n);

    /// @brief Skip whitespace characters.
    /// @param obj Opaque Scanner object pointer.
    /// @return Number of characters skipped.
    int64_t rt_scanner_skip_whitespace(void *obj);

    /// @brief Skip while predicate is true.
    /// @param obj Opaque Scanner object pointer.
    /// @param pred Predicate function.
    /// @return Number of characters skipped.
    int64_t rt_scanner_skip_while(void *obj, int8_t (*pred)(int64_t));

    //=========================================================================
    // Token Helpers
    //=========================================================================

    /// @brief Read an identifier (letters, digits, underscore, starting with letter/underscore).
    /// @param obj Opaque Scanner object pointer.
    /// @return Identifier string, or empty if not at identifier start.
    rt_string rt_scanner_read_ident(void *obj);

    /// @brief Read an integer (optional sign, digits).
    /// @param obj Opaque Scanner object pointer.
    /// @return Integer string, or empty if not at integer.
    rt_string rt_scanner_read_int(void *obj);

    /// @brief Read a number (integer or float).
    /// @param obj Opaque Scanner object pointer.
    /// @return Number string, or empty if not at number.
    rt_string rt_scanner_read_number(void *obj);

    /// @brief Read a quoted string (handles escape sequences).
    /// @param obj Opaque Scanner object pointer.
    /// @param quote Quote character ('"' or '\'').
    /// @return String contents (without quotes), or empty if not at quoted string.
    rt_string rt_scanner_read_quoted(void *obj, int64_t quote);

    /// @brief Read until end of line.
    /// @param obj Opaque Scanner object pointer.
    /// @return Line contents (not including newline).
    rt_string rt_scanner_read_line(void *obj);

    //=========================================================================
    // Character Class Predicates
    //=========================================================================

    /// @brief Check if character is a digit.
    /// @param c Character to check.
    /// @return 1 if digit, 0 otherwise.
    int8_t rt_scanner_is_digit(int64_t c);

    /// @brief Check if character is a letter.
    /// @param c Character to check.
    /// @return 1 if letter, 0 otherwise.
    int8_t rt_scanner_is_alpha(int64_t c);

    /// @brief Check if character is alphanumeric.
    /// @param c Character to check.
    /// @return 1 if alphanumeric, 0 otherwise.
    int8_t rt_scanner_is_alnum(int64_t c);

    /// @brief Check if character is whitespace.
    /// @param c Character to check.
    /// @return 1 if whitespace, 0 otherwise.
    int8_t rt_scanner_is_space(int64_t c);

#ifdef __cplusplus
}
#endif
