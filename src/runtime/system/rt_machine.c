//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine.c
// Purpose: Implements system information queries for the Viper.Machine class.
//          Provides CPU count, hostname, OS name/version, architecture,
//          total/available memory, and process ID using platform-specific APIs.
//
// Key invariants:
//   - CPU count queries use GetSystemInfo (Win32), sysconf (POSIX), or fall
//     back to 1 if the platform provides no API.
//   - OS name strings are statically determined at compile time from predefined
//     macros (_WIN32, __APPLE__, __linux__) and never change at runtime.
//   - Hostname is queried fresh on each call; it is not cached.
//   - Memory queries use GlobalMemoryStatusEx (Win32) or sysinfo (Linux) or
//     host_statistics64 (macOS).
//   - All functions return safe defaults (0, empty string) on query failure.
//
// Ownership/Lifetime:
//   - All returned rt_string values are fresh allocations owned by the caller.
//   - No state is retained between calls; all queries are stateless.
//
// Links: src/runtime/system/rt_machine.h (public API)
//
//===----------------------------------------------------------------------===//

// Define feature test macros before any includes
#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#endif

#include "rt_machine.h"

#include "rt_string.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__viperdos__)
// ViperDOS provides POSIX-compatible system info APIs.
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#else
#include <pwd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#endif

#ifdef __linux__
#include <sys/sysinfo.h>
#endif

/// @brief Helper to create a string from a C string.
static rt_string make_str(const char *s)
{
    if (!s)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(s, strlen(s));
}

// ============================================================================
// Operating System
// ============================================================================

rt_string rt_machine_os(void)
{
#if defined(_WIN32)
    return make_str("windows");
#elif defined(__APPLE__)
    return make_str("macos");
#elif defined(__linux__)
    return make_str("linux");
#elif defined(__viperdos__)
    return make_str("viperdos");
#else
    return make_str("unknown");
#endif
}

rt_string rt_machine_os_ver(void)
{
#ifdef _WIN32
    // Windows version
    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    // GetVersionExA is deprecated but still works for basic version info
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExA(&osvi))
    {
        char buf[64];
        snprintf(buf,
                 sizeof(buf),
                 "%lu.%lu.%lu",
                 (unsigned long)osvi.dwMajorVersion,
                 (unsigned long)osvi.dwMinorVersion,
                 (unsigned long)osvi.dwBuildNumber);
        return make_str(buf);
    }
#pragma warning(pop)
    return make_str("unknown");

#elif defined(__APPLE__)
    // macOS version via sysctl
    char ver[64] = {0};
    size_t len = sizeof(ver);
    if (sysctlbyname("kern.osproductversion", ver, &len, NULL, 0) == 0)
    {
        return make_str(ver);
    }
    // Fallback to uname
    struct utsname uts;
    if (uname(&uts) == 0)
    {
        return make_str(uts.release);
    }
    return make_str("unknown");

#elif defined(__linux__)
    // Linux: read /etc/os-release or use uname
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp)
    {
        char line[256];
        while (fgets(line, sizeof(line), fp))
        {
            if (strncmp(line, "VERSION_ID=", 11) == 0)
            {
                fclose(fp);
                // Remove quotes and newline
                char *ver = line + 11;
                size_t vlen = strlen(ver);
                if (vlen > 0 && ver[vlen - 1] == '\n')
                    ver[--vlen] = '\0';
                if (vlen >= 2 && ver[0] == '"' && ver[vlen - 1] == '"')
                {
                    ver[vlen - 1] = '\0';
                    ver++;
                }
                return make_str(ver);
            }
        }
        fclose(fp);
    }
    // Fallback to uname
    struct utsname uts;
    if (uname(&uts) == 0)
    {
        return make_str(uts.release);
    }
    return make_str("unknown");

#else
    // Unix and ViperDOS: use uname.
    struct utsname uts;
    if (uname(&uts) == 0)
    {
        return make_str(uts.release);
    }
    return make_str("unknown");
#endif
}

// ============================================================================
// Host and User
// ============================================================================

rt_string rt_machine_host(void)
{
#ifdef _WIN32
    char buf[256];
    DWORD len = sizeof(buf);
    if (GetComputerNameA(buf, &len))
    {
        return make_str(buf);
    }
    return make_str("unknown");
#else
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0)
    {
        buf[sizeof(buf) - 1] = '\0';
        return make_str(buf);
    }
    return make_str("unknown");
#endif
}

rt_string rt_machine_user(void)
{
#ifdef _WIN32
    char buf[256];
    DWORD len = sizeof(buf);
    if (GetUserNameA(buf, &len))
    {
        return make_str(buf);
    }
    // Fallback to environment variable
    const char *user = getenv("USERNAME");
    if (user)
        return make_str(user);
    return make_str("unknown");
#else
    // Try getpwuid first
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name)
    {
        return make_str(pw->pw_name);
    }
    // Fallback to environment variables
    const char *user = getenv("USER");
    if (!user)
        user = getenv("LOGNAME");
    if (user)
        return make_str(user);
    return make_str("unknown");
#endif
}

// ============================================================================
// Directories
// ============================================================================

rt_string rt_machine_home(void)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (home)
        return make_str(home);
    // Try HOMEDRIVE + HOMEPATH
    const char *drive = getenv("HOMEDRIVE");
    const char *path = getenv("HOMEPATH");
    if (drive && path)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s%s", drive, path);
        return make_str(buf);
    }
    return make_str("");
#else
    const char *home = getenv("HOME");
    if (home)
        return make_str(home);
    // Fallback to passwd entry
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
    {
        return make_str(pw->pw_dir);
    }
    return make_str("");
#endif
}

rt_string rt_machine_temp(void)
{
#ifdef _WIN32
    char buf[512];
    DWORD len = GetTempPathA(sizeof(buf), buf);
    if (len > 0 && len < sizeof(buf))
    {
        // Remove trailing backslash if present
        if (len > 1 && buf[len - 1] == '\\')
            buf[len - 1] = '\0';
        return make_str(buf);
    }
    return make_str("C:\\Temp");
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp)
        tmp = getenv("TMP");
    if (!tmp)
        tmp = getenv("TEMP");
    if (!tmp)
        tmp = "/tmp";
    return make_str(tmp);
#endif
}

// ============================================================================
// Hardware Information
// ============================================================================

int64_t rt_machine_cores(void)
{
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int64_t)sysinfo.dwNumberOfProcessors;

#elif defined(__APPLE__)
    int cores = 0;
    size_t len = sizeof(cores);
    if (sysctlbyname("hw.logicalcpu", &cores, &len, NULL, 0) == 0)
    {
        return (int64_t)cores;
    }
    // Fallback
    return (int64_t)sysconf(_SC_NPROCESSORS_ONLN);

#elif defined(__linux__)
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 0)
        return (int64_t)cores;
    return 1;

#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 0)
        return (int64_t)cores;
    return 1;
#endif
}

int64_t rt_machine_mem_total(void)
{
#ifdef _WIN32
    MEMORYSTATUSEX meminfo;
    meminfo.dwLength = sizeof(meminfo);
    if (GlobalMemoryStatusEx(&meminfo))
    {
        return (int64_t)meminfo.ullTotalPhys;
    }
    return 0;

#elif defined(__APPLE__)
    int64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0)
    {
        return mem;
    }
    return 0;

#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0)
    {
        return (int64_t)si.totalram * (int64_t)si.mem_unit;
    }
    return 0;

#else
    // Generic fallback using sysconf
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0)
    {
        return (int64_t)pages * (int64_t)page_size;
    }
    return 0;
#endif
}

int64_t rt_machine_mem_free(void)
{
#ifdef _WIN32
    MEMORYSTATUSEX meminfo;
    meminfo.dwLength = sizeof(meminfo);
    if (GlobalMemoryStatusEx(&meminfo))
    {
        return (int64_t)meminfo.ullAvailPhys;
    }
    return 0;

#elif defined(__APPLE__)
    // macOS: use vm_statistics64 for free memory
    vm_size_t page_size;
    mach_port_t mach_port = mach_host_self();
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);

    if (host_page_size(mach_port, &page_size) != KERN_SUCCESS)
    {
        return 0;
    }

    if (host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) !=
        KERN_SUCCESS)
    {
        return 0;
    }

    // Free memory = free pages + inactive pages (can be reclaimed)
    int64_t free_mem =
        ((int64_t)vm_stats.free_count + (int64_t)vm_stats.inactive_count) * (int64_t)page_size;
    return free_mem;

#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0)
    {
        return (int64_t)si.freeram * (int64_t)si.mem_unit;
    }
    return 0;

#else
    // Generic fallback using sysconf
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0)
    {
        return (int64_t)pages * (int64_t)page_size;
    }
    return 0;
#endif
}

// ============================================================================
// Endianness
// ============================================================================

rt_string rt_machine_endian(void)
{
    // Detect endianness at runtime
    union
    {
        uint32_t i;
        char c[4];
    } test = {0x01020304};

    if (test.c[0] == 1)
    {
        return make_str("big");
    }
    else
    {
        return make_str("little");
    }
}
