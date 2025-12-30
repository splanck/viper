/**
 * @file table.hpp
 * @brief Syscall dispatch table definitions.
 *
 * @details
 * Provides a table-driven syscall dispatch mechanism. Each syscall is
 * registered with its number, handler function, name, and argument count.
 * This enables:
 * - Auditable syscall enumeration
 * - Uniform error handling (-ENOSYS for unknown syscalls)
 * - Optional syscall tracing
 * - Host-side testing of table invariants
 */
#pragma once

#include "../include/error.hpp"
#include "../include/types.hpp"

namespace syscall
{

/**
 * @brief Result structure returned by syscall handlers.
 *
 * @details
 * Encapsulates the ABI contract:
 * - verr: Error code (0 = success, negative = error)
 * - res0-res2: Up to 3 result values
 */
struct SyscallResult
{
    i64 verr; ///< Error code (VError)
    u64 res0; ///< Result value 0
    u64 res1; ///< Result value 1
    u64 res2; ///< Result value 2

    /// Construct a success result with no values
    static SyscallResult ok()
    {
        return {error::VOK, 0, 0, 0};
    }

    /// Construct a success result with one value
    static SyscallResult ok(u64 r0)
    {
        return {error::VOK, r0, 0, 0};
    }

    /// Construct a success result with two values
    static SyscallResult ok(u64 r0, u64 r1)
    {
        return {error::VOK, r0, r1, 0};
    }

    /// Construct a success result with three values
    static SyscallResult ok(u64 r0, u64 r1, u64 r2)
    {
        return {error::VOK, r0, r1, r2};
    }

    /// Construct an error result
    static SyscallResult err(i64 verr)
    {
        return {verr, 0, 0, 0};
    }

    /// Construct an error result with partial results
    static SyscallResult err_with(i64 verr, u64 r0)
    {
        return {verr, r0, 0, 0};
    }
};

/**
 * @brief Syscall handler function type.
 *
 * @details
 * Takes up to 6 arguments (matching x0-x5 registers) and returns
 * a SyscallResult containing error code and result values.
 */
using SyscallHandler = SyscallResult (*)(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

/**
 * @brief Syscall table entry.
 *
 * @details
 * Describes a single syscall with:
 * - number: Syscall number (SYS_* constant)
 * - handler: Function pointer to handler (null = unimplemented)
 * - name: Human-readable name for tracing/debugging
 * - argcount: Number of arguments (0-6)
 */
struct SyscallEntry
{
    u32 number;             ///< Syscall number
    SyscallHandler handler; ///< Handler function (null if unimplemented)
    const char *name;       ///< Syscall name (e.g., "task_yield")
    u8 argcount;            ///< Number of arguments (0-6)
};

/**
 * @brief Maximum syscall number supported.
 */
constexpr u32 SYSCALL_MAX = 0x10F;

/**
 * @brief Get the syscall dispatch table.
 *
 * @return Pointer to the first entry in the table.
 */
const SyscallEntry *get_table();

/**
 * @brief Get the number of entries in the syscall table.
 *
 * @return Number of registered syscalls.
 */
usize get_table_size();

/**
 * @brief Look up a syscall entry by number.
 *
 * @param number Syscall number to look up.
 * @return Pointer to entry if found, nullptr otherwise.
 */
const SyscallEntry *lookup(u32 number);

/**
 * @brief Dispatch a syscall by number.
 *
 * @param number Syscall number.
 * @param a0-a5 Arguments from registers x0-x5.
 * @return SyscallResult containing error code and results.
 */
SyscallResult dispatch_syscall(u32 number, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

// =============================================================================
// Tracing Support
// =============================================================================

#ifdef CONFIG_SYSCALL_TRACE
/**
 * @brief Enable or disable syscall tracing.
 *
 * @param enabled Whether to enable tracing.
 */
void set_tracing(bool enabled);

/**
 * @brief Check if syscall tracing is enabled.
 *
 * @return true if tracing is enabled.
 */
bool is_tracing();
#endif

// =============================================================================
// User Pointer Validation (exported for handlers and tests)
// =============================================================================

/**
 * @brief Validate a user-provided pointer for reading.
 *
 * @param ptr User-provided pointer.
 * @param size Size of the buffer in bytes.
 * @param null_ok Whether null is acceptable (with size=0).
 * @return true if the pointer is valid for reading.
 */
bool validate_user_read(const void *ptr, usize size, bool null_ok = false);

/**
 * @brief Validate a user-provided pointer for writing.
 *
 * @param ptr User-provided pointer.
 * @param size Size of the buffer in bytes.
 * @param null_ok Whether null is acceptable (with size=0).
 * @return true if the pointer is valid for writing.
 */
bool validate_user_write(void *ptr, usize size, bool null_ok = false);

/**
 * @brief Validate a user-provided string.
 *
 * @param str User-provided string.
 * @param max_len Maximum allowed length (not including null terminator).
 * @return String length, or -1 if invalid.
 */
i64 validate_user_string(const char *str, usize max_len);

} // namespace syscall
