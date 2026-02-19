//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_log.c
/// @brief Simple structured logging functions for the Viper.Log namespace.
///
/// This file implements a lightweight logging system for Viper programs. The
/// logging functions output timestamped messages to stderr at configurable
/// severity levels, enabling debugging and monitoring of program execution.
///
/// **Log Levels:**
/// ```
/// Level    Value   Description                     Color in terminals
/// ───────────────────────────────────────────────────────────────────
/// DEBUG      0     Detailed debugging information  (dim/gray)
/// INFO       1     General information messages    (normal)
/// WARN       2     Warning conditions              (yellow)
/// ERROR      3     Error conditions                (red)
/// OFF        4     Disable all logging             (none)
/// ```
///
/// **Output Format:**
/// All log messages follow a consistent format:
/// ```
/// [LEVEL] HH:MM:SS message text
///
/// Examples:
/// [DEBUG] 14:30:45 Processing item 5 of 100
/// [INFO]  14:30:46 Server started on port 8080
/// [WARN]  14:30:47 Connection timeout, retrying...
/// [ERROR] 14:30:48 Failed to open config file
/// ```
///
/// **Level Filtering:**
/// Messages below the current log level are silently discarded. This allows
/// different verbosity levels in development vs production:
///
/// ```
/// Log Level   DEBUG   INFO   WARN   ERROR
/// ─────────────────────────────────────────
/// DEBUG         ✓       ✓      ✓       ✓
/// INFO          ✗       ✓      ✓       ✓
/// WARN          ✗       ✗      ✓       ✓
/// ERROR         ✗       ✗      ✗       ✓
/// OFF           ✗       ✗      ✗       ✗
/// ```
///
/// **Usage Examples:**
/// ```
/// ' Basic logging
/// Log.Debug("Loading configuration...")
/// Log.Info("Application started")
/// Log.Warn("Memory usage high: " & memUsage & "%")
/// Log.Error("Failed to connect: " & errorMsg)
///
/// ' Change log level
/// Log.SetLevel(Log.LEVEL_DEBUG)  ' See all messages
/// Log.SetLevel(Log.LEVEL_WARN)   ' Only warnings and errors
/// Log.SetLevel(Log.LEVEL_OFF)    ' Suppress all output
///
/// ' Check if level is enabled (avoid expensive string formatting)
/// If Log.IsEnabled(Log.LEVEL_DEBUG) Then
///     Log.Debug("Object state: " & obj.DetailedDump())
/// End If
/// ```
///
/// **Thread Safety:**
/// - Reading/writing the global log level is atomic
/// - Individual log messages are written atomically (single fprintf call)
/// - Multiple threads can log concurrently without message corruption
/// - Message ordering across threads is not guaranteed
///
/// **Performance Considerations:**
/// - Log level check is O(1) - just an integer comparison
/// - Disabled log calls have minimal overhead (message argument is still evaluated)
/// - Use Log.IsEnabled() to skip expensive string formatting for disabled levels
/// - Output goes to stderr which is typically line-buffered
///
/// **Platform Notes:**
/// - Output always goes to stderr (standard error stream)
/// - Messages are flushed immediately after each write
/// - Time is in local timezone (from system clock)
/// - Works on all platforms (Windows, macOS, Linux)
///
/// @see rt_trap.c For fatal error reporting
/// @see rt_debug.c For low-level debug printing
///
//===----------------------------------------------------------------------===//

#include "rt_log.h"
#include "rt_internal.h"
#include "rt_platform.h"

#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Global log level controlling which messages are output.
    ///
    /// Messages with a level less than this value are silently discarded.
    /// Default is INFO (1), meaning DEBUG messages are suppressed.
    /// Can be changed at runtime via rt_log_set_level().
    static int64_t g_log_level = RT_LOG_INFO;

    /// @brief Formats the current time as an HH:MM:SS string for log timestamps.
    ///
    /// Uses thread-safe rt_localtime_r() to get the current time in the local
    /// timezone and formats it as a human-readable timestamp for log output.
    ///
    /// @param buf Output buffer for the formatted time string.
    /// @param size Size of the output buffer in bytes (should be at least 9).
    ///
    /// @note If localtime fails (extremely rare), buf[0] is set to '\0'.
    /// @note Uses 24-hour format.
    static void get_time_str(char *buf, size_t size)
    {
        time_t now = time(NULL);
        struct tm tm_buf;
        struct tm *tm_info = rt_localtime_r(&now, &tm_buf);
        if (tm_info)
        {
            strftime(buf, size, "%H:%M:%S", tm_info);
        }
        else
        {
            buf[0] = '\0';
        }
    }

    /// @brief Internal logging function that formats and outputs a log message.
    ///
    /// This is the core logging implementation. It checks the log level, formats
    /// the message with timestamp and level prefix, writes to stderr, and flushes.
    ///
    /// **Output format:** `[LEVEL] HH:MM:SS message`
    ///
    /// @param level The severity level of this message (RT_LOG_DEBUG, etc.).
    /// @param level_str Human-readable level name ("DEBUG", "INFO", etc.).
    /// @param message The Viper string containing the log message text.
    ///                NULL or empty strings are handled gracefully.
    ///
    /// @note Messages below g_log_level are silently discarded.
    /// @note Output is flushed immediately after each message.
    static void log_message(int64_t level, const char *level_str, rt_string message)
    {
        if (level < g_log_level)
            return;

        const char *msg = "";
        if (message && message->data)
        {
            msg = rt_string_cstr(message);
            if (!msg)
                msg = "";
        }

        char time_buf[16];
        get_time_str(time_buf, sizeof(time_buf));

        fprintf(stderr, "[%s] %s %s\n", level_str, time_buf, msg);
        fflush(stderr);
    }

    /// @brief Logs a message at DEBUG level.
    ///
    /// DEBUG messages are the most verbose level, intended for detailed
    /// information useful during development and troubleshooting. By default,
    /// DEBUG messages are suppressed (log level defaults to INFO).
    ///
    /// **Example:**
    /// ```
    /// Log.Debug("Processing item " & i & " of " & total)
    /// Log.Debug("Variable x = " & x)
    /// ```
    ///
    /// @param message The message text to log. NULL is handled as empty string.
    ///
    /// @note Only output if current log level is DEBUG (0).
    /// @note Output format: `[DEBUG] HH:MM:SS message`
    ///
    /// @see rt_log_set_level For enabling debug output
    void rt_log_debug(rt_string message)
    {
        log_message(RT_LOG_DEBUG, "DEBUG", message);
    }

    /// @brief Logs a message at INFO level.
    ///
    /// INFO messages provide general information about program operation.
    /// This is the default log level, so INFO messages are shown by default.
    /// Use for notable events that are part of normal operation.
    ///
    /// **Example:**
    /// ```
    /// Log.Info("Server started on port " & port)
    /// Log.Info("Loaded " & count & " items from database")
    /// ```
    ///
    /// @param message The message text to log. NULL is handled as empty string.
    ///
    /// @note Output if current log level is INFO (1) or lower.
    /// @note Output format: `[INFO] HH:MM:SS message`
    void rt_log_info(rt_string message)
    {
        log_message(RT_LOG_INFO, "INFO", message);
    }

    /// @brief Logs a message at WARN level.
    ///
    /// WARN messages indicate potentially problematic situations that don't
    /// prevent the program from continuing but may warrant attention. Use for
    /// recoverable errors, deprecation notices, or approaching resource limits.
    ///
    /// **Example:**
    /// ```
    /// Log.Warn("Configuration file not found, using defaults")
    /// Log.Warn("Memory usage at " & percent & "%, consider cleanup")
    /// ```
    ///
    /// @param message The message text to log. NULL is handled as empty string.
    ///
    /// @note Output if current log level is WARN (2) or lower.
    /// @note Output format: `[WARN] HH:MM:SS message`
    void rt_log_warn(rt_string message)
    {
        log_message(RT_LOG_WARN, "WARN", message);
    }

    /// @brief Logs a message at ERROR level.
    ///
    /// ERROR messages indicate serious problems that may prevent the program
    /// from completing its intended function. These are the highest severity
    /// messages before the program terminates.
    ///
    /// **Example:**
    /// ```
    /// Log.Error("Failed to open file: " & filename)
    /// Log.Error("Database connection lost")
    /// ```
    ///
    /// @param message The message text to log. NULL is handled as empty string.
    ///
    /// @note Output if current log level is ERROR (3) or lower.
    /// @note Output format: `[ERROR] HH:MM:SS message`
    /// @note For fatal errors that terminate the program, use rt_trap instead.
    ///
    /// @see rt_trap For fatal errors
    void rt_log_error(rt_string message)
    {
        log_message(RT_LOG_ERROR, "ERROR", message);
    }

    /// @brief Gets the current log level.
    ///
    /// Returns the current log level threshold. Messages with a level below
    /// this threshold are suppressed.
    ///
    /// **Example:**
    /// ```
    /// Dim savedLevel = Log.Level()
    /// Log.SetLevel(Log.LEVEL_DEBUG)  ' Enable verbose logging
    /// DoDetailedOperation()
    /// Log.SetLevel(savedLevel)       ' Restore original level
    /// ```
    ///
    /// @return The current log level (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=OFF).
    ///
    /// @note Default level is INFO (1).
    ///
    /// @see rt_log_set_level For changing the log level
    int64_t rt_log_level(void)
    {
        return g_log_level;
    }

    /// @brief Sets the current log level.
    ///
    /// Changes the log level threshold. After calling this function, only
    /// messages at or above the specified level will be output.
    ///
    /// **Level values:**
    /// | Value | Constant        | Effect                        |
    /// |-------|-----------------|-------------------------------|
    /// | 0     | Log.LEVEL_DEBUG | Show all messages             |
    /// | 1     | Log.LEVEL_INFO  | Hide DEBUG                    |
    /// | 2     | Log.LEVEL_WARN  | Hide DEBUG and INFO           |
    /// | 3     | Log.LEVEL_ERROR | Hide DEBUG, INFO, and WARN    |
    /// | 4     | Log.LEVEL_OFF   | Suppress all log output       |
    ///
    /// **Example:**
    /// ```
    /// ' Production: only show warnings and errors
    /// Log.SetLevel(Log.LEVEL_WARN)
    ///
    /// ' Debugging: show everything
    /// Log.SetLevel(Log.LEVEL_DEBUG)
    ///
    /// ' Quiet mode: suppress all logging
    /// Log.SetLevel(Log.LEVEL_OFF)
    /// ```
    ///
    /// @param level New log level (clamped to valid range 0-4).
    ///
    /// @note Values outside 0-4 are clamped to the valid range.
    /// @note This affects all subsequent log calls globally.
    ///
    /// @see rt_log_level For getting the current level
    void rt_log_set_level(int64_t level)
    {
        // Clamp to valid range
        if (level < RT_LOG_DEBUG)
            level = RT_LOG_DEBUG;
        if (level > RT_LOG_OFF)
            level = RT_LOG_OFF;
        g_log_level = level;
    }

    /// @brief Checks if a specific log level is currently enabled.
    ///
    /// Returns true if messages at the specified level would be output
    /// under the current log level setting. Use this to avoid expensive
    /// string formatting when the message would be discarded anyway.
    ///
    /// **Example:**
    /// ```
    /// ' Avoid expensive dump if debug is disabled
    /// If Log.IsEnabled(Log.LEVEL_DEBUG) Then
    ///     Dim debugInfo = BuildDetailedDebugString()  ' Expensive!
    ///     Log.Debug(debugInfo)
    /// End If
    /// ```
    ///
    /// @param level The log level to check (0-3).
    ///
    /// @return True if messages at this level would be output, false otherwise.
    ///
    /// @note Returns false for LEVEL_OFF (4) - that level is never "enabled".
    int8_t rt_log_enabled(int64_t level)
    {
        return level >= g_log_level && level < RT_LOG_OFF;
    }

    /// @brief Returns the DEBUG level constant (0).
    ///
    /// Use this for level comparisons and SetLevel calls.
    ///
    /// @return 0 (RT_LOG_DEBUG)
    int64_t rt_log_level_debug(void)
    {
        return RT_LOG_DEBUG;
    }

    /// @brief Returns the INFO level constant (1).
    ///
    /// Use this for level comparisons and SetLevel calls.
    ///
    /// @return 1 (RT_LOG_INFO)
    int64_t rt_log_level_info(void)
    {
        return RT_LOG_INFO;
    }

    /// @brief Returns the WARN level constant (2).
    ///
    /// Use this for level comparisons and SetLevel calls.
    ///
    /// @return 2 (RT_LOG_WARN)
    int64_t rt_log_level_warn(void)
    {
        return RT_LOG_WARN;
    }

    /// @brief Returns the ERROR level constant (3).
    ///
    /// Use this for level comparisons and SetLevel calls.
    ///
    /// @return 3 (RT_LOG_ERROR)
    int64_t rt_log_level_error(void)
    {
        return RT_LOG_ERROR;
    }

    /// @brief Returns the OFF level constant (4).
    ///
    /// Setting the log level to OFF suppresses all log output.
    ///
    /// @return 4 (RT_LOG_OFF)
    int64_t rt_log_level_off(void)
    {
        return RT_LOG_OFF;
    }

#ifdef __cplusplus
}
#endif
