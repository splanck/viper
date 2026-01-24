//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/errno.c
// Purpose: Error number storage and assertion handling.
// Key invariants: Thread-local errno (currently process-global).
// Ownership/Lifetime: Library; errno persists across function calls.
// Links: user/libc/include/errno.h
//
//===----------------------------------------------------------------------===//

/**
 * @file errno.c
 * @brief Error number storage and assertion handling for ViperDOS libc.
 *
 * @details
 * This file provides:
 *
 * - errno: Thread-local error number storage (currently process-global)
 * - __assert_fail: Assertion failure handler for the assert() macro
 *
 * The errno mechanism allows library functions to report error conditions
 * without using return values. When a function fails, it sets errno to
 * an error code (defined in errno.h) that describes the failure.
 *
 * Note: The current implementation uses a single global errno, which is
 * not thread-safe. A future implementation should use thread-local storage.
 */

#include "../include/errno.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

/* Simple errno storage - one per process for now (not thread-safe) */
static int __errno_value = 0;

/**
 * @brief Get pointer to thread-local errno variable.
 *
 * @details
 * Returns a pointer to the errno variable for the current thread.
 * The errno macro expands to (*__errno_location()), allowing errno
 * to be used as an lvalue (e.g., errno = 0).
 *
 * Note: Current implementation uses a single global variable, so
 * this is not thread-safe. A future implementation should use TLS.
 *
 * @return Pointer to the errno integer for the current thread.
 */
int *__errno_location(void) {
    return &__errno_value;
}

/**
 * @brief Handle assertion failure.
 *
 * @details
 * Called by the assert() macro when an assertion fails. Prints a
 * diagnostic message to stderr including the failed expression,
 * source file, line number, and optionally the function name,
 * then terminates the program via abort().
 *
 * This function does not return.
 *
 * @param expr String representation of the failed assertion expression.
 * @param file Source file name where assertion failed.
 * @param line Line number where assertion failed.
 * @param func Function name where assertion failed (may be NULL).
 */
void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    fprintf(stderr, "Assertion failed: %s, file %s, line %d", expr, file, line);
    if (func) {
        fprintf(stderr, ", function %s", func);
    }
    fprintf(stderr, "\n");
    abort();
}
