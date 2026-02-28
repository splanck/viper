//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_error.h
// Purpose: Structured runtime error system with categorized error codes (Err enum) and an RtError
// record propagated via out-parameters, mapping to BASIC ON ERROR handlers and IL trap
// instructions.
//
// Key invariants:
//   - Err_None == 0 means success; all other values indicate failure.
//   - RtError.kind provides portable classification across platforms.
//   - RtError.code preserves platform-specific detail (e.g., errno).
//   - rt_ok returns true only when kind == Err_None.
//
// Ownership/Lifetime:
//   - RtError is a small value type intended for stack allocation by callers.
//   - RT_ERROR_NONE is a global constant; no heap allocation is involved.
//   - No ownership transfer; callers pass by value or pointer as needed.
//
// Links: src/runtime/core/rt_error.c (implementation)
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
        Err_RuntimeError = 9,     ///< Unclassified runtime error.

        // Network error codes (10–19).
        Err_ConnectionRefused = 10, ///< Remote host actively refused connection.
        Err_HostNotFound = 11,      ///< Hostname could not be resolved.
        Err_ConnectionReset = 12,   ///< Connection reset by remote peer (EPIPE, RST).
        Err_Timeout = 13,           ///< Operation timed out.
        Err_ConnectionClosed = 14,  ///< Operation on a closed connection.
        Err_DnsError = 15,          ///< DNS resolution failed.
        Err_InvalidUrl = 16,        ///< URL is malformed or unparseable.
        Err_TlsError = 17,          ///< TLS handshake or certificate failure.
        Err_NetworkError = 18,      ///< Generic network I/O failure.
        Err_ProtocolError = 19      ///< Protocol-level error (HTTP, WebSocket).
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
