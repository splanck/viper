//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "types.hpp"

/**
 * @file serial.hpp
 * @brief Low-level UART-backed serial console I/O.
 *
 * @details
 * The serial console is the kernel's lowest-dependency output mechanism and is
 * intended to work very early during boot (before the heap, scheduler, or any
 * higher-level drivers are initialized). On the current QEMU `virt` machine
 * configuration this targets the PL011 UART mapped at a fixed physical
 * address.
 *
 * The API is deliberately small and synchronous:
 * - Output routines busy-wait on UART FIFO state and never allocate memory.
 * - Input routines can block until a byte arrives, suitable for simple debug
 *   shells or "press any key" prompts.
 * - Formatting helpers are provided for common integer output during bring-up.
 *
 * Concurrency: these routines do not perform locking. If multiple CPUs/threads
 * may print concurrently, callers should serialize access at a higher level to
 * avoid interleaved output.
 */
namespace serial {

/**
 * @brief Initialize the serial console.
 *
 * @details
 * Initializes the platform UART used by the kernel for debugging and early
 * boot logging. On platforms where firmware/bootloader has already configured
 * the UART (as QEMU typically does for PL011), this may be a no-op and exists
 * primarily to make early-boot code explicit and portable across platforms.
 *
 * This function must be safe to call:
 * - Before the kernel heap is initialized.
 * - Before interrupts are enabled.
 * - From panic paths where minimal dependencies are required.
 */
void init();

/**
 * @brief Write a single character to the serial console.
 *
 * @details
 * Performs a blocking transmit of one character. The implementation waits for
 * the UART transmit FIFO to have space and then writes the byte to the UART
 * data register. This means the call is deterministic and simple, but can be
 * slow if the UART is busy or the baud rate is low.
 *
 * @param c The character to transmit.
 */
void putc(char c);

/**
 * @brief Write a NUL-terminated string to the serial console.
 *
 * @details
 * Prints bytes until the first NUL terminator is encountered. Newline handling
 * is normalized for typical terminal emulators by translating `\\n` into
 * `\\r\\n` (carriage return + line feed).
 *
 * @param s Pointer to a NUL-terminated string. The memory must remain valid
 *          for the duration of the call.
 */
void puts(const char *s);

/**
 * @brief Check whether a character is available to read.
 *
 * @details
 * Reads the UART flag/status register and reports whether the receive FIFO is
 * non-empty. This is a non-blocking poll that does not consume the character.
 *
 * @return `true` if at least one byte can be read without blocking, otherwise
 *         `false`.
 */
bool has_char();

/**
 * @brief Read one character from the serial console (blocking).
 *
 * @details
 * Waits until at least one byte is available in the UART receive FIFO and then
 * returns it. The wait loop may use low-power CPU instructions depending on
 * the platform to reduce busy spinning.
 *
 * @return The next received byte, truncated to `char`.
 */
char getc();

/**
 * @brief Read one character from the serial console (non-blocking).
 *
 * @details
 * Checks the UART receive FIFO and returns a byte if one is available.
 * Unlike @ref getc, this function never waits and can be used from polling
 * loops where the kernel needs to remain responsive.
 *
 * @return The received byte (0-255) on success, or `-1` if no byte is available.
 */
i32 getc_nonblock();

/**
 * @brief Print an unsigned integer in hexadecimal.
 *
 * @details
 * Formats `value` as a conventional hexadecimal literal prefixed with `0x` and
 * prints it to the serial console. The exact width is minimal (no leading
 * zeros) which keeps early-boot logs compact while remaining unambiguous.
 *
 * @param value The value to print.
 */
void put_hex(u64 value);

/**
 * @brief Print a signed integer in decimal.
 *
 * @details
 * Formats `value` as base-10 ASCII and prints it to the serial console. A
 * leading `-` is emitted for negative inputs.
 *
 * @param value The value to print.
 */
void put_dec(i64 value);

/**
 * @brief Print an IPv4 address in dotted decimal notation.
 *
 * @details
 * Formats a 4-byte IPv4 address as "a.b.c.d" and prints it to the serial
 * console. This is a convenience wrapper that eliminates repetitive
 * put_dec/putc sequences when logging network addresses.
 *
 * @param bytes Pointer to 4 bytes representing the IPv4 address in
 *              network byte order (big-endian, e.g., 192.168.1.1 is
 *              stored as {192, 168, 1, 1}).
 */
void put_ipv4(const u8 *bytes);

/**
 * @brief Print a MAC address in hexadecimal notation.
 *
 * @details
 * Formats a 6-byte MAC address as "xx:xx:xx:xx:xx:xx" and prints it to the
 * serial console. Each byte is printed as two lowercase hex digits.
 *
 * @param bytes Pointer to 6 bytes representing the MAC address.
 */
void put_mac(const u8 *bytes);

/**
 * @brief Print a byte size in megabytes.
 *
 * @details
 * Converts a byte count to megabytes (dividing by 1024*1024) and prints
 * the result followed by " MB". This is useful for logging memory sizes
 * during boot and diagnostics.
 *
 * @param bytes The size in bytes to convert and print.
 */
void put_size_mb(u64 bytes);

} // namespace serial
