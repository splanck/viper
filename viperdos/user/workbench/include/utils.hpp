//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/workbench/include/utils.hpp
// Purpose: Common utility functions for the Workbench application.
// Key invariants: Inline functions to avoid ODR violations.
// Ownership/Lifetime: Header-only; included by workbench components.
// Links: user/workbench/src/desktop.cpp, user/workbench/src/filebrowser.cpp
//
//===----------------------------------------------------------------------===//

/**
 * @file utils.hpp
 * @brief Common utility functions for the Workbench application.
 *
 * @details
 * Provides shared helper functions used across multiple Workbench components.
 * These functions are defined inline to avoid ODR violations when included
 * in multiple translation units.
 */
#pragma once

#include <stdint.h>

namespace workbench {

/**
 * @brief Get system uptime in milliseconds.
 *
 * @details
 * Calls the SYS_TIME_UPTIME syscall to retrieve the system uptime.
 * Used for timing operations like double-click detection.
 *
 * @return System uptime in milliseconds.
 */
inline uint64_t get_uptime_ms() {
    uint64_t result;
    __asm__ volatile("mov x8, #0xA2\n\t" // SYS_TIME_UPTIME
                     "svc #0\n\t"
                     "mov %[result], x1" // Result is in x1 after syscall
                     : [result] "=r"(result)
                     :
                     : "x0", "x1", "x8", "memory");
    return result;
}

/**
 * @brief Print debug message directly to serial console.
 *
 * @details
 * Bypasses the consoled daemon and writes directly to the kernel's
 * serial output. Useful for debugging when the GUI may be frozen.
 *
 * @param msg NUL-terminated message string.
 */
inline void debug_serial(const char *msg) {
    __asm__ volatile("mov x0, %[msg]\n\t"
                     "mov x8, #0xF0\n\t" // SYS_DEBUG_PRINT
                     "svc #0" ::[msg] "r"(msg)
                     : "x0", "x8", "memory");
}

} // namespace workbench
