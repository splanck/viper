//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_stack_safety.h
// Purpose: Stack overflow detection and graceful error handling for native code, registering platform-specific exception/signal handlers to produce a diagnostic message instead of a hard crash.
//
// Key invariants:
//   - rt_init_stack_safety must be called once at program startup before user code runs.
//   - On Unix, registers a signal handler with an alternate signal stack (SIGSTKSZ bytes).
//   - On Windows, uses Vectored Exception Handling to catch EXCEPTION_STACK_OVERFLOW.
//   - rt_trap_stack_overflow prints a diagnostic to stderr and exits with code 1.
//
// Ownership/Lifetime:
//   - Handler registration has process-wide effect; only call once.
//   - The alternate signal stack is allocated internally; caller does not manage it.
//
// Links: src/runtime/core/rt_stack_safety.c (implementation)
//
//===----------------------------------------------------------------------===//
#ifndef RT_STACK_SAFETY_H
#define RT_STACK_SAFETY_H

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Initialize stack safety handlers.
    /// @details Registers exception handlers to catch stack overflow and provide
    ///          a graceful error message instead of crashing. This function should
    ///          be called once at program startup before any user code runs.
    /// @note On Windows, uses Vectored Exception Handling.
    ///       On Unix, uses signal handlers with alternate signal stack.
    void rt_init_stack_safety(void);

    /// @brief Report a stack overflow trap and terminate the process.
    /// @details Prints a diagnostic message to stderr and exits with code 1.
    ///          This function is called by the exception handler when stack
    ///          overflow is detected.
    void rt_trap_stack_overflow(void);

#ifdef __cplusplus
}
#endif

#endif // RT_STACK_SAFETY_H
