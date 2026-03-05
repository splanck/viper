//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/repl/ReplColorScheme.hpp
// Purpose: ANSI color constants for the Viper REPL. Colors are applied via
//          escape codes when stdout is a terminal; all codes resolve to empty
//          strings when output is piped or redirected.
// Key invariants:
//   - isColorEnabled() checks isatty(STDOUT_FILENO) once at startup.
//   - All color() calls return "" when color is disabled.
// Ownership/Lifetime:
//   - Header-only; no heap allocations.
// Links: src/repl/ReplSession.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#include <stdio.h>
#endif

namespace viper::repl
{

/// @brief ANSI color scheme for the REPL.
/// @details All methods return empty strings when stdout is not a terminal.
namespace colors
{

/// @brief Check whether ANSI color output is supported and enabled.
/// @return True if stdout is connected to a terminal.
inline bool isColorEnabled()
{
    static const bool enabled = []
    {
#if defined(__unix__) || defined(__APPLE__)
        return isatty(STDOUT_FILENO) != 0;
#elif defined(_WIN32)
        return _isatty(_fileno(stdout)) != 0;
#else
        return false;
#endif
    }();
    return enabled;
}

// --- Reset ---
inline const char *reset()
{
    return isColorEnabled() ? "\033[0m" : "";
}

inline const char *bold()
{
    return isColorEnabled() ? "\033[1m" : "";
}

inline const char *dim()
{
    return isColorEnabled() ? "\033[2m" : "";
}

// --- Prompt ---
inline const char *prompt()
{
    return isColorEnabled() ? "\033[1;36m" : "";
}

inline const char *contPrompt()
{
    return isColorEnabled() ? "\033[36m" : "";
}

// --- Output types ---
inline const char *result()
{
    return isColorEnabled() ? "\033[1;32m" : "";
}

inline const char *string()
{
    return isColorEnabled() ? "\033[33m" : "";
}

inline const char *number()
{
    return isColorEnabled() ? "\033[34m" : "";
}

inline const char *boolean()
{
    return isColorEnabled() ? "\033[35m" : "";
}

inline const char *null()
{
    return isColorEnabled() ? "\033[2;37m" : "";
}

inline const char *type()
{
    return isColorEnabled() ? "\033[36m" : "";
}

// --- Diagnostics ---
inline const char *error()
{
    return isColorEnabled() ? "\033[1;31m" : "";
}

inline const char *warning()
{
    return isColorEnabled() ? "\033[1;33m" : "";
}

inline const char *note()
{
    return isColorEnabled() ? "\033[1;34m" : "";
}

inline const char *success()
{
    return isColorEnabled() ? "\033[1;32m" : "";
}

} // namespace colors

} // namespace viper::repl
