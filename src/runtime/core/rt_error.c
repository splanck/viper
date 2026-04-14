//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_error.c
// Purpose: Defines the canonical success sentinel RT_ERROR_NONE shared across
//   the runtime error reporting infrastructure. Centralising the definition in
//   a single translation unit ensures both VM and native runtimes observe the
//   same storage address, avoiding discrepancies when checking for the absence
//   of errors by pointer identity or atomic replacement.
//
// Key invariants:
//   - RT_ERROR_NONE.kind == Err_None and RT_ERROR_NONE.payload == 0.
//   - The object resides in static storage and is never modified at runtime.
//   - All runtime subsystems that return RtError use {Err_None, 0} for success;
//     any other kind value indicates a specific error category.
//
// Ownership/Lifetime:
//   - Static storage — no allocation, no cleanup required.
//   - Callers must treat RT_ERROR_NONE as a read-only constant.
//
// Links: src/runtime/core/rt_error.h (public API, RtError struct definition)
//
//===----------------------------------------------------------------------===//

#include "rt_error.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Canonical success error record shared across the runtime.
/// @details Initialises the discriminant to @ref Err_None and clears the
///          auxiliary payload.  Because the object resides in static
///          storage, every consumer observes the same address when checking
///          for pointer identity or performing atomic replacements.
const RtError RT_ERROR_NONE = {Err_None, 0};

#include "rt_trap.h"

/// Thread-local storage for the most recently thrown message.
/// This enables catch(e) handlers to retrieve the throw message
/// without requiring new IL opcodes.
static _Thread_local rt_string tls_throw_msg = NULL;

/// @brief Set the thread-local exception message used by `catch(e)` handlers.
/// Releases any prior message and retains a reference to @p msg (NULL clears).
void rt_throw_msg_set(rt_string msg) {
    // Release previous message if any
    if (tls_throw_msg) {
        rt_str_release_maybe(tls_throw_msg);
        tls_throw_msg = NULL;
    }
    if (msg) {
        tls_throw_msg = rt_string_ref(msg);
    }
}

/// @brief Read the most recently thrown message on this thread (returns a fresh ref).
/// Returns the empty string if no exception has been thrown.
rt_string rt_throw_msg_get(void) {
    if (tls_throw_msg) {
        return rt_string_ref(tls_throw_msg);
    }
    return rt_str_empty();
}

/// Thread-local storage for the most recently raised trap's classification.
/// Populated by rt_trap_fields_set() (called from the Zia lowerer before
/// trap instructions) and read by rt_trap_get_kind/code/line (called from
/// ErrGetKind/Code/Line in native codegen).
static _Thread_local int32_t tls_trap_kind = 0;
static _Thread_local int32_t tls_trap_code = 0;
static _Thread_local uint64_t tls_trap_ip = 0;
static _Thread_local int32_t tls_trap_line = -1;

/// @brief Populate the thread-local trap classification fields prior to a trap.
/// Called by Zia lowering before emitting trap instructions so `ErrGetKind/Code/Line`
/// can recover the values inside catch handlers.
void rt_trap_fields_set(int32_t kind, int32_t code, int32_t line) {
    tls_trap_kind = kind;
    tls_trap_code = code;
    tls_trap_line = line;
}

/// @brief Record the instruction pointer at which a trap occurred (native handler use).
void rt_trap_set_ip(uint64_t ip) {
    tls_trap_ip = ip;
}

/// @brief Read the trap kind enum from the most recent trap on this thread.
int64_t rt_trap_get_kind(void) {
    return (int64_t)tls_trap_kind;
}

/// @brief Read the underlying error code from the most recent trap on this thread.
int64_t rt_trap_get_code(void) {
    return (int64_t)tls_trap_code;
}

/// @brief Read the IL/native instruction pointer where the most recent trap fired.
int64_t rt_trap_get_ip(void) {
    return (int64_t)tls_trap_ip;
}

/// @brief Read the source line associated with the most recent trap (-1 if unknown).
int64_t rt_trap_get_line(void) {
    return (int64_t)tls_trap_line;
}

/// @brief Map an `Err_*` error code to its corresponding `RT_TRAP_KIND_*` enum.
/// All network-related errors collapse to `RT_TRAP_KIND_NETWORK_ERROR`; unknown
/// codes (including `Err_None`) map to `RT_TRAP_KIND_RUNTIME_ERROR`.
int32_t rt_err_to_trap_kind(int32_t code) {
    switch (code) {
        case Err_FileNotFound:
            return RT_TRAP_KIND_FILE_NOT_FOUND;
        case Err_EOF:
            return RT_TRAP_KIND_EOF;
        case Err_IOError:
            return RT_TRAP_KIND_IO_ERROR;
        case Err_Overflow:
            return RT_TRAP_KIND_OVERFLOW;
        case Err_InvalidCast:
            return RT_TRAP_KIND_INVALID_CAST;
        case Err_DomainError:
            return RT_TRAP_KIND_DOMAIN_ERROR;
        case Err_Bounds:
            return RT_TRAP_KIND_BOUNDS;
        case Err_InvalidOperation:
            return RT_TRAP_KIND_INVALID_OPERATION;
        case Err_ConnectionRefused:
        case Err_HostNotFound:
        case Err_ConnectionReset:
        case Err_Timeout:
        case Err_ConnectionClosed:
        case Err_DnsError:
        case Err_InvalidUrl:
        case Err_TlsError:
        case Err_NetworkError:
        case Err_ProtocolError:
            return RT_TRAP_KIND_NETWORK_ERROR;
        case Err_None:
            return RT_TRAP_KIND_RUNTIME_ERROR;
        default:
            return RT_TRAP_KIND_RUNTIME_ERROR;
    }
}

/// @brief Package an error code and message into a trap-payload pointer.
/// Sets the thread-local message and trap fields, then returns the code as a
/// pointer-sized value suitable for the IL trap operand.
void *rt_trap_error_make(int32_t code, rt_string msg) {
    rt_throw_msg_set(msg);
    rt_trap_fields_set(rt_err_to_trap_kind(code), code, -1);
    return (void *)(uintptr_t)(uint32_t)code;
}

/// @brief Raise a trap with the given error code and an optional C-string message.
/// Populates trap classification fields from @p code before calling `rt_trap`.
void rt_trap_raise_error_msg(int32_t code, const char *msg) {
    rt_trap_fields_set(rt_err_to_trap_kind(code), code, -1);
    rt_trap(msg);
}

/// @brief Raise a trap with the given error code and no associated message.
void rt_trap_raise_error(int32_t code) {
    rt_trap_raise_error_msg(code, NULL);
}

#ifdef __cplusplus
}
#endif
