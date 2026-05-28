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
    ASSERT_TRUE(planLinuxImports(
        {"cbrtf", "cos", "dlopen", "pthread_create", "XOpenDisplay", "snd_pcm_open"}, plan, err));
    EXPECT_EQ(std::vector<std::string>({"libc.so.6",
                                        "libm.so.6",
                                        "libdl.so.2",
                                        "libpthread.so.0",
                                        "libX11.so.6",
                                        "libasound.so.2"}),
              plan.neededLibs);
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

TEST(PlatformImportPlanners, MacPlannerMapsCbrtfToLibSystem) {
    MacImportPlan plan;
    std::ostringstream err;
    ASSERT_TRUE(planMacImports({"cbrtf"}, plan, err));

    EXPECT_TRUE(std::any_of(plan.dylibs.begin(), plan.dylibs.end(), [](const DylibImport &import) {
        return import.path == "/usr/lib/libSystem.B.dylib";
    }));
    ASSERT_TRUE(plan.symOrdinals.count("cbrtf") != 0);
    EXPECT_EQ(1u, plan.symOrdinals["cbrtf"]);
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
                                        "CreateWindowExW",
                                        "CreateWaitableTimerExW",
                                        "SetWaitableTimer",
                                        "D3D11CreateDevice",
                                        "cbrtf",
                                        "cos",
                                        "__RTDynamicCast",
                                        "_Init_thread_header",
                                        "_Smtx_lock_exclusive",
                                        "__std_find_trivial_1",
                                        "fsetpos",
                                        "_beginthreadex",
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
    EXPECT_TRUE(objHasSymbol(plan.obj, "__imp_ExitProcess"));
    EXPECT_TRUE(objHasSymbol(plan.obj, "ExitProcess"));
    EXPECT_TRUE(plan.obj.sections.size() >= 2);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
