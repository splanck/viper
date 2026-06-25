//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/DynamicSymbolPolicy.hpp
// Purpose: Shared policy helpers for symbols that are allowed to resolve
//          dynamically through system libraries or platform frameworks. The
//          native linker uses these to decide whether an undefined symbol is a
//          legitimate dyld/dlopen reference (libc, ObjC, Win32, pthreads, etc.)
//          versus a real linker error.
// Key invariants:
//   - The exact-match list and prefix lists are sorted by use frequency, not
//     alphabetically; do not reorder without measuring impact on link cost.
//   - Symbols beginning with leading underscores are stripped before matching
//     to handle the Mach-O "_main"/"main" convention transparently.
// Ownership/Lifetime: Stateless inline helpers; no allocation beyond returned
//                     strings.
// Links: SymbolResolver.cpp, NativeLinker.cpp, MachOExeWriter.cpp,
//        PeExeWriter.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"

#include <string>

namespace viper::codegen::linker {

/// @brief Normalise a symbol name to its "bare" form for prefix matching.
/// @details Drops any leading underscores (Mach-O mangles "main" to "_main")
///          and trims the trailing "$DARWIN_EXTSN" Darwin-extension marker so
///          variants like `select` and `select$DARWIN_EXTSN` compare equal.
/// @return The stripped name, or @p name unchanged when no transform applied.
inline std::string stripDynamicSymbolLeadingUnderscores(const std::string &name) {
    size_t i = 0;
    while (i < name.size() && name[i] == '_')
        ++i;

    size_t end = name.size();
    static constexpr const char *kDarwinExtSuffix = "$DARWIN_EXTSN";
    static constexpr size_t kDarwinExtSuffixLen = 13;
    if (end >= i + kDarwinExtSuffixLen &&
        name.compare(end - kDarwinExtSuffixLen, kDarwinExtSuffixLen, kDarwinExtSuffix) == 0) {
        end -= kDarwinExtSuffixLen;
    }

    return (i == 0 && end == name.size()) ? name : name.substr(i, end - i);
}

/// @brief Test whether @p name (or its stripped form) starts with any of the
///        null-terminated prefix list @p prefixes (terminated by a nullptr entry).
inline bool dynamicSymbolHasPrefix(const std::string &name, const char *const *prefixes) {
    const std::string stripped = stripDynamicSymbolLeadingUnderscores(name);
    for (const char *const *p = prefixes; p != nullptr && *p != nullptr; ++p) {
        const std::string prefix(*p);
        if (name.rfind(prefix, 0) == 0 || stripped.rfind(prefix, 0) == 0)
            return true;
    }
    return false;
}

/// @brief Recognise Itanium-mangled C++ runtime/library symbols (`std::*`, RTTI, etc.).
/// @details These symbols are supplied by the platform C++ runtime rather than
///          Viper's own archives. The matcher stays narrow to known `std::*`,
///          RTTI/vtable, operator new/delete, and exception-runtime prefixes so
///          arbitrary user-defined C++ mangled names are not treated as system imports.
inline bool isKnownCppRuntimeDynamicSymbol(const std::string &name) {
    static const char *const kCppRuntimePrefixes[] = {
        "ZNSt",
        "ZNKSt",
        "ZNKRSt",
        "ZNSi",
        "ZNSo",
        "ZTINSt",
        "ZTSNSt",
        "ZTVNSt",
        "ZTTNSt",
        "ZSt",
        "ZTISt",
        "ZTSSt",
        "ZTVSt",
        "ZTTSt",
        // libc++abi base RTTI vtables / type-infos (__cxxabiv1::*). Pulled in
        // by embedded C++ editor services; supplied by libc++abi, which
        // libc++.1.dylib re-exports on macOS.
        "ZTVN10__cxxabiv",
        "ZTIN10__cxxabiv",
        "ZTSN10__cxxabiv",
        // Fundamental-type RTTI (typeinfo for void/int/double/...) emitted by
        // C++ editor-service code; supplied by libc++abi. The single trailing
        // builtin-code letter keeps these from matching class type-infos
        // (`ZTIN...` / `ZTI<len>...`), which the editor-service closure defines
        // itself.
        "ZTIv",
        "ZTIb",
        "ZTIc",
        "ZTIa",
        "ZTIh",
        "ZTIs",
        "ZTIt",
        "ZTIi",
        "ZTIj",
        "ZTIl",
        "ZTIm",
        "ZTIx",
        "ZTIy",
        "ZTIf",
        "ZTId",
        "ZTIe",
        "ZTIw",
        "ZTIn",
        "ZTIo",
        "ZTIDn",
        "ZTIPv",
        "ZTIPKc",
        "ZTIPc",
        "Zda",
        "Zdl",
        "Zna",
        "Znw",
        "cxa_",
        "dynamic_cast",
        "gxx_personality_",
        nullptr,
    };
    return dynamicSymbolHasPrefix(name, kCppRuntimePrefixes);
}

/// @brief Recognise compiler runtime helper symbols supplied by libgcc_s/compiler-rt.
inline bool isKnownCompilerRuntimeDynamicSymbol(const std::string &name, LinkPlatform platform) {
    const std::string stripped = stripDynamicSymbolLeadingUnderscores(name);

    static const char *const kCompilerRuntimeExact[] = {
        "addtf3",      "divtf3",     "eqtf2",     "extenddftf2", "fixtfdi",
        "fixtfsi",     "fixunstfdi", "floatditf", "floatsitf",   "floatunditf",
        "floatunsitf", "getf2",      "gttf2",     "letf2",       "lttf2",
        "multf3",      "netf2",      "subtf3",    "trunctfdf2",  nullptr,
    };
    for (const char *const *p = kCompilerRuntimeExact; p && *p; ++p) {
        if (stripped == *p)
            return true;
    }
    return false;
}

/// Known system/dynamic library symbols that won't be present in runtime
/// archives and may be resolved through platform loader metadata instead.
inline bool isKnownDynamicSymbol(const std::string &name, LinkPlatform platform) {
    const std::string stripped = stripDynamicSymbolLeadingUnderscores(name);

    if (isKnownCompilerRuntimeDynamicSymbol(name, platform))
        return true;

    if ((platform == LinkPlatform::macOS || platform == LinkPlatform::Linux) && stripped == "exp10")
        return true;

    static const char *const kDynSymExact[] = {
        // libSystem ctype data/helpers referenced by libc++ <locale>/<sstream>
        // (pulled in by the embedded C++ frontend). Both raw and de-underscored
        // forms are listed so the match holds regardless of Mach-O mangling.
        "__maskrune",
        "maskrune",
        "_DefaultRuneLocale",
        "DefaultRuneLocale",
        // libm (<cmath>/<charconv>) referenced by the embedded C++ frontend's
        // libc++ instantiations. All are libSystem/libm exports.
        "modf",
        "frexp",
        "ldexp",
        "scalbn",
        "copysign",
        "fmod",
        "hypot",
        "fma",
        "nearbyint",
        "rint",
        "trunc",
        "round",
        "lround",
        "llround",
        "lrint",
        "lrintf",
        "llrint",
        "fmin",
        "fmax",
        "fdim",
        "remainder",
        "remquo",
        "cbrt",
        "expm1",
        "log1p",
        "exp2",
        "log2",
        "tgamma",
        "lgamma",
        "sinh",
        "cosh",
        "tanh",
        "asinh",
        "acosh",
        "atanh",
        "asin",
        "acos",
        "atan",
        "atan2",
        "sin",
        "cos",
        "tan",
        "exp",
        "log",
        "log10",
        "pow",
        "sqrt",
        "ceil",
        "floor",
        "fabs",
        "wmemchr",
        "wmemcmp",
        "wmemcpy",
        "wmemmove",
        "wmemset",
        "wcslen",
        "wcscmp",
        "wcscpy",
        "wcsncmp",
        "printf",
        "fprintf",
        "sprintf",
        "snprintf",
        "vsnprintf",
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
        "strnlen",
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
        "atoll",
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
        "difftime",
        "gettimeofday",
        "gmtime_r",
        "localtime_r",
        "mktime",
        "strftime",
        "nanosleep",
        "usleep",
        "sleep",
        "open",
        "openat",
        "close",
        "read",
        "write",
        "lseek",
        "stat",
        "fstat",
        "fstatat",
        "lstat",
        "chmod",
        "fchmod",
        "mkdir",
        "mkdirat",
        "rmdir",
        "link",
        "unlink",
        "unlinkat",
        "remove",
        "rename",
        "renameat",
        "getcwd",
        "chdir",
        "socket",
        "bind",
        "listen",
        "accept",
        "connect",
        "send",
        "recv",
        "recvmsg",
        "select",
        "poll",
        "setsockopt",
        "getsockopt",
        "getsockname",
        "closesocket",
        "ioctlsocket",
        "recvfrom",
        "sendto",
        "shutdown",
        "WSAStartup",
        "WSACleanup",
        "WSAGetLastError",
        "htons",
        "htonl",
        "ntohs",
        "ntohl",
        "inet_ntop",
        "inet_pton",
        "getaddrinfo",
        "freeaddrinfo",
        "getifaddrs",
        "freeifaddrs",
        "getnameinfo",
        "GetAdaptersAddresses",
        "GetComputerNameA",
        "GetComputerNameW",
        "GetFileSizeEx",
        "GetClientRect",
        "GlobalMemoryStatusEx",
        "GetUserNameA",
        "GetUserNameW",
        "ResetEvent",
        "SetEvent",
        "D3D11CreateDevice",
        "D3D11CreateDeviceAndSwapChain",
        "D3DCompile",
        "D3DCompile2",
        "D3DCompileFromFile",
        "D3DReflect",
        "CertAddEncodedCertificateToStore",
        "CertCloseStore",
        "CertCreateCertificateContext",
        "CertCreateCertificateChainEngine",
        "CertFreeCertificateChain",
        "CertFreeCertificateChainEngine",
        "CertFreeCertificateContext",
        "CertGetCertificateChain",
        "CertOpenStore",
        "CertVerifyCertificateChainPolicy",
        "CryptAcquireCertificatePrivateKey",
        "CryptStringToBinaryA",
        "pthread_create",
        "pthread_join",
        "pthread_detach",
        "pthread_mutex_init",
        "pthread_mutex_lock",
        "pthread_mutex_trylock",
        "pthread_mutex_unlock",
        "pthread_mutex_destroy",
        "pthread_cond_init",
        "pthread_cond_wait",
        "pthread_cond_signal",
        "pthread_cond_broadcast",
        "pthread_cond_destroy",
        "pthread_cond_timedwait",
        "pthread_cond_timedwait_relative_np",
        "pthread_condattr_init",
        "pthread_condattr_setclock",
        "pthread_condattr_destroy",
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
        "posix_spawnp",
        "posix_spawn_file_actions_addchdir",
        "posix_spawn_file_actions_addchdir_np",
        "posix_spawn_file_actions_addclose",
        "posix_spawn_file_actions_adddup2",
        "posix_spawn_file_actions_destroy",
        "posix_spawn_file_actions_init",
        "sysconf",
        "getpid",
        "geteuid",
        "getgid",
        "getegid",
        "getuid",
        "kill",
        "signal",
        "sigaction",
        "sigaltstack",
        "sigemptyset",
        "sigpending",
        "sigprocmask",
        "sigwait",
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
        "isxdigit",
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
        "regcomp",
        "regexec",
        "regfree",
        "regerror",
        "vfprintf",
        "setvbuf",
        "setbuf",
        "freopen",
        "tmpfile",
        "tmpnam",
        "mkstemp",
        "mkdtemp",
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
        "fsync",
        "ioctl",
        "realpath",
        "uname",
        "kqueue",
        "kevent",
        "tcgetattr",
        "tcsetattr",
        "fileno",
        "isatty",
        "ttyname",
        "gethostname",
        "sched_yield",
        "clock_gettime",
        "readlink",
        "strtof",
        "rint",
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
        "atan2l",
        "sinf",
        "cosf",
        "tanf",
        "asinf",
        "acosf",
        "atanf",
        "atan2f",
        // Darwin libSystem helper emitted by Clang when neighbouring sinf/cosf
        // calls are combined, for example circular progress rendering.
        "sincosf_stret",
        "cbrt",
        "cbrtf",
        "sqrt",
        "sqrtl",
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
        "ceill",
        "ceilf",
        "floor",
        "floorl",
        "floorf",
        "round",
        "roundf",
        "fmod",
        "fmodl",
        "fmodf",
        "fabs",
        "fabsf",
        "fmin",
        "fminf",
        "fmax",
        "fmaxl",
        "fmaxf",
        "cosl",
        "sinl",
        "copysign",
        "copysignf",
        "trunc",
        "truncf",
        "_NSGetExecutablePath",
        "_NSGetArgc",
        "_NSGetArgv",
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
        "__strncat_chk",
        "__strcpy_chk",
        "__strncpy_chk",
        "__vsnprintf_chk",
        "__darwin_check_fd_set_overflow",
        "select$DARWIN_EXTSN",
        "mach_timebase_info",
        "mach_absolute_time",
        "mach_task_self_",
        "mach_host_self",
        "sysctlbyname",
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
        "arc4random_buf",
        "backtrace",
        "backtrace_symbols_fd",
        "closedir",
        "opendir",
        "fdopendir",
        "dirfd",
        "readdir",
        "environ",
        "stdin",
        "stdout",
        "stderr",
        "__errno_location",
        "__assert_fail",
        "__ctype_b_loc",
        "__isoc23_strtol",
        "__isoc23_strtoll",
        "__isoc99_sscanf",
        "fopen64",
        "fseeko64",
        "ftello64",
        "getrandom",
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
        "GetFullPathNameA",
        "BCryptGenRandom",
        "BCryptDestroyKey",
        "BCryptVerifySignature",
        "XInputGetState",
        "XInputSetState",
        "__acrt_iob_func",
        "__local_stdio_printf_options",
        "__local_stdio_scanf_options",
        "__stdio_common_vfprintf",
        "__stdio_common_vsprintf",
        "__stdio_common_vsprintf_s",
        "_vfprintf_l",
        "_vsscanf_l",
        "__C_specific_handler",
        "__C_specific_handler_noexcept",
        "__current_exception",
        "__current_exception_context",
        "_CxxThrowException",
        "__CxxFrameHandler3",
        "__CxxFrameHandler4",
        "__RTDynamicCast",
        "__std_exception_copy",
        "__std_exception_destroy",
        "__std_type_info_compare",
        "__security_check_cookie",
        "__security_init_cookie",
        "__security_pop_cookie",
        "__security_push_cookie",
        "__GSHandlerCheck",
        "__GSHandlerCheck_EH4",
        "__chkstk",
        "_Avx2WmemEnabled",
        "dclass",
        "_purecall",
        "__RTC_memset",
        "_setjmpex",
        "_byteswap_uint64",
        "_InterlockedCompareExchange",
        "_InterlockedCompareExchange64",
        "_InterlockedCompareExchangePointer",
        "_InterlockedDecrement",
        "_InterlockedExchange",
        "_InterlockedExchange64",
        "_InterlockedExchange8",
        "_InterlockedExchangeAdd",
        "_InterlockedExchangeAdd64",
        "_InterlockedIncrement64",
        "_InterlockedOr",
        "TryEnterCriticalSection",
        "CoTaskMemFree",
        "DragQueryFileW",
        "_open_osfhandle",
        "_cexit",
        "_configure_narrow_argv",
        "_crt_at_quick_exit",
        "_crt_atexit",
        "_execute_onexit_table",
        "_initialize_narrow_environment",
        "_initialize_onexit_table",
        "_register_onexit_function",
        "_seh_filter_dll",
        "terminate",
        "_CrtDbgReport",
        "_CrtDbgReportW",
        "abs",
        "fmaxl",
        "fminl",
        "rand_s",
        "strcpy_s",
        "strcat_s",
        "sysinfo",
        "_wcsnicmp",
        "_wchmod",
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
        "kSec",
        "CC_",
        "mach_",
        "task_",
        "host_",
        "vm_",
        "kern_",
        "Unwind_",
        "dispatch_",
        "MTL",
        "AudioObject",
        "AudioDevice",
    };
    static const char *const kLinuxDynPrefixes[] = {
        "Unwind_",
        "X",
        "__isoc23_",
        "__isoc99_",
        "inotify_",
        "pthread_",
        "snd_",
    };
    static const char *const kWindowsDynPrefixes[] = {
        "__imp_",
        "__vcrt_",
        "_Cnd_",
        "_Init_thread_",
        "_Mtx_",
        "_Query_perf_",
        "_Smtx_",
        "_Thrd_",
        "__std_",
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

    if ((platform == LinkPlatform::macOS || platform == LinkPlatform::Linux) &&
        isKnownCppRuntimeDynamicSymbol(name))
        return true;

    if (platform == LinkPlatform::Windows) {
        if (isMsvcThreadSafeStaticGuardSymbol(name) || isMsvcThreadSafeStaticGuardSymbol(stripped))
            return false;
        if (name.find("@std@@") != std::string::npos ||
            stripped.find("@std@@") != std::string::npos || name.rfind("??", 0) == 0 ||
            stripped.rfind("??", 0) == 0 || name.rfind("?_", 0) == 0 ||
            stripped.rfind("?_", 0) == 0)
            return true;
    }

    return false;
}

} // namespace viper::codegen::linker
