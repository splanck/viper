//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_log.c
// Purpose: Implements the Viper.Log namespace — a lightweight structured
//          logging system with five severity levels (DEBUG, INFO, WARN, ERROR,
//          OFF). Log messages are written to stderr with a
//          "[LEVEL] YYYY-MM-DD HH:MM:SS" timestamp prefix and are filtered by
//          the active global log level.
//
// Key invariants:
//   - The global log level is an atomic integer; reads and writes are
//     thread-safe without a mutex.
//   - Messages below the current level are discarded before any string
//     processing; the level check is O(1).
//   - Message bytes are written length-aware; embedded NUL and control bytes
//     are escaped instead of truncating or injecting extra lines.
//   - Each accepted log call is emitted as one complete physical line.
//   - Message ordering across concurrent threads is not guaranteed.
//   - Output always goes to stderr; stdout is not touched.
//   - Default log level is INFO (1); DEBUG messages are suppressed by default.
//
// Ownership/Lifetime:
//   - The log level is a process-global integer; no heap allocation is
//     associated with it.
//   - Message strings are borrowed from the caller; they are not retained
//     after the logging call returns.
//
// Links: src/runtime/core/rt_log.h (public API),
//        src/runtime/core/rt_trap.c (fatal error reporting),
//        src/runtime/core/rt_debug.c (low-level debug printing)
//
//===----------------------------------------------------------------------===//

#include "rt_log.h"
#include "rt_internal.h"
#include "rt_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sched.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Global log level controlling which messages are output.
///
/// Messages with a level less than this value are silently discarded.
/// Default is INFO (1), meaning DEBUG messages are suppressed.
/// Can be changed at runtime via rt_log_set_level().
// Accessed across threads — use atomic operations for reads/writes
static volatile int64_t g_log_level = RT_LOG_INFO;
static volatile unsigned char g_log_write_lock = 0;

static void rt_log_yield(void) {
#if defined(_WIN32)
    Sleep(0);
#else
    (void)sched_yield();
#endif
}

static void rt_log_lock(void) {
    unsigned spins = 0;
    while (__atomic_test_and_set(&g_log_write_lock, __ATOMIC_ACQUIRE)) {
        if (++spins >= 64) {
            rt_log_yield();
            spins = 0;
        }
    }
}

static void rt_log_unlock(void) {
    __atomic_clear(&g_log_write_lock, __ATOMIC_RELEASE);
}

/// @brief Formats the current time as a date-time string for log timestamps.
///
/// Uses thread-safe rt_localtime_r() to get the current time in the local
/// timezone and formats it as a human-readable timestamp for log output.
///
/// @param buf Output buffer for the formatted time string.
/// @param size Size of the output buffer in bytes (should be at least 20).
///
/// @note If localtime fails (extremely rare), buf[0] is set to '\0'.
/// @note Uses local date and 24-hour time.
static void get_time_str(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = rt_localtime_r(&now, &tm_buf);
    if (tm_info) {
        strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        buf[0] = '\0';
    }
}

static size_t log_escaped_message_len(rt_string message) {
    if (!message || !rt_string_is_handle((const void *)message) || !message->data)
        return 0;

    const unsigned char *data = (const unsigned char *)message->data;
    size_t len = (size_t)rt_str_len(message);
    size_t escaped_len = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = data[i];
        size_t add = 1;
        if (ch == '\0' || ch == '\n' || ch == '\r' || ch == '\t')
            add = 2;
        else if (ch < 0x20 || ch == 0x7f)
            add = 4;
        if (escaped_len > SIZE_MAX - add)
            return SIZE_MAX;
        escaped_len += add;
    }
    return escaped_len;
}

static void log_append_escaped_message(char *dst, size_t *pos, rt_string message) {
    static const char hex[] = "0123456789ABCDEF";
    if (!dst || !pos || !message || !rt_string_is_handle((const void *)message) || !message->data)
        return;

    const unsigned char *data = (const unsigned char *)message->data;
    size_t len = (size_t)rt_str_len(message);
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = data[i];
        switch (ch) {
        case '\0':
            dst[(*pos)++] = '\\';
            dst[(*pos)++] = '0';
            break;
        case '\n':
            dst[(*pos)++] = '\\';
            dst[(*pos)++] = 'n';
            break;
        case '\r':
            dst[(*pos)++] = '\\';
            dst[(*pos)++] = 'r';
            break;
        case '\t':
            dst[(*pos)++] = '\\';
            dst[(*pos)++] = 't';
            break;
        default:
            if (ch < 0x20 || ch == 0x7f) {
                dst[(*pos)++] = '\\';
                dst[(*pos)++] = 'x';
                dst[(*pos)++] = hex[ch >> 4];
                dst[(*pos)++] = hex[ch & 0x0f];
            } else {
                dst[(*pos)++] = (char)ch;
            }
            break;
        }
    }
}

static void log_write_escaped_message(FILE *stream, rt_string message) {
    static const char hex[] = "0123456789ABCDEF";
    if (!stream || !message || !rt_string_is_handle((const void *)message) || !message->data)
        return;

    const unsigned char *data = (const unsigned char *)message->data;
    size_t len = (size_t)rt_str_len(message);
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = data[i];
        char escaped[4];
        size_t escaped_len = 0;
        switch (ch) {
        case '\0':
            escaped[0] = '\\';
            escaped[1] = '0';
            escaped_len = 2;
            break;
        case '\n':
            escaped[0] = '\\';
            escaped[1] = 'n';
            escaped_len = 2;
            break;
        case '\r':
            escaped[0] = '\\';
            escaped[1] = 'r';
            escaped_len = 2;
            break;
        case '\t':
            escaped[0] = '\\';
            escaped[1] = 't';
            escaped_len = 2;
            break;
        default:
            if (ch < 0x20 || ch == 0x7f) {
                escaped[0] = '\\';
                escaped[1] = 'x';
                escaped[2] = hex[ch >> 4];
                escaped[3] = hex[ch & 0x0f];
                escaped_len = 4;
            } else {
                escaped[0] = (char)ch;
                escaped_len = 1;
            }
            break;
        }
        (void)fwrite(escaped, 1, escaped_len, stream);
    }
}

/// @brief Internal logging function that formats and outputs a log message.
///
/// This is the core logging implementation. It checks the log level, formats
/// the message with timestamp and level prefix, writes to stderr, and flushes.
///
/// **Output format:** `[LEVEL] YYYY-MM-DD HH:MM:SS message`
///
/// @param level The severity level of this message (RT_LOG_DEBUG, etc.).
/// @param level_str Human-readable level name ("DEBUG", "INFO", etc.).
/// @param message The Viper string containing the log message text.
///                NULL or empty strings are handled gracefully.
///
/// @note Messages below g_log_level are silently discarded.
/// @note Output is flushed immediately after each message.
static void log_message(int64_t level, const char *level_str, rt_string message) {
    int64_t current_level = __atomic_load_n(&g_log_level, __ATOMIC_ACQUIRE);
    if (level < current_level)
        return;

    char time_buf[32];
    get_time_str(time_buf, sizeof(time_buf));

    char prefix[64];
    int prefix_len = snprintf(prefix, sizeof(prefix), "[%s] %s ", level_str, time_buf);
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(prefix))
        return;

    size_t escaped_len = log_escaped_message_len(message);
    if (escaped_len == SIZE_MAX)
        return;

    size_t total = (size_t)prefix_len;
    if (escaped_len > SIZE_MAX - total - 1)
        return;
    total += escaped_len + 1;

    char *line = (char *)malloc(total);
    if (!line) {
        rt_log_lock();
        (void)fwrite(prefix, 1, (size_t)prefix_len, stderr);
        log_write_escaped_message(stderr, message);
        (void)fwrite("\n", 1, 1, stderr);
        (void)fflush(stderr);
        rt_log_unlock();
        return;
    }

    size_t pos = 0;
    memcpy(line + pos, prefix, (size_t)prefix_len);
    pos += (size_t)prefix_len;
    log_append_escaped_message(line, &pos, message);
    line[pos++] = '\n';

    rt_log_lock();
    (void)fwrite(line, 1, pos, stderr);
    (void)fflush(stderr);
    rt_log_unlock();

    free(line);
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
/// @note Output format: `[DEBUG] YYYY-MM-DD HH:MM:SS message`
///
/// @see rt_log_set_level For enabling debug output
void rt_log_debug(rt_string message) {
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
/// @note Output format: `[INFO] YYYY-MM-DD HH:MM:SS message`
void rt_log_info(rt_string message) {
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
/// @note Output format: `[WARN] YYYY-MM-DD HH:MM:SS message`
void rt_log_warn(rt_string message) {
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
/// @note Output format: `[ERROR] YYYY-MM-DD HH:MM:SS message`
/// @note For fatal errors that terminate the program, use rt_trap instead.
///
/// @see rt_trap For fatal errors
void rt_log_error(rt_string message) {
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
int64_t rt_log_level(void) {
    return __atomic_load_n(&g_log_level, __ATOMIC_ACQUIRE);
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
void rt_log_set_level(int64_t level) {
    // Clamp to valid range
    if (level < RT_LOG_DEBUG)
        level = RT_LOG_DEBUG;
    if (level > RT_LOG_OFF)
        level = RT_LOG_OFF;
    __atomic_store_n(&g_log_level, level, __ATOMIC_RELEASE);
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
int8_t rt_log_enabled(int64_t level) {
    int64_t current_level = __atomic_load_n(&g_log_level, __ATOMIC_ACQUIRE);
    return level >= current_level && level < RT_LOG_OFF;
}

/// @brief Returns the DEBUG level constant (0).
///
/// Use this for level comparisons and SetLevel calls.
///
/// @return 0 (RT_LOG_DEBUG)
int64_t rt_log_level_debug(void) {
    return RT_LOG_DEBUG;
}

/// @brief Returns the INFO level constant (1).
///
/// Use this for level comparisons and SetLevel calls.
///
/// @return 1 (RT_LOG_INFO)
int64_t rt_log_level_info(void) {
    return RT_LOG_INFO;
}

/// @brief Returns the WARN level constant (2).
///
/// Use this for level comparisons and SetLevel calls.
///
/// @return 2 (RT_LOG_WARN)
int64_t rt_log_level_warn(void) {
    return RT_LOG_WARN;
}

/// @brief Returns the ERROR level constant (3).
///
/// Use this for level comparisons and SetLevel calls.
///
/// @return 3 (RT_LOG_ERROR)
int64_t rt_log_level_error(void) {
    return RT_LOG_ERROR;
}

/// @brief Returns the OFF level constant (4).
///
/// Setting the log level to OFF suppresses all log output.
///
/// @return 4 (RT_LOG_OFF)
int64_t rt_log_level_off(void) {
    return RT_LOG_OFF;
}

#ifdef __cplusplus
}
#endif
