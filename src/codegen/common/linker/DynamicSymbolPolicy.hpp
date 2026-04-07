//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/DynamicSymbolPolicy.hpp
// Purpose: Shared policy helpers for symbols that are allowed to resolve
//          dynamically through system libraries or platform frameworks.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <string>

namespace viper::codegen::linker {

inline std::string stripDynamicSymbolLeadingUnderscores(const std::string &name) {
    size_t i = 0;
    while (i < name.size() && name[i] == '_')
        ++i;
    return i == 0 ? name : name.substr(i);
}

/// Known system/dynamic library symbols that won't be present in runtime
/// archives and may be resolved through platform loader metadata instead.
inline bool isKnownDynamicSymbol(const std::string &name, LinkPlatform platform) {
    const std::string stripped = stripDynamicSymbolLeadingUnderscores(name);

    static const char *const kDynSymExact[] = {
        "printf",
        "fprintf",
        "sprintf",
        "snprintf",
        "puts",
        "fputs",
        "fopen",
        "fclose",
        "fread",
        "fwrite",
        "fseek",
        "fseeko",
        "ftell",
        "ftello",
        "fflush",
        "fgets",
        "malloc",
        "calloc",
        "realloc",
        "free",
        "memcpy",
        "memmove",
        "memset",
        "memcmp",
        "memchr",
        "strlen",
        "strcmp",
        "strcpy",
        "strncpy",
        "strdup",
        "strndup",
        "strcat",
        "strncat",
        "strstr",
        "strchr",
        "strrchr",
        "atoi",
        "atol",
        "atof",
        "strtol",
        "strtod",
        "strtoul",
        "exit",
        "_exit",
        "abort",
        "atexit",
        "getenv",
        "setenv",
        "system",
        "time",
        "clock",
        "gettimeofday",
        "gmtime_r",
        "localtime_r",
        "mktime",
        "strftime",
        "nanosleep",
        "usleep",
        "sleep",
        "open",
        "close",
        "read",
        "write",
        "lseek",
        "stat",
        "fstat",
        "lstat",
        "mkdir",
        "rmdir",
        "unlink",
        "remove",
        "rename",
        "getcwd",
        "chdir",
        "socket",
        "bind",
        "listen",
        "accept",
        "connect",
        "send",
        "recv",
        "select",
        "poll",
        "setsockopt",
        "getsockopt",
        "getsockname",
        "getaddrinfo",
        "freeaddrinfo",
        "getnameinfo",
        "pthread_create",
        "pthread_join",
        "pthread_detach",
        "pthread_mutex_init",
        "pthread_mutex_lock",
        "pthread_mutex_unlock",
        "pthread_mutex_destroy",
        "pthread_cond_init",
        "pthread_cond_wait",
        "pthread_cond_signal",
        "pthread_cond_broadcast",
        "pthread_cond_destroy",
        "pthread_cond_timedwait",
        "pthread_cond_timedwait_relative_np",
        "pthread_key_create",
        "pthread_getspecific",
        "pthread_setspecific",
        "pthread_once",
        "pthread_rwlock_init",
        "pthread_rwlock_rdlock",
        "pthread_rwlock_wrlock",
        "pthread_rwlock_unlock",
        "pthread_rwlock_destroy",
        "pthread_self",
        "pthread_equal",
        "pthread_attr_init",
        "pthread_attr_destroy",
        "pthread_attr_setdetachstate",
        "dlopen",
        "dlsym",
        "dlclose",
        "dlerror",
        "mmap",
        "munmap",
        "mprotect",
        "popen",
        "pclose",
        "posix_spawn",
        "posix_spawn_file_actions_addclose",
        "posix_spawn_file_actions_adddup2",
        "posix_spawn_file_actions_destroy",
        "posix_spawn_file_actions_init",
        "sysconf",
        "getpid",
        "getuid",
        "signal",
        "sigaction",
        "raise",
        "setjmp",
        "longjmp",
        "_setjmp",
        "_longjmp",
        "qsort",
        "bsearch",
        "isalpha",
        "isalnum",
        "isdigit",
        "islower",
        "isspace",
        "isupper",
        "toupper",
        "tolower",
        "localeconv",
        "strerror",
        "strcasecmp",
        "strncasecmp",
        "perror",
        "sscanf",
        "strtoll",
        "strncmp",
        "strtok_r",
        "fnmatch",
        "vfprintf",
        "setvbuf",
        "setbuf",
        "freopen",
        "tmpfile",
        "tmpnam",
        "fdopen",
        "getc",
        "putc",
        "fgetc",
        "fputc",
        "ungetc",
        "ferror",
        "feof",
        "clearerr",
        "rewind",
        "_Exit",
        "posix_memalign",
        "aligned_alloc",
        "reallocf",
        "bzero",
        "access",
        "dup",
        "dup2",
        "pipe",
        "fork",
        "execv",
        "execve",
        "waitpid",
        "wait",
        "fcntl",
        "ioctl",
        "kqueue",
        "kevent",
        "tcgetattr",
        "tcsetattr",
        "fileno",
        "isatty",
        "ttyname",
        "sched_yield",
        "clock_gettime",
        "sin",
        "cos",
        "sinh",
        "cosh",
        "tan",
        "tanh",
        "asin",
        "acos",
        "atan",
        "atan2",
        "sinf",
        "cosf",
        "tanf",
        "asinf",
        "acosf",
        "atanf",
        "atan2f",
        "sqrt",
        "sqrtf",
        "pow",
        "powf",
        "hypot",
        "ldexp",
        "exp",
        "expf",
        "log",
        "logf",
        "log2",
        "log10",
        "ceil",
        "ceilf",
        "floor",
        "floorf",
        "round",
        "roundf",
        "fmod",
        "fmodf",
        "fabs",
        "fabsf",
        "fmin",
        "fminf",
        "fmax",
        "fmaxf",
        "copysign",
        "copysignf",
        "trunc",
        "truncf",
        "_NSGetExecutablePath",
        "_NSConcreteStackBlock",
        "_NSConcreteGlobalBlock",
        "_NSConcreteMallocBlock",
        "_Block_copy",
        "_Block_release",
        "_Block_object_assign",
        "_Block_object_dispose",
        "dyld_stub_binder",
        "_tlv_atexit",
        "_tlv_bootstrap",
        "__assert_rtn",
        "__chkstk_darwin",
        "__error",
        "__stderrp",
        "__stdinp",
        "__stdoutp",
        "__memcpy_chk",
        "__memmove_chk",
        "__memset_chk",
        "__snprintf_chk",
        "__strcat_chk",
        "__strcpy_chk",
        "__strncpy_chk",
        "__vsnprintf_chk",
        "__darwin_check_fd_set_overflow",
        "select$DARWIN_EXTSN",
        "mach_timebase_info",
        "mach_absolute_time",
        "mach_task_self_",
        "mach_host_self",
        "task_info",
        "host_page_size",
        "_os_unfair_lock_lock",
        "_os_unfair_lock_unlock",
        "os_unfair_lock_lock",
        "os_unfair_lock_unlock",
        "newlocale",
        "freelocale",
        "uselocale",
        "utime",
        "backtrace",
        "backtrace_symbols_fd",
        "closedir",
        "opendir",
        "readdir",
        "environ",
        "stdin",
        "stdout",
        "stderr",
        "__errno_location",
        "getpwuid",
        "nan",
        "sel_registerName",
        "sel_getName",
        "_objc_empty_cache",
        "_objc_empty_vtable",
        "ExitProcess",
        "GetModuleHandleA",
        "GetProcAddress",
        "VirtualAlloc",
        "VirtualFree",
        "GetLastError",
        "BCryptGenRandom",
        "__acrt_iob_func",
        "__local_stdio_printf_options",
        "__local_stdio_scanf_options",
        "__stdio_common_vfprintf",
        "__stdio_common_vsprintf",
        "_vfprintf_l",
        "_vsscanf_l",
        "__security_check_cookie",
        "__security_init_cookie",
        "__GSHandlerCheck",
        "__chkstk",
        "_CrtDbgReport",
        "_CrtDbgReportW",
        "strcpy_s",
        "strcat_s",
        "_wsplitpath_s",
        "_wmakepath_s",
        "wcscpy_s",
        "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9",
        "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9",
    };

    for (const char *sym : kDynSymExact) {
        if (name == sym || stripped == sym)
            return true;
    }

    static const char *const kCommonDynPrefixes[] = {
        "__libc_",
        "__stack_chk_",
    };
    static const char *const kMacDynPrefixes[] = {
        "CF",
        "kCF",
        "CG",
        "kCG",
        "NS",
        "IOKit",
        "IOHID",
        "IOService",
        "IORegistryEntry",
        "objc_",
        "OBJC_",
        "_objc_",
        "UTType",
        "UTCopy",
        "AudioQueue",
        "AudioServices",
        "AudioComponent",
        "Sec",
        "mach_",
        "task_",
        "host_",
        "vm_",
        "kern_",
        "dispatch_",
        "MTL",
        "AudioObject",
        "AudioDevice",
    };
    static const char *const kLinuxDynPrefixes[] = {
        "X",
        "snd_",
    };
    static const char *const kWindowsDynPrefixes[] = {
        "__imp_",
    };

    for (const char *prefix : kCommonDynPrefixes) {
        size_t plen = 0;
        while (prefix[plen] != '\0')
            ++plen;
        if ((name.size() >= plen && name.compare(0, plen, prefix) == 0) ||
            (stripped.size() >= plen && stripped.compare(0, plen, prefix) == 0)) {
            return true;
        }
    }

    const char *const *platformPrefixes = nullptr;
    size_t prefixCount = 0;
    switch (platform) {
        case LinkPlatform::Linux:
            platformPrefixes = kLinuxDynPrefixes;
            prefixCount = sizeof(kLinuxDynPrefixes) / sizeof(kLinuxDynPrefixes[0]);
            break;
        case LinkPlatform::macOS:
            platformPrefixes = kMacDynPrefixes;
            prefixCount = sizeof(kMacDynPrefixes) / sizeof(kMacDynPrefixes[0]);
            break;
        case LinkPlatform::Windows:
            platformPrefixes = kWindowsDynPrefixes;
            prefixCount = sizeof(kWindowsDynPrefixes) / sizeof(kWindowsDynPrefixes[0]);
            break;
        default:
            break;
    }

    for (size_t i = 0; i < prefixCount; ++i) {
        const char *prefix = platformPrefixes[i];
        size_t plen = 0;
        while (prefix[plen] != '\0')
            ++plen;
        if ((name.size() >= plen && name.compare(0, plen, prefix) == 0) ||
            (stripped.size() >= plen && stripped.compare(0, plen, prefix) == 0)) {
            return true;
        }
    }

    return false;
}

} // namespace viper::codegen::linker
