//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine.c
// Purpose: Implements system information queries for the Zanna.System.Machine class.
//          Provides CPU count, hostname, OS name/version, architecture,
//          total/free-memory estimates, page/pointer size, and endianness using
//          platform-specific APIs.
//
// Key invariants:
//   - CPU-count queries return at least one and include every Windows processor group.
//   - Windows environment and temporary-directory reads use retrying UTF-16 snapshots.
//   - OS name strings are statically determined at compile time from predefined
//     platform macros and never change at runtime.
//   - Hostname is queried fresh on each call; it is not cached.
//   - Memory queries use GlobalMemoryStatusEx (Win32) or sysinfo (Linux) or
//     host_statistics64 (macOS).
//   - Query failures use API-specific fallbacks such as 0, 1, 4096, empty, or "unknown".
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

#include "rt_platform.h"
#include "rt_string.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
#include <windows.h>

#include "rt_file_path.h" // rt_file_path_wide_to_string for UTF-8 conversion (VDOC-217)

static wchar_t *machine_win32_environment(const wchar_t *name) {
    DWORD capacity;
    if (!name || !*name)
        return NULL;
    capacity = GetEnvironmentVariableW(name, NULL, 0);
    if (capacity == 0)
        return NULL;
    for (int attempt = 0; attempt < 8; attempt++) {
        if ((size_t)capacity > SIZE_MAX / sizeof(wchar_t))
            return NULL;
        wchar_t *value = (wchar_t *)malloc((size_t)capacity * sizeof(wchar_t));
        if (!value)
            return NULL;
        DWORD length = GetEnvironmentVariableW(name, value, capacity);
        if (length > 0 && length < capacity) {
            value[length] = L'\0';
            return value;
        }
        free(value);
        if (length == 0 || length == UINT32_MAX)
            return NULL;
        capacity = length + 1u;
    }
    return NULL;
}

static wchar_t *machine_win32_user_name(void) {
    DWORD capacity = 256;
    for (int attempt = 0; attempt < 8; attempt++) {
        if ((size_t)capacity > SIZE_MAX / sizeof(wchar_t))
            return NULL;
        wchar_t *value = (wchar_t *)malloc((size_t)capacity * sizeof(wchar_t));
        if (!value)
            return NULL;
        DWORD length = capacity;
        if (GetUserNameW(value, &length) && length > 0) {
            value[length - 1u] = L'\0';
            return value;
        }
        DWORD error = GetLastError();
        free(value);
        if (error != ERROR_INSUFFICIENT_BUFFER || length <= capacity)
            return NULL;
        capacity = length;
    }
    return NULL;
}

static wchar_t *machine_win32_temp_path(void) {
    DWORD capacity = 512;
    for (int attempt = 0; attempt < 8; attempt++) {
        if ((size_t)capacity > SIZE_MAX / sizeof(wchar_t))
            return NULL;
        wchar_t *value = (wchar_t *)malloc((size_t)capacity * sizeof(wchar_t));
        if (!value)
            return NULL;
        DWORD length = GetTempPathW(capacity, value);
        if (length > 0 && length < capacity) {
            value[length] = L'\0';
            return value;
        }
        free(value);
        if (length == 0 || length == UINT32_MAX)
            return NULL;
        capacity = length + 1u;
    }
    return NULL;
}
#else
#include <pwd.h>
#include <sys/stat.h>
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
static rt_string make_str(const char *s) {
    if (!s)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(s, strlen(s));
}

#if !RT_PLATFORM_WINDOWS
static const char *nonempty_env(const char *name) {
    const char *value = getenv(name);
    return value && value[0] != '\0' ? value : NULL;
}

static rt_string machine_passwd_field(int home_directory) {
    long hint = sysconf(_SC_GETPW_R_SIZE_MAX);
    size_t size = hint > 0 && (unsigned long)hint <= SIZE_MAX ? (size_t)hint : 4096u;
    for (;;) {
        char *buffer = (char *)malloc(size);
        if (!buffer)
            return make_str("");
        struct passwd entry;
        struct passwd *result = NULL;
        int rc = getpwuid_r(getuid(), &entry, buffer, size, &result);
        if (rc == 0 && result) {
            const char *value = home_directory ? result->pw_dir : result->pw_name;
            rt_string out = make_str(value && value[0] != '\0' ? value : "");
            free(buffer);
            return out;
        }
        free(buffer);
        if (rc != ERANGE || size > SIZE_MAX / 2u)
            return make_str("");
        size *= 2u;
    }
}

#endif

#if RT_PLATFORM_LINUX
static int64_t checked_u64_bytes(unsigned long long value, unsigned long long unit) {
    if (unit != 0 && value > (unsigned long long)INT64_MAX / unit)
        return INT64_MAX;
    return (int64_t)(value * unit);
}

static int linux_read_control_line(const char *path, char *buffer, size_t capacity) {
    if (!path || !buffer || capacity < 2)
        return 0;
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    char *result = fgets(buffer, (int)capacity, file);
    int extra = result && !strchr(buffer, '\n') ? fgetc(file) : EOF;
    fclose(file);
    if (!result || extra != EOF)
        return 0;
    buffer[strcspn(buffer, "\r\n")] = '\0';
    return buffer[0] != '\0';
}

static int linux_parse_u64(const char *text, unsigned long long *out) {
    if (!text || !out || text[0] == '\0' || text[0] == '-')
        return 0;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || (*end != '\0' && *end != ' '))
        return 0;
    *out = value;
    return 1;
}

static int64_t linux_read_control_u64(const char *path) {
    char line[128];
    if (!linux_read_control_line(path, line, sizeof(line)) || strcmp(line, "max") == 0)
        return 0;
    unsigned long long value = 0;
    return linux_parse_u64(line, &value) && value <= (unsigned long long)INT64_MAX ? (int64_t)value
                                                                                   : 0;
}

static int64_t linux_cgroup_memory_value(const char *name) {
    if (strcmp(name, "memory.max") == 0) {
        int64_t value = linux_read_control_u64("/sys/fs/cgroup/memory.max");
        return value > 0 ? value
                         : linux_read_control_u64("/sys/fs/cgroup/memory/memory.limit_in_bytes");
    }
    if (strcmp(name, "memory.current") == 0) {
        int64_t value = linux_read_control_u64("/sys/fs/cgroup/memory.current");
        return value > 0 ? value
                         : linux_read_control_u64("/sys/fs/cgroup/memory/memory.usage_in_bytes");
    }
    return 0;
}

static int64_t linux_count_cpuset(const char *text) {
    if (!text || text[0] == '\0')
        return 0;
    unsigned long long count = 0;
    const char *cursor = text;
    while (*cursor) {
        errno = 0;
        char *end = NULL;
        unsigned long long first = strtoull(cursor, &end, 10);
        if (errno != 0 || end == cursor)
            return 0;
        unsigned long long last = first;
        if (*end == '-') {
            cursor = end + 1;
            errno = 0;
            last = strtoull(cursor, &end, 10);
            if (errno != 0 || end == cursor || last < first)
                return 0;
        }
        unsigned long long span = last - first + 1u;
        if (ULLONG_MAX - count < span)
            return 0;
        count += span;
        if (*end == '\0')
            break;
        if (*end != ',')
            return 0;
        cursor = end + 1;
    }
    return count > 0 && count <= (unsigned long long)INT64_MAX ? (int64_t)count : 0;
}

static int64_t linux_cgroup_cpu_limit(void) {
    char line[128];
    int64_t limit = 0;
    if (linux_read_control_line("/sys/fs/cgroup/cpu.max", line, sizeof(line)) &&
        strncmp(line, "max ", 4) != 0) {
        unsigned long long quota = 0, period = 0;
        char *space = strchr(line, ' ');
        if (space) {
            *space++ = '\0';
            if (linux_parse_u64(line, &quota) && linux_parse_u64(space, &period) && period > 0) {
                unsigned long long rounded = quota / period + (quota % period != 0);
                if (rounded > 0 && rounded <= (unsigned long long)INT64_MAX)
                    limit = (int64_t)rounded;
            }
        }
    }
    if (limit == 0) {
        int64_t quota = linux_read_control_u64("/sys/fs/cgroup/cpu/cpu.cfs_quota_us");
        int64_t period = linux_read_control_u64("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
        if (quota > 0 && period > 0)
            limit = quota / period + (quota % period != 0);
    }
    if (linux_read_control_line("/sys/fs/cgroup/cpuset.cpus.effective", line, sizeof(line))) {
        int64_t cpuset = linux_count_cpuset(line);
        if (cpuset > 0 && (limit == 0 || cpuset < limit))
            limit = cpuset;
    } else if (linux_read_control_line("/sys/fs/cgroup/cpuset/cpuset.cpus", line, sizeof(line))) {
        int64_t cpuset = linux_count_cpuset(line);
        if (cpuset > 0 && (limit == 0 || cpuset < limit))
            limit = cpuset;
    }
    return limit;
}

rt_string rt_machine_linux_parse_os_release_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file)
        return NULL;
    char *line = NULL;
    size_t capacity = 0;
    rt_string result = NULL;
    while (getline(&line, &capacity, file) >= 0) {
        if (strncmp(line, "VERSION_ID=", 11) != 0)
            continue;
        char *value = line + 11;
        value[strcspn(value, "\r\n")] = '\0';
        size_t length = strlen(value);
        char quote = 0;
        if (length > 0 && (value[0] == '\'' || value[0] == '"')) {
            quote = value[0];
            if (length < 2 || value[length - 1] != quote)
                break;
            value[length - 1] = '\0';
            value++;
        }
        char *read_cursor = value;
        char *write_cursor = value;
        int valid = 1;
        while (*read_cursor) {
            unsigned char ch = (unsigned char)*read_cursor++;
            if (ch == '\\' && quote != '\'' && *read_cursor)
                ch = (unsigned char)*read_cursor++;
            if (ch < 0x20 || ch == 0x7f) {
                valid = 0;
                break;
            }
            *write_cursor++ = (char)ch;
        }
        *write_cursor = '\0';
        if (valid && value[0] != '\0')
            result = make_str(value);
        break;
    }
    free(line);
    fclose(file);
    return result;
}
#endif

// ============================================================================
// Operating System
// ============================================================================

/// @brief Return a lowercase platform identifier ("windows", "macos", "linux",
/// "unknown"). Compile-time selected from `_WIN32`/`__APPLE__`/`__linux__`.
rt_string rt_machine_os(void) {
#if defined(_WIN32)
    return make_str("windows");
#elif defined(__APPLE__)
    return make_str("macos");
#elif defined(__linux__)
    return make_str("linux");
#else
    return make_str("unknown");
#endif
}

/// @brief Return the OS version string in the platform's native format. Per-OS source:
///   - **Windows:** `RtlGetVersion` (ntdll) → `"major.minor.build"`, the true OS version
///   independent of the executable's compatibility manifest; falls back to the deprecated
///   `GetVersionExA` only if RtlGetVersion is unavailable.
///   - **macOS:** `sysctlbyname("kern.osproductversion")` (e.g. "14.5"); falls back to
///   `uname.release`.
///   - **Linux:** Parses `VERSION_ID=` from `/etc/os-release` (e.g. "22.04"); falls back to
///   `uname.release`.
///   - **Other Unix:** `uname.release`.
/// Returns "unknown" if every probe fails.
rt_string rt_machine_os_ver(void) {
#ifdef _WIN32
    // Prefer RtlGetVersion (ntdll): unlike GetVersionExA it returns the TRUE OS
    // version regardless of the executable's supported-OS compatibility manifest,
    // so an unmanifested native output or custom embedder does not misreport
    // Windows 10/11 as an older release (VDOC-216). Loaded dynamically because it
    // is not in a linkable import library.
    {
        typedef LONG(WINAPI * RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            RtlGetVersionFn rtl_get_version =
                (RtlGetVersionFn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
            if (rtl_get_version) {
                RTL_OSVERSIONINFOW info;
                ZeroMemory(&info, sizeof(info));
                info.dwOSVersionInfoSize = sizeof(info);
                if (rtl_get_version(&info) == 0) { // STATUS_SUCCESS
                    char buf[64];
                    snprintf(buf,
                             sizeof(buf),
                             "%lu.%lu.%lu",
                             (unsigned long)info.dwMajorVersion,
                             (unsigned long)info.dwMinorVersion,
                             (unsigned long)info.dwBuildNumber);
                    return make_str(buf);
                }
            }
        }
    }

    // Fallback: GetVersionExA (deprecated; may be manifest-conditioned).
    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
#pragma warning(push)
#pragma warning(disable : 4996)
    if (GetVersionExA(&osvi)) {
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
    if (sysctlbyname("kern.osproductversion", ver, &len, NULL, 0) == 0) {
        return make_str(ver);
    }
    // Fallback to uname
    struct utsname uts;
    if (uname(&uts) == 0) {
        return make_str(uts.release);
    }
    return make_str("unknown");

#elif defined(__linux__)
    rt_string version = rt_machine_linux_parse_os_release_file("/etc/os-release");
    if (!version)
        version = rt_machine_linux_parse_os_release_file("/usr/lib/os-release");
    if (version)
        return version;
    // Fallback to uname
    struct utsname uts;
    if (uname(&uts) == 0) {
        return make_str(uts.release);
    }
    return make_str("unknown");

#else
    // Unix: use uname.
    struct utsname uts;
    if (uname(&uts) == 0) {
        return make_str(uts.release);
    }
    return make_str("unknown");
#endif
}

// ============================================================================
// Host and User
// ============================================================================

/// @brief Return the machine's hostname. Uses `GetComputerNameA` (Win32) or `gethostname` (POSIX).
/// Truncated to 256 characters and NUL-terminated. "unknown" if the syscall fails.
rt_string rt_machine_host(void) {
#ifdef _WIN32
    // Wide API + validated UTF-8 conversion so non-ASCII host names survive,
    // matching Zanna.System.Environment (VDOC-217).
    wchar_t buf[256];
    DWORD len = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    if (GetComputerNameW(buf, &len))
        return rt_file_path_wide_to_string(buf);
    return make_str("unknown");
#else
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return make_str(buf);
    }
    return make_str("unknown");
#endif
}

/// @brief Return the current user's login name. Win32 uses `GetUserNameA` (then `%USERNAME%`);
/// POSIX uses `getpwuid(getuid())` (then `$USER` / `$LOGNAME`). "unknown" if all probes fail.
rt_string rt_machine_user(void) {
#ifdef _WIN32
    wchar_t *user = machine_win32_user_name();
    if (!user)
        user = machine_win32_environment(L"USERNAME");
    if (user) {
        rt_string result = rt_file_path_wide_to_string(user);
        free(user);
        return result;
    }
    return make_str("unknown");
#else
    rt_string account = machine_passwd_field(0);
    if (rt_str_len(account) > 0)
        return account;
    rt_string_unref(account);
    // Fallback to environment variables
    const char *user = nonempty_env("USER");
    if (!user)
        user = nonempty_env("LOGNAME");
    if (user)
        return make_str(user);
    return make_str("unknown");
#endif
}

// ============================================================================
// Directories
// ============================================================================

/// @brief Return the current user's home directory. Win32 prefers `%USERPROFILE%`, falling back
/// to `HOMEDRIVE+HOMEPATH`. POSIX prefers `$HOME`, falling back to `pw_dir` from the passwd
/// entry. Empty string if no probe succeeds (rare on a properly configured system).
rt_string rt_machine_home(void) {
#ifdef _WIN32
    wchar_t *home = machine_win32_environment(L"USERPROFILE");
    if (home) {
        rt_string result = rt_file_path_wide_to_string(home);
        free(home);
        return result;
    }
    wchar_t *drive = machine_win32_environment(L"HOMEDRIVE");
    wchar_t *path = machine_win32_environment(L"HOMEPATH");
    if (drive && path) {
        size_t drive_length = wcslen(drive);
        size_t path_length = wcslen(path);
        if (drive_length <= SIZE_MAX - path_length - 1u &&
            drive_length + path_length + 1u <= SIZE_MAX / sizeof(wchar_t)) {
            wchar_t *combined =
                (wchar_t *)malloc((drive_length + path_length + 1u) * sizeof(wchar_t));
            if (combined) {
                memcpy(combined, drive, drive_length * sizeof(wchar_t));
                memcpy(combined + drive_length, path, (path_length + 1u) * sizeof(wchar_t));
                rt_string result = rt_file_path_wide_to_string(combined);
                free(combined);
                free(drive);
                free(path);
                return result;
            }
        }
    }
    free(drive);
    free(path);
    return make_str("");
#else
    const char *home = nonempty_env("HOME");
    if (home)
        return make_str(home);
    return machine_passwd_field(1);
#endif
}

/// @brief Return the platform's temp directory. Win32 uses `GetTempPathA` (trailing `\` stripped).
/// POSIX checks `$TMPDIR` → `$TMP` → `$TEMP` → fixed `/tmp` fallback.
rt_string rt_machine_temp(void) {
#ifdef _WIN32
    wchar_t *path = machine_win32_temp_path();
    if (path) {
        size_t length = wcslen(path);
        int drive_root = length == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
        if (length > 1 && !drive_root && (path[length - 1] == L'\\' || path[length - 1] == L'/'))
            path[length - 1] = L'\0';
        rt_string result = rt_file_path_wide_to_string(path);
        free(path);
        return result;
    }
    return make_str("C:\\Temp");
#else
    const char *tmp = nonempty_env("TMPDIR");
    if (!tmp)
        tmp = nonempty_env("TMP");
    if (!tmp)
        tmp = nonempty_env("TEMP");
    struct stat st;
    if (!tmp || tmp[0] != '/' || stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode))
        tmp = "/tmp";
    return make_str(tmp);
#endif
}

// ============================================================================
// Hardware Information
// ============================================================================

/// @brief Return the number of logical CPU cores. Win32: `GetSystemInfo.dwNumberOfProcessors`.
/// macOS: `sysctlbyname("hw.logicalcpu")`, validated and falling back to a
/// positive `sysconf(_SC_NPROCESSORS_ONLN)`, else 1.
/// Linux/other: `sysconf(_SC_NPROCESSORS_ONLN)`, with 1 as the safe minimum.
/// Every platform returns at least 1 (VDOC-219).
int64_t rt_machine_cores(void) {
#ifdef _WIN32
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (count > 0)
        return (int64_t)count;
    return 1;

#elif defined(__APPLE__)
    // Validate the sysctl result and fall back to a POSITIVE sysconf value, else
    // the safe minimum of 1 — the raw sysctl/sysconf values could be 0 or -1 and
    // callers sizing worker pools must never receive a non-positive count
    // (VDOC-219).
    int cores = 0;
    size_t len = sizeof(cores);
    if (sysctlbyname("hw.logicalcpu", &cores, &len, NULL, 0) == 0 && cores > 0)
        return (int64_t)cores;
    long fallback = sysconf(_SC_NPROCESSORS_ONLN);
    if (fallback > 0)
        return (int64_t)fallback;
    return 1;

#elif defined(__linux__)
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int64_t host = cores > 0 ? (int64_t)cores : 1;
    int64_t constrained = linux_cgroup_cpu_limit();
    return constrained > 0 && constrained < host ? constrained : host;

#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 0)
        return (int64_t)cores;
    return 1;
#endif
}

/// @brief Return a stable CPU architecture identifier.
rt_string rt_machine_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return make_str("x86_64");
#elif defined(__aarch64__) || defined(_M_ARM64)
    return make_str("arm64");
#elif defined(__i386__) || defined(_M_IX86)
    return make_str("x86");
#elif defined(__arm__) || defined(_M_ARM)
    return make_str("arm");
#elif defined(__wasm32__)
    return make_str("wasm32");
#else
    return make_str("unknown");
#endif
}

/// @brief Return the system page size in bytes.
int64_t rt_machine_page_size(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int64_t)sysinfo.dwPageSize;
#else
#if defined(_SC_PAGESIZE)
    long size = sysconf(_SC_PAGESIZE);
#elif defined(_SC_PAGE_SIZE)
    long size = sysconf(_SC_PAGE_SIZE);
#else
    long size = 0;
#endif
    return size > 0 ? (int64_t)size : 4096;
#endif
}

/// @brief Return native pointer width in bits.
int64_t rt_machine_pointer_size(void) {
    return (int64_t)(sizeof(void *) * 8);
}

/// @brief Return total physical RAM in bytes. Win32: `GlobalMemoryStatusEx.ullTotalPhys`.
/// macOS: `sysctlbyname("hw.memsize")`. Linux: `sysinfo.totalram * mem_unit`. Generic POSIX
/// fallback: `sysconf(_SC_PHYS_PAGES) * _SC_PAGE_SIZE`. Returns 0 if no probe succeeds.
int64_t rt_machine_mem_total(void) {
#ifdef _WIN32
    MEMORYSTATUSEX meminfo;
    meminfo.dwLength = sizeof(meminfo);
    if (GlobalMemoryStatusEx(&meminfo)) {
        return (int64_t)meminfo.ullTotalPhys;
    }
    return 0;

#elif defined(__APPLE__)
    int64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0) {
        return mem;
    }
    return 0;

#elif defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        int64_t host =
            checked_u64_bytes((unsigned long long)si.totalram, (unsigned long long)si.mem_unit);
        int64_t constrained = linux_cgroup_memory_value("memory.max");
        return constrained > 0 && constrained < host ? constrained : host;
    }
    return 0;

#else
    // Generic fallback using sysconf
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return (int64_t)pages * (int64_t)page_size;
    }
    return 0;
#endif
}

/// @brief Return AVAILABLE physical RAM in bytes — one portable estimate of the
///        memory that can be given to new work without swapping, INCLUDING
///        reclaimable page cache, on every platform (VDOC-218):
///   - Win32: `GlobalMemoryStatusEx.ullAvailPhys` (available physical memory).
///   - macOS: `host_statistics64(HOST_VM_INFO64)`, `(free_count + inactive_count)` pages —
///     inactive pages are reclaimable on macOS (truly idle, not just unmapped).
///   - Linux: `MemAvailable` from `/proc/meminfo` (kernel's available estimate, includes
///     reclaimable cache); falls back to `(freeram + bufferram) * mem_unit` on kernels lacking it.
///   - Generic POSIX: `sysconf(_SC_AVPHYS_PAGES) * _SC_PAGE_SIZE`.
/// Mach paths carefully `mach_port_deallocate` on every exit (success or failure).
int64_t rt_machine_mem_free(void) {
#ifdef _WIN32
    MEMORYSTATUSEX meminfo;
    meminfo.dwLength = sizeof(meminfo);
    if (GlobalMemoryStatusEx(&meminfo)) {
        return (int64_t)meminfo.ullAvailPhys;
    }
    return 0;

#elif defined(__APPLE__)
    // macOS: use vm_statistics64 for free memory
    vm_size_t page_size = 0;
    mach_port_t mach_port = mach_host_self();
    vm_statistics64_data_t vm_stats = {0};
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);

    if (host_page_size(mach_port, &page_size) != KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), mach_port);
        return 0;
    }

    if (host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stats, &count) !=
        KERN_SUCCESS) {
        mach_port_deallocate(mach_task_self(), mach_port);
        return 0;
    }

    mach_port_deallocate(mach_task_self(), mach_port);

    // Free memory = free pages + inactive pages (can be reclaimed)
    int64_t free_mem =
        ((int64_t)vm_stats.free_count + (int64_t)vm_stats.inactive_count) * (int64_t)page_size;
    return free_mem;

#elif defined(__linux__)
    // Prefer MemAvailable from /proc/meminfo — the kernel's estimate of memory
    // available for new work WITHOUT swapping, which INCLUDES reclaimable page
    // cache. Plain sysinfo.freeram counts only strictly-free pages and badly
    // understates usable memory, making the metric incomparable with macOS
    // (free+inactive) and Windows (ullAvailPhys). Reporting MemAvailable puts all
    // three platforms on one "available memory" definition (VDOC-218).
    {
        FILE *meminfo = fopen("/proc/meminfo", "r");
        if (meminfo) {
            char line[256];
            while (fgets(line, sizeof(line), meminfo)) {
                unsigned long long kb = 0;
                if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1) {
                    fclose(meminfo);
                    int64_t host_available = checked_u64_bytes(kb, 1024ULL);
                    int64_t limit = linux_cgroup_memory_value("memory.max");
                    int64_t current = linux_cgroup_memory_value("memory.current");
                    if (limit > 0 && current >= 0) {
                        int64_t container_available = current < limit ? limit - current : 0;
                        if (container_available < host_available)
                            host_available = container_available;
                    }
                    return host_available;
                }
            }
            fclose(meminfo);
        }
    }
    // Fallback for older kernels without MemAvailable: free + buffers + cached is
    // a closer approximation than freeram alone.
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        unsigned long long pages = (unsigned long long)si.freeram;
        if (ULLONG_MAX - pages < (unsigned long long)si.bufferram)
            return INT64_MAX;
        pages += (unsigned long long)si.bufferram;
        return checked_u64_bytes(pages, (unsigned long long)si.mem_unit);
    }
    return 0;

#else
    // Generic fallback using sysconf
    long pages = sysconf(_SC_AVPHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page_size > 0) {
        return (int64_t)pages * (int64_t)page_size;
    }
    return 0;
#endif
}

// ============================================================================
// Endianness
// ============================================================================

/// @brief Detect endianness at runtime via the classic "byte-of-an-int" trick:
/// `union { uint32_t i; char c[4]; } = {0x01020304}` — `c[0]==1` means big-endian (MSB first),
/// otherwise little-endian. Branchless, no syscalls. Returns "big" or "little".
rt_string rt_machine_endian(void) {
    // Detect endianness at runtime
    union {
        uint32_t i;
        char c[4];
    } test = {0x01020304};

    if (test.c[0] == 1) {
        return make_str("big");
    } else {
        return make_str("little");
    }
}
