//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_cross_platform_abi.cpp
// Purpose: Verify cross-platform ABI properties for both x86-64 and AArch64.
//
//  CRIT-1: x86-64 Win64 shadow space — win64Target().shadowSpace must equal 32
//           and sysvTarget().shadowSpace must equal 0.  The incoming stack arg
//           offset in LowerILToMIR is `shadowSpace + 16 + stackArgIdx*8`, so
//           these invariants directly control correctness for stack-spilled
//           arguments on Windows.
//
//  CRIT-3: AArch64 Windows ARM64 — windowsTarget() must exist; the assembly
//           emitted for a Windows target must contain no ELF .type/.size
//           directives and no underscore-prefixed symbol names.
//
//  HIGH-4: LinkerSupport archive extension — runtimeArchivePath() must end in
//           ".a" on non-Windows platforms, and ".lib" on Windows.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"
#include "codegen/aarch64/TargetAArch64.hpp"
#include "codegen/common/LinkerSupport.hpp"
#include "codegen/x86_64/TargetX64.hpp"

#include <sstream>
#include <string>

// =============================================================================
// CRIT-1: x86-64 Win64 / SysV shadow space invariants
// =============================================================================

TEST(CrossPlatformABI, X64SysVShadowSpaceIsZero)
{
    // SysV AMD64 has no shadow space — stack args start immediately above the
    // return address after prologue.
    EXPECT_EQ(viper::codegen::x64::sysvTarget().shadowSpace, std::size_t{0});
}

TEST(CrossPlatformABI, X64Win64ShadowSpaceIs32)
{
    // Windows x64 requires 32 bytes of shadow space above the return address.
    // Stack-passed arguments start at RBP+48 (= 32 shadow + 8 saved-RBP + 8 ret addr).
    EXPECT_EQ(viper::codegen::x64::win64Target().shadowSpace, std::size_t{32});
}

TEST(CrossPlatformABI, X64SysVStackArgOffsetFormula)
{
    // Verify the offset formula: shadowSpace + 16 + stackArgIdx*8
    const auto &sysv = viper::codegen::x64::sysvTarget();
    const std::size_t shadowSpace = sysv.shadowSpace; // 0

    // First stack arg: offset = 0 + 16 + 0 = 16
    // Second stack arg: offset = 0 + 16 + 8 = 24
    EXPECT_EQ(shadowSpace + 16 + 0 * 8, std::size_t{16});
    EXPECT_EQ(shadowSpace + 16 + 1 * 8, std::size_t{24});
    EXPECT_EQ(shadowSpace + 16 + 2 * 8, std::size_t{32});
}

TEST(CrossPlatformABI, X64Win64StackArgOffsetFormula)
{
    // Windows x64: shadow space is 32 bytes.
    // First stack arg: offset = 32 + 16 + 0 = 48
    // Second stack arg: offset = 32 + 16 + 8 = 56
    const auto &win64 = viper::codegen::x64::win64Target();
    const std::size_t shadowSpace = win64.shadowSpace; // 32

    EXPECT_EQ(shadowSpace + 16 + 0 * 8, std::size_t{48});
    EXPECT_EQ(shadowSpace + 16 + 1 * 8, std::size_t{56});
    EXPECT_EQ(shadowSpace + 16 + 2 * 8, std::size_t{64});
}

TEST(CrossPlatformABI, X64Win64RegisterArgOrder)
{
    // Windows x64 integer arg order: RCX, RDX, R8, R9 (4 registers).
    // SysV order: RDI, RSI, RDX, RCX, R8, R9 (6 registers).
    using namespace viper::codegen::x64;

    const auto &win64 = win64Target();
    const auto &sysv = sysvTarget();

    EXPECT_EQ(win64.maxGPRArgs, std::size_t{4});
    EXPECT_EQ(sysv.maxGPRArgs, std::size_t{6});

    // Win64: first arg in RCX
    EXPECT_EQ(win64.intArgOrder[0], PhysReg::RCX);
    EXPECT_EQ(win64.intArgOrder[1], PhysReg::RDX);
    EXPECT_EQ(win64.intArgOrder[2], PhysReg::R8);
    EXPECT_EQ(win64.intArgOrder[3], PhysReg::R9);

    // SysV: first arg in RDI
    EXPECT_EQ(sysv.intArgOrder[0], PhysReg::RDI);
    EXPECT_EQ(sysv.intArgOrder[1], PhysReg::RSI);
    EXPECT_EQ(sysv.intArgOrder[2], PhysReg::RDX);
}

// =============================================================================
// CRIT-3: AArch64 Windows ARM64 target
// =============================================================================

/// @brief Emit a simple function header using the given target and return it.
static std::string emitAarch64FunctionHeader(const viper::codegen::aarch64::TargetInfo &ti,
                                             const std::string &name)
{
    viper::codegen::aarch64::AsmEmitter emitter{ti};
    std::ostringstream oss;
    emitter.emitFunctionHeader(oss, name);
    return oss.str();
}

TEST(CrossPlatformABI, AArch64WindowsTargetExists)
{
    // windowsTarget() must return a valid reference without crashing.
    const auto &ti = viper::codegen::aarch64::windowsTarget();
    EXPECT_TRUE(ti.isWindows());
    EXPECT_FALSE(ti.isLinux());
}

TEST(CrossPlatformABI, AArch64WindowsTargetSameRegistersAsLinux)
{
    // Windows ARM64 uses identical AAPCS64 register conventions to Linux.
    const auto &linux_ti = viper::codegen::aarch64::linuxTarget();
    const auto &windows_ti = viper::codegen::aarch64::windowsTarget();

    EXPECT_EQ(windows_ti.intArgOrder, linux_ti.intArgOrder);
    EXPECT_EQ(windows_ti.f64ArgOrder, linux_ti.f64ArgOrder);
    EXPECT_EQ(windows_ti.calleeSavedGPR, linux_ti.calleeSavedGPR);
    EXPECT_EQ(windows_ti.calleeSavedFPR, linux_ti.calleeSavedFPR);
    EXPECT_EQ(windows_ti.intReturnReg, linux_ti.intReturnReg);
    EXPECT_EQ(windows_ti.f64ReturnReg, linux_ti.f64ReturnReg);
    EXPECT_EQ(windows_ti.stackAlignment, linux_ti.stackAlignment);
}

TEST(CrossPlatformABI, AArch64WindowsFunctionHeaderNoELFType)
{
    // PE/COFF does not support ELF .type directives.
    const auto &ti = viper::codegen::aarch64::windowsTarget();
    const std::string out = emitAarch64FunctionHeader(ti, "myfunc");

    // Must not contain .type
    EXPECT_EQ(out.find(".type"), std::string::npos);
}

TEST(CrossPlatformABI, AArch64WindowsFunctionHeaderNoUnderscorePrefix)
{
    // Windows ARM64 does not use underscore-prefixed symbols (unlike Darwin).
    const auto &ti = viper::codegen::aarch64::windowsTarget();
    const std::string out = emitAarch64FunctionHeader(ti, "myfunc");

    // The function label itself must appear without a leading underscore.
    EXPECT_NE(out.find("myfunc:"), std::string::npos);
    EXPECT_EQ(out.find("_myfunc"), std::string::npos);
}

TEST(CrossPlatformABI, AArch64DarwinFunctionHeaderHasUnderscorePrefix)
{
    // Darwin uses underscore-prefixed symbols.
    const auto &ti = viper::codegen::aarch64::darwinTarget();
    const std::string out = emitAarch64FunctionHeader(ti, "myfunc");

    EXPECT_NE(out.find("_myfunc"), std::string::npos);
}

TEST(CrossPlatformABI, AArch64LinuxFunctionHeaderHasELFType)
{
    // Linux ELF emits .type for function metadata.
    const auto &ti = viper::codegen::aarch64::linuxTarget();
    const std::string out = emitAarch64FunctionHeader(ti, "myfunc");

    EXPECT_NE(out.find(".type"), std::string::npos);
}

// =============================================================================
// HIGH-4: LinkerSupport archive extension
// =============================================================================

TEST(CrossPlatformABI, LinkerSupportArchiveExtension)
{
    // On non-Windows platforms, the runtime archive must end in ".a".
    // On Windows, it must end in ".lib".
    const std::filesystem::path path =
        viper::codegen::common::runtimeArchivePath({}, "viper_rt_base");

#if defined(_WIN32)
    const std::string ext = path.extension().string();
    EXPECT_EQ(ext, std::string(".lib"));
    // No "lib" prefix on Windows (check stem to avoid matching ".lib" extension)
    EXPECT_EQ(path.stem().string().find("lib"), std::string::npos);
#else
    const std::string ext = path.extension().string();
    EXPECT_EQ(ext, std::string(".a"));
    // Must have "lib" prefix on Unix
    EXPECT_NE(path.filename().string().find("lib"), std::string::npos);
#endif
}

TEST(CrossPlatformABI, LinkerSupportArchivePathContainsBaseName)
{
    // The archive path must contain the base name regardless of platform.
    const std::filesystem::path path = viper::codegen::common::runtimeArchivePath({}, "my_lib");
    EXPECT_NE(path.string().find("my_lib"), std::string::npos);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return viper_test::run_all_tests();
}
