#pragma once

#include "../include/types.hpp"

/**
 * @file console.hpp
 * @brief Simple kernel console printing helpers.
 *
 * @details
 * The `console` namespace provides a tiny, dependency-light printing interface
 * used throughout the kernel. At present it is a thin wrapper over the serial
 * console backend, but keeping a separate façade makes it easier to later route
 * output to multiple devices (serial, graphics console, log buffer) without
 * rewriting call sites.
 *
 * The functions here are intentionally synchronous and formatting is limited
 * to integers and strings to keep early-boot and panic usage safe.
 */
namespace console
{

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
