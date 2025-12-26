//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_machine.h
// Purpose: System information queries for Viper.Machine.
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
    /// @return "linux", "macos", "windows", or "unknown".
    rt_string rt_machine_os(void);

    /// @brief Get the operating system version.
    /// @return OS version string (e.g., "14.2.1" on macOS).
    rt_string rt_machine_os_ver(void);

    /// @brief Get the hostname.
    /// @return Machine hostname.
    rt_string rt_machine_host(void);

    /// @brief Get the current username.
    /// @return Username of the current user.
    rt_string rt_machine_user(void);

    /// @brief Get the home directory path.
    /// @return Path to the user's home directory.
    rt_string rt_machine_home(void);

    /// @brief Get the temporary directory path.
    /// @return Path to the system temporary directory.
    rt_string rt_machine_temp(void);

    /// @brief Get the number of CPU cores.
    /// @return Number of logical CPU cores.
    int64_t rt_machine_cores(void);

    /// @brief Get total system memory.
    /// @return Total RAM in bytes, or 0 if unavailable.
    int64_t rt_machine_mem_total(void);

    /// @brief Get free system memory.
    /// @return Free RAM in bytes, or 0 if unavailable.
    int64_t rt_machine_mem_free(void);

    /// @brief Get the system byte order.
    /// @return "little" or "big".
    rt_string rt_machine_endian(void);

#ifdef __cplusplus
}
#endif
