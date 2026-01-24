#include "serial.hpp"
#include "../include/constants.hpp"
#include "../lib/spinlock.hpp"

namespace kc = kernel::constants;

/**
 * @file serial.cpp
 * @brief PL011 UART implementation for the serial console API.
 *
 * @details
 * This translation unit contains the concrete implementation of the serial
 * console declared in `serial.hpp`. The implementation assumes the QEMU
 * AArch64 `virt` machine's PL011 UART is memory-mapped at a fixed physical
 * address and uses simple polling on the UART flag register for both transmit
 * and receive paths.
 *
 * Design goals:
 * - Minimal dependencies: safe during early boot and panic handling.
 * - Predictable behavior: polling-based I/O with no dynamic allocation.
 * - Terminal-friendly output: newline normalization to CRLF.
 * - SMP-safe: spinlock protects multi-character output operations.
 */
namespace serial
{

// Spinlock for serializing output (prevents interleaved output from multiple CPUs)
static Spinlock serial_lock;

// PL011 UART registers for QEMU virt machine
namespace
{
constexpr uintptr UART_BASE = kc::hw::UART_BASE;

// Register offsets
constexpr uintptr UART_DR = 0x00; // Data Register
constexpr uintptr UART_FR = 0x18; // Flag Register

// Flag register bits
constexpr u32 UART_FR_RXFE = (1 << 4); // Receive FIFO Empty
constexpr u32 UART_FR_TXFF = (1 << 5); // Transmit FIFO Full

// Register access helpers
/**
 * @brief Return a reference to a memory-mapped UART register.
 *
 * @details
 * The PL011 UART is accessed via memory-mapped I/O. Returning a reference
 * to a `volatile` location makes reads/writes explicit and prevents the
 * compiler from optimizing away register accesses.
 *
 * @param offset Byte offset from the UART base address.
 * @return Reference to the 32-bit register at `UART_BASE + offset`.
 */
inline volatile u32 &reg(uintptr offset)
{
    return *reinterpret_cast<volatile u32 *>(UART_BASE + offset);
}
} // namespace

/** @copydoc serial::init */
void init()
{
    // QEMU's PL011 UART is already initialized by firmware
    // Nothing to do for basic serial output
}

/** @copydoc serial::putc */
void putc(char c)
{
    // Wait for transmit FIFO to have space
    while (reg(UART_FR) & UART_FR_TXFF)
    {
        asm volatile("nop");
    }

    // Write character
    reg(UART_DR) = static_cast<u32>(c);
}

/** @copydoc serial::has_char */
bool has_char()
{
    // Check if receive FIFO has data
    return !(reg(UART_FR) & UART_FR_RXFE);
}

/** @copydoc serial::getc */
char getc()
{
    // Wait for receive FIFO to have data
    while (reg(UART_FR) & UART_FR_RXFE)
    {
        asm volatile("wfe"); // Wait for event (low power wait)
    }

    // Read and return character
    return static_cast<char>(reg(UART_DR) & 0xFF);
}

/** @copydoc serial::getc_nonblock */
i32 getc_nonblock()
{
    // Return character if available, -1 otherwise
    if (reg(UART_FR) & UART_FR_RXFE)
    {
        return -1;
    }
    return static_cast<i32>(reg(UART_DR) & 0xFF);
}

// Internal puts without lock (caller must hold serial_lock)
static void puts_unlocked(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
        {
            putc('\r');
        }
        putc(*s++);
    }
}

/** @copydoc serial::puts */
void puts(const char *s)
{
    SpinlockGuard guard(serial_lock);
    puts_unlocked(s);
}

/** @copydoc serial::put_hex */
void put_hex(u64 value)
{
    SpinlockGuard guard(serial_lock);

    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i = 16;
    buf[i] = '\0';

    do
    {
        buf[--i] = hex[value & 0xF];
        value >>= 4;
    } while (value && i > 0);

    puts_unlocked("0x");
    puts_unlocked(&buf[i]);
}

/** @copydoc serial::put_dec */
void put_dec(i64 value)
{
    SpinlockGuard guard(serial_lock);

    if (value < 0)
    {
        putc('-');
        value = -value;
    }

    char buf[21];
    int i = 20;
    buf[i] = '\0';

    do
    {
        buf[--i] = '0' + (value % 10);
        value /= 10;
    } while (value && i > 0);

    puts_unlocked(&buf[i]);
}

/** @copydoc serial::put_ipv4 */
void put_ipv4(const u8 *bytes)
{
    SpinlockGuard guard(serial_lock);
    for (int i = 0; i < 4; i++)
    {
        if (i > 0)
            putc('.');
        // Inline decimal conversion for bytes (0-255)
        u8 val = bytes[i];
        if (val >= 100)
        {
            putc('0' + (val / 100));
            val %= 100;
            putc('0' + (val / 10));
            putc('0' + (val % 10));
        }
        else if (val >= 10)
        {
            putc('0' + (val / 10));
            putc('0' + (val % 10));
        }
        else
        {
            putc('0' + val);
        }
    }
}

/** @copydoc serial::put_mac */
void put_mac(const u8 *bytes)
{
    SpinlockGuard guard(serial_lock);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++)
    {
        if (i > 0)
            putc(':');
        putc(hex[(bytes[i] >> 4) & 0xF]);
        putc(hex[bytes[i] & 0xF]);
    }
}

/** @copydoc serial::put_size_mb */
void put_size_mb(u64 bytes)
{
    SpinlockGuard guard(serial_lock);
    u64 mb = bytes / (1024 * 1024);

    // Convert to decimal string
    char buf[21];
    int i = 20;
    buf[i] = '\0';

    do
    {
        buf[--i] = '0' + (mb % 10);
        mb /= 10;
    } while (mb && i > 0);

    puts_unlocked(&buf[i]);
    puts_unlocked(" MB");
}

} // namespace serial
