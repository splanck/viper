/**
 * @file faulttest_null.cpp
 * @brief Test program that intentionally dereferences NULL.
 *
 * @details
 * This program triggers a user-mode data abort by reading from address 0.
 * The kernel should:
 * 1. Print a USERFAULT line
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
 * Triggers a null pointer dereference fault.
 */
extern "C" void _start() {
    puts("[faulttest_null] About to dereference NULL...\n");

    // Intentionally dereference a null pointer
    // This should cause a data abort (translation fault at level 0)
    volatile int *null_ptr = nullptr;
    int value = *null_ptr; // BOOM!

    // Should never reach here - if we do, the fault handling failed
    (void)value;
    puts("[faulttest_null] ERROR: Should have faulted!\n");
    sys::exit(99);
}
