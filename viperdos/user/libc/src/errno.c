//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/errno.c
// Purpose: Error number storage and assertion handling.
// Key invariants: Per-thread errno via TPIDR_EL0 TCB; main thread uses global.
// Ownership/Lifetime: Library; errno persists across function calls.
// Links: user/libc/include/errno.h, user/libc/src/pthread.c (tcb_t layout)
//
//===----------------------------------------------------------------------===//

/**
 * @file errno.c
 * @brief Error number storage and assertion handling for ViperDOS libc.
 *
 * @details
 * This file provides:
 *
 * - errno: Per-thread error number storage via TPIDR_EL0 / TCB
 * - __assert_fail: Assertion failure handler for the assert() macro
 *
 * The errno mechanism allows library functions to report error conditions
 * without using return values. When a function fails, it sets errno to
 * an error code (defined in errno.h) that describes the failure.
 *
 * Per-thread errno is stored in the Thread Control Block (TCB) pointed
 * to by TPIDR_EL0. The main thread (TPIDR_EL0 == 0) uses a static
 * global fallback.
 */

#include "../include/errno.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/stddef.h"

/*
 * Partial TCB layout matching pthread.c's tcb_t struct.
 * Only fields up to errno_value are needed for offsetof().
 */
struct __tcb_layout {
    void *start_routine;
    void *arg;
    void *stack_base;
    unsigned long stack_size;
    unsigned long thread_id;
    int detached;
    int errno_value;
};

/* Main thread errno (fallback when TPIDR_EL0 == 0) */
static int __main_errno = 0;

/**
 * @brief Get pointer to the current thread's errno variable.
 *
 * @details
 * Returns a pointer to the errno variable for the current thread.
 * For spawned threads, this reads TPIDR_EL0 to find the TCB and
 * returns &tcb->errno_value. For the main thread (TPIDR_EL0 == 0),
 * returns a static global.
 *
 * @return Pointer to the errno integer for the current thread.
 */
int *__errno_location(void) {
    unsigned long tpidr;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tpidr));
    if (tpidr) {
        return (int *)((char *)tpidr +
                        offsetof(struct __tcb_layout, errno_value));
    }
    return &__main_errno;
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
