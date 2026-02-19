//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_log.h
// Purpose: Simple logging functions for Viper.Log namespace.
//
// Log Levels:
//   DEBUG (0) - Detailed diagnostic information
//   INFO  (1) - General informational messages (default)
//   WARN  (2) - Warning conditions
//   ERROR (3) - Error conditions
//   OFF   (4) - Disable all logging
//
// Messages are written to stderr with format: [LEVEL] HH:MM:SS message
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Log level constants
#define RT_LOG_DEBUG 0
#define RT_LOG_INFO 1
#define RT_LOG_WARN 2
#define RT_LOG_ERROR 3
#define RT_LOG_OFF 4

    /// @brief Log a debug message.
    /// @param message The message to log.
    void rt_log_debug(rt_string message);

    /// @brief Log an info message.
    /// @param message The message to log.
    void rt_log_info(rt_string message);

    /// @brief Log a warning message.
    /// @param message The message to log.
    void rt_log_warn(rt_string message);

    /// @brief Log an error message.
    /// @param message The message to log.
    void rt_log_error(rt_string message);

    /// @brief Get the current log level.
    /// @return Current log level (DEBUG=0, INFO=1, WARN=2, ERROR=3, OFF=4).
    int64_t rt_log_level(void);

    /// @brief Set the log level.
    /// @param level New log level (DEBUG=0, INFO=1, WARN=2, ERROR=3, OFF=4).
    void rt_log_set_level(int64_t level);

    /// @brief Check if a log level is enabled.
    /// @param level Level to check.
    /// @return 1 if messages at this level would be logged, 0 otherwise.
    int8_t rt_log_enabled(int64_t level);

    /// @brief Get the DEBUG level constant.
    /// @return 0.
    int64_t rt_log_level_debug(void);

    /// @brief Get the INFO level constant.
    /// @return 1.
    int64_t rt_log_level_info(void);

    /// @brief Get the WARN level constant.
    /// @return 2.
    int64_t rt_log_level_warn(void);

    /// @brief Get the ERROR level constant.
    /// @return 3.
    int64_t rt_log_level_error(void);

    /// @brief Get the OFF level constant.
    /// @return 4.
    int64_t rt_log_level_off(void);

#ifdef __cplusplus
}
#endif
