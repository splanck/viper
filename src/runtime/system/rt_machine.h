//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_machine.h
// Purpose: System information queries for Viper.Machine.
// Key invariants: All functions return freshly allocated runtime strings or
//                 integer values. String results must be released by the caller.
// Ownership: Returned strings are caller-owned (refcnt == 1).
// Lifetime: Stateless queries; no persistent state.
// Links: rt_string.h
//
// Machine provides read-only properties for system information:
// - OS: Operating system name ("linux", "macos", "windows", "unknown")
// - OSVer: Operating system version string
// - Host: Hostname
// - User: Current username
// - Home: Home directory path
// - Temp: Temporary directory path
// - Cores: Number of CPU cores
// - MemTotal: Total RAM in bytes
// - MemFree: Free RAM in bytes
// - Endian: Byte order ("little" or "big")
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
