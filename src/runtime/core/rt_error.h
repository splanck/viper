//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
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

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Canonical runtime trap kinds shared by the native runtime and VM.
typedef enum RtTrapKind {
    RT_TRAP_KIND_DIVIDE_BY_ZERO = 0,
    RT_TRAP_KIND_OVERFLOW = 1,
    RT_TRAP_KIND_INVALID_CAST = 2,
    RT_TRAP_KIND_DOMAIN_ERROR = 3,
    RT_TRAP_KIND_BOUNDS = 4,
    RT_TRAP_KIND_FILE_NOT_FOUND = 5,
    RT_TRAP_KIND_EOF = 6,
    RT_TRAP_KIND_IO_ERROR = 7,
    RT_TRAP_KIND_INVALID_OPERATION = 8,
    RT_TRAP_KIND_RUNTIME_ERROR = 9,
    RT_TRAP_KIND_INTERRUPT = 10,
    RT_TRAP_KIND_NETWORK_ERROR = 11
} RtTrapKind;

/// @brief Canonical runtime error codes surfaced by runtime helpers.
enum Err {
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
typedef struct RtError {
    enum Err kind; ///< High-level error category.
    int32_t code;  ///< Implementation-specific detail code.
} RtError;

/// @brief Helper returning whether @p error encodes success.
/// @param error Error record to inspect.
/// @return 1 when the error represents success, 0 otherwise.
static inline int8_t rt_ok(RtError error) {
    return error.kind == Err_None;
}

/// @brief Constant representing a successful runtime operation.
extern const RtError RT_ERROR_NONE;

/// @brief Store the thrown message string for retrieval by catch handlers.
/// @param msg The message string (ownership is NOT transferred; the string is copied).
void rt_throw_msg_set(rt_string msg);

/// @brief Retrieve the last thrown message string.
/// @return The message string, or an empty string if none was stored.
///         Caller receives a new reference (must release).
rt_string rt_throw_msg_get(void);

/// @brief Store trap classification fields for retrieval by catch handlers.
/// @param kind TrapKind enum value (0=DivByZero, 3=DomainError, 9=RuntimeError, etc.)
/// @param code Secondary error code.
/// @param line Source line number (-1 if unknown).
void rt_trap_fields_set(int32_t kind, int32_t code, int32_t line);

/// @brief Store the native instruction pointer associated with the most recent trap.
/// @param ip Native return address captured at the trap site.
void rt_trap_set_ip(uint64_t ip);

/// @brief Retrieve the last trap's kind classification.
int64_t rt_trap_get_kind(void);

/// @brief Retrieve the last trap's error code.
int64_t rt_trap_get_code(void);

/// @brief Retrieve the native instruction pointer of the last trap.
int64_t rt_trap_get_ip(void);

/// @brief Retrieve the last trap's source line number.
int64_t rt_trap_get_line(void);

/// @brief Map a legacy runtime Err_* code to the canonical trap-kind integer.
int32_t rt_err_to_trap_kind(int32_t code);

/// @brief Construct the current thread's native trap payload and return an opaque token.
/// @param code Legacy Err_* code.
/// @param msg User-visible trap message.
/// @return Opaque token value suitable for carrying the Error-typed result in native codegen.
void *rt_trap_error_make(int32_t code, rt_string msg);

/// @brief Raise a trap with explicit trap metadata.
/// @param kind Canonical trap classification.
/// @param code Secondary runtime error code (Err_* or 0).
/// @param line Source line number (-1 if unknown).
/// @param msg User-visible trap message.
void rt_trap_raise_kind(int32_t kind, int32_t code, int32_t line, const char *msg);

/// @brief Raise a trap classified from a legacy Err_* code while preserving @p msg.
/// @param code Legacy Err_* code.
/// @param msg User-visible trap message.
void rt_trap_raise_error_msg(int32_t code, const char *msg);

/// @brief Raise a trap classified from a legacy runtime Err_* code.
/// @param code Legacy Err_* code.
void rt_trap_raise_error(int32_t code);

#ifdef __cplusplus
}
#endif
