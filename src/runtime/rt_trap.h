//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares runtime trap handlers for unrecoverable error conditions
// in IL programs. When a program violates fundamental invariants (division by zero,
// array bounds violations, null dereference), execution must terminate immediately
// with a diagnostic message.
//
// The IL uses an explicit error-handling model without exceptions. Instructions
// that can fail (division, array access, file operations) either return error codes
// or trap immediately for unrecoverable conditions. This file provides trap handlers
// for the latter category.
//
// Trap handlers print diagnostic messages to stderr and terminate the process with
// a non-zero exit code. They are designed to be called from IL-generated code and
// runtime library implementations when continuing execution would be unsafe or
// meaningless.
//
// Key Properties:
// - Immediate termination: Trap functions never return to caller
// - Diagnostic output: Each trap prints a descriptive error message before exit
// - Process-wide scope: No attempt at recovery or cleanup beyond basic message printing
// - ABI stability: These functions are part of the runtime's stable C interface
//
// Integration: The IL verifier ensures that paths calling trap functions are properly
// marked as terminating. The codegen backends can optimize subsequent code knowing
// that trap calls do not return.
//
//===----------------------------------------------------------------------===//

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Traps the runtime on division by zero.
    void rt_trap_div0(void);

#ifdef __cplusplus
}
#endif
