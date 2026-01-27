//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../include/types.hpp"

/**
 * @file console.hpp
 * @brief Kernel console I/O with buffered input.
 *
 * @details
 * The `console` namespace provides a unified console interface that:
 * - Routes output to serial and graphics console backends.
 * - Provides a buffered input path that merges keyboard and serial input.
 * - Supports canonical mode line editing (backspace, line buffering).
 *
 * The input buffer is a ring buffer that accumulates characters from both
 * the virtio keyboard (via the input subsystem) and the serial UART. This
 * allows characters to be queued between polls and provides a consistent
 * interface regardless of input source.
 */
namespace console {

/** @brief Size of the console input ring buffer. */
constexpr usize INPUT_BUFFER_SIZE = 1024;

/** @brief Size of the canonical mode line buffer. */
constexpr usize LINE_BUFFER_SIZE = 256;

/**
 * @brief Initialize the console input buffer.
 *
 * @details
 * Resets the input ring buffer and line buffer state. Call once during
 * kernel initialization after serial and input subsystems are ready.
 */
void init_input();

/**
 * @brief Poll input sources and buffer any available characters.
 *
 * @details
 * Checks both the virtio keyboard (via input::poll/getchar) and the serial
 * UART for available characters and pushes them into the console input
 * ring buffer. Should be called periodically (e.g., from timer interrupt).
 */
void poll_input();

/**
 * @brief Check if a character is available in the input buffer.
 *
 * @return `true` if at least one character can be read without blocking.
 */
bool has_input();

/**
 * @brief Read one character from the input buffer (non-blocking).
 *
 * @return Character value (0-255) or -1 if no character is available.
 */
i32 getchar();

/**
 * @brief Get the number of characters available in the input buffer.
 *
 * @return Number of buffered characters ready to read.
 */
usize input_available();

/**
 * @brief Read a line with canonical mode editing.
 *
 * @details
 * Reads characters into a line buffer with support for:
 * - Backspace: delete previous character
 * - Enter: complete the line
 * - Ctrl+C: cancel and return empty line
 * - Ctrl+D: EOF (returns -1)
 *
 * Characters are echoed to the console as they are typed.
 *
 * @param buf Output buffer for the line.
 * @param maxlen Maximum line length (including NUL terminator).
 * @return Number of characters read (not including NUL), or -1 on EOF.
 */
i32 readline(char *buf, usize maxlen);

/**
 * @brief Print a NUL-terminated string to the kernel console.
 *
 * @details
 * Writes the provided string to the current console backend. Today this is
 * forwarded to the serial UART output path.
 *
 * @param s Pointer to a NUL-terminated string.
 */
void print(const char *s);

/**
 * @brief Print a signed integer in decimal form.
 *
 * @details
 * Convenience wrapper for printing numbers in early debug output without
 * pulling in a full printf implementation.
 *
 * @param value The value to print.
 */
void print_dec(i64 value);

/**
 * @brief Print an unsigned integer in hexadecimal form.
 *
 * @details
 * Convenience wrapper intended for printing addresses and bitmasks. The output
 * is prefixed with `0x` and uses lowercase hexadecimal digits.
 *
 * @param value The value to print.
 */
void print_hex(u64 value);

} // namespace console
