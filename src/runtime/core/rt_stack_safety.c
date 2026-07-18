//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_stack_safety.c
// Purpose: Implements stack overflow detection and graceful error reporting for
//          the Zanna runtime. On POSIX systems, installs a SIGSEGV handler on
//          an alternate signal stack to catch stack overflows. On Windows,
//          registers a Vectored Exception Handler for EXCEPTION_STACK_OVERFLOW.
//
// Key invariants:
//   - Process-wide signal/exception handlers are installed transactionally at
//     most once, while concurrent callers wait for publication.
//   - POSIX alternate signal stacks are per-thread. Every caller preserves an
//     already-enabled stack (including sanitizer-owned stacks) or installs a
//     distinct thread-local buffer before returning.
//   - Signal/exception handlers write diagnostic messages using async-signal-
//     safe methods (write/WriteFile) rather than fprintf, which is unsafe in
//     low-stack conditions.
//   - After detecting a stack overflow the process is terminated immediately
//     via ExitProcess/exit(1); recovery is not attempted.
//   - The fallback alternate stack (POSIX) is a thread-local buffer of size
//     SIGSTKSZ; no heap allocation is used for signal infrastructure.
//   - On platforms without signal support (e.g., bare-metal) the functions
//     are compiled as no-ops.
//
// Ownership/Lifetime:
//   - Handler state is process-global; POSIX alternate-stack storage belongs to
//     each calling thread and remains valid for that thread's lifetime.
//   - Existing alternate stacks and existing non-default fatal-signal handlers
//     remain owned by the component that installed them.
//
// Links: src/runtime/core/rt_stack_safety.h (public API),
//        src/runtime/core/rt_trap.c (general runtime trap/abort mechanism)
//
//===----------------------------------------------------------------------===//

#include "rt_stack_safety.h"

#include "rt_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum {
    RT_STACK_SAFETY_UNINITIALIZED = 0,
    RT_STACK_SAFETY_INITIALIZING = 1,
    RT_STACK_SAFETY_READY = 2,
};

/// @brief Atomic publication state for the process-wide exception handler.
static int g_stack_safety_state = RT_STACK_SAFETY_UNINITIALIZED;

/// @brief Registration token retained for the lifetime of the process.
static PVOID g_stack_safety_handler = NULL;

/// @brief Vectored exception handler for stack overflow detection.
static LONG WINAPI stack_overflow_handler(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        // Cannot safely use fprintf here as we're out of stack space.
        // Use WriteFile to stderr directly.
        HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
        const char *msg = "Zanna runtime error: stack overflow\n"
                          "Hint: Reduce recursion depth or use iterative algorithms.\n"
                          "      Consider using --stack-size=SIZE to increase stack.\n";
        DWORD written;
        WriteFile(hStderr, msg, (DWORD)strlen(msg), &written, NULL);

        // Terminate immediately - cannot recover from stack overflow
        ExitProcess(1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/// @brief Install the Windows exception handler after winning initialization.
/// @details Registration occurs before global error-mode changes so a failed
///          `AddVectoredExceptionHandler` attempt leaves those modes untouched.
/// @return Non-zero when the handler was registered and can be published.
static int install_stack_safety_handler(void) {
    PVOID handler = AddVectoredExceptionHandler(1, stack_overflow_handler);
    if (!handler)
        return 0;

    g_stack_safety_handler = handler;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    return 1;
}

void rt_init_stack_safety(void) {
    for (;;) {
        int state = __atomic_load_n(&g_stack_safety_state, __ATOMIC_ACQUIRE);
        if (state == RT_STACK_SAFETY_READY)
            return;
        if (state == RT_STACK_SAFETY_UNINITIALIZED) {
            int expected = RT_STACK_SAFETY_UNINITIALIZED;
            if (__atomic_compare_exchange_n(&g_stack_safety_state,
                                            &expected,
                                            RT_STACK_SAFETY_INITIALIZING,
                                            0,
                                            __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                int ready = install_stack_safety_handler();
                __atomic_store_n(&g_stack_safety_state,
                                 ready ? RT_STACK_SAFETY_READY : RT_STACK_SAFETY_UNINITIALIZED,
                                 __ATOMIC_RELEASE);
                return;
            }
        }
    }
}

void rt_trap_stack_overflow(void) {
    // Use WriteFile for safety in low-stack conditions
    HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
    const char *msg = "Zanna runtime trap: stack overflow\n";
    DWORD written;
    WriteFile(hStderr, msg, (DWORD)strlen(msg), &written, NULL);
    ExitProcess(1);
}

#elif defined(__unix__) || defined(__APPLE__)
#include <signal.h>
#include <string.h>
#include <unistd.h>

enum {
    RT_STACK_SAFETY_UNINITIALIZED = 0,
    RT_STACK_SAFETY_INITIALIZING = 1,
    RT_STACK_SAFETY_READY = 2,
};

/// @brief Naturally aligned per-thread storage used only when no stack exists.
static RT_THREAD_LOCAL union {
    max_align_t alignment;
    unsigned char bytes[SIGSTKSZ];
} g_alt_stack;

/// @brief Atomic publication state for the process-wide signal handlers.
static int g_stack_safety_state = RT_STACK_SAFETY_UNINITIALIZED;

/// @brief Signal handler for SIGSEGV (stack overflow detection).
static void sigsegv_handler(int sig, siginfo_t *info, void *context) {
    (void)info;
    (void)context;

    if (sig == SIGSEGV || sig == SIGBUS) {
        static const char msg[] = "Zanna runtime error: stack overflow (or segmentation fault)\n"
                                  "Hint: Reduce recursion depth or use iterative algorithms.\n"
                                  "      Consider increasing stack limit with ulimit -s.\n";
        (void)write(STDERR_FILENO, msg, sizeof(msg) - 1U);
        _exit(1);
    }
}

/// @brief Ensure the calling POSIX thread has an alternate signal stack.
/// @details `sigaltstack` state is thread-specific. An existing enabled stack
///          is deliberately preserved because runtimes such as ASan own and
///          unmap the stack they install during thread teardown. When disabled,
///          this function installs the caller's distinct thread-local buffer.
/// @return Non-zero when an alternate stack was already present or installed.
static int ensure_thread_alt_stack(void) {
    stack_t current;
    if (sigaltstack(NULL, &current) != 0)
        return 0;
    if ((current.ss_flags & SS_DISABLE) == 0)
        return 1;

    stack_t replacement;
    memset(&replacement, 0, sizeof(replacement));
    replacement.ss_sp = g_alt_stack.bytes;
    replacement.ss_size = sizeof(g_alt_stack.bytes);
    return sigaltstack(&replacement, NULL) == 0;
}

/// @brief Test whether a fatal-signal disposition belongs to another runtime.
/// @details Zanna does not replace non-default handlers (including ignored
///          dispositions), preserving sanitizer, debugger, embedding-host, and
///          crash-reporter ownership.
/// @param action Signal disposition returned by a query-only `sigaction` call.
/// @return Non-zero when the disposition is not the platform default.
static int has_external_signal_handler(const struct sigaction *action) {
    return action->sa_handler != SIG_DFL;
}

/// @brief Install both process-wide POSIX fatal-signal handlers transactionally.
/// @details Existing non-default ownership causes a successful no-op. If the
///          SIGBUS installation fails after SIGSEGV succeeds, the previous
///          SIGSEGV disposition is restored before reporting failure.
/// @return Non-zero when the desired state is safe to publish as ready.
static int install_stack_safety_handlers(void) {
    struct sigaction previous_segv;
    struct sigaction previous_bus;
    if (sigaction(SIGSEGV, NULL, &previous_segv) != 0 ||
        sigaction(SIGBUS, NULL, &previous_bus) != 0) {
        return 0;
    }
    if (has_external_signal_handler(&previous_segv) || has_external_signal_handler(&previous_bus)) {
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0)
        return 0;
    if (sigaction(SIGBUS, &sa, NULL) != 0) {
        (void)sigaction(SIGSEGV, &previous_segv, NULL);
        return 0;
    }
    return 1;
}

void rt_init_stack_safety(void) {
    (void)ensure_thread_alt_stack();

    for (;;) {
        int state = __atomic_load_n(&g_stack_safety_state, __ATOMIC_ACQUIRE);
        if (state == RT_STACK_SAFETY_READY)
            return;
        if (state == RT_STACK_SAFETY_UNINITIALIZED) {
            int expected = RT_STACK_SAFETY_UNINITIALIZED;
            if (__atomic_compare_exchange_n(&g_stack_safety_state,
                                            &expected,
                                            RT_STACK_SAFETY_INITIALIZING,
                                            0,
                                            __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                int ready = install_stack_safety_handlers();
                __atomic_store_n(&g_stack_safety_state,
                                 ready ? RT_STACK_SAFETY_READY : RT_STACK_SAFETY_UNINITIALIZED,
                                 __ATOMIC_RELEASE);
                return;
            }
        }
    }
}

void rt_trap_stack_overflow(void) {
    static const char msg[] = "Zanna runtime trap: stack overflow\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1U);
    _exit(1);
}

#else
// Fallback for other platforms - no-op implementation
void rt_init_stack_safety(void) {
    // No-op on unsupported platforms
}

void rt_trap_stack_overflow(void) {
    fprintf(stderr, "Zanna runtime trap: stack overflow\n");
    fflush(stderr);
    exit(1);
}
#endif
