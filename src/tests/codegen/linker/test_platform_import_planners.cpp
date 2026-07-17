//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/codegen/linker/test_platform_import_planners.cpp
// Purpose: Targeted unit coverage for per-platform native-link import planners.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/DynamicSymbolPolicy.hpp"
#include "codegen/common/linker/PlatformImportPlanner.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>

using namespace viper::codegen::linker;

namespace {

template <typename T> bool contains(const std::vector<T> &items, const T &value) {
    return std::find(items.begin(), items.end(), value) != items.end();
}

bool importPlanHasDll(const WindowsImportPlan &plan, const std::string &dll) {
    return std::any_of(plan.imports.begin(), plan.imports.end(), [&](const DllImport &entry) {
        return entry.dllName == dll;
    });
}

bool importPlanDllHasFunction(const WindowsImportPlan &plan,
                              const std::string &dll,
                              const std::string &function) {
    for (const auto &entry : plan.imports) {
        if (entry.dllName == dll)
            return contains(entry.functions, function);
    }
    return false;
}

bool objHasSymbol(const ObjFile &obj, const std::string &name) {
    return std::any_of(obj.symbols.begin(), obj.symbols.end(), [&](const ObjSymbol &sym) {
        return sym.name == name;
    });
}

uint32_t dylibOrdinalForPath(const MacImportPlan &plan, const std::string &path) {
    for (size_t i = 0; i < plan.dylibs.size(); ++i) {
        if (plan.dylibs[i].path == path)
            return static_cast<uint32_t>(i + 1);
    }
    return 0;
}

} // namespace

TEST(PlatformImportPlanners, LinuxPlannerClassifiesNeededLibraries) {
    LinuxImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planLinuxImports({"cbrtf",
                                  "cos",
                                  "dlopen",
                                  "exp10",
                                  "getpwuid_r",
                                  "pipe2",
                                  "pthread_create",
                                  "XOpenDisplay",
                                  "snd_pcm_open",
                                  "__once_proxy"},
                                 plan,
                                 err));
    EXPECT_EQ(std::vector<std::string>({"libc.so.6",
                                        "libm.so.6",
                                        "libdl.so.2",
                                        "libpthread.so.0",
                                        "libstdc++.so.6",
                                        "libX11.so.6",
                                        "libasound.so.2"}),
              plan.neededLibs);
}

TEST(PlatformImportPlanners, LinuxPlannerRejectsUnknownImportsWithoutPartialPlan) {
    LinuxImportPlan plan;
    plan.neededLibs = {"stale.so"};
    std::ostringstream err;
    EXPECT_FALSE(planLinuxImports({"malloc", "viper_missing_linux_symbol"}, plan, err));
    EXPECT_TRUE(plan.neededLibs.empty());
    EXPECT_NE(err.str().find("unrecognized Linux dynamic import 'viper_missing_linux_symbol'"),
              std::string::npos);
}

TEST(PlatformImportPlanners, MacPlannerMapsFrameworkAndFlatLookupSymbols) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports(
        {"CFStringCreateWithCString", "_OBJC_CLASS_$_NSApplication", "dispatch_async", "fsync"},
        plan,
        err));

    EXPECT_TRUE(std::any_of(plan.dylibs.begin(), plan.dylibs.end(), [](const DylibImport &import) {
        return import.path == "/usr/lib/libSystem.B.dylib";
    }));
    EXPECT_TRUE(std::any_of(plan.dylibs.begin(), plan.dylibs.end(), [](const DylibImport &import) {
        return import.path.find("CoreFoundation.framework") != std::string::npos;
    }));
    ASSERT_TRUE(plan.symOrdinals.count("_OBJC_CLASS_$_NSApplication") != 0);
    EXPECT_EQ(0u, plan.symOrdinals["_OBJC_CLASS_$_NSApplication"]);
    ASSERT_TRUE(plan.symOrdinals.count("dispatch_async") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["dispatch_async"]);
    ASSERT_TRUE(plan.symOrdinals.count("fsync") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["fsync"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsMachineAndHostSyscallsToLibSystem) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports({"gethostname", "sysctlbyname", "uname"}, plan, err));

    EXPECT_TRUE(std::any_of(plan.dylibs.begin(), plan.dylibs.end(), [](const DylibImport &import) {
        return import.path == "/usr/lib/libSystem.B.dylib";
    }));
    ASSERT_TRUE(plan.symOrdinals.count("gethostname") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["gethostname"]);
    ASSERT_TRUE(plan.symOrdinals.count("sysctlbyname") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["sysctlbyname"]);
    ASSERT_TRUE(plan.symOrdinals.count("uname") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["uname"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsDarwinArgvAccessorsToLibSystem) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports({"_NSGetArgc", "_NSGetArgv"}, plan, err));

    EXPECT_TRUE(std::any_of(plan.dylibs.begin(), plan.dylibs.end(), [](const DylibImport &import) {
        return import.path == "/usr/lib/libSystem.B.dylib";
    }));
    ASSERT_TRUE(plan.symOrdinals.count("_NSGetArgc") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["_NSGetArgc"]);
    ASSERT_TRUE(plan.symOrdinals.count("_NSGetArgv") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["_NSGetArgv"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsDarwinMathHelpersToLibSystem) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports({"cbrtf", "___exp10", "__sincosf_stret"}, plan, err));

    EXPECT_TRUE(std::any_of(plan.dylibs.begin(), plan.dylibs.end(), [](const DylibImport &import) {
        return import.path == "/usr/lib/libSystem.B.dylib";
    }));
    ASSERT_TRUE(plan.symOrdinals.count("cbrtf") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["cbrtf"]);
    ASSERT_TRUE(plan.symOrdinals.count("___exp10") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["___exp10"]);
    ASSERT_TRUE(plan.symOrdinals.count("__sincosf_stret") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["__sincosf_stret"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsLibcxxRuntimeSymbolsToLibcxxDylib) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports(
        {"__ZNSt3__118condition_variable10notify_allEv", "__cxa_throw", "_Unwind_Resume"},
        plan,
        err));

    const uint32_t libcxxOrdinal = dylibOrdinalForPath(plan, "/usr/lib/libc++.1.dylib");
    ASSERT_NE(0u, libcxxOrdinal);
    ASSERT_TRUE(plan.symOrdinals.count("__ZNSt3__118condition_variable10notify_allEv") != 0);
    EXPECT_EQ(libcxxOrdinal, plan.symOrdinals["__ZNSt3__118condition_variable10notify_allEv"]);
    ASSERT_TRUE(plan.symOrdinals.count("__cxa_throw") != 0);
    EXPECT_EQ(libcxxOrdinal, plan.symOrdinals["__cxa_throw"]);
    ASSERT_TRUE(plan.symOrdinals.count("_Unwind_Resume") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["_Unwind_Resume"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsCxaAtexitSymbolsToLibSystem) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(
        planMacImports({"___cxa_atexit", "___cxa_finalize", "___cxa_thread_atexit"}, plan, err));

    EXPECT_EQ(1u, plan.symOrdinals["___cxa_atexit"]);
    EXPECT_EQ(1u, plan.symOrdinals["___cxa_finalize"]);
    EXPECT_EQ(1u, plan.symOrdinals["___cxa_thread_atexit"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsSecurityConstantsToSecurityFramework) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports({"kSecKeyAlgorithmECDSASignatureDigestX962SHA256"}, plan, err));

    const uint32_t securityOrdinal = dylibOrdinalForPath(
        plan, "/System/Library/Frameworks/Security.framework/Versions/A/Security");
    ASSERT_NE(0u, securityOrdinal);
    ASSERT_TRUE(plan.symOrdinals.count("kSecKeyAlgorithmECDSASignatureDigestX962SHA256") != 0);
    EXPECT_EQ(securityOrdinal, plan.symOrdinals["kSecKeyAlgorithmECDSASignatureDigestX962SHA256"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsImageIOSymbolsBeforeCoreGraphics) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(
        planMacImports({"_CGImageSourceCreateImageAtIndex", "CGContextDrawImage"}, plan, err));

    const uint32_t imageIOOrdinal = dylibOrdinalForPath(
        plan, "/System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO");
    const uint32_t coreGraphicsOrdinal = dylibOrdinalForPath(
        plan, "/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics");
    ASSERT_NE(0u, imageIOOrdinal);
    ASSERT_NE(0u, coreGraphicsOrdinal);
    ASSERT_TRUE(plan.symOrdinals.count("_CGImageSourceCreateImageAtIndex") != 0);
    ASSERT_TRUE(plan.symOrdinals.count("CGContextDrawImage") != 0);
    EXPECT_EQ(imageIOOrdinal, plan.symOrdinals["_CGImageSourceCreateImageAtIndex"]);
    EXPECT_EQ(coreGraphicsOrdinal, plan.symOrdinals["CGContextDrawImage"]);
}

TEST(PlatformImportPlanners, MacPlannerMapsCommonCryptoSymbolsToLibSystem) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports({"CC_SHA512"}, plan, err));

    ASSERT_TRUE(plan.symOrdinals.count("CC_SHA512") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["CC_SHA512"]);
}

TEST(PlatformImportPlanners, WindowsPlannerCreatesGroupedImportsAndThunks) {
    WindowsImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(generateWindowsImports(LinkArch::X86_64,
                                       {"ExitProcess",
                                        "GetModuleFileNameW",
                                        "InitializeCriticalSectionAndSpinCount",
                                        "BringWindowToTop",
                                        "CreateWindowExW",
                                        "LoadIconW",
                                        "LoadImageW",
                                        "CreateWaitableTimerExW",
                                        "ClipCursor",
                                        "SetWaitableTimer",
                                        "GetDiskFreeSpaceExW",
                                        "CopyFile2",
                                        "FormatMessageA",
                                        "GetLocaleInfoEx",
                                        "CreateDirectoryExW",
                                        "DeviceIoControl",
                                        "TryAcquireSRWLockShared",
                                        "SetFileInformationByHandle",
                                        "CreateHardLinkW",
                                        "AreFileApisANSI",
                                        "SetFileAttributesW",
                                        "FindFirstFileExW",
                                        "GetFinalPathNameByHandleW",
                                        "GetRawInputData",
                                        "SetFileTime",
                                        "CreateFile2",
                                        "GetFileInformationByHandleEx",
                                        "CreateSymbolicLinkW",
                                        "SetConsoleCtrlHandler",
                                        "LockFileEx",
                                        "UnlockFileEx",
                                        "SleepConditionVariableSRW",
                                        "TryAcquireSRWLockExclusive",
                                        "SetFocus",
                                        "MsgWaitForMultipleObjectsEx",
                                        "RegisterRawInputDevices",
                                        "D3D11CreateDevice",
                                        "cbrtf",
                                        "cos",
                                        "exp2f",
                                        "log2f",
                                        "__RTDynamicCast",
                                        "_Init_thread_header",
                                        "_Smtx_lock_exclusive",
                                        "__std_type_info_name",
                                        "__std_smf_beta",
                                        "__std_atomic_wait_direct",
                                        "fsetpos",
                                        "_invalid_parameter",
                                        "_callnewh",
                                        "_initialize_onexit_table",
                                        "_cexit",
                                        "_configure_narrow_argv",
                                        "___lc_codepage_func",
                                        "_register_onexit_function",
                                        "_seh_filter_dll",
                                        "_crt_atexit",
                                        "_initialize_narrow_environment",
                                        "_execute_onexit_table",
                                        "_crt_at_quick_exit",
                                        "__stdio_common_vsprintf_s",
                                        "__stdio_common_vswprintf",
                                        "_get_osfhandle",
                                        "_wgetenv",
                                        "_rotl",
                                        "_beginthreadex",
                                        "__intrinsic_setjmp",
                                        "terminate",
                                        "__imp_ExitProcess"},
                                       false,
                                       plan,
                                       err));

    EXPECT_TRUE(importPlanHasDll(plan, "kernel32.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "user32.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "d3d11.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "ucrtbase.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "VCRUNTIME140.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "MSVCP140.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "MSVCP140_2.dll"));
    EXPECT_TRUE(importPlanHasDll(plan, "MSVCP140_ATOMIC_WAIT.dll"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "RegisterRawInputDevices"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "MsgWaitForMultipleObjectsEx"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "BringWindowToTop"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "ClipCursor"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "GetRawInputData"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "SetFocus"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "LoadIconW"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "user32.dll", "LoadImageW"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "kernel32.dll", "GetModuleFileNameW"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "kernel32.dll", "LockFileEx"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "kernel32.dll", "SetConsoleCtrlHandler"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "kernel32.dll", "UnlockFileEx"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ucrtbase.dll", "log2f"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ucrtbase.dll", "__stdio_common_vswprintf"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ucrtbase.dll", "_get_osfhandle"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ucrtbase.dll", "_wgetenv"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ucrtbase.dll", "terminate"));
    EXPECT_FALSE(importPlanDllHasFunction(plan, "VCRUNTIME140.dll", "terminate"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "__imp_ExitProcess"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "ExitProcess"));
    EXPECT_TRUE(plan.obj.sections.size() >= 2);
}

TEST(PlatformImportPlanners, WindowsPlannerMapsDebugOnlyUcrtImports) {
    WindowsImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(
        generateWindowsImports(LinkArch::X86_64, {"_CrtDbgReport", "_free_dbg"}, true, plan, err));

    EXPECT_TRUE(importPlanHasDll(plan, "ucrtbased.dll"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "__imp__CrtDbgReport"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "_CrtDbgReport"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "__imp__free_dbg"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "_free_dbg"));
}

TEST(PlatformImportPlanners, WindowsPlannerMapsCertificateKeyImportToCrypt32) {
    WindowsImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(
        generateWindowsImports(LinkArch::X86_64, {"CryptImportPublicKeyInfo"}, false, plan, err));

    EXPECT_TRUE(importPlanDllHasFunction(plan, "crypt32.dll", "CryptImportPublicKeyInfo"));
    EXPECT_FALSE(importPlanDllHasFunction(plan, "advapi32.dll", "CryptImportPublicKeyInfo"));
}

TEST(PlatformImportPlanners, WindowsPlannerRejectsStaticOnlyMsvcStdHelperImports) {
    WindowsImportPlan plan;
    std::ostringstream err;
    EXPECT_FALSE(
        generateWindowsImports(LinkArch::X86_64, {"__std_find_trivial_1"}, false, plan, err));
    EXPECT_NE(std::string::npos, err.str().find("__std_find_trivial_1"));
    EXPECT_NE(std::string::npos, err.str().find("no DLL mapping"));
}

// F10: the dynamic-symbol allow-list is platform-scoped for names exclusive to
// one platform's system libraries, so a foreign/typo'd API is a link error
// elsewhere instead of a dynamic import that never resolves at load time.
TEST(DynamicSymbolPolicy, ForeignPlatformSymbolsRejectedNativeAccepted) {
    // Win32 API: accepted on Windows, rejected on Linux/macOS.
    EXPECT_TRUE(isKnownDynamicSymbol("GetProcAddress", LinkPlatform::Windows));
    EXPECT_FALSE(isKnownDynamicSymbol("GetProcAddress", LinkPlatform::Linux));
    EXPECT_FALSE(isKnownDynamicSymbol("GetProcAddress", LinkPlatform::macOS));

    // Darwin/Mach: accepted on macOS, rejected on Linux/Windows.
    EXPECT_TRUE(isKnownDynamicSymbol("mach_absolute_time", LinkPlatform::macOS));
    EXPECT_FALSE(isKnownDynamicSymbol("mach_absolute_time", LinkPlatform::Linux));
    EXPECT_FALSE(isKnownDynamicSymbol("mach_absolute_time", LinkPlatform::Windows));

    // glibc-internal: accepted on Linux, rejected on macOS/Windows.
    EXPECT_TRUE(isKnownDynamicSymbol("__errno_location", LinkPlatform::Linux));
    EXPECT_FALSE(isKnownDynamicSymbol("__errno_location", LinkPlatform::macOS));
    EXPECT_FALSE(isKnownDynamicSymbol("__errno_location", LinkPlatform::Windows));
    EXPECT_TRUE(isKnownDynamicSymbol("__ctype_tolower_loc", LinkPlatform::Linux));
    EXPECT_FALSE(isKnownDynamicSymbol("__ctype_tolower_loc", LinkPlatform::macOS));
    EXPECT_FALSE(isKnownDynamicSymbol("__ctype_tolower_loc", LinkPlatform::Windows));

    // Genuinely cross-platform libc stays accepted on every platform.
    EXPECT_TRUE(isKnownDynamicSymbol("malloc", LinkPlatform::Windows));
    EXPECT_TRUE(isKnownDynamicSymbol("malloc", LinkPlatform::Linux));
    EXPECT_TRUE(isKnownDynamicSymbol("malloc", LinkPlatform::macOS));
    EXPECT_TRUE(isKnownDynamicSymbol("bcmp", LinkPlatform::Linux));
    EXPECT_FALSE(isKnownDynamicSymbol("bcmp", LinkPlatform::macOS));
    EXPECT_FALSE(isKnownDynamicSymbol("bcmp", LinkPlatform::Windows));
}

// F24/F25: OpenGL (gl + CamelCase, incl. glX) resolves to libGL and X11 (X +
// CamelCase) to libX11, while libc glob / stray uppercase-X names are NOT treated
// as GL/X11 dynamic imports.
TEST(PlatformImportPlanners, LinuxClassifiesGlAndX11Precisely) {
    std::unordered_set<std::string> syms = {"glClear", "glXCreateContext", "XOpenDisplay"};
    LinuxImportPlan plan;
    std::ostringstream err;
    EXPECT_TRUE(planLinuxImports(syms, plan, err));
    EXPECT_TRUE(contains(plan.neededLibs, std::string("libGL.so.1")));
    EXPECT_TRUE(contains(plan.neededLibs, std::string("libX11.so.6")));

    EXPECT_TRUE(isKnownDynamicSymbol("glClear", LinkPlatform::Linux));
    EXPECT_TRUE(isKnownDynamicSymbol("glXCreateContext", LinkPlatform::Linux));
    EXPECT_TRUE(isKnownDynamicSymbol("XOpenDisplay", LinkPlatform::Linux));
    EXPECT_FALSE(isKnownDynamicSymbol("Xtypo", LinkPlatform::Linux));     // X + lowercase
    EXPECT_FALSE(isKnownDynamicSymbol("globmatch", LinkPlatform::Linux)); // gl + lowercase
}

// F22: the WASAPI/COM entry points used by the Windows audio backend are both
// accepted as dynamic imports and mapped to ole32.dll (previously the planner
// listed them but isKnownDynamicSymbol rejected them, so audio failed to link).
TEST(PlatformImportPlanners, WindowsComAudioSymbolsResolveToOle32) {
    for (const char *sym : {"CoCreateInstance", "CoInitializeEx", "CoUninitialize"})
        EXPECT_TRUE(isKnownDynamicSymbol(sym, LinkPlatform::Windows));

    std::unordered_set<std::string> syms = {"CoCreateInstance", "CoInitializeEx", "CoUninitialize"};
    WindowsImportPlan plan;
    std::ostringstream err;
    EXPECT_TRUE(generateWindowsImports(LinkArch::X86_64, syms, false, plan, err));
    EXPECT_TRUE(importPlanHasDll(plan, "ole32.dll"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ole32.dll", "CoCreateInstance"));
    EXPECT_TRUE(importPlanDllHasFunction(plan, "ole32.dll", "CoUninitialize"));
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
