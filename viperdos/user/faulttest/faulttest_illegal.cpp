/**
 * @file faulttest_illegal.cpp
 * @brief Test program that intentionally executes an illegal instruction.
 *
 * @details
 * This program triggers a user-mode illegal instruction exception by
 * executing an undefined opcode (UDF). The kernel should:
 * 1. Print a USERFAULT line with kind=illegal_instruction
 * 2. Terminate this task
 * 3. Continue running (not panic)
 */

#include "../syscall.hpp"

/**
 * @brief Print a string to the console.
 */
static void puts(const char *s) {
    while (*s) {
        sys::putchar(*s++);
    }
}

/**
 * @brief Program entry point.
 *
 * Triggers an illegal instruction fault using UDF (permanently undefined).
 */
extern "C" void _start() {
    puts("[faulttest_illegal] About to execute illegal instruction...\n");

    // Execute an undefined instruction (UDF #0)
    // On AArch64, this triggers EC=0x00 (UNKNOWN) exception
    asm volatile("udf #0");

    // Should never reach here - if we do, the fault handling failed
    puts("[faulttest_illegal] ERROR: Should have faulted!\n");
    sys::exit(99);
}
