//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements stack safety utilities for the Viper runtime.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stack overflow detection and graceful error handling.

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
