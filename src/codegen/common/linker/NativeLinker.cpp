//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/NativeLinker.cpp
// Purpose: Top-level native linker implementation.
// Key invariants:
//   - Pipeline: parse .o → parse archives → resolve symbols → generate stubs →
//     merge sections → branch trampolines → apply relocations → write executable
//   - For macOS: generates GOT entries and stub trampolines for dynamic symbols,
//     uses non-lazy binding (dyld fills GOT at load time)
//   - Falls back gracefully with clear error messages
// Links: codegen/common/linker/NativeLinker.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/NativeLinker.hpp"

#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/BranchTrampoline.hpp"
#include "codegen/common/linker/DeadStripPass.hpp"
#include "codegen/common/linker/DynamicSymbolPolicy.hpp"
#include "codegen/common/linker/DynStubGen.hpp"
#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/ICF.hpp"
#include "codegen/common/linker/MachOExeWriter.hpp"
#include "codegen/common/linker/NameMangling.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/PeExeWriter.hpp"
#include "codegen/common/linker/RelocApplier.hpp"
#include "codegen/common/linker/RelocConstants.hpp"
#include "codegen/common/linker/SectionMerger.hpp"
#include "codegen/common/linker/StringDedup.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

namespace {

struct WindowsImportPlan {
    ObjFile obj;
    std::vector<DllImport> imports;
};

struct MacImportRule {
    const char *dylibPath;
    const char *const *prefixes;
    const char *const *exactSyms;
};

struct MacImportPlan {
    std::vector<DylibImport> dylibs;
    std::unordered_map<std::string, uint32_t> symOrdinals;
};

void registerSyntheticSymbols(const ObjFile &obj,
                              size_t objIdx,
                              std::unordered_map<std::string, GlobalSymEntry> &globalSyms) {
    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined ||
            sym.name.empty())
            continue;

        GlobalSymEntry e;
        e.name = sym.name;
        e.binding = GlobalSymEntry::Global;
        e.objIndex = objIdx;
        e.secIndex = sym.sectionIndex;
        e.offset = sym.offset;
        globalSyms[sym.name] = std::move(e);
    }
}

void removeDynamicSymbol(const char *name, std::unordered_set<std::string> &dynamicSyms) {
    dynamicSyms.erase(name);
}

ObjFile makeUndefinedRootObject(const ObjFile &userObj, const std::string &symbolName) {
    ObjFile root;
    root.name = "<entry-root>";
    root.format = userObj.format;
    root.is64bit = userObj.is64bit;
    root.isLittleEndian = userObj.isLittleEndian;
    root.machine = userObj.machine;
    root.sections.push_back(ObjSection{});
    root.symbols.push_back(ObjSymbol{});

    ObjSymbol sym;
    sym.name = symbolName;
    sym.binding = ObjSymbol::Undefined;
    root.symbols.push_back(std::move(sym));
    return root;
}

std::string stripImpPrefix(const std::string &name) {
    if (name.rfind("__imp_", 0) == 0)
        return name.substr(6);
    return name;
}

bool usesDebugWindowsRuntime(const std::vector<std::string> &archivePaths) {
    for (const auto &path : archivePaths) {
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.find("\\debug\\") != std::string::npos ||
            lower.find("/debug/") != std::string::npos ||
            lower.rfind("msvcrtd.lib") != std::string::npos ||
            lower.rfind("ucrtd.lib") != std::string::npos ||
            lower.rfind("vcruntimed.lib") != std::string::npos)
            return true;
    }
    return false;
}

std::string stripLeadingUnderscores(const std::string &name) {
    size_t i = 0;
    while (i < name.size() && name[i] == '_')
        ++i;
    return (i > 0) ? name.substr(i) : name;
}

bool isObjcClassLookupSymbol(const std::string &name) {
    const std::string stripped = stripLeadingUnderscores(name);
    return stripped.rfind("OBJC_CLASS_$_", 0) == 0 || stripped.rfind("OBJC_METACLASS_$_", 0) == 0;
}

bool isObjcFrameworkTypeSymbol(const std::string &name) {
    const std::string stripped = stripLeadingUnderscores(name);
    return stripped.rfind("OBJC_CLASS_$_", 0) == 0 || stripped.rfind("OBJC_METACLASS_$_", 0) == 0 ||
           stripped.rfind("OBJC_EHTYPE_$_", 0) == 0;
}

std::string normalizeMacFrameworkSymbol(const std::string &name) {
    std::string normalized = stripLeadingUnderscores(name);
    static constexpr const char *kObjcPrefixes[] = {
        "OBJC_CLASS_$_",
        "OBJC_METACLASS_$_",
        "OBJC_EHTYPE_$_",
    };
    for (const char *prefix : kObjcPrefixes) {
        if (normalized.rfind(prefix, 0) == 0)
            return normalized.substr(std::char_traits<char>::length(prefix));
    }
    return normalized;
}

const char *platformName(LinkPlatform platform) {
    switch (platform) {
        case LinkPlatform::Linux:
            return "Linux";
        case LinkPlatform::macOS:
            return "macOS";
        case LinkPlatform::Windows:
            return "Windows";
    }
    return "unknown";
}

const char *archName(LinkArch arch) {
    switch (arch) {
        case LinkArch::X86_64:
            return "x86_64";
        case LinkArch::AArch64:
            return "AArch64";
    }
    return "unknown";
}

static constexpr const char *kMacNoMatches[] = {nullptr};
static constexpr const char *kMacLibSystemPrefixes[] = {
    "Block_",
    "NSConcrete",
    "dispatch_",
    "mach_",
    "task_",
    "host_",
    "vm_",
    "kern_",
    "os_",
    nullptr,
};
static constexpr const char *kMacLibSystemExact[] = {
    "_NSGetExecutablePath",
    "_Block_copy",
    "_Block_release",
    "_Block_object_assign",
    "_Block_object_dispose",
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
    nullptr,
};
static constexpr const char *kMacCoreFoundationPrefixes[] = {"CF", "kCF", nullptr};
static constexpr const char *kMacFoundationPrefixes[] = {
    "NSString",
    "NSAttributedString",
    "NSArray",
    "NSDictionary",
    "NSSet",
    "NSMutable",
    "NSData",
    "NSError",
    "NSURL",
    "NSBundle",
    "NSFileManager",
    "NSDate",
    "NSLocale",
    "NSProcessInfo",
    "NSRunLoop",
    "NSTimer",
    "NSThread",
    "NSNotification",
    "NSIndexSet",
    "NSCharacterSet",
    "NSPredicate",
    "NSCoder",
    "NSJSON",
    "NSUserDefaults",
    "NSAutoreleasePool",
    "NSObject",
    "NSDefaultRunLoop",
    nullptr,
};
static constexpr const char *kMacFoundationExact[] = {
    "NSLog",
    "NSSearchPathForDirectoriesInDomains",
    nullptr,
};
static constexpr const char *kMacAppKitPrefixes[] = {
    "NSApp",
    "NSApplication",
    "NSWindow",
    "NSView",
    "NSColor",
    "NSEvent",
    "NSCursor",
    "NSGraphicsContext",
    "NSOpenGL",
    "NSMenu",
    "NSMenuItem",
    "NSScreen",
    "NSImage",
    "NSFont",
    "NSResponder",
    "NSPanel",
    "NSPasteboard",
    "NSText",
    "NSControl",
    "NSButton",
    "NSScroll",
    "NSTable",
    "NSOutline",
    "NSBezierPath",
    "NSMake",
    "NSRect",
    "NSPoint",
    "NSSize",
    "NSDrag",
    "NSBackingStore",
    "NSWindowStyle",
    "NSApplicationActivationPolicy",
    nullptr,
};
static constexpr const char *kMacCoreGraphicsPrefixes[] = {"CG", "kCG", nullptr};
static constexpr const char *kMacIOKitPrefixes[] = {
    "IOKit",
    "IOHID",
    "IOService",
    "IORegistryEntry",
    nullptr,
};
static constexpr const char *kMacObjCPrefixes[] = {"objc_", "OBJC_", "_objc_", nullptr};
static constexpr const char *kMacObjCExact[] = {"sel_registerName", "sel_getName", nullptr};
static constexpr const char *kMacUTIPrefixes[] = {"UTType", "UTCopy", nullptr};
static constexpr const char *kMacAudioToolboxPrefixes[] = {
    "AudioQueue",
    "AudioServices",
    "AudioComponent",
    nullptr,
};
static constexpr const char *kMacCoreAudioPrefixes[] = {"AudioObject", "AudioDevice", nullptr};
static constexpr const char *kMacMetalPrefixes[] = {"MTLCreate", "MTL", nullptr};
static constexpr const char *kMacQuartzCorePrefixes[] = {
    "CAMetalLayer",
    "CATransaction",
    "CALayer",
    "CAAnimation",
    "CAMediaTiming",
    nullptr,
};
static constexpr const char *kMacSecurityPrefixes[] = {"Sec", nullptr};

static constexpr MacImportRule kMacImportRules[] = {
    {"/usr/lib/libSystem.B.dylib", kMacLibSystemPrefixes, kMacLibSystemExact},
    {"/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation",
     kMacCoreFoundationPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation",
     kMacFoundationPrefixes,
     kMacFoundationExact},
    {"/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit",
     kMacAppKitPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics",
     kMacCoreGraphicsPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",
     kMacIOKitPrefixes,
     kMacNoMatches},
    {"/usr/lib/libobjc.A.dylib", kMacObjCPrefixes, kMacObjCExact},
    {"/System/Library/Frameworks/UniformTypeIdentifiers.framework/Versions/A/"
     "UniformTypeIdentifiers",
     kMacUTIPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox",
     kMacAudioToolboxPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/CoreAudio.framework/Versions/A/CoreAudio",
     kMacCoreAudioPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/Metal.framework/Versions/A/Metal",
     kMacMetalPrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore",
     kMacQuartzCorePrefixes,
     kMacNoMatches},
    {"/System/Library/Frameworks/Security.framework/Versions/A/Security",
     kMacSecurityPrefixes,
     kMacNoMatches},
};

bool macSymbolMatchesRule(const std::string &sym, const MacImportRule &rule) {
    const std::string stripped = stripLeadingUnderscores(sym);
    const std::string normalized = normalizeMacFrameworkSymbol(sym);

    for (const char *const *e = rule.exactSyms; e != nullptr && *e != nullptr; ++e) {
        if (sym == *e || stripped == *e || normalized == *e)
            return true;
    }
    for (const char *const *p = rule.prefixes; p != nullptr && *p != nullptr; ++p) {
        if (sym.find(*p) == 0 || stripped.find(*p) == 0 || normalized.find(*p) == 0)
            return true;
    }
    return false;
}

const MacImportRule *findMacImportRule(const std::string &sym) {
    for (const auto &rule : kMacImportRules) {
        if (isObjcFrameworkTypeSymbol(sym) &&
            std::string(rule.dylibPath) == "/usr/lib/libobjc.A.dylib") {
            continue;
        }
        if (macSymbolMatchesRule(sym, rule))
            return &rule;
    }
    return nullptr;
}

bool isMacFrameworkLikeSymbol(const std::string &sym) {
    static constexpr const char *kFrameworkPrefixes[] = {
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
        "AudioObject",
        "AudioDevice",
        "MTL",
        "Sec",
        "CAMetalLayer",
        "CATransaction",
        "CALayer",
        nullptr,
    };

    const std::string stripped = stripLeadingUnderscores(sym);
    const std::string normalized = normalizeMacFrameworkSymbol(sym);
    for (const char *const *p = kFrameworkPrefixes; *p != nullptr; ++p) {
        if (sym.find(*p) == 0 || stripped.find(*p) == 0 || normalized.find(*p) == 0)
            return true;
    }
    return false;
}

uint32_t ensureMacDylibOrdinal(const char *path,
                               MacImportPlan &plan,
                               std::unordered_map<std::string, uint32_t> &pathToOrdinal) {
    auto it = pathToOrdinal.find(path);
    if (it != pathToOrdinal.end())
        return it->second;

    plan.dylibs.push_back({path});
    const uint32_t ordinal = static_cast<uint32_t>(plan.dylibs.size());
    pathToOrdinal.emplace(path, ordinal);
    return ordinal;
}

bool planMacImports(const std::unordered_set<std::string> &dynamicSyms,
                    MacImportPlan &plan,
                    std::ostream &err) {
    static constexpr const char *kCocoaPath =
        "/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa";

    std::unordered_map<std::string, uint32_t> pathToOrdinal;
    ensureMacDylibOrdinal("/usr/lib/libSystem.B.dylib", plan, pathToOrdinal);

    std::vector<std::string> sortedSyms(dynamicSyms.begin(), dynamicSyms.end());
    std::sort(sortedSyms.begin(), sortedSyms.end());

    for (const auto &sym : sortedSyms) {
        if (const MacImportRule *rule = findMacImportRule(sym)) {
            const uint32_t ordinal = ensureMacDylibOrdinal(rule->dylibPath, plan, pathToOrdinal);
            plan.symOrdinals[sym] = isObjcClassLookupSymbol(sym) ? 0 : ordinal;
            continue;
        }

        const std::string normalized = normalizeMacFrameworkSymbol(sym);
        if (isObjcClassLookupSymbol(sym) && normalized.rfind("NS", 0) == 0) {
            ensureMacDylibOrdinal(kCocoaPath, plan, pathToOrdinal);
            plan.symOrdinals[sym] = 0;
            continue;
        }

        if (!isMacFrameworkLikeSymbol(sym) && isKnownDynamicSymbol(sym, LinkPlatform::macOS)) {
            plan.symOrdinals[sym] = 1; // libSystem.B.dylib
            continue;
        }

        err << "error: macOS import symbol '" << sym
            << "' is unresolved but has no dylib mapping\n";
        return false;
    }

    return true;
}

bool dllForImport(const std::string &name, bool debugRuntime, std::string &dllName) {
    static const std::unordered_set<std::string> kernel32 = {
        "ExitProcess",
        "FreeLibrary",
        "GetCurrentProcessId",
        "GetCurrentThreadId",
        "GetEnvironmentVariableA",
        "GetLastError",
        "GetModuleHandleW",
        "GetProcAddress",
        "GetProcessHeap",
        "GetStartupInfoW",
        "GetStdHandle",
        "GetSystemTimeAsFileTime",
        "HeapAlloc",
        "HeapFree",
        "IsDebuggerPresent",
        "InitOnceExecuteOnce",
        "InitializeCriticalSection",
        "InitializeSListHead",
        "LeaveCriticalSection",
        "DeleteCriticalSection",
        "EnterCriticalSection",
        "MultiByteToWideChar",
        "RaiseException",
        "SetEnvironmentVariableA",
        "SetUnhandledExceptionFilter",
        "SetErrorMode",
        "SwitchToThread",
        "WriteFile",
        "GetTickCount64",
        "AddVectoredExceptionHandler",
        "GlobalAlloc",
        "GlobalFree",
        "GlobalLock",
        "GlobalUnlock",
        "InitializeSRWLock",
        "AcquireSRWLockExclusive",
        "AcquireSRWLockShared",
        "ReleaseSRWLockExclusive",
        "ReleaseSRWLockShared",
        "QueryPerformanceCounter",
        "VirtualQuery",
        "WideCharToMultiByte",
        "Beep",
        "CloseHandle",
        "CreateEventA",
        "CreateFileA",
        "CreatePipe",
        "CreateProcessA",
        "CreateThread",
        "FindClose",
        "FindFirstFileA",
        "FindNextFileA",
        "GetConsoleMode",
        "GetExitCodeProcess",
        "GetOverlappedResult",
        "InitializeConditionVariable",
        "QueryPerformanceFrequency",
        "ReadDirectoryChangesW",
        "ReadFile",
        "SetConsoleCP",
        "SetConsoleMode",
        "SetConsoleOutputCP",
        "SetEvent",
        "SetHandleInformation",
        "Sleep",
        "SleepConditionVariableCS",
        "WaitForMultipleObjects",
        "WaitForSingleObject",
        "WakeConditionVariable",
    };
    static const std::unordered_set<std::string> user32 = {
        "AdjustWindowRect",
        "BeginPaint",
        "ClientToScreen",
        "CloseClipboard",
        "CreateWindowExW",
        "DefWindowProcW",
        "DestroyWindow",
        "DispatchMessageW",
        "EmptyClipboard",
        "EndPaint",
        "GetClipboardData",
        "GetDC",
        "GetMonitorInfoA",
        "GetSystemMetrics",
        "GetWindowLongA",
        "GetWindowLongPtrA",
        "GetWindowRect",
        "IsClipboardFormatAvailable",
        "IsIconic",
        "IsZoomed",
        "LoadCursorA",
        "MonitorFromWindow",
        "OpenClipboard",
        "PeekMessageW",
        "RegisterClassExW",
        "RegisterClipboardFormatW",
        "ReleaseDC",
        "SetClipboardData",
        "SetCursor",
        "SetCursorPos",
        "SetForegroundWindow",
        "SetWindowLongA",
        "SetWindowLongPtrW",
        "SetWindowPos",
        "SetWindowTextW",
        "ShowCursor",
        "ShowWindow",
        "TranslateMessage",
        "UpdateWindow",
    };
    static const std::unordered_set<std::string> gdi32 = {
        "CreateCompatibleDC",
        "CreateDIBSection",
        "DeleteDC",
        "DeleteObject",
        "GetDeviceCaps",
        "GetStockObject",
        "SelectObject",
        "StretchBlt",
    };
    static const std::unordered_set<std::string> shell32 = {
        "DragAcceptFiles",
        "DragFinish",
        "DragQueryFileA",
    };
    static const std::unordered_set<std::string> ole32 = {
        "CoCreateInstance",
        "CoInitializeEx",
        "CoUninitialize",
    };
    static const std::unordered_set<std::string> xinput = {
        "XInputGetState",
        "XInputSetState",
    };
    static const std::unordered_set<std::string> advapi32 = {
        "CryptAcquireContextA",
        "CryptGenRandom",
        "CryptReleaseContext",
    };
    static const std::unordered_set<std::string> bcrypt = {"BCryptGenRandom"};
    static const std::unordered_set<std::string> ws2_32 = {
        "accept",
        "bind",
        "connect",
        "getsockopt",
        "listen",
        "recv",
        "select",
        "send",
        "setsockopt",
        "socket",
    };
    static const std::unordered_set<std::string> ucrt = {
        "_Exit",
        "_exit",
        "__acrt_iob_func",
        "__local_stdio_printf_options",
        "__local_stdio_scanf_options",
        "__stdio_common_vfprintf",
        "__stdio_common_vsprintf",
        "_vfprintf_l",
        "_vsscanf_l",
        "abort",
        "access",
        "aligned_alloc",
        "atexit",
        "atof",
        "atoi",
        "atol",
        "bsearch",
        "calloc",
        "ceil",
        "ceilf",
        "clearerr",
        "clock",
        "close",
        "cos",
        "cosf",
        "dup",
        "dup2",
        "exit",
        "fabs",
        "fabsf",
        "fclose",
        "fcntl",
        "ferror",
        "feof",
        "fflush",
        "fgetc",
        "fgets",
        "fileno",
        "floor",
        "floorf",
        "fmod",
        "fmodf",
        "fopen",
        "fprintf",
        "fputc",
        "fputs",
        "fread",
        "free",
        "freopen",
        "fseek",
        "ftell",
        "fwrite",
        "getc",
        "getcwd",
        "getenv",
        "isalnum",
        "isalpha",
        "isdigit",
        "islower",
        "isspace",
        "isupper",
        "isatty",
        "localeconv",
        "log",
        "log10",
        "log2",
        "logf",
        "longjmp",
        "lseek",
        "malloc",
        "memchr",
        "memcmp",
        "memcpy",
        "memmove",
        "memset",
        "open",
        "perror",
        "posix_memalign",
        "pow",
        "powf",
        "printf",
        "putc",
        "puts",
        "qsort",
        "raise",
        "read",
        "realloc",
        "rewind",
        "round",
        "roundf",
        "setbuf",
        "setenv",
        "setjmp",
        "setvbuf",
        "sin",
        "sinf",
        "snprintf",
        "sprintf",
        "sqrt",
        "sqrtf",
        "sscanf",
        "strcat",
        "strcat_s",
        "strchr",
        "strcmp",
        "strcpy",
        "strcpy_s",
        "strdup",
        "strerror",
        "strlen",
        "strncat",
        "strncmp",
        "strncpy",
        "strstr",
        "strtod",
        "strtol",
        "strtoul",
        "strrchr",
        "system",
        "tan",
        "tanf",
        "time",
        "tmpfile",
        "tmpnam",
        "tolower",
        "toupper",
        "trunc",
        "truncf",
        "ungetc",
        "unlink",
        "vfprintf",
        "wcscpy_s",
        "write",
        "_wmakepath_s",
        "_wsplitpath_s",
    };
    static const std::unordered_set<std::string> debugOnlyUcrt = {
        "_CrtDbgReport",
        "_CrtDbgReportW",
    };

    if (kernel32.count(name)) {
        dllName = "kernel32.dll";
        return true;
    }
    if (user32.count(name)) {
        dllName = "user32.dll";
        return true;
    }
    if (gdi32.count(name)) {
        dllName = "gdi32.dll";
        return true;
    }
    if (shell32.count(name)) {
        dllName = "shell32.dll";
        return true;
    }
    if (ole32.count(name)) {
        dllName = "ole32.dll";
        return true;
    }
    if (xinput.count(name)) {
        dllName = "xinput1_4.dll";
        return true;
    }
    if (advapi32.count(name)) {
        dllName = "advapi32.dll";
        return true;
    }
    if (bcrypt.count(name)) {
        dllName = "bcrypt.dll";
        return true;
    }
    if (ws2_32.count(name)) {
        dllName = "ws2_32.dll";
        return true;
    }

    if (name == "__C_specific_handler" || name == "__C_specific_handler_noexcept" ||
        name == "__current_exception" || name == "__current_exception_context" ||
        name.rfind("__vcrt_", 0) == 0) {
        dllName = debugRuntime ? "VCRUNTIME140D.dll" : "VCRUNTIME140.dll";
        return true;
    }

    if (ucrt.count(name)) {
        dllName = debugRuntime ? "ucrtbased.dll" : "ucrtbase.dll";
        return true;
    }
    if (debugRuntime && debugOnlyUcrt.count(name)) {
        dllName = "ucrtbased.dll";
        return true;
    }
    return false;
}

std::string importNameForSymbol(const std::string &name) {
    static const std::unordered_map<std::string, std::string> remap = {
        {"atexit", "_crt_atexit"},
        {"close", "_close"},
        {"lseek", "_lseek"},
        {"open", "_open"},
        {"read", "_read"},
        {"strdup", "_strdup"},
        {"unlink", "_unlink"},
        {"write", "_write"},
    };

    auto it = remap.find(name);
    if (it != remap.end())
        return it->second;
    return name;
}

bool generateWindowsImports(LinkArch arch,
                            const std::unordered_set<std::string> &dynamicSyms,
                            bool debugRuntime,
                            WindowsImportPlan &plan,
                            std::ostream &err) {
    plan.obj.name = (arch == LinkArch::AArch64) ? "<winarm64-imports>" : "<win64-imports>";
    plan.obj.format = ObjFileFormat::COFF;
    plan.obj.is64bit = true;
    plan.obj.isLittleEndian = true;
    plan.obj.machine = (arch == LinkArch::AArch64) ? 0xAA64 : 0x8664;
    plan.obj.sections.push_back(ObjSection{});
    plan.obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    std::unordered_map<std::string, std::vector<std::string>> dllToFuncs;
    std::unordered_set<std::string> seenFuncs;
    for (const auto &sym : dynamicSyms) {
        if (sym == "__ImageBase")
            continue;
        const std::string base = stripImpPrefix(sym);
        if (isWindowsLinkerHelperSymbol(sym) || isWindowsLinkerHelperSymbol(base))
            continue;
        if (base.rfind("rt_", 0) == 0)
            continue;
        if (!seenFuncs.insert(base).second)
            continue;
        std::string dllName;
        if (!dllForImport(base, debugRuntime, dllName)) {
            err << "error: Windows import symbol '" << base
                << "' is unresolved but has no DLL mapping\n";
            return false;
        }
        dllToFuncs[dllName].push_back(base);
    }

    std::vector<std::string> dllNames;
    dllNames.reserve(dllToFuncs.size());
    for (const auto &[dll, _] : dllToFuncs)
        dllNames.push_back(dll);
    std::sort(dllNames.begin(), dllNames.end());

    for (const auto &dll : dllNames) {
        auto funcs = dllToFuncs[dll];
        std::sort(funcs.begin(), funcs.end());
        DllImport dllImport;
        dllImport.dllName = dll;
        dllImport.functions = funcs;
        for (const auto &fn : funcs) {
            const std::string importName = importNameForSymbol(fn);
            if (importName != fn)
                dllImport.importNames.emplace(fn, importName);
        }
        plan.imports.push_back(std::move(dllImport));

        for (const auto &fn : funcs) {
            const size_t slotOff = dataSec.data.size();
            dataSec.data.resize(slotOff + 8, 0);

            ObjSymbol slotSym;
            slotSym.name = "__imp_" + fn;
            slotSym.binding = ObjSymbol::Global;
            slotSym.sectionIndex = 2;
            slotSym.offset = slotOff;
            const uint32_t slotSymIdx = static_cast<uint32_t>(plan.obj.symbols.size());
            plan.obj.symbols.push_back(std::move(slotSym));

            const size_t stubOff = textSec.data.size();
            if (arch == LinkArch::AArch64) {
                textSec.data.insert(textSec.data.end(),
                                    {
                                        0x10,
                                        0x00,
                                        0x00,
                                        0x90, // adrp x16, __imp_fn
                                        0x10,
                                        0x02,
                                        0x40,
                                        0xF9, // ldr  x16, [x16, #lo12]
                                        0x00,
                                        0x02,
                                        0x1F,
                                        0xD6, // br   x16
                                    });
            } else {
                textSec.data.insert(textSec.data.end(), {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});
            }

            ObjSymbol stubSym;
            stubSym.name = fn;
            stubSym.binding = ObjSymbol::Global;
            stubSym.sectionIndex = 1;
            stubSym.offset = stubOff;
            plan.obj.symbols.push_back(std::move(stubSym));

            if (arch == LinkArch::AArch64) {
                ObjReloc pageReloc;
                pageReloc.offset = stubOff + 0;
                pageReloc.type = coff_a64::kPageRel21;
                pageReloc.symIndex = slotSymIdx;
                pageReloc.addend = 0;
                textSec.relocs.push_back(pageReloc);

                ObjReloc loadReloc;
                loadReloc.offset = stubOff + 4;
                loadReloc.type = coff_a64::kPageOff12L;
                loadReloc.symIndex = slotSymIdx;
                loadReloc.addend = 0;
                textSec.relocs.push_back(loadReloc);
            } else {
                ObjReloc reloc;
                reloc.offset = stubOff + 2;
                reloc.type = coff_x64::kRel32;
                reloc.symIndex = slotSymIdx;
                reloc.addend = 0;
                textSec.relocs.push_back(reloc);
            }
        }

        dataSec.data.resize(dataSec.data.size() + 8, 0);
    }

    if (!textSec.data.empty())
        plan.obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (plan.obj.sections.size() == 1)
            plan.obj.sections.push_back(ObjSection{});
        plan.obj.sections.push_back(std::move(dataSec));
    }

    return true;
}

void appendArm64Insn(std::vector<uint8_t> &data, uint32_t insn) {
    data.push_back(static_cast<uint8_t>(insn & 0xFF));
    data.push_back(static_cast<uint8_t>((insn >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((insn >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((insn >> 24) & 0xFF));
}

ObjFile generateWindowsX64Helpers(const std::unordered_set<std::string> &dynamicSyms,
                                  bool haveVmTrapDefault,
                                  bool needTlsIndex) {
    ObjFile obj;
    obj.name = "<win64-helpers>";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0x8664;
    obj.sections.push_back(ObjSection{});
    obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    auto needsHelper = [&](const std::string &name) {
        return dynamicSyms.count(name) || dynamicSyms.count("__imp_" + name);
    };

    auto addRetFn = [&](const std::string &name, std::initializer_list<uint8_t> bytes) {
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addData = [&](const std::string &name, const std::vector<uint8_t> &bytes, uint32_t align) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.insert(dataSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addAbs64DataRef = [&](const std::string &name, uint32_t align, uint32_t targetSymIdx) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.resize(off + 8, 0);
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));

        ObjReloc reloc;
        reloc.offset = off;
        reloc.type = coff_x64::kAddr64;
        reloc.symIndex = targetSymIdx;
        reloc.addend = 0;
        dataSec.relocs.push_back(reloc);
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addImportAlias = [&](const std::string &name, uint32_t targetSymIdx) {
        if (dynamicSyms.count("__imp_" + name))
            addAbs64DataRef("__imp_" + name, 8, targetSymIdx);
    };

    if (needsHelper("_fltused")) {
        const uint32_t idx = addData("_fltused", {1, 0, 0, 0}, 4);
        addImportAlias("_fltused", idx);
    }
    if (needsHelper("__security_cookie")) {
        const uint32_t idx =
            addData("__security_cookie", {0x32, 0xA2, 0xDF, 0x2D, 0x99, 0x2B, 0x00, 0x00}, 8);
        addImportAlias("__security_cookie", idx);
    }
    if (needsHelper("__security_cookie_complement")) {
        const uint32_t idx = addData(
            "__security_cookie_complement", {0xCD, 0x5D, 0x20, 0xD2, 0x66, 0xD4, 0xFF, 0xFF}, 8);
        addImportAlias("__security_cookie_complement", idx);
    }
    if (needTlsIndex || needsHelper("_tls_index")) {
        const uint32_t idx = addData("_tls_index", {0, 0, 0, 0}, 4);
        addImportAlias("_tls_index", idx);
    }
    if (needsHelper("_is_c_termination_complete")) {
        const uint32_t idx = addData("_is_c_termination_complete", {0, 0, 0, 0}, 4);
        addImportAlias("_is_c_termination_complete", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", idx);
    }

    if (needsHelper("__security_check_cookie")) {
        const uint32_t idx = addRetFn("__security_check_cookie", {0xC3});
        addImportAlias("__security_check_cookie", idx);
    }
    if (needsHelper("__security_init_cookie")) {
        const uint32_t idx = addRetFn("__security_init_cookie", {0xC3});
        addImportAlias("__security_init_cookie", idx);
    }
    if (needsHelper("__GSHandlerCheck")) {
        const uint32_t idx = addRetFn("__GSHandlerCheck", {0xC3});
        addImportAlias("__GSHandlerCheck", idx);
    }
    if (needsHelper("_RTC_CheckStackVars")) {
        const uint32_t idx = addRetFn("_RTC_CheckStackVars", {0xC3});
        addImportAlias("_RTC_CheckStackVars", idx);
    }
    if (needsHelper("_RTC_InitBase")) {
        const uint32_t idx = addRetFn("_RTC_InitBase", {0x31, 0xC0, 0xC3});
        addImportAlias("_RTC_InitBase", idx);
    }
    if (needsHelper("_RTC_Shutdown")) {
        const uint32_t idx = addRetFn("_RTC_Shutdown", {0xC3});
        addImportAlias("_RTC_Shutdown", idx);
    }
    if (needsHelper("__report_rangecheckfailure")) {
        const uint32_t idx = addRetFn("__report_rangecheckfailure", {0xCC, 0xC3});
        addImportAlias("__report_rangecheckfailure", idx);
    }
    if (needsHelper("__chkstk")) {
        std::vector<uint8_t> chkstk = {
            0x49, 0x89, 0xC2,                         // mov r10, rax
            0x49, 0x89, 0xE3,                         // mov r11, rsp
            0x49, 0x81, 0xFA, 0x00, 0x10, 0x00, 0x00, // cmp r10, 0x1000
            0x72, 0x1B,                               // jb tail
            0x49, 0x81, 0xEB, 0x00, 0x10, 0x00, 0x00, // sub r11, 0x1000
            0x41, 0xF6, 0x03, 0x00,                   // test byte ptr [r11], 0
            0x49, 0x81, 0xEA, 0x00, 0x10, 0x00, 0x00, // sub r10, 0x1000
            0x49, 0x81, 0xFA, 0x00, 0x10, 0x00, 0x00, // cmp r10, 0x1000
            0x73, 0xE5,                               // jae probe_loop
            0x4D, 0x29, 0xD3,                         // sub r11, r10
            0x41, 0xF6, 0x03, 0x00,                   // test byte ptr [r11], 0
            0xC3,                                     // ret
        };
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), chkstk.begin(), chkstk.end());
        ObjSymbol sym;
        sym.name = "__chkstk";
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        const uint32_t idx = static_cast<uint32_t>(obj.symbols.size() - 1);
        addImportAlias("__chkstk", idx);
    }
    if (needsHelper("rt_audio_shutdown")) {
        const uint32_t idx = addRetFn("rt_audio_shutdown", {0xC3});
        addImportAlias("rt_audio_shutdown", idx);
    }
    if (needsHelper("__vcrt_initialize")) {
        const uint32_t idx = addRetFn("__vcrt_initialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_initialize", idx);
    }
    if (needsHelper("__vcrt_thread_attach")) {
        const uint32_t idx = addRetFn("__vcrt_thread_attach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_thread_attach", idx);
    }
    if (needsHelper("__vcrt_thread_detach")) {
        const uint32_t idx = addRetFn("__vcrt_thread_detach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_thread_detach", idx);
    }
    if (needsHelper("__vcrt_uninitialize")) {
        const uint32_t idx = addRetFn("__vcrt_uninitialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_uninitialize", idx);
    }
    if (needsHelper("__vcrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__vcrt_uninitialize_critical", {0xC3});
        addImportAlias("__vcrt_uninitialize_critical", idx);
    }
    if (needsHelper("__acrt_initialize")) {
        const uint32_t idx = addRetFn("__acrt_initialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_initialize", idx);
    }
    if (needsHelper("__acrt_thread_attach")) {
        const uint32_t idx = addRetFn("__acrt_thread_attach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_thread_attach", idx);
    }
    if (needsHelper("__acrt_thread_detach")) {
        const uint32_t idx = addRetFn("__acrt_thread_detach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_thread_detach", idx);
    }
    if (needsHelper("__acrt_uninitialize")) {
        const uint32_t idx = addRetFn("__acrt_uninitialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_uninitialize", idx);
    }
    if (needsHelper("__acrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__acrt_uninitialize_critical", {0xC3});
        addImportAlias("__acrt_uninitialize_critical", idx);
    }
    if (needsHelper("__isa_available_init")) {
        const uint32_t idx = addRetFn("__isa_available_init", {0xC3});
        addImportAlias("__isa_available_init", idx);
    }
    if (needsHelper("__scrt_exe_initialize_mta")) {
        const uint32_t idx = addRetFn("__scrt_exe_initialize_mta", {0x31, 0xC0, 0xC3});
        addImportAlias("__scrt_exe_initialize_mta", idx);
    }

    if (needsHelper("__guard_dispatch_icall_fptr")) {
        const uint32_t dispatchIdx = addRetFn("__guard_dispatch_icall_stub", {0xFF, 0xE0});
        const uint32_t ptrIdx = addAbs64DataRef("__guard_dispatch_icall_fptr", 8, dispatchIdx);
        addImportAlias("__guard_dispatch_icall_fptr", ptrIdx);
    }

    if (dynamicSyms.count("vm_trap")) {
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), {0xE9, 0x00, 0x00, 0x00, 0x00});

        ObjSymbol vmTrap;
        vmTrap.name = "vm_trap";
        vmTrap.binding = ObjSymbol::Global;
        vmTrap.sectionIndex = 1;
        vmTrap.offset = off;
        obj.symbols.push_back(std::move(vmTrap));

        ObjSymbol target;
        target.name = haveVmTrapDefault ? "vm_trap_default" : "rt_abort";
        target.binding = ObjSymbol::Undefined;
        const uint32_t targetIdx = static_cast<uint32_t>(obj.symbols.size());
        obj.symbols.push_back(std::move(target));

        ObjReloc reloc;
        reloc.offset = off + 1;
        reloc.type = coff_x64::kRel32;
        reloc.symIndex = targetIdx;
        reloc.addend = 0;
        textSec.relocs.push_back(reloc);
    }

    if (!textSec.data.empty())
        obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (obj.sections.size() == 1)
            obj.sections.push_back(ObjSection{});
        obj.sections.push_back(std::move(dataSec));
    }
    return obj;
}

ObjFile generateWindowsArm64Helpers(const std::unordered_set<std::string> &dynamicSyms,
                                    bool haveVmTrapDefault,
                                    bool needTlsIndex) {
    ObjFile obj;
    obj.name = "<winarm64-helpers>";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0xAA64;
    obj.sections.push_back(ObjSection{});
    obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    auto needsHelper = [&](const std::string &name) {
        return dynamicSyms.count(name) || dynamicSyms.count("__imp_" + name);
    };

    auto addTextFn = [&](const std::string &name, const std::vector<uint32_t> &insns) {
        const size_t off = textSec.data.size();
        for (uint32_t insn : insns)
            appendArm64Insn(textSec.data, insn);

        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addRetFn = [&](const std::string &name) {
        return addTextFn(name, {0xD65F03C0U}); // ret
    };

    auto addRetImmFn = [&](const std::string &name, uint16_t imm) {
        return addTextFn(name, {0xD2800000U | (static_cast<uint32_t>(imm) << 5), 0xD65F03C0U});
    };

    auto addData = [&](const std::string &name, const std::vector<uint8_t> &bytes, uint32_t align) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.insert(dataSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addAbs64DataRef = [&](const std::string &name, uint32_t align, uint32_t targetSymIdx) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.resize(off + 8, 0);
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));

        ObjReloc reloc;
        reloc.offset = off;
        reloc.type = coff_a64::kAddr64;
        reloc.symIndex = targetSymIdx;
        reloc.addend = 0;
        dataSec.relocs.push_back(reloc);
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addImportAlias = [&](const std::string &name, uint32_t targetSymIdx) {
        if (dynamicSyms.count("__imp_" + name))
            addAbs64DataRef("__imp_" + name, 8, targetSymIdx);
    };

    if (needsHelper("_fltused")) {
        const uint32_t idx = addData("_fltused", {1, 0, 0, 0}, 4);
        addImportAlias("_fltused", idx);
    }
    if (needsHelper("__security_cookie")) {
        const uint32_t idx =
            addData("__security_cookie", {0x32, 0xA2, 0xDF, 0x2D, 0x99, 0x2B, 0x00, 0x00}, 8);
        addImportAlias("__security_cookie", idx);
    }
    if (needsHelper("__security_cookie_complement")) {
        const uint32_t idx = addData(
            "__security_cookie_complement", {0xCD, 0x5D, 0x20, 0xD2, 0x66, 0xD4, 0xFF, 0xFF}, 8);
        addImportAlias("__security_cookie_complement", idx);
    }
    if (needTlsIndex || needsHelper("_tls_index")) {
        const uint32_t idx = addData("_tls_index", {0, 0, 0, 0}, 4);
        addImportAlias("_tls_index", idx);
    }
    if (needsHelper("_is_c_termination_complete")) {
        const uint32_t idx = addData("_is_c_termination_complete", {0, 0, 0, 0}, 4);
        addImportAlias("_is_c_termination_complete", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", idx);
    }

    if (needsHelper("__security_check_cookie")) {
        const uint32_t idx = addRetFn("__security_check_cookie");
        addImportAlias("__security_check_cookie", idx);
    }
    if (needsHelper("__security_init_cookie")) {
        const uint32_t idx = addRetFn("__security_init_cookie");
        addImportAlias("__security_init_cookie", idx);
    }
    if (needsHelper("__GSHandlerCheck")) {
        const uint32_t idx = addRetFn("__GSHandlerCheck");
        addImportAlias("__GSHandlerCheck", idx);
    }
    if (needsHelper("_RTC_CheckStackVars")) {
        const uint32_t idx = addRetFn("_RTC_CheckStackVars");
        addImportAlias("_RTC_CheckStackVars", idx);
    }
    if (needsHelper("_RTC_InitBase")) {
        const uint32_t idx = addRetImmFn("_RTC_InitBase", 0);
        addImportAlias("_RTC_InitBase", idx);
    }
    if (needsHelper("_RTC_Shutdown")) {
        const uint32_t idx = addRetFn("_RTC_Shutdown");
        addImportAlias("_RTC_Shutdown", idx);
    }
    if (needsHelper("__report_rangecheckfailure")) {
        const uint32_t idx = addTextFn("__report_rangecheckfailure", {0xD4200000U, 0xD65F03C0U});
        addImportAlias("__report_rangecheckfailure", idx);
    }
    if (needsHelper("__chkstk")) {
        const uint32_t idx = addRetFn("__chkstk");
        addImportAlias("__chkstk", idx);
    }
    if (needsHelper("rt_audio_shutdown")) {
        const uint32_t idx = addRetFn("rt_audio_shutdown");
        addImportAlias("rt_audio_shutdown", idx);
    }
    if (needsHelper("__vcrt_initialize")) {
        const uint32_t idx = addRetImmFn("__vcrt_initialize", 1);
        addImportAlias("__vcrt_initialize", idx);
    }
    if (needsHelper("__vcrt_thread_attach")) {
        const uint32_t idx = addRetImmFn("__vcrt_thread_attach", 1);
        addImportAlias("__vcrt_thread_attach", idx);
    }
    if (needsHelper("__vcrt_thread_detach")) {
        const uint32_t idx = addRetImmFn("__vcrt_thread_detach", 1);
        addImportAlias("__vcrt_thread_detach", idx);
    }
    if (needsHelper("__vcrt_uninitialize")) {
        const uint32_t idx = addRetImmFn("__vcrt_uninitialize", 1);
        addImportAlias("__vcrt_uninitialize", idx);
    }
    if (needsHelper("__vcrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__vcrt_uninitialize_critical");
        addImportAlias("__vcrt_uninitialize_critical", idx);
    }
    if (needsHelper("__acrt_initialize")) {
        const uint32_t idx = addRetImmFn("__acrt_initialize", 1);
        addImportAlias("__acrt_initialize", idx);
    }
    if (needsHelper("__acrt_thread_attach")) {
        const uint32_t idx = addRetImmFn("__acrt_thread_attach", 1);
        addImportAlias("__acrt_thread_attach", idx);
    }
    if (needsHelper("__acrt_thread_detach")) {
        const uint32_t idx = addRetImmFn("__acrt_thread_detach", 1);
        addImportAlias("__acrt_thread_detach", idx);
    }
    if (needsHelper("__acrt_uninitialize")) {
        const uint32_t idx = addRetImmFn("__acrt_uninitialize", 1);
        addImportAlias("__acrt_uninitialize", idx);
    }
    if (needsHelper("__acrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__acrt_uninitialize_critical");
        addImportAlias("__acrt_uninitialize_critical", idx);
    }
    if (needsHelper("__isa_available_init")) {
        const uint32_t idx = addRetFn("__isa_available_init");
        addImportAlias("__isa_available_init", idx);
    }
    if (needsHelper("__scrt_exe_initialize_mta")) {
        const uint32_t idx = addRetImmFn("__scrt_exe_initialize_mta", 0);
        addImportAlias("__scrt_exe_initialize_mta", idx);
    }

    if (needsHelper("__guard_dispatch_icall_fptr")) {
        const uint32_t dispatchIdx = addTextFn("__guard_dispatch_icall_stub", {0xD61F0200U});
        const uint32_t ptrIdx = addAbs64DataRef("__guard_dispatch_icall_fptr", 8, dispatchIdx);
        addImportAlias("__guard_dispatch_icall_fptr", ptrIdx);
    }

    if (dynamicSyms.count("vm_trap")) {
        const size_t off = textSec.data.size();
        appendArm64Insn(textSec.data, 0x14000000U); // b target

        ObjSymbol vmTrap;
        vmTrap.name = "vm_trap";
        vmTrap.binding = ObjSymbol::Global;
        vmTrap.sectionIndex = 1;
        vmTrap.offset = off;
        obj.symbols.push_back(std::move(vmTrap));

        ObjSymbol target;
        target.name = haveVmTrapDefault ? "vm_trap_default" : "rt_abort";
        target.binding = ObjSymbol::Undefined;
        const uint32_t targetIdx = static_cast<uint32_t>(obj.symbols.size());
        obj.symbols.push_back(std::move(target));

        ObjReloc reloc;
        reloc.offset = off;
        reloc.type = coff_a64::kBranch26;
        reloc.symIndex = targetIdx;
        reloc.addend = 0;
        textSec.relocs.push_back(reloc);
    }

    if (!textSec.data.empty())
        obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (obj.sections.size() == 1)
            obj.sections.push_back(ObjSection{});
        obj.sections.push_back(std::move(dataSec));
    }
    return obj;
}

} // namespace

/// @brief Run the full native link pipeline: parse objects → resolve symbols →
///        merge sections → apply relocations → write executable.
/// @details Supports ELF (Linux), Mach-O (macOS), and PE (Windows). The pipeline
///          reads the user's .o and runtime .a archives, resolves all symbols
///          (including dynamic stubs on macOS), dead-strips unreachable code,
///          performs ICF, inserts branch trampolines, applies relocations, and
///          writes the final executable. Zero external tool dependencies.
int nativeLink(const NativeLinkerOptions &opts, std::ostream & /*out*/, std::ostream &err) {
    // Step 1: Read the user's object file.
    ObjFile userObj;
    if (!readObjFile(opts.objPath, userObj, err)) {
        err << "error: failed to read object file '" << opts.objPath << "'\n";
        return 1;
    }

    // Step 1b: Read extra object files (e.g., asset blob).
    std::vector<ObjFile> extraObjects;
    for (const auto &extraPath : opts.extraObjPaths) {
        ObjFile extraObj;
        if (!readObjFile(extraPath, extraObj, err)) {
            err << "warning: failed to read extra object '" << extraPath << "', skipping\n";
            continue;
        }
        extraObjects.push_back(std::move(extraObj));
    }

    // Step 2: Read all archive files.
    std::vector<Archive> archives;
    for (const auto &arPath : opts.archivePaths) {
        Archive ar;
        if (!readArchive(arPath, ar, err)) {
            err << "warning: failed to read archive '" << arPath << "', skipping\n";
            continue;
        }
        archives.push_back(std::move(ar));
    }

    // Step 3: Symbol resolution (iterative archive extraction).
    std::vector<ObjFile> initialObjects = {userObj};
    for (auto &extra : extraObjects)
        initialObjects.push_back(std::move(extra));
    if (!opts.entrySymbol.empty())
        initialObjects.push_back(makeUndefinedRootObject(userObj, opts.entrySymbol));
    std::unordered_map<std::string, GlobalSymEntry> globalSyms;
    std::vector<ObjFile> allObjects;
    std::unordered_set<std::string> dynamicSyms;
    const bool debugWindowsRuntime =
        opts.platform == LinkPlatform::Windows &&
        opts.windowsDebugRuntime.value_or(usesDebugWindowsRuntime(opts.archivePaths));

    if (!resolveSymbols(
            initialObjects, archives, globalSyms, allObjects, dynamicSyms, err, opts.platform)) {
        err << "error: symbol resolution failed\n";
        return 1;
    }
    // Step 3.5a: Generate ObjC selector stubs (macOS — objc_msgSend$selector symbols).
    // Must come before dynamic stubs since it moves symbols from dynamicSyms and
    // ensures objc_msgSend itself is in the dynamic set.
    if (opts.arch == LinkArch::AArch64 && opts.platform == LinkPlatform::macOS) {
        ObjFile objcStubs = generateObjcSelectorStubsAArch64(dynamicSyms);
        if (!objcStubs.sections.empty()) {
            const size_t objcIdx = allObjects.size();
            allObjects.push_back(std::move(objcStubs));

            const auto &stubs = allObjects[objcIdx];
            for (size_t i = 1; i < stubs.symbols.size(); ++i) {
                const auto &sym = stubs.symbols[i];
                if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined)
                    continue;
                GlobalSymEntry e;
                e.name = sym.name;
                e.binding = GlobalSymEntry::Global;
                e.objIndex = objcIdx;
                e.secIndex = sym.sectionIndex;
                e.offset = sym.offset;
                globalSyms[sym.name] = std::move(e);
            }
        }
    }

    // Step 3.5b: Generate dynamic symbol stubs (macOS/ELF — needed for shared library imports).
    std::vector<DllImport> peImports;
    std::unordered_map<std::string, uint32_t> peImportSlotRvas;
    if (opts.platform == LinkPlatform::Windows) {
        dynamicSyms.erase("__ImageBase");
        const bool haveVmTrapDefault = globalSyms.find("vm_trap_default") != globalSyms.end();
        const bool needTlsIndex =
            std::any_of(allObjects.begin(), allObjects.end(), [](const ObjFile &obj) {
                return std::any_of(
                    obj.sections.begin(), obj.sections.end(), [](const ObjSection &sec) {
                        return sec.alloc && sec.tls && !sec.data.empty();
                    });
            });

        if (opts.arch == LinkArch::X86_64 || opts.arch == LinkArch::AArch64) {
            ObjFile helperObj =
                (opts.arch == LinkArch::AArch64)
                    ? generateWindowsArm64Helpers(dynamicSyms, haveVmTrapDefault, needTlsIndex)
                    : generateWindowsX64Helpers(dynamicSyms, haveVmTrapDefault, needTlsIndex);
            if (!helperObj.sections.empty()) {
                const size_t helperIdx = allObjects.size();
                allObjects.push_back(std::move(helperObj));
                registerSyntheticSymbols(allObjects[helperIdx], helperIdx, globalSyms);
                for (const auto &sym : allObjects[helperIdx].symbols) {
                    if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined ||
                        sym.name.empty())
                        continue;
                    dynamicSyms.erase(sym.name);
                }
            }
        }

        WindowsImportPlan importPlan;
        if (!generateWindowsImports(
                opts.arch, dynamicSyms, debugWindowsRuntime, importPlan, err)) {
            return 1;
        }
        peImports = importPlan.imports;
        if (!importPlan.obj.sections.empty()) {
            const size_t importIdx = allObjects.size();
            allObjects.push_back(std::move(importPlan.obj));
            registerSyntheticSymbols(allObjects[importIdx], importIdx, globalSyms);
            for (const auto &imp : peImports) {
                for (const auto &fn : imp.functions) {
                    dynamicSyms.erase(fn);
                    dynamicSyms.erase("__imp_" + fn);
                }
            }
        }
    }

    const bool supportsDynamicStubs =
        opts.platform == LinkPlatform::macOS && opts.arch == LinkArch::AArch64;
    if (!dynamicSyms.empty() && supportsDynamicStubs) {
        ObjFile stubObj = generateDynStubsAArch64(dynamicSyms);
        const size_t stubObjIdx = allObjects.size();
        allObjects.push_back(std::move(stubObj));

        // Manually register stub and GOT symbols in globalSyms.
        // This overrides Dynamic entries with Global entries pointing to stubs.
        const auto &stubs = allObjects[stubObjIdx];
        for (size_t i = 1; i < stubs.symbols.size(); ++i) {
            const auto &sym = stubs.symbols[i];
            if (sym.binding == ObjSymbol::Local)
                continue;

            GlobalSymEntry e;
            e.name = sym.name;
            e.binding = GlobalSymEntry::Global;
            e.objIndex = stubObjIdx;
            e.secIndex = sym.sectionIndex;
            e.offset = sym.offset;
            globalSyms[sym.name] = std::move(e);
        }
    }

    if (opts.platform == LinkPlatform::Windows) {
        dynamicSyms.erase("__ImageBase");
        dynamicSyms.erase("vm_trap");
    }

    if (!dynamicSyms.empty() && !supportsDynamicStubs) {
        std::vector<std::string> unsupported(dynamicSyms.begin(), dynamicSyms.end());
        std::sort(unsupported.begin(), unsupported.end());

        err << "error: native linker does not support dynamic imports on "
            << platformName(opts.platform) << ' ' << archName(opts.arch) << "\n";
        err << "error: supported dynamic-import targets are Windows x86_64, Windows AArch64, "
               "and macOS AArch64\n";
        err << "error: unresolved dynamic symbols:";
        for (const auto &sym : unsupported)
            err << ' ' << sym;
        err << "\nerror: use the system linker for programs that depend on CRT or OS import "
               "libraries\n";
        return 1;
    }

    // Step 3.5c: Dead-strip unused sections from all non-synthetic input
    // objects, rooting only entry points and always-live metadata.
    deadStrip(allObjects, initialObjects.size(), globalSyms, opts.entrySymbol, err);

    // Step 3.5d: Deduplicate identical rodata strings across object files.
    deduplicateStrings(allObjects, globalSyms);

    // Step 3.5d2: Fold identical .text sections (Identical Code Folding).
    foldIdenticalCode(allObjects, globalSyms);

    // Step 3.5e: Remove global symbols that reference stripped (empty) sections.
    // After dead stripping, sections with cleared data are skipped by the merger,
    // so symbols pointing to them would resolve to invalid addresses.
    {
        std::vector<std::string> deadSyms;
        for (const auto &[name, entry] : globalSyms) {
            if (entry.binding == GlobalSymEntry::Dynamic)
                continue; // Dynamic symbols have no section reference.
            if (entry.objIndex < allObjects.size() &&
                entry.secIndex < allObjects[entry.objIndex].sections.size() &&
                allObjects[entry.objIndex].sections[entry.secIndex].data.empty()) {
                deadSyms.push_back(name);
            }
        }
        for (const auto &name : deadSyms)
            globalSyms.erase(name);
    }

    // Step 4: Merge sections and compute layout.
    LinkLayout layout;
    layout.globalSyms = std::move(globalSyms);
    if (!mergeSections(allObjects, opts.platform, opts.arch, layout, err)) {
        err << "error: section merging failed\n";
        return 1;
    }

    // Step 5: Insert branch trampolines for out-of-range AArch64 B/BL instructions.
    if (!insertBranchTrampolines(allObjects, layout, opts.arch, opts.platform, err)) {
        err << "error: branch trampoline insertion failed\n";
        return 1;
    }

    // Step 6: Apply relocations. This also resolves final symbol addresses.
    if (!applyRelocations(allObjects, layout, dynamicSyms, opts.platform, opts.arch, err)) {
        err << "error: relocation application failed\n";
        return 1;
    }

    // Step 6.25: Resolve the final entry point after symbol addresses are known.
    {
        auto it = findWithMachoFallback(layout.globalSyms, opts.entrySymbol);
        if (it != layout.globalSyms.end())
            layout.entryAddr = it->second.resolvedAddr;
    }

    // Step 6.5: Build GOT entry table for the executable writer (needed for bind opcodes).
    for (const auto &[name, entry] : layout.globalSyms) {
        if (name.size() > 6 && name.substr(0, 6) == "__got_") {
            GotEntry ge;
            ge.symbolName = name.substr(6); // Remove "__got_" prefix → original symbol name.
            ge.gotAddr = entry.resolvedAddr;
            layout.gotEntries.push_back(std::move(ge));
        }
    }
    std::sort(layout.gotEntries.begin(),
              layout.gotEntries.end(),
              [](const GotEntry &a, const GotEntry &b) { return a.symbolName < b.symbolName; });

    // Step 7: Write executable.
    bool writeOk = false;
    switch (opts.platform) {
        case LinkPlatform::Linux:
            writeOk = writeElfExe(opts.exePath, layout, opts.arch, opts.stackSize, err);
            break;
        case LinkPlatform::macOS: {
            MacImportPlan importPlan;
            if (!planMacImports(dynamicSyms, importPlan, err))
                return 1;

            writeOk = writeMachOExe(opts.exePath,
                                    layout,
                                    opts.arch,
                                    importPlan.dylibs,
                                    dynamicSyms,
                                    importPlan.symOrdinals,
                                    opts.stackSize,
                                    err);
            break;
        }
        case LinkPlatform::Windows: {
            if (peImports.empty())
                peImports.push_back({"kernel32.dll", {"ExitProcess"}, {}});
            for (const auto &imp : peImports) {
                for (const auto &fn : imp.functions) {
                    auto it = layout.globalSyms.find("__imp_" + fn);
                    if (it != layout.globalSyms.end())
                        peImportSlotRvas[fn] =
                            static_cast<uint32_t>(it->second.resolvedAddr - 0x140000000ULL);
                }
            }
            const bool emitStartupStub = opts.entrySymbol == "main";
            writeOk = writePeExe(opts.exePath,
                                 layout,
                                 opts.arch,
                                 peImports,
                                 peImportSlotRvas,
                                 emitStartupStub,
                                 opts.stackSize,
                                 err);
            break;
        }
    }

    if (!writeOk) {
        err << "error: failed to write executable '" << opts.exePath << "'\n";
        return 1;
    }

    return 0;
}

} // namespace viper::codegen::linker
