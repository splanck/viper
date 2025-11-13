//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares the runtime error system used for error reporting across
// all runtime library functions. The error system provides a structured,
// language-agnostic way to communicate failures from C runtime code back to
// IL programs and BASIC ON ERROR handlers.
//
// The Viper runtime uses explicit error handling through out-parameters rather
// than C++ exceptions or errno. This design ensures:
// - C compatibility: Pure C runtime works in all environments
// - Predictable performance: No exception unwinding overhead
// - Clear error paths: Errors are explicit in function signatures
// - BASIC semantics: Maps cleanly to BASIC's ON ERROR mechanism
//
// Error Reporting Model:
// Runtime functions that can fail accept an optional RtError* out-parameter.
// On success, the function returns true and leaves the error unmodified. On
// failure, it returns false and populates the error structure with diagnostic
// information.
//
// RtError Structure:
// - kind: High-level error category (Err_FileNotFound, Err_Overflow, etc.)
// - code: Platform-specific detail code (errno, Win32 error code, etc.)
//
// The kind field provides portable error classification that BASIC programs
// can test in ON ERROR handlers (ERR function). The code field preserves
// platform-specific details for diagnostic logging but isn't exposed to
// user programs.
//
// Error Categories:
// - Err_None: Success (not an error)
// - Err_FileNotFound: File operations when file doesn't exist
// - Err_EOF: Read operations at end of file
// - Err_IOError: Generic I/O failures (permission denied, device error)
// - Err_Overflow: Numeric overflow or underflow
// - Err_InvalidCast: Type conversion out of range
// - Err_DomainError: Math function domain errors (sqrt negative, log zero)
// - Err_Bounds: Array subscript out of range
// - Err_InvalidOperation: Operation invalid in current state
// - Err_RuntimeError: Catch-all for unclassified errors
//
// Usage Pattern:
//   RtError err;
//   if (!rt_file_open(&file, path, "r", 0, &err)) {
//     // Handle error based on err.kind
//     if (err.kind == Err_FileNotFound) { ... }
//   }
//
// Integration with IL Traps:
// When BASIC code calls a runtime function that fails, the IL lowering
// translates the error into a trap instruction. The VM's trap mechanism
// then invokes the appropriate ON ERROR handler or terminates execution
// with a diagnostic message.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Canonical runtime error codes surfaced by runtime helpers.
    enum Err
    {
        Err_None = 0,             ///< Success.
        Err_FileNotFound = 1,     ///< File could not be located.
        Err_EOF = 2,              ///< Reached end of input.
        Err_IOError = 3,          ///< Generic input/output failure.
        Err_Overflow = 4,         ///< Numeric overflow or underflow occurred.
        Err_InvalidCast = 5,      ///< Requested cast is invalid.
        Err_DomainError = 6,      ///< Input outside valid domain.
        Err_Bounds = 7,           ///< Bounds check failed.
        Err_InvalidOperation = 8, ///< Operation unsupported in current state.
        Err_RuntimeError = 9      ///< Unclassified runtime error.
    };

    /// @brief Structured runtime error record propagated via out-parameters.
    typedef struct RtError
    {
        enum Err kind; ///< High-level error category.
        int32_t code;  ///< Implementation-specific detail code.
    } RtError;

    /// @brief Helper returning whether @p error encodes success.
    /// @param error Error record to inspect.
    /// @return True when the error represents success.
    static inline bool rt_ok(RtError error)
    {
        return error.kind == Err_None;
    }

    /// @brief Constant representing a successful runtime operation.
    extern const RtError RT_ERROR_NONE;

#ifdef __cplusplus
}
#endif
