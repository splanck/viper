//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: runtime/rt.hpp
// Purpose: Declares C runtime utilities for memory, strings, and I/O.
// Key invariants: Reference counts remain non-negative.
// Ownership/Lifetime: Caller manages returned strings.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "viper/runtime/rt.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Abort execution with message @p msg.
    /// @param msg Null-terminated message string.
    void rt_trap(const char *msg);

    /// @brief Trap when @p condition is false with the provided diagnostic.
    /// @param condition Boolean flag; zero triggers a trap.
    /// @param message Runtime string describing the assertion; defaults to
    ///        "Assertion failed" when empty or null.
    void rt_diag_assert(int8_t condition, rt_string message);

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
    int64_t rt_str_split_fields(rt_string line, rt_string *out_fields, int64_t max_fields);

    // =========================================================================
    // Viper.Terminal I/O Functions
    // =========================================================================

    /// @brief Print string followed by newline.
    void rt_term_say(rt_string s);

    /// @brief Print integer followed by newline.
    void rt_term_say_i64(int64_t v);

    /// @brief Print floating-point number followed by newline.
    void rt_term_say_f64(double v);

    /// @brief Print boolean as "true" or "false" followed by newline.
    void rt_term_say_bool(int8_t v);

    /// @brief Print string without trailing newline.
    void rt_term_print(rt_string s);

    /// @brief Print integer without trailing newline.
    void rt_term_print_i64(int64_t v);

    /// @brief Print floating-point number without trailing newline.
    void rt_term_print_f64(double v);

    /// @brief Print prompt and read a line of input.
    rt_string rt_term_ask(rt_string prompt);

    /// @brief Read a line of input from stdin.
    rt_string rt_term_read_line(void);

    /// @brief Report EOF status for the specified channel.
    /// @param ch Numeric channel identifier previously passed to OPEN.
    /// @return -1 when the next read would hit EOF, 0 otherwise, or an error code on failure.
    int rt_eof_ch(int ch);

    /// @brief Compute the length in bytes of the file associated with @p ch.
    /// @param ch Numeric channel identifier previously passed to OPEN.
    /// @return Non-negative byte length on success or -Err_* on failure.
    int64_t rt_lof_ch(int ch);

    /// @brief Report the current file position for channel @p ch.
    /// @param ch Numeric channel identifier previously passed to OPEN.
    /// @return Non-negative offset on success or -Err_* on failure.
    int64_t rt_loc_ch(int ch);

    /// @brief Reposition channel @p ch to absolute byte offset @p pos.
    /// @param ch Numeric channel identifier previously passed to OPEN.
    /// @param pos Non-negative byte offset from start of file.
    /// @return 0 on success or Err_* code on failure.
    int32_t rt_seek_ch_err(int ch, int64_t pos);

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

    /// @brief Show or hide the terminal cursor using ANSI escape sequences.
    /// @param show Non-zero to show cursor, zero to hide cursor.
    void rt_term_cursor_visible_i32(int32_t show);

    /// @brief Toggle alternate screen buffer using ANSI DEC Private Mode sequences.
    /// @param enable Non-zero to enter alternate screen, zero to exit.
    void rt_term_alt_screen_i32(int32_t enable);

    /// @brief Emit a bell/beep sound using BEL character or platform-specific API.
    /// @details Writes ASCII BEL to stdout. On Windows, optionally calls Beep() API
    ///          when VIPER_BEEP_WINAPI environment variable is set to "1".
    void rt_bell(void);

    /// @brief Block until a single key is read and return it as a 1-character string.
    rt_string rt_getkey_str(void);

    /// @brief Block for a key with optional timeout; return empty string if timeout expires.
    /// @param timeout_ms Maximum wait time in milliseconds; negative values block indefinitely.
    rt_string rt_getkey_timeout_i32(int32_t timeout_ms);

    /// @brief Return a pending key as a 1-character string or empty string if none available.
    rt_string rt_inkey_str(void);

    /// @brief Check if a key is available in the input buffer without reading it.
    /// @return Non-zero if a key is pending, zero otherwise.
    int32_t rt_keypressed(void);
    int64_t rt_keypressed_i64(void);

    // Pascal-compatible wrappers (i64 arguments)
    void rt_term_locate(int64_t row, int64_t col);
    void rt_term_color(int64_t fg, int64_t bg);
    void rt_term_textcolor(int64_t fg);
    void rt_term_textbg(int64_t bg);
    void rt_term_hide_cursor(void);
    void rt_term_show_cursor(void);
    void rt_term_cursor_visible(int64_t show);
    void rt_term_alt_screen(int64_t enable);
    void rt_sleep_ms_i64(int64_t ms);
    rt_string rt_getkey_timeout(int64_t timeout_ms);

    // Output buffering control for improved terminal rendering performance

    /// @brief Begin batch mode for output operations.
    /// @details While in batch mode, terminal control sequences do not trigger
    ///          individual flushes. This dramatically improves rendering performance
    ///          for games and animations (reduces syscalls from ~6000/frame to ~1).
    void rt_term_begin_batch(void);

    /// @brief End batch mode and flush accumulated output.
    /// @details Decrements batch mode reference count. When zero, flushes all
    ///          accumulated output in a single system call.
    void rt_term_end_batch(void);

    /// @brief Explicitly flush terminal output.
    /// @details Forces all buffered output to be written immediately.
    void rt_term_flush(void);

    // =========================================================================
    // Terminal Raw Mode Caching (Performance Optimization)
    // =========================================================================

    /// @brief Enable cached raw mode for efficient key polling.
    /// @details Switches terminal to raw mode once. Subsequent INKEY$ calls
    ///          use select() without needing to change terminal settings.
    ///          This dramatically improves performance in game loops.
    void rt_term_enable_raw_mode(void);

    /// @brief Disable raw mode and restore original terminal settings.
    /// @details Should be called before program exit or when leaving game mode.
    void rt_term_disable_raw_mode(void);

    /// @brief Check if raw mode caching is currently active.
    /// @return Non-zero if raw mode is active, zero otherwise.
    int rt_term_is_raw_mode(void);

#ifdef __cplusplus
}
#endif
