#pragma once

#include "types.hpp"

/**
 * @file error.hpp
 * @brief Shared kernel error codes and helper predicates.
 *
 * @details
 * Many kernel APIs (especially syscall-style entry points) return integer
 * status codes rather than throwing exceptions. ViperOS follows a common kernel
 * convention:
 * - `0` (and, for some APIs, any non-negative value) indicates success.
 * - Negative values indicate an error.
 *
 * The @ref error::Code enumeration defines a stable set of negative error
 * codes used across subsystems, and helper functions are provided to test
 * whether a result represents success or failure.
 */
namespace error
{

// Error codes returned by syscalls
// 0 = success, negative = error

/**
 * @brief Standard error codes.
 *
 * @details
 * Codes are grouped by subsystem/area to make it easier to reason about
 * failures at call sites (general, handles, tasks, channels, polling, I/O).
 *
 * Values are chosen to be stable across kernel and user components so that
 * user-space code can interpret syscall results consistently.
 */
enum Code : i64
{
    VOK = 0, // Success

    // General errors (-1 to -99)
    VERR_UNKNOWN = -1,        // Unknown error
    VERR_INVALID_ARG = -2,    // Invalid argument
    VERR_OUT_OF_MEMORY = -3,  // Out of memory
    VERR_NOT_FOUND = -4,      // Resource not found
    VERR_ALREADY_EXISTS = -5, // Resource already exists
    VERR_PERMISSION = -6,     // Permission denied
    VERR_NOT_SUPPORTED = -7,  // Operation not supported
    VERR_BUSY = -8,           // Resource busy
    VERR_TIMEOUT = -9,        // Operation timed out

    // Handle errors (-100 to -199)
    VERR_INVALID_HANDLE = -100, // Invalid handle
    VERR_HANDLE_CLOSED = -101,  // Handle was closed
    VERR_WRONG_TYPE = -102,     // Wrong handle type

    // Task errors (-200 to -299)
    VERR_TASK_EXITED = -200,    // Task has exited
    VERR_TASK_NOT_FOUND = -201, // Task not found

    // Channel errors (-300 to -399)
    VERR_WOULD_BLOCK = -300,    // Operation would block
    VERR_CHANNEL_CLOSED = -301, // Channel closed
    VERR_MSG_TOO_LARGE = -302,  // Message too large

    // Poll errors (-400 to -499)
    VERR_POLL_FULL = -400, // Poll set is full

    // I/O errors (-500 to -599)
    VERR_IO = -500,              // I/O error
    VERR_NO_RESOURCE = -501,     // No resource available (e.g., no free slots)
    VERR_CONNECTION = -502,      // Connection error
    VERR_BUFFER_TOO_SMALL = -503, // Buffer too small
    VERR_NOT_DIR = -504,         // Not a directory
};

/**
 * @brief Check whether a result code indicates success.
 *
 * @details
 * Many APIs return either `0` on success or a non-negative value (e.g. a byte
 * count or an object ID). This helper treats any non-negative value as success.
 *
 * @param code Result code.
 * @return `true` if `code >= 0`, otherwise `false`.
 */
inline bool is_ok(i64 code)
{
    return code >= 0;
}

/**
 * @brief Check whether a result code indicates an error.
 *
 * @param code Result code.
 * @return `true` if `code < 0`, otherwise `false`.
 */
inline bool is_err(i64 code)
{
    return code < 0;
}

} // namespace error
