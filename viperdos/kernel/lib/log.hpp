//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

/**
 * @file log.hpp
 * @brief Kernel logging interface.
 *
 * @details
 * Provides a simple logging abstraction for kernel subsystems. All logs are
 * currently directed to the serial console, but this interface allows for
 * future expansion (e.g., ring buffers, per-subsystem filtering, log levels).
 *
 * Usage:
 * @code
 * LOG_INFO("net", "Network initialized on IP %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
 * LOG_WARN("vfs", "File not found: %s", path);
 * LOG_ERROR("mm", "Out of memory allocating %zu bytes", size);
 * @endcode
 */

#include "../console/serial.hpp"

namespace log {

/**
 * @brief Log level enumeration.
 */
enum class Level {
    Debug = 0, ///< Detailed debugging information
    Info = 1,  ///< General informational messages
    Warn = 2,  ///< Warning conditions
    Error = 3, ///< Error conditions
    Fatal = 4, ///< Fatal errors (system will halt)
};

// Current minimum log level (can be changed at runtime)
inline Level g_min_level = Level::Info;

/**
 * @brief Set the minimum log level.
 *
 * @param level Messages below this level will be suppressed.
 */
inline void set_level(Level level) {
    g_min_level = level;
}

/**
 * @brief Get the current minimum log level.
 */
inline Level get_level() {
    return g_min_level;
}

/**
 * @brief Log a message at the specified level.
 *
 * @param level Log level.
 * @param subsystem Short subsystem name (e.g., "net", "vfs", "mm").
 * @param message Log message.
 */
inline void log(Level level, const char *subsystem, const char *message) {
    if (static_cast<int>(level) < static_cast<int>(g_min_level)) {
        return;
    }

    // Print level prefix
    switch (level) {
        case Level::Debug:
            serial::puts("[D]");
            break;
        case Level::Info:
            serial::puts("[I]");
            break;
        case Level::Warn:
            serial::puts("[W]");
            break;
        case Level::Error:
            serial::puts("[E]");
            break;
        case Level::Fatal:
            serial::puts("[F]");
            break;
    }

    // Print subsystem
    serial::puts("[");
    serial::puts(subsystem);
    serial::puts("] ");

    // Print message
    serial::puts(message);
    serial::puts("\n");
}

/**
 * @brief Log a debug message.
 */
inline void debug(const char *subsystem, const char *message) {
    log(Level::Debug, subsystem, message);
}

/**
 * @brief Log an info message.
 */
inline void info(const char *subsystem, const char *message) {
    log(Level::Info, subsystem, message);
}

/**
 * @brief Log a warning message.
 */
inline void warn(const char *subsystem, const char *message) {
    log(Level::Warn, subsystem, message);
}

/**
 * @brief Log an error message.
 */
inline void error(const char *subsystem, const char *message) {
    log(Level::Error, subsystem, message);
}

/**
 * @brief Log a fatal error and halt.
 */
inline void fatal(const char *subsystem, const char *message) {
    log(Level::Fatal, subsystem, message);
    serial::puts("FATAL ERROR - System halted\n");
    for (;;) {
        asm volatile("wfi");
    }
}

} // namespace log

// Convenience macros for common log levels
#define LOG_DEBUG(subsystem, msg) log::debug(subsystem, msg)
#define LOG_INFO(subsystem, msg) log::info(subsystem, msg)
#define LOG_WARN(subsystem, msg) log::warn(subsystem, msg)
#define LOG_ERROR(subsystem, msg) log::error(subsystem, msg)
#define LOG_FATAL(subsystem, msg) log::fatal(subsystem, msg)
