//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/system/rt_machine.c
// Purpose: Implements system information queries for the Viper.System.Machine class.
//          Provides CPU count, hostname, OS name/version, architecture,
//          total/free-memory estimates, page/pointer size, and endianness using
//          platform-specific APIs.
//
// Key invariants:
//   - CPU count queries use GetSystemInfo (Win32), sysctl/sysconf (POSIX).
//     Linux/generic POSIX fall back to 1; the macOS sysconf fallback can return -1.
//   - OS name strings are statically determined at compile time from predefined
//     platform macros, including ViperDOS, and never change at runtime.
//   - Hostname is queried fresh on each call; it is not cached.
//   - Memory queries use GlobalMemoryStatusEx (Win32) or sysinfo (Linux) or
//     host_statistics64 (macOS).
//   - Query failures use API-specific fallbacks such as 0, 1, 4096, empty, or
//     "unknown"; the macOS core-count fallback currently leaks sysconf's -1.
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

#include "rt_file_path.h" // rt_file_path_wide_to_string for UTF-8 conversion (VDOC-217)
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
static rt_string make_str(const char *s) {
    if (!s)
        return rt_string_from_bytes("", 0);
    return rt_string_from_bytes(s, strlen(s));
}

// ============================================================================
// Operating System
// ============================================================================

/// @brief Return a lowercase platform identifier ("windows", "macos", "linux", "viperdos",
/// "unknown"). Compile-time selected from `_WIN32`/`__APPLE__`/`__linux__`/`__viperdos__`.
rt_string rt_machine_os(void) {
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

/// @brief Return the OS version string in the platform's native format. Per-OS source:
///   - **Windows:** `RtlGetVersion` (ntdll) → `"major.minor.build"`, the true OS version
///   independent of the executable's compatibility manifest; falls back to the deprecated
///   `GetVersionExA` only if RtlGetVersion is unavailable.
///   - **macOS:** `sysctlbyname("kern.osproductversion")` (e.g. "14.5"); falls back to
///   `uname.release`.
///   - **Linux:** Parses `VERSION_ID=` from `/etc/os-release` (e.g. "22.04"); falls back to
///   `uname.release`.
///   - **Other Unix / ViperDOS:** `uname.release`.
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
    // Linux: read /etc/os-release or use uname
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "VERSION_ID=", 11) == 0) {
                fclose(fp);
                // Remove quotes and newline
                char *ver = line + 11;
                size_t vlen = strlen(ver);
                if (vlen > 0 && ver[vlen - 1] == '\n')
                    ver[--vlen] = '\0';
                if (vlen >= 2 && ver[0] == '"' && ver[vlen - 1] == '"') {
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
    if (uname(&uts) == 0) {
        return make_str(uts.release);
    }
    return make_str("unknown");

#else
    // Unix and ViperDOS: use uname.
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
    // matching Viper.System.Environment (VDOC-217).
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
    // Wide API + validated UTF-8 conversion so non-ASCII user names survive
    // (VDOC-217).
    wchar_t buf[256];
    DWORD len = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    if (GetUserNameW(buf, &len))
        return rt_file_path_wide_to_string(buf);
    // Fallback to the wide environment variable.
    const wchar_t *user = _wgetenv(L"USERNAME");
    if (user)
        return rt_file_path_wide_to_string(user);
    return make_str("unknown");
#else
    // Try getpwuid first
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) {
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

/// @brief Return the current user's home directory. Win32 prefers `%USERPROFILE%`, falling back
/// to `HOMEDRIVE+HOMEPATH`. POSIX prefers `$HOME`, falling back to `pw_dir` from the passwd
/// entry. Empty string if no probe succeeds (rare on a properly configured system).
rt_string rt_machine_home(void) {
#ifdef _WIN32
    // Wide environment variables + validated UTF-8 conversion so non-ASCII home
    // paths survive (VDOC-217).
    const wchar_t *home = _wgetenv(L"USERPROFILE");
    if (home)
        return rt_file_path_wide_to_string(home);
    // Try HOMEDRIVE + HOMEPATH.
    const wchar_t *drive = _wgetenv(L"HOMEDRIVE");
    const wchar_t *path = _wgetenv(L"HOMEPATH");
    if (drive && path) {
        wchar_t buf[512];
        _snwprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%ls%ls", drive, path);
        buf[(sizeof(buf) / sizeof(buf[0])) - 1] = L'\0';
        return rt_file_path_wide_to_string(buf);
    }
    return make_str("");
#else
    const char *home = getenv("HOME");
    if (home)
        return make_str(home);
    // Fallback to passwd entry
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        return make_str(pw->pw_dir);
    }
    return make_str("");
#endif
}

/// @brief Return the platform's temp directory. Win32 uses `GetTempPathA` (trailing `\` stripped).
/// POSIX checks `$TMPDIR` → `$TMP` → `$TEMP` → fixed `/tmp` fallback.
rt_string rt_machine_temp(void) {
#ifdef _WIN32
    // Wide API + validated UTF-8 conversion so non-ASCII temp paths survive
    // (VDOC-217).
    wchar_t buf[512];
    DWORD cap = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    DWORD len = GetTempPathW(cap, buf);
    if (len > 0 && len < cap) {
        // Remove trailing backslash if present.
        if (len > 1 && buf[len - 1] == L'\\')
            buf[len - 1] = L'\0';
        return rt_file_path_wide_to_string(buf);
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

/// @brief Return the number of logical CPU cores. Win32: `GetSystemInfo.dwNumberOfProcessors`.
/// macOS: `sysctlbyname("hw.logicalcpu")`, validated and falling back to a
/// positive `sysconf(_SC_NPROCESSORS_ONLN)`, else 1.
/// Linux/other: `sysconf(_SC_NPROCESSORS_ONLN)`, with 1 as the safe minimum.
/// Every platform returns at least 1 (VDOC-219).
int64_t rt_machine_cores(void) {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    if (sysinfo.dwNumberOfProcessors > 0)
        return (int64_t)sysinfo.dwNumberOfProcessors;
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
        return (int64_t)si.totalram * (int64_t)si.mem_unit;
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
                    return (int64_t)(kb * 1024ULL);
                }
            }
            fclose(meminfo);
        }
    }
    // Fallback for older kernels without MemAvailable: free + buffers + cached is
    // a closer approximation than freeram alone.
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        int64_t unit = (int64_t)si.mem_unit;
        return ((int64_t)si.freeram + (int64_t)si.bufferram) * unit;
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
