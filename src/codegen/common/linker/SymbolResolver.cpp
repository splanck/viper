//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/SymbolResolver.cpp
// Purpose: Implementation of global symbol resolution.
//          Iteratively extracts archive members until all symbols resolved.
// Key invariants:
//   - Strong > Weak > Undefined precedence
//   - Multiple strong definitions of the same symbol = linker error
//   - Archives re-scanned until fixed point (handles cross-archive deps)
// Links: codegen/common/linker/SymbolResolver.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/SymbolResolver.hpp"
#include "codegen/common/linker/NameMangling.hpp"

#include <sstream>

namespace viper::codegen::linker {

static bool isKnownDynamicSymbol(const std::string &name);
static bool preferArchiveDefinition(const std::string &name);

/// Add symbols from a single object file into the global table.
/// @param obj        The object file.
/// @param objIdx     Its index in allObjects.
/// @param globalSyms The global symbol table to update.
/// @param undefined  Set of currently undefined symbol names.
/// @param err        Error stream.
/// @return false if multiply-defined symbol error.
static bool addObjSymbols(const ObjFile &obj,
                          size_t objIdx,
                          std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                          std::unordered_set<std::string> &undefined,
                          std::ostream &err) {
    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.name.empty())
            continue;

        if (sym.binding == ObjSymbol::Undefined) {
            // Only add to undefined set if not already defined.
            auto it = globalSyms.find(sym.name);
            if (it == globalSyms.end() || it->second.binding == GlobalSymEntry::Undefined)
                undefined.insert(sym.name);
            if (it == globalSyms.end()) {
                GlobalSymEntry e;
                e.name = sym.name;
                e.binding = GlobalSymEntry::Undefined;
                globalSyms[sym.name] = std::move(e);
            }
            continue;
        }

        if (sym.binding == ObjSymbol::Local)
            continue; // Locals don't participate in global resolution.

        // COFF/MSVC archives often contain helper stubs or repeated CRT import
        // shims for symbols that still need to be resolved dynamically. Let
        // those remain external imports instead of treating archive-local
        // definitions as link-time providers.
        if (isKnownDynamicSymbol(sym.name) && !preferArchiveDefinition(sym.name))
            continue;

        const bool isWeak = (sym.binding == ObjSymbol::Weak);
        auto it = globalSyms.find(sym.name);
        if (it == globalSyms.end()) {
            // New symbol.
            GlobalSymEntry e;
            e.name = sym.name;
            e.binding = isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global;
            e.objIndex = objIdx;
            e.secIndex = sym.sectionIndex;
            e.offset = sym.offset;
            globalSyms[sym.name] = std::move(e);
            undefined.erase(sym.name);
        } else {
            auto &existing = it->second;
            if (existing.binding == GlobalSymEntry::Undefined) {
                // Was undefined, now defined.
                existing.binding = isWeak ? GlobalSymEntry::Weak : GlobalSymEntry::Global;
                existing.objIndex = objIdx;
                existing.secIndex = sym.sectionIndex;
                existing.offset = sym.offset;
                undefined.erase(sym.name);
            } else if (existing.binding == GlobalSymEntry::Weak && !isWeak) {
                // Strong overrides weak.
                existing.binding = GlobalSymEntry::Global;
                existing.objIndex = objIdx;
                existing.secIndex = sym.sectionIndex;
                existing.offset = sym.offset;
            } else if (existing.binding == GlobalSymEntry::Global && !isWeak) {
                if (preferArchiveDefinition(sym.name))
                    continue;
                err << "error: multiply defined symbol '" << sym.name << "' in " << obj.name
                    << "\n";
                return false;
            }
            // Weak doesn't override anything.
        }
    }
    return true;
}

/// Known system/dynamic library symbols that won't be in archives.
static bool isKnownDynamicSymbol(const std::string &name) {
    // Common C library and platform functions (exact matches).
    static const char *const kDynSymExact[] = {
        // C library
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
        "ftell",
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
        "pthread_create",
        "pthread_join",
        "pthread_mutex_init",
        "pthread_mutex_lock",
        "pthread_mutex_unlock",
        "pthread_mutex_destroy",
        "pthread_cond_init",
        "pthread_cond_wait",
        "pthread_cond_signal",
        "pthread_cond_broadcast",
        "pthread_cond_destroy",
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
        "perror",
        "sscanf",
        "vfprintf",
        "setvbuf",
        "setbuf",
        "freopen",
        "tmpfile",
        "tmpnam",
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
        "fileno",
        "isatty",
        "ttyname",
        // POSIX scheduling / threads
        "sched_yield",
        // Math
        "sin",
        "cos",
        "tan",
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
        // macOS specific
        "_NSGetExecutablePath",
        "dyld_stub_binder",
        "_tlv_atexit",
        "_tlv_bootstrap",
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
        // ObjC runtime
        "sel_registerName",
        "sel_getName",
        "_objc_empty_cache",
        "_objc_empty_vtable",
        // Windows CRT
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
        "rt_audio_shutdown",
    };

    for (const char *sym : kDynSymExact) {
        if (name == sym)
            return true;
    }

    // Prefix-based matching for platform framework symbols.
    static const char *const kDynSymPrefixes[] = {
        // C library internal symbols
        "__", // __libc_start_main, __stack_chk_fail, etc.
        // macOS CoreFoundation
        "CF",
        "kCF",
        // macOS CoreGraphics
        "CG",
        "kCG",
        // macOS AppKit / Cocoa / Foundation
        "NS",
        // macOS IOKit
        "IOKit",
        "IOHID",
        "IOService",
        "IORegistryEntry",
        // macOS ObjC runtime
        "objc_",
        "OBJC_",
        "_objc_",
        // macOS UniformTypeIdentifiers
        "UTType",
        "UTCopy",
        // macOS AudioToolbox
        "AudioQueue",
        "AudioServices",
        "AudioComponent",
        // macOS Security
        "Sec",
        // macOS kernel/Mach
        "mach_",
        "task_",
        "host_",
        "vm_",
        "kern_",
        "__imp_",
        // macOS dispatch (GCD)
        "dispatch_",
        // macOS Metal
        "MTL",
        // macOS CoreAudio
        "AudioObject",
        "AudioDevice",
    };

    for (const char *prefix : kDynSymPrefixes) {
        size_t plen = 0;
        while (prefix[plen] != '\0')
            ++plen;
        if (name.size() >= plen && name.compare(0, plen, prefix) == 0)
            return true;
    }

    return false;
}

/// Runtime archives provide Windows compatibility shims for a small set of
/// formatting functions. Those definitions must participate in archive
/// resolution instead of being forced down the dynamic-import path.
static bool preferArchiveDefinition(const std::string &name) {
    if (name == "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9")
        return false;

    if (name == "fprintf" || name == "snprintf" || name == "vsnprintf" ||
        name == "mainCRTStartup" || name == "WinMainCRTStartup" || name == "wmainCRTStartup" ||
        name == "wWinMainCRTStartup" || name == "__security_check_cookie" ||
        name == "__security_init_cookie" || name == "__GSHandlerCheck" || name == "__chkstk")
        return true;

    return name.find("__scrt_") != std::string::npos ||
           name.find("__local_stdio_printf_options") != std::string::npos ||
           name.rfind("__xi_", 0) == 0 ||
           name.rfind("__xc_", 0) == 0 || name.rfind("__xl_", 0) == 0 ||
           name.rfind("__dyn_tls_", 0) == 0 || name.rfind("__tls_", 0) == 0;
}

bool resolveSymbols(const std::vector<ObjFile> &initialObjects,
                    std::vector<Archive> &archives,
                    std::unordered_map<std::string, GlobalSymEntry> &globalSyms,
                    std::vector<ObjFile> &allObjects,
                    std::unordered_set<std::string> &dynamicSyms,
                    std::ostream &err) {
    // Start with initial objects.
    allObjects = initialObjects;
    std::unordered_set<std::string> undefined;

    for (size_t i = 0; i < allObjects.size(); ++i) {
        if (!addObjSymbols(allObjects[i], i, globalSyms, undefined, err))
            return false;
    }

    // Iteratively resolve from archives until fixed point.
    std::unordered_set<size_t> extractedMembers; // Track (archiveIdx << 32) | memberIdx.
    constexpr size_t kMaxResolveIterations = 1000;
    size_t iteration = 0;
    bool changed = true;
    while (changed) {
        if (++iteration > kMaxResolveIterations) {
            err << "error: symbol resolution exceeded " << kMaxResolveIterations << " iterations\n";
            return false;
        }
        changed = false;
        // Snapshot undefined set — addObjSymbols modifies it, invalidating iterators.
        std::vector<std::string> undefSnapshot(undefined.begin(), undefined.end());
        for (size_t ai = 0; ai < archives.size(); ++ai) {
            auto &ar = archives[ai];
            for (const auto &undef : undefSnapshot) {
                if (isKnownDynamicSymbol(undef) && !preferArchiveDefinition(undef))
                    continue;

                // Mach-O archives use underscore-prefixed symbol names.
                auto symIt = findWithMachoFallback(ar.symbolIndex, undef);
                if (symIt == ar.symbolIndex.end())
                    continue;

                size_t memberIdx = symIt->second;
                size_t key = (ai << 32) | memberIdx;
                if (extractedMembers.count(key))
                    continue;

                // Extract and parse this member.
                extractedMembers.insert(key);
                auto memberData = extractMember(ar, ar.members[memberIdx]);
                if (memberData.empty())
                    continue;

                ObjFile memberObj;
                std::ostringstream memberErr;
                if (!readObjFile(memberData.data(),
                                 memberData.size(),
                                 ar.path + "(" + ar.members[memberIdx].name + ")",
                                 memberObj,
                                 memberErr)) {
                    err << memberErr.str();
                    continue;
                }

                size_t newIdx = allObjects.size();
                allObjects.push_back(std::move(memberObj));
                if (!addObjSymbols(allObjects[newIdx], newIdx, globalSyms, undefined, err))
                    return false;
                changed = true;
            }
        }
    }

    // Mark remaining undefined as dynamic.
    // Symbols matching known dynamic patterns are silently accepted.
    // Others are warned — they'll likely be resolved by dyld, but if not,
    // the program will crash at launch with a clear dyld error.
    for (const auto &undef : undefined) {
        auto it = globalSyms.find(undef);
        if (it != globalSyms.end() && it->second.binding != GlobalSymEntry::Undefined)
            continue; // Was resolved during iteration.

        dynamicSyms.insert(undef);
        if (it != globalSyms.end())
            it->second.binding = GlobalSymEntry::Dynamic;

        if (!isKnownDynamicSymbol(undef) || preferArchiveDefinition(undef))
            err << "warning: treating undefined symbol '" << undef << "' as dynamic\n";
    }

    return true;
}

} // namespace viper::codegen::linker
