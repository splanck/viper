//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rt_object.h"
#include "rt_option.h"

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

/// @brief Clear the thread-local exception message used by `catch(e)`.
void rt_throw_msg_clear(void) {
    if (tls_throw_msg) {
        rt_str_release_maybe(tls_throw_msg);
        tls_throw_msg = NULL;
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

/// @brief Map a trap-kind code to its short PascalCase name (e.g. "Overflow").
/// @return A static string; unknown kinds fall back to "RuntimeError".
static const char *rt_trap_kind_name_cstr(int32_t kind) {
    switch (kind) {
        case RT_TRAP_KIND_DIVIDE_BY_ZERO:
            return "DivideByZero";
        case RT_TRAP_KIND_OVERFLOW:
            return "Overflow";
        case RT_TRAP_KIND_INVALID_CAST:
            return "InvalidCast";
        case RT_TRAP_KIND_DOMAIN_ERROR:
            return "DomainError";
        case RT_TRAP_KIND_BOUNDS:
            return "Bounds";
        case RT_TRAP_KIND_FILE_NOT_FOUND:
            return "FileNotFound";
        case RT_TRAP_KIND_EOF:
            return "EOF";
        case RT_TRAP_KIND_IO_ERROR:
            return "IOError";
        case RT_TRAP_KIND_INVALID_OPERATION:
            return "InvalidOperation";
        case RT_TRAP_KIND_RUNTIME_ERROR:
            return "RuntimeError";
        case RT_TRAP_KIND_INTERRUPT:
            return "Interrupt";
        case RT_TRAP_KIND_NETWORK_ERROR:
            return "NetworkError";
        default:
            return "RuntimeError";
    }
}

/// @brief Default human-readable message for a trap kind (e.g. "Division by
///        zero"). @return A static string; unknown kinds fall back to the
///        generic runtime-error message.
static const char *rt_trap_kind_default_message_cstr(int32_t kind) {
    switch (kind) {
        case RT_TRAP_KIND_DIVIDE_BY_ZERO:
            return "Division by zero";
        case RT_TRAP_KIND_OVERFLOW:
            return "Numeric overflow";
        case RT_TRAP_KIND_INVALID_CAST:
            return "Invalid cast";
        case RT_TRAP_KIND_DOMAIN_ERROR:
            return "Domain error";
        case RT_TRAP_KIND_BOUNDS:
            return "Bounds check failed";
        case RT_TRAP_KIND_FILE_NOT_FOUND:
            return "File not found";
        case RT_TRAP_KIND_EOF:
            return "End of file";
        case RT_TRAP_KIND_IO_ERROR:
            return "I/O error";
        case RT_TRAP_KIND_INVALID_OPERATION:
            return "Invalid operation";
        case RT_TRAP_KIND_INTERRUPT:
            return "Interrupted";
        case RT_TRAP_KIND_NETWORK_ERROR:
            return "Network error";
        case RT_TRAP_KIND_RUNTIME_ERROR:
        default:
            return "Runtime error";
    }
}

rt_string rt_error_kind_name(int32_t kind) {
    const char *name = rt_trap_kind_name_cstr(kind);
    return rt_string_from_bytes(name, strlen(name));
}

rt_string rt_error_message(int32_t kind, int32_t code, int32_t line) {
    (void)code;
    (void)line;
    if (kind == RT_TRAP_KIND_RUNTIME_ERROR && tls_throw_msg) {
        return rt_string_ref(tls_throw_msg);
    }
    const char *message = rt_trap_kind_default_message_cstr(kind);
    return rt_string_from_bytes(message, strlen(message));
}

rt_string rt_error_location(int32_t kind, int32_t code, int32_t line) {
    (void)kind;
    (void)code;
    if (line < 0)
        return rt_str_empty();
    char buf[64];
    int written = snprintf(buf, sizeof(buf), "line %d", line);
    if (written <= 0)
        return rt_str_empty();
    size_t len = (size_t)written;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    return rt_string_from_bytes(buf, len);
}

/// Thread-local storage for the most recently raised trap's classification.
/// Populated by rt_trap_fields_set() (called from the Zia lowerer before
/// trap instructions) and read by rt_trap_get_kind/code/line (called from
/// ErrGetKind/Code/Line in native codegen).
static _Thread_local int32_t tls_trap_kind = 0;
static _Thread_local int32_t tls_trap_code = 0;
static _Thread_local uint64_t tls_trap_ip = 0;
static _Thread_local int32_t tls_trap_line = -1;
static _Thread_local int8_t tls_trap_has_current = 0;

/// @brief Immutable snapshot backing `Zanna.Diagnostics.TrapInfo`.
/// @details Captures thread-local trap metadata at the moment
///          `Zanna.Diagnostics.CurrentTrap` is called. String fields are owned
///          by the snapshot and released by @ref rt_trap_info_finalizer.
typedef struct rt_trap_info {
    int64_t kind;
    int64_t code;
    int64_t ip;
    int64_t line;
    rt_string kind_name;
    rt_string message;
    rt_string location;
} rt_trap_info_t;

/// @brief Release owned strings stored inside a TrapInfo snapshot.
/// @param obj Opaque `rt_trap_info_t` payload allocated by @ref rt_obj_new_i64.
static void rt_trap_info_finalizer(void *obj) {
    rt_trap_info_t *info = (rt_trap_info_t *)obj;
    if (!info)
        return;
    rt_str_release_maybe(info->kind_name);
    rt_str_release_maybe(info->message);
    rt_str_release_maybe(info->location);
    info->kind_name = NULL;
    info->message = NULL;
    info->location = NULL;
}

/// @brief Release the caller-owned TrapInfo object reference.
/// @details Used after wrapping a snapshot in `Option.Some`. If the Option
///          retained the object, this drops only the temporary reference. If a
///          later allocation failed, this frees the partially built snapshot.
/// @param info Snapshot to release; may be NULL.
static void rt_trap_info_release_maybe(rt_trap_info_t *info) {
    if (info && rt_obj_release_check0(info))
        rt_obj_free(info);
}

/// @brief Validate and cast an opaque value to a TrapInfo snapshot.
/// @param obj Candidate runtime object.
/// @param member Member name used in trap diagnostics.
/// @return Valid snapshot pointer, or NULL after reporting a trap.
static rt_trap_info_t *rt_trap_info_checked(void *obj, const char *member) {
    if (rt_obj_is_instance(obj, RT_TRAP_INFO_CLASS_ID, sizeof(rt_trap_info_t)))
        return (rt_trap_info_t *)obj;
    char buffer[160];
    snprintf(buffer,
             sizeof(buffer),
             "Zanna.Diagnostics.TrapInfo.%s: invalid TrapInfo object",
             member ? member : "member");
    rt_trap_raise_kind(RT_TRAP_KIND_INVALID_OPERATION, Err_InvalidOperation, -1, buffer);
    return NULL;
}

/// @brief Allocate a TrapInfo snapshot from the current thread-local trap state.
/// @details Copies scalar fields and materializes string fields into owned
///          runtime strings. The caller owns the returned object reference and
///          must release it after wrapping or consuming it.
/// @return Owned `rt_trap_info_t` object snapshot.
static rt_trap_info_t *rt_trap_info_snapshot_new(void) {
    rt_trap_info_t *info =
        (rt_trap_info_t *)rt_obj_new_i64(RT_TRAP_INFO_CLASS_ID, (int64_t)sizeof(rt_trap_info_t));
    if (!info)
        return NULL;

    info->kind = (int64_t)tls_trap_kind;
    info->code = (int64_t)tls_trap_code;
    info->ip = (int64_t)tls_trap_ip;
    info->line = (int64_t)tls_trap_line;
    info->kind_name = NULL;
    info->message = NULL;
    info->location = NULL;
    rt_obj_set_finalizer(info, rt_trap_info_finalizer);

    info->kind_name = rt_error_kind_name(tls_trap_kind);
    const char *recovered_message = rt_trap_get_error();
    if (recovered_message && recovered_message[0])
        info->message = rt_const_cstr(recovered_message);
    else
        info->message = rt_error_message(tls_trap_kind, tls_trap_code, tls_trap_line);
    info->location = rt_error_location(tls_trap_kind, tls_trap_code, tls_trap_line);
    return info;
}

/// @brief Populate the thread-local trap classification fields prior to a trap.
/// Called by Zia lowering before emitting trap instructions so `ErrGetKind/Code/Line`
/// can recover the values inside catch handlers.
void rt_trap_fields_set(int32_t kind, int32_t code, int32_t line) {
    tls_trap_kind = kind;
    tls_trap_code = code;
    tls_trap_line = line;
    tls_trap_has_current = 1;
}

/// @brief Record the instruction pointer at which a trap occurred (native handler use).
void rt_trap_set_ip(uint64_t ip) {
    tls_trap_ip = ip;
    tls_trap_has_current = 1;
}

/// @brief Return the current thread's trap snapshot as `Option<TrapInfo>`.
void *rt_diagnostics_current_trap(void) {
    if (!tls_trap_has_current)
        return rt_option_none();

    rt_trap_info_t *info = NULL;
    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) != 0) {
        char saved_error[256];
        const char *err = rt_trap_get_error();
        snprintf(saved_error,
                 sizeof(saved_error),
                 "%s",
                 err && err[0] ? err : "Zanna.Diagnostics.CurrentTrap: allocation failed");
        rt_trap_clear_recovery();
        rt_trap_info_release_maybe(info);
        rt_trap(saved_error);
        return NULL;
    }

    info = rt_trap_info_snapshot_new();
    void *option = rt_option_some(info);
    rt_trap_clear_recovery();
    rt_trap_info_release_maybe(info);
    return option;
}

/// @brief Read the trap kind from a `Zanna.Diagnostics.TrapInfo` snapshot.
int64_t rt_trap_info_get_kind(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "Kind");
    return info ? info->kind : 0;
}

/// @brief Read the runtime error code from a `Zanna.Diagnostics.TrapInfo` snapshot.
int64_t rt_trap_info_get_code(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "Code");
    return info ? info->code : 0;
}

/// @brief Read the instruction pointer from a `Zanna.Diagnostics.TrapInfo` snapshot.
int64_t rt_trap_info_get_ip(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "Ip");
    return info ? info->ip : 0;
}

/// @brief Read the source line from a `Zanna.Diagnostics.TrapInfo` snapshot.
int64_t rt_trap_info_get_line(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "Line");
    return info ? info->line : -1;
}

/// @brief Read the trap kind name from a `Zanna.Diagnostics.TrapInfo` snapshot.
rt_string rt_trap_info_get_kind_name(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "KindName");
    if (!info || !info->kind_name)
        return rt_str_empty();
    return rt_string_ref(info->kind_name);
}

/// @brief Read the message from a `Zanna.Diagnostics.TrapInfo` snapshot.
rt_string rt_trap_info_get_message(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "Message");
    if (!info || !info->message)
        return rt_str_empty();
    return rt_string_ref(info->message);
}

/// @brief Read the formatted location from a `Zanna.Diagnostics.TrapInfo` snapshot.
rt_string rt_trap_info_get_location(void *obj) {
    rt_trap_info_t *info = rt_trap_info_checked(obj, "Location");
    if (!info || !info->location)
        return rt_str_empty();
    return rt_string_ref(info->location);
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
/// Preserves the trap classification mapped from @p code.
void rt_trap_raise_error_msg(int32_t code, const char *msg) {
    rt_trap_raise_kind(rt_err_to_trap_kind(code), code, -1, msg);
}

/// @brief Raise a trap with the given error code and no associated message.
void rt_trap_raise_error(int32_t code) {
    rt_trap_raise_error_msg(code, NULL);
}

#ifdef __cplusplus
}
#endif
