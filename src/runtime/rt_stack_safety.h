//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Stack safety utilities for the Viper runtime. Provides stack overflow
// detection and graceful error handling.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Stack safety and overflow detection for native code.

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
