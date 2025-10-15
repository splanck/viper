// File: src/runtime/rt.hpp
// Purpose: Declares C runtime utilities for memory, strings, and I/O.
// Key invariants: Reference counts remain non-negative.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/codemap.md
#pragma once

#include <stdint.h>

#include "rt_array.h"
#include "rt_debug.h"
#include "rt_error.h"
#include "rt_file.h"
#include "rt_format.h"
#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

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

    /// @brief Print signed 32-bit integer @p value with newline.
    /// @param value Value to print in decimal.
    void rt_println_i32(int32_t value);

    /// @brief Print C string @p text with newline.
    /// @param text Null-terminated string; treated as empty when null.
    void rt_println_str(const char *text);

    /// @brief Read a line from stdin.
    /// @return Newly allocated string without trailing newline.
    rt_string rt_input_line(void);

    /// @brief Split comma-separated fields from @p line into @p out_fields.
    /// @param line Input line to split; may be NULL.
    /// @param out_fields Destination array receiving at most @p max_fields entries.
    /// @param max_fields Number of fields expected; must be >= 0.
    /// @return Total number of fields discovered in @p line.
    int64_t rt_split_fields(rt_string line, rt_string *out_fields, int64_t max_fields);

    /// @brief Report EOF status for the specified channel.
    /// @param ch Numeric channel identifier previously passed to OPEN.
    /// @return -1 when the next read would hit EOF, 0 otherwise, or an error code on failure.
    int rt_eof_ch(int ch);

    /// @brief Compute the length in bytes of the file associated with @p ch.
    /// @param ch Numeric channel identifier previously passed to OPEN.
    /// @return Non-negative byte length on success or -Err_* on failure.
    int64_t rt_lof_ch(int ch);

    /// @brief Allocate @p bytes of zeroed memory.
    /// @param bytes Number of bytes to allocate.
    /// @return Pointer to zeroed block or trap on failure.
    void *rt_alloc(int64_t bytes);

    // Terminal control + single-key input

    /// @brief Clear the terminal when stdout is a TTY.
    void rt_term_cls(void);

    /// @brief Set foreground/background colors using terminal SGR sequences.
    /// @param fg Foreground color index (-1 to leave unchanged).
    /// @param bg Background color index (-1 to leave unchanged).
    void rt_term_color_i32(int32_t fg, int32_t bg);

    /// @brief Move the cursor to 1-based row/column when stdout is a TTY.
    /// @param row Target row (clamped to >= 1).
    /// @param col Target column (clamped to >= 1).
    void rt_term_locate_i32(int32_t row, int32_t col);

    /// @brief Block until a single key is read and return it as a 1-character string.
    rt_string rt_getkey_str(void);

    /// @brief Return a pending key as a 1-character string or empty string if none available.
    rt_string rt_inkey_str(void);

#ifdef __cplusplus
}
#endif

