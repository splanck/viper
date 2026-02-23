//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_stack_safety.c
// Purpose: Implements stack overflow detection and graceful error reporting for
//          the Viper runtime. On POSIX systems, installs a SIGSEGV handler on
//          an alternate signal stack to catch stack overflows. On Windows,
//          registers a Vectored Exception Handler for EXCEPTION_STACK_OVERFLOW.
//
// Key invariants:
//   - Initialization is idempotent; repeated calls to rt_init_stack_safety are
//     safe and guarded by a volatile flag.
//   - Signal/exception handlers write diagnostic messages using async-signal-
//     safe methods (write/WriteFile) rather than fprintf, which is unsafe in
//     low-stack conditions.
//   - After detecting a stack overflow the process is terminated immediately
//     via ExitProcess/exit(1); recovery is not attempted.
//   - The alternate stack (POSIX) is a static char array of size SIGSTKSZ;
//     no heap allocation is used for signal handling infrastructure.
//   - On platforms without signal support (e.g., bare-metal) the functions
//     are compiled as no-ops.
//
// Ownership/Lifetime:
//   - All signal-handling state is in process-global static variables; no
//     heap allocation is performed by this module.
//   - The alternate signal stack buffer is statically allocated and valid for
//     the entire process lifetime once rt_init_stack_safety is called.
//
// Links: src/runtime/core/rt_stack_safety.h (public API),
//        src/runtime/core/rt_trap.c (general runtime trap/abort mechanism)
//
//===----------------------------------------------------------------------===//

#include "rt_stack_safety.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/// @brief Alternate stack for signal handling (not used on Windows).
static volatile int g_stack_safety_initialized = 0;

/// @brief Vectored exception handler for stack overflow detection.
static LONG WINAPI stack_overflow_handler(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
    {
        // Cannot safely use fprintf here as we're out of stack space.
        // Use WriteFile to stderr directly.
        HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
        const char *msg = "Viper runtime error: stack overflow\n"
                          "Hint: Reduce recursion depth or use iterative algorithms.\n"
                          "      Consider using --stack-size=SIZE to increase stack.\n";
        DWORD written;
        WriteFile(hStderr, msg, (DWORD)strlen(msg), &written, NULL);

        // Terminate immediately - cannot recover from stack overflow
        ExitProcess(1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void rt_init_stack_safety(void)
{
    if (g_stack_safety_initialized)
        return;

    // Add vectored exception handler (called before structured handlers)
    // Use 1 to make it first in the handler chain
    AddVectoredExceptionHandler(1, stack_overflow_handler);
    g_stack_safety_initialized = 1;
}

void rt_trap_stack_overflow(void)
{
    // Use WriteFile for safety in low-stack conditions
    HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
    const char *msg = "Viper runtime trap: stack overflow\n";
    DWORD written;
    WriteFile(hStderr, msg, (DWORD)strlen(msg), &written, NULL);
    ExitProcess(1);
}

#elif defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <string.h>
#include <unistd.h>

/// @brief Alternate signal stack for handling SIGSEGV.
static char g_alt_stack[SIGSTKSZ];
static volatile int g_stack_safety_initialized = 0;

/// @brief Signal handler for SIGSEGV (stack overflow detection).
static void sigsegv_handler(int sig, siginfo_t *info, void *context)
{
    (void)info;
    (void)context;

    if (sig == SIGSEGV || sig == SIGBUS)
    {
        // Write directly to stderr using write() syscall
        // (safe to use in signal handlers)
        const char *msg = "Viper runtime error: stack overflow (or segmentation fault)\n"
                          "Hint: Reduce recursion depth or use iterative algorithms.\n"
                          "      Consider increasing stack limit with ulimit -s.\n";
        write(STDERR_FILENO, msg, strlen(msg));
        _exit(1);
    }
}

void rt_init_stack_safety(void)
{
    if (g_stack_safety_initialized)
        return;

    // Set up alternate signal stack
    stack_t ss;
    ss.ss_sp = g_alt_stack;
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1)
    {
        // Failed to set up alternate stack - continue without it
        return;
    }

    // Set up signal handler with SA_ONSTACK to use alternate stack
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    // Handle both SIGSEGV and SIGBUS (macOS uses SIGBUS for some stack faults)
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);

    g_stack_safety_initialized = 1;
}

void rt_trap_stack_overflow(void)
{
    const char *msg = "Viper runtime trap: stack overflow\n";
    write(STDERR_FILENO, msg, strlen(msg));
    _exit(1);
}

#elif defined(__viperdos__)

// ViperDOS stack safety implementation.
// Signal-based guard pages require ViperDOS signal handler trampoline.
// For now, init is a no-op; overflow traps explicitly.

void rt_init_stack_safety(void)
{
    // No-op until ViperDOS signal handler trampoline is available.
}

void rt_trap_stack_overflow(void)
{
    // Use fprintf for simplicity; could use write() if available
    fprintf(stderr, "Viper runtime trap: stack overflow\n");
    fflush(stderr);
    exit(1);
}

#else
// Fallback for other platforms - no-op implementation
void rt_init_stack_safety(void)
{
    // No-op on unsupported platforms
}

void rt_trap_stack_overflow(void)
{
    fprintf(stderr, "Viper runtime trap: stack overflow\n");
    fflush(stderr);
    exit(1);
}
#endif
