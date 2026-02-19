//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_error.h
// Purpose: Structured runtime error system with categorized error codes (Err
//          enum) and an RtError record propagated via out-parameters, mapping
//          to BASIC ON ERROR handlers and IL trap instructions.
// Key invariants: Err_None == 0 means success; RtError.kind provides portable
//                 classification; RtError.code preserves platform-specific
//                 detail; rt_ok returns true only for Err_None.
// Ownership/Lifetime: RtError is a small value type (stack-allocated by
//                     callers); RT_ERROR_NONE is a global constant; no heap
//                     allocation is involved.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

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
    /// @return 1 when the error represents success, 0 otherwise.
    static inline int8_t rt_ok(RtError error)
    {
        return error.kind == Err_None;
    }

    /// @brief Constant representing a successful runtime operation.
    extern const RtError RT_ERROR_NONE;

#ifdef __cplusplus
}
#endif
