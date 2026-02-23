//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine.h
// Purpose: System information queries for Viper.Machine providing OS name, version, hostname, username, home/temp directories, CPU count, and memory statistics.
//
// Key invariants:
//   - OS values: 'linux', 'macos', 'windows', 'unknown'.
//   - All string-returning functions allocate new strings with refcount 1.
//   - Integer values (Cores, MemTotal, MemFree) are returned directly as int64_t.
//   - Queries are stateless; values may change between calls (e.g., MemFree).
//
// Ownership/Lifetime:
//   - Returned strings are caller-owned (refcnt == 1); callers must release them.
//   - No persistent state; all queries read from the OS on each call.
//
// Links: src/runtime/system/rt_machine.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Get the operating system name.
    /// @details Returns a runtime string identifying the host OS. The caller
    ///          is responsible for releasing the returned string.
    /// @return "linux", "macos", "windows", or "unknown" as an rt_string.
    rt_string rt_machine_os(void);

    /// @brief Get the operating system version string.
    /// @details Queries the host OS for its version information (e.g., "14.2.1"
    ///          on macOS, "10.0.22621" on Windows). The caller must release the result.
    /// @return OS version string as an rt_string.
    rt_string rt_machine_os_ver(void);

    /// @brief Get the hostname of the machine.
    /// @details Queries the system hostname. The caller must release the result.
    /// @return Machine hostname as an rt_string.
    rt_string rt_machine_host(void);

    /// @brief Get the current username.
    /// @details Queries the operating system for the user running the process.
    ///          The caller must release the result.
    /// @return Username of the current user as an rt_string.
    rt_string rt_machine_user(void);

    /// @brief Get the home directory path.
    /// @details Returns the path to the current user's home directory.
    ///          The caller must release the result.
    /// @return Path to the home directory as an rt_string.
    rt_string rt_machine_home(void);

    /// @brief Get the temporary directory path.
    /// @details Returns the path to the system's temporary directory (e.g.,
    ///          "/tmp" on Unix, the TEMP environment variable on Windows).
    ///          The caller must release the result.
    /// @return Path to the temporary directory as an rt_string.
    rt_string rt_machine_temp(void);

    /// @brief Get the number of logical CPU cores.
    /// @details Queries the system for the number of available logical processors.
    /// @return Number of logical CPU cores, or 0 if the query fails.
    int64_t rt_machine_cores(void);

    /// @brief Get the total system memory in bytes.
    /// @details Queries the operating system for the total installed RAM.
    /// @return Total RAM in bytes, or 0 if the information is unavailable.
    int64_t rt_machine_mem_total(void);

    /// @brief Get the free system memory in bytes.
    /// @details Queries the operating system for the currently available RAM.
    /// @return Free RAM in bytes, or 0 if the information is unavailable.
    int64_t rt_machine_mem_free(void);

    /// @brief Get the system byte order.
    /// @details Detects the endianness of the host machine at runtime.
    ///          The caller must release the result.
    /// @return "little" or "big" as an rt_string.
    rt_string rt_machine_endian(void);

#ifdef __cplusplus
}
#endif
