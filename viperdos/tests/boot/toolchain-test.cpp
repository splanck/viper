/**
 * @file toolchain-test.cpp
 * @brief Minimal freestanding C++ test to verify the AArch64 toolchain works.
 *
 * @details
 * This program is used as a sanity check for the cross-compilation setup and
 * early-boot runtime assumptions. It intentionally avoids all standard library
 * dependencies and communicates only through the QEMU `virt` machine's PL011
 * UART.
 *
 * Expected behavior:
 * - `startup.S` sets up a stack and branches to @ref test_main.
 * - @ref test_main prints a short banner to the UART.
 * - The CPU then halts in a low-power loop (`wfi`).
 */

/** @brief Freestanding type aliases (no standard library headers). */
using uint32_t = unsigned int;
using uintptr_t = unsigned long;

/**
 * @name PL011 UART registers (QEMU virt)
 * @brief Memory-mapped UART definitions used for test output.
 *
 * @details
 * QEMU's `virt` machine exposes a PL011-compatible UART at `0x09000000`. This
 * test writes characters by polling the transmit FIFO-full flag and then
 * writing to the data register.
 * @{
 */
constexpr uintptr_t UART_BASE = 0x09000000;
constexpr uintptr_t UART_DR = UART_BASE + 0x00; /**< Data register (write to transmit). */
constexpr uintptr_t UART_FR = UART_BASE + 0x18; /**< Flag register (status bits). */
constexpr uint32_t UART_FR_TXFF = (1 << 5);     /**< Transmit FIFO full flag. */

/** @} */

/**
 * @brief Write a single character to the PL011 UART.
 *
 * @details
 * Polls the TX FIFO-full bit until there is space and then writes the
 * character to the data register.
 *
 * @param c Character to transmit.
 */
static void uart_putc(char c) {
    auto *dr = reinterpret_cast<volatile uint32_t *>(UART_DR);
    auto *fr = reinterpret_cast<volatile uint32_t *>(UART_FR);

    // Wait for TX FIFO to have space
    while (*fr & UART_FR_TXFF) {
        asm volatile("nop");
    }
    *dr = static_cast<uint32_t>(c);
}

/**
 * @brief Write a NUL-terminated string to the PL011 UART.
 *
 * @details
 * Sends the string byte-by-byte using @ref uart_putc. Newlines are translated
 * to CRLF (`\\r\\n`) for typical serial console compatibility.
 *
 * @param s NUL-terminated string to transmit.
 */
static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

/**
 * @brief Entry point called by the startup assembly.
 *
 * @details
 * Prints a short banner that confirms the toolchain can:
 * - Build freestanding C++ for AArch64.
 * - Link and run on the QEMU `virt` machine.
 * - Perform basic MMIO output to the PL011 UART.
 *
 * The function never returns; it halts the CPU in an infinite loop.
 */
extern "C" void test_main() {
    uart_puts("\n");
    uart_puts("=================================\n");
    uart_puts("  ViperDOS Toolchain Test\n");
    uart_puts("=================================\n");
    uart_puts("\n");
    uart_puts("Toolchain works!\n");
    uart_puts("C++ cross-compilation successful.\n");
    uart_puts("\n");
    uart_puts("Halting...\n");

    // Halt
    for (;;) {
        asm volatile("wfi");
    }
}
