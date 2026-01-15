//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperdos/syscall_abi.hpp
// Purpose: Shared syscall ABI contract for ViperDOS (AArch64).
// Key invariants: x8=syscall#, x0-x5=args, x0=error, x1-x3=results.
// Ownership/Lifetime: Header-only; ABI documentation and types.
// Links: kernel/syscall/dispatch.cpp
//
//===----------------------------------------------------------------------===//

/**
 * @file syscall_abi.hpp
 * @brief Shared syscall ABI contract for ViperDOS (AArch64).
 *
 * @details
 * This header defines the AArch64 syscall register convention used by ViperDOS.
 * Both kernel and user-space must agree on this ABI for correct operation.
 *
 * ## ViperDOS Syscall ABI (AArch64)
 *
 * **Input registers:**
 * - x8: Syscall number (SYS_* constant)
 * - x0-x5: Up to 6 input arguments
 *
 * **Output registers:**
 * - x0: VError code (0 = success, negative = error)
 * - x1: Result value 0 (if syscall produces a result)
 * - x2: Result value 1 (if syscall produces multiple results)
 * - x3: Result value 2 (if syscall produces multiple results)
 *
 * This differs from Linux ABI where x0 contains both error and result.
 * The ViperDOS convention ensures that:
 * - Error checking is always `if (x0 != 0) { handle_error(); }`
 * - Results are always in consistent registers x1-x3
 * - Multi-value returns are natural (e.g., returning handle + size)
 *
 * ## Error Codes
 *
 * Error codes are negative i64 values. Zero indicates success.
 * See `error.hpp` for the complete list of kernel error codes.
 *
 * ## Syscall Categories by Return Convention
 *
 * Most syscalls return one of these patterns:
 * - **Void**: x0=VError, x1-x3 unused
 * - **Handle/ID**: x0=VError, x1=handle/id
 * - **Count/Size**: x0=VError, x1=count or bytes
 * - **Multi-value**: x0=VError, x1=value0, x2=value1, etc.
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief VError type - syscall error code.
     *
     * @details
     * Always returned in x0. Zero indicates success, negative values indicate
     * an error from the kernel error namespace.
     */
    typedef long long VError;

/**
 * @name Standard VError codes
 * @details These match the kernel error::Code values.
 * @{
 */
#define VERR_OK 0LL                /**< Success */
#define VERR_UNKNOWN -1LL          /**< Unknown error */
#define VERR_INVALID_ARG -2LL      /**< Invalid argument */
#define VERR_OUT_OF_MEMORY -3LL    /**< Out of memory */
#define VERR_NOT_FOUND -4LL        /**< Resource not found */
#define VERR_ALREADY_EXISTS -5LL   /**< Resource already exists */
#define VERR_PERMISSION -6LL       /**< Permission denied */
#define VERR_NOT_SUPPORTED -7LL    /**< Operation not supported */
#define VERR_BUSY -8LL             /**< Resource busy */
#define VERR_TIMEOUT -9LL          /**< Operation timed out */
#define VERR_INVALID_HANDLE -100LL /**< Invalid handle */
#define VERR_HANDLE_CLOSED -101LL  /**< Handle was closed */
#define VERR_WRONG_TYPE -102LL     /**< Wrong handle type */
#define VERR_TASK_EXITED -200LL    /**< Task has exited */
#define VERR_TASK_NOT_FOUND -201LL /**< Task not found */
#define VERR_WOULD_BLOCK -300LL    /**< Operation would block */
#define VERR_CHANNEL_CLOSED -301LL /**< Channel closed */
#define VERR_MSG_TOO_LARGE -302LL  /**< Message too large */
#define VERR_POLL_FULL -400LL      /**< Poll set is full */
#define VERR_IO -500LL             /**< I/O error */
#define VERR_NO_RESOURCE -501LL    /**< No resource available */
#define VERR_CONNECTION -502LL     /**< Connection error */
/** @} */

/**
 * @brief Check if a VError indicates success.
 */
#define VERR_IS_OK(e) ((e) == 0)

/**
 * @brief Check if a VError indicates an error.
 */
#define VERR_IS_ERR(e) ((e) < 0)

#ifdef __cplusplus
}

namespace viper
{

/**
 * @brief Syscall result structure for user-space.
 *
 * @details
 * This structure captures all output registers from a syscall.
 * User-space syscall stubs fill this from x0-x3 after `svc #0`.
 */
struct SyscallResult
{
    VError error;            ///< x0: Error code (0 = success)
    unsigned long long val0; ///< x1: First result value
    unsigned long long val1; ///< x2: Second result value
    unsigned long long val2; ///< x3: Third result value

    /**
     * @brief Check if the syscall succeeded.
     */
    bool ok() const
    {
        return error == 0;
    }

    /**
     * @brief Check if the syscall failed.
     */
    bool failed() const
    {
        return error < 0;
    }

    /**
     * @brief Get val0 as a signed value.
     */
    long long sval0() const
    {
        return static_cast<long long>(val0);
    }

    /**
     * @brief Get val0 as i32 (for handles/fds).
     */
    int handle() const
    {
        return static_cast<int>(val0);
    }

    /**
     * @brief Get val0 as size_t (for counts/sizes).
     */
    unsigned long long size() const
    {
        return val0;
    }
};

/**
 * @brief Helper to check VError success.
 */
inline bool is_ok(VError e)
{
    return e == 0;
}

/**
 * @brief Helper to check VError failure.
 */
inline bool is_err(VError e)
{
    return e < 0;
}

} // namespace viper

#endif // __cplusplus
