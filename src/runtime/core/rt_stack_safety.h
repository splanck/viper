//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/core/rt_stack_safety.h
// Purpose: Stack overflow detection and graceful error handling for native code, registering
// platform-specific exception/signal handlers to produce a diagnostic message instead of a hard
// crash.
//
// Key invariants:
//   - rt_init_stack_safety is idempotent and safe under concurrent first use.
//   - On Unix, every calling thread preserves an existing alternate stack or
//     receives its own SIGSTKSZ-byte fallback stack.
//   - Process-wide handlers are installed once and do not replace an existing
//     non-default sanitizer, debugger, host, or crash-reporter handler.
//   - On Windows, uses Vectored Exception Handling to catch EXCEPTION_STACK_OVERFLOW.
//   - rt_trap_stack_overflow prints a diagnostic to stderr and exits with code 1.
//
// Ownership/Lifetime:
//   - Handler registration has process-wide effect and is managed internally.
//   - Fallback alternate-stack storage is thread-local; callers never free it.
//   - Pre-existing alternate stacks and handlers retain their original ownership.
//
// Links: src/runtime/core/rt_stack_safety.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Initialize stack safety handlers.
/// @details Registers exception handlers to catch stack overflow and provide
///          a graceful error message instead of crashing. Concurrent calls are
///          safe. On POSIX, callers should invoke this once on every native
///          thread that needs a runtime-provided alternate stack; an existing
///          stack or non-default process signal handler is preserved.
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
